"""Dynamic-analysis orchestrator: build → run → llvm-cov → gdb.

A thin shell around existing infra/ossfuzz primitives, with per-worker
workspace isolation so many DynamicRunner instances can live in parallel
without stepping on each other's build output.

Workspace layout (per worker)::

    /tmp/agf-ze/system-check-runs/<run-id>/<case-id>/
        workspace/            ← worker-local OSS-Fuzz worktree
            oss-fuzz/         ← cloned from pinned source
                build/out/<project>/<fuzzer>   ← binary lives here
                build/corpus/<project>/<fuzzer>

Shared across workers: the **source** OSS-Fuzz checkout (pinned).  By default
``/tmp/agf-ze/oss-fuzz-pinned``; the first DynamicRunner clones it, later
instances reuse it (read-only — each worker branches its own worktree).

Concurrency:
    * LLM calls: no lock
    * build_project: caller must pass a ``docker_lock`` shared across all
      workers operating on the same project (Docker image tags are
      daemon-global; concurrent ``docker build`` on the same tag collides)
    * run/coverage/gdb: independent per worker (run inside its own
      ``build/out/<project>/`` tree which is isolated by worktree)
"""

from __future__ import annotations

import base64
import getpass as _getpass
import logging
import os
import shutil
import subprocess
import tempfile
import threading
from dataclasses import dataclass
from pathlib import Path

from harness.builder import BuildValidator
from infra.ossfuzz.build import build_project, reset_oss_fuzz_project
from infra.ossfuzz.models import BuildResult, CoverageMetrics, FuzzerRunResult
from infra.ossfuzz.runner import collect_coverage, count_corpus, generate_corpus
from infra.ossfuzz.workspace import (
    OssFuzzPaths,
    force_rmtree,
    prepare_pinned_oss_fuzz_workspace,
)

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Default paths — our user only, never Dimi's workspace
# ---------------------------------------------------------------------------

_USER = _getpass.getuser()
_DEFAULT_PINNED_OSS_FUZZ = Path(os.environ.get("AGF_OSS_FUZZ_DIR", f"/tmp/agf-{_USER}/oss-fuzz-pinned"))
_DEFAULT_RUNS_ROOT = Path(f"/tmp/agf-{_USER}/system-check-runs")


# ---------------------------------------------------------------------------
# Subclass that keeps build artifacts so we can run/cov/gdb after building
# ---------------------------------------------------------------------------


class _BuildValidatorKeepArtifacts(BuildValidator):
    """``BuildValidator`` that skips the post-build cleanup.

    The base class wipes ``build/out/<project>`` after every ``validate``
    call to reclaim disk; we need the binary to persist for the subsequent
    ``AP_Run_check`` / ``get_coverage`` / ``run_gdb`` calls in the same flow.
    """

    def _clean_build_artifacts(self, workspace_dir: Path) -> None:  # noqa: D401
        return None


# ---------------------------------------------------------------------------
# Result model
# ---------------------------------------------------------------------------


@dataclass
class DynamicRunResult:
    """Structured result for a build-run-coverage cycle."""

    build_ok: bool = False
    build_error: str = ""
    build_log_tail: str = ""
    build_log_path: str = ""  # full log of a failed build, readable by the agent

    ran: bool = False
    run: FuzzerRunResult | None = None

    coverage: CoverageMetrics | None = None

    binary_name: str = ""

    def short_summary(self) -> str:
        parts: list[str] = []
        if not self.build_ok:
            parts.append(f"build=FAIL ({self.build_error[:120]})")
            return "  ".join(parts)
        parts.append("build=OK")
        if self.run:
            parts.append(
                f"run=edges:{self.run.edges_covered} ft:{self.run.features} "
                f"corpus:{self.run.corpus_size} exec/s:{self.run.exec_per_second}"
            )
            if self.run.error:
                parts.append(f"run_error={self.run.error[:80]}")
        if self.coverage and not self.coverage.error:
            parts.append(
                f"cov=lines:{self.coverage.lines_pct:.1f}% "
                f"branches:{self.coverage.branches_pct:.1f}% "
                f"funcs:{self.coverage.functions_pct:.1f}%"
            )
        elif self.coverage and self.coverage.error:
            parts.append(f"cov_error={self.coverage.error[:80]}")
        return "  ".join(parts)


# ---------------------------------------------------------------------------
# DynamicRunner
# ---------------------------------------------------------------------------


class DynamicRunner:
    """Build/run/coverage/gdb orchestrator for ONE worker.

    Creating the runner is cheap (no work).  The first call to :meth:`build`
    prepares the OSS-Fuzz worktree under ``worker_workspace``.  Subsequent
    calls reuse the same worktree so run/coverage see the built binary.
    """

    def __init__(
        self,
        project: str,
        *,
        source_oss_fuzz_dir: Path | None = None,
        worker_workspace: Path | None = None,
        docker_lock: threading.Lock | None = None,
        sanitizer: str = "address",
        fuzzer_name: str | None = None,
        language: str = "c",
        pinned_commit: str | None = None,
        container_path: str | None = None,
        runtime_image: str | None = None,
    ) -> None:
        self.project = project
        # Where the harness goes in the build ($SRC/...), the oss-fuzz commit
        # the worktree is reset to, and the image probes/gdb run in.  The
        # runtime image must have gdb; OSS-Fuzz project images do not.
        self.pinned_commit = pinned_commit
        self.container_path = container_path
        self.runtime_image = runtime_image or f"gcr.io/oss-fuzz/{project}"
        self.source_oss_fuzz_dir = (
            Path(source_oss_fuzz_dir) if source_oss_fuzz_dir else _DEFAULT_PINNED_OSS_FUZZ
        )
        self.worker_workspace = (
            Path(worker_workspace)
            if worker_workspace
            else Path(tempfile.mkdtemp(prefix="run-", dir=_ensure_dir(_DEFAULT_RUNS_ROOT)))
        )
        self.docker_lock = docker_lock
        self.sanitizer = sanitizer
        self.fuzzer_name = fuzzer_name
        self.language = language

        # Full logs of failed builds, one file per attempt; the agent's
        # Read/Grep can open them (flow.py adds this dir to allowed_roots).
        self.build_log_dir = self.worker_workspace.parent / "build_logs"
        self.build_log_dir.mkdir(parents=True, exist_ok=True)
        self._build_attempt = 0

        self._bv = _BuildValidatorKeepArtifacts(
            project=project,
            oss_fuzz_dir=self.source_oss_fuzz_dir,
            workspace_root=self.worker_workspace,
            sanitizer=sanitizer,
            fuzzer_name=fuzzer_name,
            docker_lock=docker_lock,
            language=language,
            pinned_commit=pinned_commit,
            container_path=container_path,
        )

        # Populated after the first successful build.
        self._workspace_dir: Path | None = None
        self._built_binary: str | None = None
        self._address_binary_snapshot: Path | None = None
        # Last harness source we built — needed to re-emit the file during
        # the coverage rebuild, because reset_oss_fuzz_project will revert
        # the swapped-in file.
        self._last_harness_code: str | None = None
        self._last_source_filename: str | None = None
        # Whether the current workspace has a coverage-sanitized build.
        self._coverage_built: bool = False
        # The libFuzzer run of the last get_coverage on the current build;
        # None until get_coverage has fuzzed the current build.
        self.last_fuzz_run = None
        # What the checks found on the current build (reset by build()):
        # the submit gate reads it.
        self.checks: dict = self._fresh_checks()
        # Every successful build's harness code (stripped) -> the checks made
        # on that build (the same dict as self.checks while it is current),
        # so the agent may submit an earlier, better build.
        self.build_history: dict[str, dict] = {}

    @staticmethod
    def _fresh_checks() -> dict:
        return {"run_done": False, "reached": set(), "coverage_lines": 0}

    # ---- build -------------------------------------------------------------

    def build(self, harness_code: str, filename: str) -> DynamicRunResult:
        """Compile *harness_code* as ``filename`` inside this worker's worktree.

        On success, remembers the built binary name for later run/coverage.
        This is the address-sanitized build.  See :meth:`coverage` for the
        coverage rebuild path.
        """
        self._build_attempt += 1
        self._bv.last_full_log = ""
        result = self._bv.validate(harness_code, filename)
        if not result.success:
            log_path = ""
            full = self._bv.last_full_log or result.build_log or ""
            if full:
                f = self.build_log_dir / f"build_{self._build_attempt:02d}_{self.sanitizer}_failed.log"
                f.write_text(full)
                log_path = str(f)
            return DynamicRunResult(
                build_ok=False,
                build_error=result.error,
                build_log_tail=result.build_log or "",  # error excerpt + tail, see infra.ossfuzz.build
                build_log_path=log_path,
            )

        # Record the workspace & binary for subsequent calls.
        # BuildValidator creates workspace_root/oss-fuzz, ensure we locate it.
        self._workspace_dir = self.worker_workspace / "oss-fuzz"
        self._built_binary = self.fuzzer_name or result.binary_name or Path(filename).stem
        self._last_harness_code = harness_code
        self._last_source_filename = filename
        built_binary_path = (
            self._workspace_dir / "build" / "out" / self.project / self._built_binary
        )
        snapshot_dir = self.worker_workspace / "binary_snapshots"
        snapshot_dir.mkdir(parents=True, exist_ok=True)
        self._address_binary_snapshot = snapshot_dir / self._built_binary
        if built_binary_path.is_file():
            shutil.copy2(built_binary_path, self._address_binary_snapshot)
        # A fresh address build invalidates any prior coverage build and
        # every check made on the previous binary.
        self._coverage_built = False
        self.last_fuzz_run = None
        self.checks = self._fresh_checks()
        self.build_history[harness_code.strip()] = self.checks
        return DynamicRunResult(
            build_ok=True,
            binary_name=self._built_binary,
        )

    # ---- run ---------------------------------------------------------------

    def run(
        self,
        duration: int = 30,
        *,
        binary_name: str | None = None,
    ) -> FuzzerRunResult:
        """Run the built fuzzer for *duration* seconds (default 30 seconds).

        Benchmark convention: empty corpus, no seeds, 30-second libFuzzer
        cold-start run.  Deviating from this default breaks comparability
        across runs and against gold baselines.

        Requires a prior successful :meth:`build`.  Returns the libFuzzer
        stats via ``infra.ossfuzz.runner.generate_corpus``.
        """
        workspace = self._require_workspace()
        binary = binary_name or self._built_binary
        if not binary:
            raise RuntimeError("DynamicRunner.run() called without a built binary")
        return generate_corpus(
            oss_fuzz_dir=workspace,
            project=self.project,
            fuzz_target=binary,
            duration=duration,
            corpus_name=f"{binary}_systemcheck",
        )

    # ---- coverage ----------------------------------------------------------

    def _rebuild_with_coverage(self) -> BuildResult:
        """Reset + clean + rebuild the project with SANITIZER=coverage.

        Follows the exact same pattern as
        ``benchmark/oss_fuzz_harness/eval_gold_coverage.py`` — reset the
        OSS-Fuzz project dir (git checkout), rmtree build/out + build/work
        to avoid CMake/autoconf .o reuse, then rebuild.

        Re-swaps the last-built harness source into the project dir so the
        rebuild compiles OUR harness, not the original gold.
        """
        workspace = self._require_workspace()

        if self._last_harness_code is None or self._last_source_filename is None:
            raise RuntimeError(
                "DynamicRunner._rebuild_with_coverage() called before any "
                "successful build(); nothing to rebuild."
            )

        # Reset the OSS-Fuzz project directory to HEAD so the sanitizer
        # rebuild starts from a clean slate — otherwise swap_gold_source's
        # previous edit is still there and the re-swap below has nothing
        # to overwrite in the first place; but more importantly the build
        # infrastructure looks cleaner this way.
        reset_oss_fuzz_project(workspace, self.project)

        # Rmtree build/out/<p> and build/work/<p> — CRITICAL for CMake
        # /autoconf projects which would otherwise reuse address-sanitized
        # .o files and fail to link under coverage.
        for subdir in ("out", "work"):
            p = workspace / "build" / subdir / self.project
            if p.is_dir():
                force_rmtree(p)

        # Re-swap our harness source into the project dir.  We reuse
        # BuildValidator.validate() for the whole swap+build pipeline
        # under a throw-away BuildValidator configured with coverage
        # sanitizer.  The existing self._bv is address-sanitized; swap
        # to coverage via a sibling instance pointing at the same
        # workspace_root.
        cov_bv = _BuildValidatorKeepArtifacts(
            project=self.project,
            oss_fuzz_dir=self.source_oss_fuzz_dir,
            workspace_root=self.worker_workspace,
            sanitizer="coverage",
            fuzzer_name=self.fuzzer_name,
            docker_lock=self.docker_lock,
            language=self.language,
            pinned_commit=self.pinned_commit,
            container_path=self.container_path,
        )
        validation = cov_bv.validate(self._last_harness_code, self._last_source_filename)
        if not validation.success:
            return BuildResult(
                success=False,
                error=f"coverage build failed: {validation.error}",
                build_log=validation.build_log,
            )
        # Don't cleanup — BuildValidator.cleanup_workspace() would wipe
        # the shared worker_workspace.  Leave it alone; DynamicRunner.cleanup
        # is the single point of teardown.
        return BuildResult(
            success=True,
            output_dir=str(workspace / "build" / "out" / self.project),
            build_log=validation.build_log,
        )

    def coverage(
        self,
        binary_name: str | None = None,
        *,
        fuzz_seconds: int = 600,
    ) -> CoverageMetrics:
        """Populate a corpus by running libFuzzer, then run llvm-cov.

        Pipeline:
          1. If ``fuzz_seconds > 0`` and no corpus has been produced yet
             for this binary, run the ASan binary under libFuzzer for
             that long (empty corpus cold-start) to generate a corpus.
          2. Rebuild the project under SANITIZER=coverage (one-time per
             worker; ``self._coverage_built`` is the cache).
          3. Seed ``build/corpus/<project>/<binary>/`` from the corpus
             produced in step 1 and call ``collect_coverage``.

        Order constraint: the coverage rebuild in step 2 overwrites the
        ASan binary, so AP probes / GDB must NOT run after this method.
        Subsequent ``coverage()`` calls are no-ops on the rebuild but
        still re-run llvm-cov.
        """
        workspace = self._require_workspace()
        binary = binary_name or self._built_binary
        if not binary:
            raise RuntimeError("DynamicRunner.coverage() called without a built binary")

        # 1. Run libFuzzer to populate the systemcheck corpus, once per
        #    build: a new build() resets last_fuzz_run, so its coverage is
        #    measured on its own fresh corpus, never on the corpus of an
        #    earlier build (generate_corpus empties the dir first).
        #    fuzz_seconds=0 skips it.
        if fuzz_seconds > 0 and self.last_fuzz_run is None and not self._coverage_built:
            try:
                run_result = self.run(duration=fuzz_seconds, binary_name=binary)
                self.last_fuzz_run = run_result
            except Exception as exc:
                return CoverageMetrics(
                    error=f"libFuzzer pre-run failed: {exc}"
                )
            if run_result.error and run_result.corpus_size == 0:
                return CoverageMetrics(
                    error=(
                        f"libFuzzer pre-run errored before producing any corpus: "
                        f"{run_result.error}"
                    )
                )

        # 2. Coverage rebuild (one-shot).
        if not self._coverage_built:
            rebuild = self._rebuild_with_coverage()
            if not rebuild.success:
                return CoverageMetrics(
                    error=f"coverage rebuild failed: {rebuild.error}"
                )
            self._coverage_built = True

        # 3. Seed default corpus dir, run llvm-cov.
        self._seed_default_corpus_from_systemcheck(workspace, binary)

        return collect_coverage(
            oss_fuzz_dir=workspace,
            project=self.project,
            fuzz_target=binary,
        )

    def _seed_default_corpus_from_systemcheck(self, workspace: Path, binary: str) -> None:
        """Ensure ``build/corpus/<project>/<binary>/`` contains the inputs
        produced by :meth:`run`.

        ``run()`` writes to ``<binary>_systemcheck/`` so concurrent
        workers on the same project don't collide.  ``collect_coverage``
        reads the default path (no suffix), so we replicate the files
        there before invoking llvm-cov.
        """
        paths = OssFuzzPaths(workspace)
        src_dir = paths.corpus_dir(self.project, f"{binary}_systemcheck")
        dst_dir = paths.corpus_dir(self.project, binary)
        if not src_dir.is_dir():
            logger.warning(
                "systemcheck corpus dir missing, coverage will run on empty "
                "corpus: %s", src_dir,
            )
            dst_dir.mkdir(parents=True, exist_ok=True)
            return
        # Fresh dst dir (remove any stale content from a prior call).
        if dst_dir.exists():
            force_rmtree(dst_dir)
        dst_dir.mkdir(parents=True, exist_ok=True)
        count = 0
        for entry in src_dir.iterdir():
            if not entry.is_file():
                continue
            try:
                # Hardlink avoids duplicating potentially many KB.
                os.link(entry, dst_dir / entry.name)
            except OSError:
                # Fall back to copy on cross-device or permission errors.
                import shutil
                shutil.copy2(entry, dst_dir / entry.name)
            count += 1
        logger.info(
            "seeded %d corpus files into %s for coverage run", count, dst_dir
        )

    # ---- probes: generator + binary in the runtime container ---------------
    #
    # Every probe runs inside ``self.runtime_image`` (the case image plus
    # gdb), with no network and the ASan binary mounted read-only.  The
    # agent's ``generate()`` runs there too, never in this process: it is
    # model-written code and must not see the host filesystem.

    _GEN_RUNNER = (
        "import runpy, sys, traceback\n"
        "try:\n"
        "    ns = runpy.run_path('/qfprobe/gen.py')\n"
        "    g = ns.get('generate')\n"
        "    if g is None:\n"
        "        print('generator_code must define generate() -> bytes'); sys.exit(3)\n"
        "    b = g()\n"
        "    if not isinstance(b, (bytes, bytearray)):\n"
        "        print('generate() returned %s, expected bytes' % type(b).__name__); sys.exit(3)\n"
        "    open('/tmp/input.bin', 'wb').write(bytes(b))\n"
        "except SystemExit:\n"
        "    raise\n"
        "except BaseException:\n"
        "    traceback.print_exc(limit=5); sys.exit(3)\n"
    )
    _GEN_FAIL_MARK = "QF_GENERATOR_FAILED"

    def _asan_binary_dir(self, binary: str) -> Path | None:
        """Directory holding the ASan build of *binary* (snapshot first:
        it survives the coverage rebuild)."""
        if self._address_binary_snapshot and self._address_binary_snapshot.is_file():
            d = self._address_binary_snapshot.parent
        else:
            d = OssFuzzPaths(self._require_workspace()).build_out(self.project)
        return d if (d / binary).is_file() else None

    def _run_probe(
        self,
        generator_code: str,
        command: str,
        *,
        binary_dir: Path,
        env: dict[str, str],
        files: dict[str, str] | None = None,
        timeout: int,
        ptrace: bool = False,
    ) -> tuple[str | None, str, int, bool]:
        """Run ``generate()`` then *command* in the runtime container.

        The blob is at ``/tmp/input.bin``, extra *files* under ``/qfprobe/``.
        Returns ``(generator_error, output, exit_code, timed_out)``.
        """
        probe_dir = Path(tempfile.mkdtemp(prefix="probe-", dir=self.worker_workspace))
        try:
            (probe_dir / "gen.py").write_text(generator_code)
            (probe_dir / "run_gen.py").write_text(self._GEN_RUNNER)
            for name, text in (files or {}).items():
                (probe_dir / name).write_text(text)
            os.chmod(probe_dir, 0o755)
            script = (
                "cd /tmp && "
                f"timeout 60 python3 /qfprobe/run_gen.py > /tmp/gen.log 2>&1 "
                f"|| {{ echo {self._GEN_FAIL_MARK}; cat /tmp/gen.log; exit 0; }}; "
                f"{command}"
            )
            cmd = ["docker", "run", "--rm", "--network", "none", "--memory", "4g"]
            if ptrace:
                cmd += ["--cap-add", "SYS_PTRACE", "--security-opt", "seccomp=unconfined"]
            for k, v in env.items():
                cmd += ["-e", f"{k}={v}"]
            cmd += [
                "-v", f"{binary_dir}:/out:ro",
                "-v", f"{probe_dir}:/qfprobe:ro",
                "--entrypoint", "bash",
                self.runtime_image, "-c", script,
            ]
            try:
                proc = subprocess.run(
                    cmd, capture_output=True, text=True, timeout=timeout + 90, errors="replace",
                )
                output = (proc.stdout or "") + (proc.stderr or "")
                exit_code, timed_out = proc.returncode, False
            except subprocess.TimeoutExpired as exc:
                output = (exc.stdout or b"") + (exc.stderr or b"")
                if isinstance(output, bytes):
                    output = output.decode("utf-8", errors="replace")
                exit_code, timed_out = -9, True
        finally:
            shutil.rmtree(probe_dir, ignore_errors=True)
        if self._GEN_FAIL_MARK in output:
            detail = output.split(self._GEN_FAIL_MARK, 1)[1].strip()[-2000:]
            return f"generator_code execution failed: {detail}", "", -1, False
        return None, output, exit_code, timed_out

    # ---- gdb ---------------------------------------------------------------

    def gdb(
        self,
        generator_code: str,
        gdb_commands: str,
        *,
        binary_name: str | None = None,
        timeout: int = 120,
        max_output_bytes: int = 8000,
    ) -> str:
        """Run the ASan binary under gdb on the blob from ``generate()``.

        *gdb_commands* is a gdb script (breakpoints, ``commands`` blocks,
        ``bt``, ``print``, ...).  The input is ``/tmp/input.bin``; if the
        script has no ``run`` line, ``run /tmp/input.bin`` and ``bt`` are
        appended.  Leak detection is off (LSan does not work under ptrace)
        and ASan aborts on error so gdb stops at the faulting frame.
        """
        binary = binary_name or self._built_binary
        if not binary or not self._built_binary:
            return "Error: no successful build_harness yet: build the harness first"
        binary_dir = self._asan_binary_dir(binary)
        if binary_dir is None:
            return f"Error: binary {binary} not found"
        lines = [ln.rstrip() for ln in gdb_commands.strip().splitlines()]
        has_run = any(ln.strip().split(" ", 1)[0] in ("r", "run") for ln in lines)
        script = "set pagination off\nset confirm off\nset breakpoint pending off\n" + "\n".join(lines) + "\n"
        if not has_run:
            script += "run /tmp/input.bin\nbt\n"
        script += "quit\n"
        err, output, _, timed_out = self._run_probe(
            generator_code,
            f"timeout {timeout} gdb --batch -nx --command=/qfprobe/cmds.gdb /out/{binary} 2>&1",
            binary_dir=binary_dir,
            env={"ASAN_OPTIONS": "detect_leaks=0:abort_on_error=1:symbolize=1"},
            files={"cmds.gdb": script},
            timeout=timeout,
            ptrace=True,
        )
        if err:
            return f"Error: {err}"
        if timed_out:
            output += f"\n[gdb timed out after {timeout}s]"
        if len(output) > max_output_bytes:
            output = output[:max_output_bytes] + "\n[...gdb output truncated...]"
        return output

    # ---- targeted reachability check ----------------------------------------

    def check_target_reached(
        self,
        generator_code: str,
        target_function: str,
        *,
        binary_name: str | None = None,
        extra_functions: list[str] | None = None,
        timeout: int = 60,
    ) -> dict:
        """Run the blob from ``generate()`` under gdb with a breakpoint on
        *target_function* (and on each of *extra_functions*), and report
        which fired, in call order.

        Returns ``hit``, ``functions_hit`` (first-hit order),
        ``hit_counts``, ``gdb_output`` and ``error``.  A breakpoint gdb
        cannot set (unknown or inlined-away symbol) is an error, not a miss.
        """
        binary = binary_name or self._built_binary
        if not binary or not self._built_binary:
            return {"hit": False, "functions_hit": [], "hit_counts": {}, "gdb_output": "",
                    "error": "no successful build_harness yet: build the harness first"}
        binary_dir = self._asan_binary_dir(binary)
        if binary_dir is None:
            return {"hit": False, "functions_hit": [], "hit_counts": {}, "gdb_output": "",
                    "error": f"binary {binary} not found"}
        funcs = [target_function] + [f for f in (extra_functions or []) if f and f != target_function]
        # Set breakpoints through gdb's Python API: in a plain command file
        # one unknown symbol aborts the whole script, so a missing extra
        # function would hide a reachable target.
        script = (
            "set pagination off\nset confirm off\nset breakpoint pending off\n"
            "python\n"
            "import gdb\n"
            "class _QFBreak(gdb.Breakpoint):\n"
            "    def stop(self):\n"
            "        print('QF_HIT:' + self.qf_name)\n"
            "        return False\n"
            f"for _f in {funcs!r}:\n"
            "    try:\n"
            "        _b = _QFBreak(_f, internal=False)\n"
            "        _b.qf_name = _f\n"
            "        if _b.pending:\n"
            "            _b.delete()\n"
            "            print('QF_UNSET:' + _f)\n"
            "    except gdb.error:\n"
            "        print('QF_UNSET:' + _f)\n"
            "end\n"
            "run /tmp/input.bin\nquit\n"
        )
        err, output, _, timed_out = self._run_probe(
            generator_code,
            f"timeout {timeout} gdb --batch -nx --command=/qfprobe/reach.gdb /out/{binary} 2>&1",
            binary_dir=binary_dir,
            env={"ASAN_OPTIONS": "detect_leaks=0:symbolize=0"},
            files={"reach.gdb": script},
            timeout=timeout,
            ptrace=True,
        )
        if err:
            return {"hit": False, "functions_hit": [], "hit_counts": {}, "gdb_output": "", "error": err}
        unset = [ln.split(":", 1)[1].strip() for ln in output.splitlines() if ln.startswith("QF_UNSET:")]
        if "gdb: command not found" in output or "Python scripting is not supported" in output:
            return {"hit": False, "functions_hit": [], "hit_counts": {}, "gdb_output": output[:2000],
                    "error": "gdb could not start in the runtime image"}
        order: list[str] = []
        counts: dict[str, int] = {}
        for line in output.splitlines():
            if line.startswith("QF_HIT:"):
                fn = line.split(":", 1)[1].strip()
                counts[fn] = counts.get(fn, 0) + 1
                if fn not in order:
                    order.append(fn)
        truncated = output[:4096] + ("\n[...output truncated...]" if len(output) > 4096 else "")
        error = None
        if target_function in unset:
            error = (f'gdb could not set a breakpoint on "{target_function}" (not a symbol in the '
                     "binary: check the exact name, a C++ name may need Class::method, or the "
                     "function was inlined or not linked)")
        elif timed_out:
            error = f"timed out after {timeout}s"
        elif not counts and "Running: /tmp/input.bin" not in output:
            # The fuzzer never got to the input (loader error, crash in
            # init): that says nothing about reachability.
            error = "the binary did not run the input under gdb; see gdb_output"
        return {
            "hit": target_function in counts,
            "functions_hit": order,
            "hit_counts": counts,
            "unset_breakpoints": unset,
            "gdb_output": truncated,
            "error": error,
        }

    # ---- single-shot adversarial run --------------------------------------

    def run_blob(
        self,
        generator_code: str,
        *,
        binary_name: str | None = None,
        timeout: int = 25,
    ) -> dict:
        """Run the blob from ``generate()`` through the ASan binary once.

        libFuzzer treats a positional path as a single test input.  Returns
        ``crashed``, ``sanitizer``, ``output`` (4 KB), ``exit_code``,
        ``error``.
        """
        binary = binary_name or self._built_binary
        if not binary or not self._built_binary:
            return {"crashed": False, "sanitizer": None, "output": "", "exit_code": -1,
                    "error": "no successful build_harness yet: build the harness first"}
        binary_dir = self._asan_binary_dir(binary)
        if binary_dir is None:
            return {"crashed": False, "sanitizer": None, "output": "", "exit_code": -1,
                    "error": f"binary {binary} not found"}
        err, output, exit_code, timed_out = self._run_probe(
            generator_code,
            f"/out/{binary} -timeout={timeout} /tmp/input.bin",
            binary_dir=binary_dir,
            env={
                "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1:abort_on_error=0:print_summary=1:symbolize=1",
                "LSAN_OPTIONS": "exitcode=23:print_summary=1",
                "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1",
            },
            timeout=timeout,
        )
        if err:
            return {"crashed": False, "sanitizer": None, "output": "", "exit_code": -1, "error": err}
        # LSan output also ends with an "AddressSanitizer: detected memory
        # leaks" line, so check LSan first.
        sanitizer: str | None = None
        for needle, name in (
            ("LeakSanitizer:", "LeakSanitizer"),
            ("UndefinedBehaviorSanitizer:", "UndefinedBehaviorSanitizer"),
            ("MemorySanitizer:", "MemorySanitizer"),
            ("ThreadSanitizer:", "ThreadSanitizer"),
            ("AddressSanitizer:", "AddressSanitizer"),
        ):
            if needle in output:
                sanitizer = name
                break
        crashed = sanitizer is not None or timed_out or exit_code not in (0, 77)
        truncated = output[:4096] + ("\n[...output truncated...]" if len(output) > 4096 else "")
        return {
            "crashed": crashed,
            "sanitizer": sanitizer,
            "output": truncated,
            "exit_code": exit_code,
            "error": "timeout" if timed_out else None,
        }

    # ---- teardown ----------------------------------------------------------

    def cleanup(self) -> None:
        """Remove the worker workspace (binary, corpus, oss-fuzz worktree)."""
        try:
            self._bv.cleanup_workspace()
        except Exception as exc:
            logger.warning("DynamicRunner cleanup failed: %s", exc)

    # ---- internals ---------------------------------------------------------

    def _require_workspace(self) -> Path:
        if self._workspace_dir is None:
            # build() has not been called yet; prepare the worktree now so
            # that callers can run tools in any order (useful when an agent
            # wants to run coverage on a pre-existing binary layout).
            workspace_dir, _ = prepare_pinned_oss_fuzz_workspace(
                self.source_oss_fuzz_dir, self.worker_workspace,
                pinned_commit=self.pinned_commit,
            )
            self._workspace_dir = workspace_dir
        return self._workspace_dir


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _ensure_dir(p: Path) -> Path:
    p.mkdir(parents=True, exist_ok=True)
    return p


def decode_crash_input_b64(b64: str) -> bytes:
    """Decode base64 crash input from agent (MCP can only pass strings)."""
    return base64.b64decode(b64)
