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
_DEFAULT_PINNED_OSS_FUZZ = Path(f"/tmp/agf-{_USER}/oss-fuzz-pinned")
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
    ) -> None:
        self.project = project
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

        self._bv = _BuildValidatorKeepArtifacts(
            project=project,
            oss_fuzz_dir=self.source_oss_fuzz_dir,
            workspace_root=self.worker_workspace,
            sanitizer=sanitizer,
            fuzzer_name=fuzzer_name,
            docker_lock=docker_lock,
            language=language,
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

    # ---- build -------------------------------------------------------------

    def build(self, harness_code: str, filename: str) -> DynamicRunResult:
        """Compile *harness_code* as ``filename`` inside this worker's worktree.

        On success, remembers the built binary name for later run/coverage.
        This is the address-sanitized build.  See :meth:`coverage` for the
        coverage rebuild path.
        """
        result = self._bv.validate(harness_code, filename)
        if not result.success:
            return DynamicRunResult(
                build_ok=False,
                build_error=result.error,
                build_log_tail=result.build_log[-2000:] if result.build_log else "",
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
        # A fresh address build invalidates any prior coverage build.
        self._coverage_built = False
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

        # 1. Run libFuzzer to populate the systemcheck corpus.  Skip if
        #    the corpus already has files (a previous coverage() call or
        #    test fixture pre-populated it) or if the caller explicitly
        #    set fuzz_seconds=0.
        paths = OssFuzzPaths(workspace)
        sc_dir = paths.corpus_dir(self.project, f"{binary}_systemcheck")
        already_populated = sc_dir.is_dir() and any(sc_dir.iterdir())
        if fuzz_seconds > 0 and not already_populated and not self._coverage_built:
            try:
                run_result = self.run(duration=fuzz_seconds, binary_name=binary)
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

    # ---- gdb ---------------------------------------------------------------

    def gdb(
        self,
        crash_input: bytes,
        *,
        binary_name: str | None = None,
        max_output_bytes: int = 4096,
    ) -> str:
        """Reproduce a crash under gdb inside the project's Docker image.

        Writes *crash_input* to a temp file, mounts both build/out and the
        crash file into the container, runs ``gdb --batch -ex run -ex 'bt full'
        -ex 'info locals'`` on the fuzzer binary.  Returns the first
        ``max_output_bytes`` bytes of combined stdout+stderr.
        """
        workspace = self._require_workspace()
        binary = binary_name or self._built_binary
        if not binary:
            raise RuntimeError("DynamicRunner.gdb() called without a built binary")

        paths = OssFuzzPaths(workspace)
        out_dir = paths.build_out(self.project)
        if not (out_dir / binary).is_file():
            return f"Error: binary {binary} not found at {out_dir}"

        with tempfile.NamedTemporaryFile(
            prefix="crash-", suffix=".bin", dir=self.worker_workspace, delete=False
        ) as f:
            f.write(crash_input)
            crash_path = Path(f.name)

        gdb_script = (
            "set pagination off\n"
            "set print pretty on\n"
            "run /crash.bin\n"
            "bt full\n"
            "info locals\n"
            "info registers\n"
            "quit\n"
        )
        cmd = [
            "docker", "run", "--rm",
            "-v", f"{out_dir}:/out:ro",
            "-v", f"{crash_path}:/crash.bin:ro",
            f"gcr.io/oss-fuzz/{self.project}",
            "bash", "-c",
            f"echo '{gdb_script}' | gdb --batch --command=/dev/stdin /out/{binary}",
        ]
        try:
            result = subprocess.run(
                cmd, capture_output=True, text=True, timeout=120, errors="replace"
            )
            combined = (result.stdout or "") + "\n" + (result.stderr or "")
        except subprocess.TimeoutExpired:
            combined = "Error: gdb timed out after 120s"
        except Exception as exc:
            combined = f"Error: gdb failed: {exc}"
        finally:
            try:
                crash_path.unlink()
            except OSError:
                pass
        if len(combined) > max_output_bytes:
            combined = combined[:max_output_bytes] + "\n[...gdb output truncated...]"
        return combined

    # ---- targeted reachability check ----------------------------------------

    def check_target_reached(
        self,
        generator_code: str,
        target_function: str,
        *,
        binary_name: str | None = None,
        timeout: int = 10,
    ) -> dict:
        """Generate an input blob via Python code and check if *target_function*
        is hit when the harness processes it.

        *generator_code* must define a ``generate() -> bytes`` function.  The
        returned bytes are fed to the ASan binary under GDB with a breakpoint
        on *target_function*.

        Returns a dict with keys:
            hit (bool): whether the breakpoint was reached
            functions_hit (list[str]): all breakpointed functions that fired
            gdb_output (str): raw GDB output (truncated)
            error (str | None): if something went wrong
        """
        workspace = self._require_workspace()
        binary = binary_name or self._built_binary
        if not binary:
            raise RuntimeError("check_target_reached() called without a built binary")

        # Prefer the ASan snapshot (survives coverage rebuild).
        if self._address_binary_snapshot and self._address_binary_snapshot.is_file():
            binary_dir = self._address_binary_snapshot.parent
        else:
            paths = OssFuzzPaths(workspace)
            binary_dir = paths.build_out(self.project)
        if not (binary_dir / binary).is_file():
            return {"hit": False, "functions_hit": [], "gdb_output": "",
                    "error": f"binary {binary} not found at {binary_dir}"}

        # 1. Execute the generator code to produce a blob.
        blob: bytes = b""
        try:
            ns: dict = {}
            exec(generator_code, ns)  # noqa: S102
            gen_fn = ns.get("generate")
            if gen_fn is None:
                return {"hit": False, "functions_hit": [], "gdb_output": "",
                        "error": "generator_code must define generate() -> bytes"}
            result_blob = gen_fn()
            if isinstance(result_blob, (bytes, bytearray)):
                blob = bytes(result_blob)
            else:
                return {"hit": False, "functions_hit": [], "gdb_output": "",
                        "error": f"generate() returned {type(result_blob).__name__}, expected bytes"}
        except Exception as exc:
            return {"hit": False, "functions_hit": [], "gdb_output": "",
                    "error": f"generator_code execution failed: {exc}"}

        # 2. Write blob to temp file.
        with tempfile.NamedTemporaryFile(
            prefix="reach-", suffix=".bin", dir=self.worker_workspace, delete=False
        ) as f:
            f.write(blob)
            input_path = Path(f.name)

        # 3. Run under GDB with breakpoint on target_function inside the
        # project's OSS-Fuzz Docker image.  The ASan binary was built
        # against the container's glibc and won't load reliably on the
        # host (host glibc is typically older), so we mount build/out
        # read-only and the input blob read-only and run GDB inside.
        gdb_script_path = self.worker_workspace / "gdb_reach.cmd"
        gdb_script_path.write_text(
            "set pagination off\n"
            "set confirm off\n"
            f"break {target_function}\n"
            "commands\n"
            f"  printf \"HIT_FUNCTION:{target_function}\\n\"\n"
            "  continue\n"
            "end\n"
            "run /input.bin\n"
            "quit\n"
        )
        cmd = [
            "docker", "run", "--rm",
            "-v", f"{binary_dir}:/out:ro",
            "-v", f"{input_path}:/input.bin:ro",
            "-v", f"{gdb_script_path}:/gdb.cmd:ro",
            f"gcr.io/oss-fuzz/{self.project}",
            "bash", "-c",
            f"gdb --batch --command=/gdb.cmd /out/{binary}",
        ]
        try:
            proc = subprocess.run(
                cmd, capture_output=True, text=True,
                timeout=timeout + 30, errors="replace",
            )
            combined = (proc.stdout or "") + "\n" + (proc.stderr or "")
        except subprocess.TimeoutExpired:
            combined = f"Error: docker gdb timed out after {timeout + 30}s"
        except Exception as exc:
            combined = f"Error: docker gdb failed: {exc}"
        finally:
            for p in (input_path, gdb_script_path):
                try:
                    p.unlink()
                except OSError:
                    pass

        # 4. Parse output for HIT_FUNCTION markers.
        functions_hit = []
        for line in combined.splitlines():
            if line.startswith("HIT_FUNCTION:"):
                fn = line.split(":", 1)[1].strip()
                if fn and fn not in functions_hit:
                    functions_hit.append(fn)

        truncated = combined[:4096]
        if len(combined) > 4096:
            truncated += "\n[...output truncated...]"

        return {
            "hit": target_function in functions_hit,
            "functions_hit": functions_hit,
            "gdb_output": truncated,
            "error": None,
        }

    # ---- single-shot adversarial run --------------------------------------

    def run_blob(
        self,
        generator_code: str,
        *,
        binary_name: str | None = None,
        timeout: int = 10,
    ) -> dict:
        """Generate one input blob via Python and run it through the ASan
        binary as a single-shot.  Returns whether a sanitizer fired.

        *generator_code* must define ``generate() -> bytes``.  The blob is
        written to a temp file and passed as ``argv[1]`` to the
        address/leak-sanitized fuzzer binary; libFuzzer treats a positional
        path as a single test input and exits after running it.

        Returns a dict with keys:
            crashed (bool): a sanitizer report (ASan/LSan/UBSan/MSan) or
                signal-fatal exit was observed
            sanitizer (str | None): sanitizer family if detected
            output (str): captured stdout+stderr (truncated to 4096B)
            exit_code (int): process exit code
            error (str | None): infrastructure error if any
        """
        workspace = self._require_workspace()
        binary = binary_name or self._built_binary
        if not binary:
            raise RuntimeError("DynamicRunner.run_blob() called without a built binary")

        # Locate the ASan binary (snapshot survives coverage rebuilds).
        if self._address_binary_snapshot and self._address_binary_snapshot.is_file():
            binary_dir = self._address_binary_snapshot.parent
        else:
            paths = OssFuzzPaths(workspace)
            binary_dir = paths.build_out(self.project)
        binary_path = binary_dir / binary
        if not binary_path.is_file():
            return {
                "crashed": False,
                "sanitizer": None,
                "output": "",
                "exit_code": -1,
                "error": f"binary {binary} not found at {binary_dir}",
            }

        # 1. Execute the generator to materialise the blob.
        try:
            ns: dict = {}
            exec(generator_code, ns)  # noqa: S102
            gen_fn = ns.get("generate")
            if gen_fn is None:
                return {
                    "crashed": False, "sanitizer": None, "output": "",
                    "exit_code": -1,
                    "error": "generator_code must define generate() -> bytes",
                }
            result_blob = gen_fn()
            if not isinstance(result_blob, (bytes, bytearray)):
                return {
                    "crashed": False, "sanitizer": None, "output": "",
                    "exit_code": -1,
                    "error": (
                        f"generate() returned {type(result_blob).__name__}, "
                        "expected bytes"
                    ),
                }
            blob = bytes(result_blob)
        except Exception as exc:
            return {
                "crashed": False, "sanitizer": None, "output": "",
                "exit_code": -1,
                "error": f"generator_code execution failed: {exc}",
            }

        # 2. Persist blob to a temp file; libFuzzer expects a path.
        with tempfile.NamedTemporaryFile(
            prefix="run-", suffix=".bin", dir=self.worker_workspace, delete=False
        ) as f:
            f.write(blob)
            input_path = Path(f.name)

        # 3. Single-shot exec inside the project's OSS-Fuzz Docker image.
        # The ASan binary was built against the container's glibc, so it
        # cannot reliably run on the host (host glibc is often older than
        # the OSS-Fuzz base image, e.g. 2.36 vs 2.38).  Mount build/out
        # read-only and the input blob read-only, then invoke the binary
        # under the container's runtime.
        out_dir_for_mount = (
            self._address_binary_snapshot.parent
            if self._address_binary_snapshot and self._address_binary_snapshot.is_file()
            else OssFuzzPaths(workspace).build_out(self.project)
        )
        asan_opts = (
            "detect_leaks=1:halt_on_error=1:abort_on_error=0:"
            "print_summary=1:symbolize=1"
        )
        lsan_opts = "exitcode=23:print_summary=1"
        ubsan_opts = "print_stacktrace=1:halt_on_error=1"
        cmd = [
            "docker", "run", "--rm",
            "-e", f"ASAN_OPTIONS={asan_opts}",
            "-e", f"LSAN_OPTIONS={lsan_opts}",
            "-e", f"UBSAN_OPTIONS={ubsan_opts}",
            "-v", f"{out_dir_for_mount}:/out:ro",
            "-v", f"{input_path}:/input.bin:ro",
            f"gcr.io/oss-fuzz/{self.project}",
            "bash", "-c",
            f"/out/{binary} /input.bin",
        ]
        try:
            proc = subprocess.run(
                cmd, capture_output=True, text=True,
                timeout=timeout + 30, errors="replace",
            )
            output = (proc.stdout or "") + (proc.stderr or "")
            exit_code = proc.returncode
            timed_out = False
        except subprocess.TimeoutExpired as exc:
            output = (exc.stdout or b"") + (exc.stderr or b"")
            if isinstance(output, bytes):
                output = output.decode("utf-8", errors="replace")
            exit_code = -9
            timed_out = True
        except Exception as exc:
            try:
                input_path.unlink()
            except OSError:
                pass
            return {
                "crashed": False, "sanitizer": None, "output": "",
                "exit_code": -1,
                "error": f"docker exec failed: {exc}",
            }
        finally:
            try:
                input_path.unlink()
            except OSError:
                pass

        # 4. Detect sanitizer reports.  Order matters: LeakSanitizer
        #    output also contains a final "AddressSanitizer: detected
        #    memory leaks" summary line because LSan is integrated into
        #    the ASan runtime — check LSan-specific markers first so we
        #    don't misclassify a leak as ASan.
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
        crashed = (
            sanitizer is not None
            or timed_out
            or (exit_code != 0 and exit_code != 77)  # 77 = libFuzzer "test passed"
        )

        truncated = output[:4096]
        if len(output) > 4096:
            truncated += "\n[...output truncated...]"

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
                self.source_oss_fuzz_dir, self.worker_workspace
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
