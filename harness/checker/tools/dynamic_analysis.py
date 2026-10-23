"""MCP tools backed by the DynamicRunner (build / run / coverage / gdb).

Agent-callable wrappers around :class:`harness.checker.dynamic.DynamicRunner`.
Each tool returns human-readable text summarising the outcome.

Cost model (so the agent knows when to call):
    build_harness    ~30-120s  (first call) / ~20-60s (incremental)
    AP_Run_check     ~5-30s   (= AP Probe 2 in the paper: agent supplies
                     a Python generator() that emits one attack blob;
                     the blob is executed once through the ASan/LSan
                     binary; returns pass/fail + sanitizer trace)
    AP_Reach_check   ~10-30s  (= AP Probe 1 in the paper: agent
                     supplies a Python generator(); blob runs under
                     GDB with breakpoint at target API; returns
                     hit/miss + observed call sequence)
    get_coverage     ~10 min  (default duration=600s libFuzzer first
                     to populate a corpus, then coverage rebuild +
                     llvm-cov)
    run_gdb          ~10-30s  (post-crash debug helper)

If ``ctx.dynamic_runner`` is ``None`` (dynamic analysis disabled for this
flow), each tool returns an "unavailable" message so the agent can still
complete a draft without dynamic verification.
"""

from __future__ import annotations

import base64
import os
import binascii
import logging

from mcp.server.fastmcp import FastMCP

from harness.checker.tools.context import CheckerContext

logger = logging.getLogger(__name__)

_MAX_OUTPUT_CHARS = 4000  # cap per-tool output so it fits the LLM context

# Hard ceiling on build attempts per harness-generation run.  Matches the
# per-system "build-retry budget of 5" the paper claims for fairness with
# OFG / PromeFuzz baselines.  After the 5th invocation, build_harness
# returns BUILD BUDGET EXHAUSTED and the agent must submit best-effort.
_MAX_BUILD_ATTEMPTS = 5


def register(mcp: FastMCP, ctx: CheckerContext) -> None:
    # QF_NO_AP (w/o AP ablation): the two adversarial probes are not offered.
    ap_tool = (lambda f: f) if os.environ.get("QF_NO_AP") else mcp.tool()
    runner = ctx.dynamic_runner
    project = ctx.project
    unavailable = (
        f"Dynamic analysis disabled for project '{project}'.  Cannot build "
        f"or run the harness in this flow.  Complete your self-review with "
        f"static + LLM checks and submit the harness."
    )

    # Per-run mutable counter, captured by the build_harness closure below.
    # One register() call mounts tools for one agent run, so the counter
    # is naturally scoped to a single case.
    _state: dict[str, int] = {"build_attempts": 0}

    @mcp.tool()
    def build_harness(harness_code: str, filename: str):
        """Compile `harness_code` as this project's fuzzer.

        The source is written into the worker's OSS-Fuzz worktree at the
        canonical gold-source location for this fuzzer, then the project
        is built via OSS-Fuzz helper.py.  Returns 'BUILD OK' or a
        compiler-error tail.

        This is expensive (~30-120 seconds) — call it when you want to
        verify your draft actually compiles against the real project
        headers and link against the real library.  A compile error from
        this tool is the ground truth for P2 (wrong API usage shows up
        as a type error, missing symbol, or link failure).

        Args:
            harness_code: Complete C/C++ source, must define
                LLVMFuzzerTestOneInput.
            filename: Name to give the source file in the project
                build tree (e.g. "yaml_loader_fuzz.c").  The binary name
                will be the filename without its extension.
        """
        if runner is None:
            return unavailable
        # Enforce the 5-attempt budget BEFORE invoking the runner so a
        # blocked attempt does not consume Docker/build time.  The agent
        # is expected to submit best-effort after the cap is hit.
        _state["build_attempts"] += 1
        runner.build_attempts = _state["build_attempts"]
        if _state["build_attempts"] > _MAX_BUILD_ATTEMPTS:
            return (
                f"BUILD BUDGET EXHAUSTED: {_MAX_BUILD_ATTEMPTS} build attempts "
                f"have been used (this is the {_state['build_attempts']}th).  "
                f"Stop trying to build — submit the best harness you have so "
                f"far via submit_harness.  This budget matches the OFG / "
                f"PromeFuzz baselines and is a hard limit; further "
                f"build_harness calls will return this same error."
            )
        try:
            result = runner.build(harness_code, filename)
        except Exception as exc:
            logger.exception("build_harness failed unexpectedly")
            return f"BUILD ERROR (infrastructure): {exc}"
        if result.build_ok:
            return (
                f"BUILD OK  binary={result.binary_name}\n"
                f"The compiled fuzzer is ready.  Next: write a generator() "
                f"that emits an attack blob and call "
                f"AP_Run_check(generator_code, {result.binary_name!r}) for "
                f"a P1 sanitizer probe, AP_Reach_check(generator_code, "
                f"target_function, {result.binary_name!r}) for a P2 reach "
                f"probe, or get_coverage({result.binary_name!r}, "
                f"duration=600) for a libFuzzer corpus + llvm-cov pass."
            )
        # The log starts with the compiler/linker error lines (see
        # infra.ossfuzz.build._error_excerpt) and ends with the raw tail;
        # keep both ends.
        log_text = result.build_log_tail or ""
        if len(log_text) > 8000:
            log_text = log_text[:6000] + "\n[...]\n" + log_text[-2000:]
        msg = (
            f"BUILD FAILED\n"
            f"Error: {result.build_error}\n\n"
            f"Compiler output:\n{log_text}"
        )
        if result.build_log_path:
            n = sum(1 for _ in open(result.build_log_path, errors="replace"))
            msg += (
                f"\n\nFull build log ({n} lines): {result.build_log_path}\n"
                f"The output above is an excerpt (compiler/linker error lines, "
                f"then the log tail).  If it does not show why the build "
                f"failed, Grep or Read the full log."
            )
        return msg

    @ap_tool
    def AP_Run_check(generator_code: str, binary_name: str = ""):
        """**AP Probe 2 (run check).**  Adversarial single-shot run.

        Write a Python function ``generate() -> bytes`` in *generator_code*
        that produces an attack blob aimed at a P1.x sub-check (e.g.
        oversize input, malformed length prefix, embedded null, double-
        free trigger, leaked allocator path).  The system executes the
        generator, feeds the resulting blob to the ASan/LSan binary as a
        single test input, and returns whether any sanitizer fired.

        Use this to (P1) verify the harness does not crash on hostile
        input — an immediate sanitizer fire on a blob you did not even
        try to make malicious is a harness bug (UAF, missing NULL,
        unaligned read, stale state) rather than a library bug.

        Example generator_code::

            def generate() -> bytes:
                # max-size buffer; tests P1.5 (size guard) and P1.6
                # (boundary handling).
                return b"\\x00" * (1 << 16)

        Args:
            generator_code: Python source defining ``generate() -> bytes``.
            binary_name: Fuzzer binary name (default: last built).
        """
        if runner is None:
            return unavailable
        try:
            result = runner.run_blob(
                generator_code,
                binary_name=binary_name or None,
            )
        except Exception as exc:
            logger.exception("AP_Run_check failed unexpectedly")
            return f"RUN ERROR (infrastructure): {exc}"
        if result.get("error") and not result.get("crashed"):
            return f"RUN ERROR: {result['error']}"
        verdict = "FAIL" if result.get("crashed") else "PASS"
        runner.checks["run_done"] = True
        sanitizer = result.get("sanitizer") or "—"
        exit_code = result.get("exit_code", -1)
        lines = [
            f"RUN {verdict}  binary={binary_name or '(last built)'}  "
            f"sanitizer={sanitizer}  exit_code={exit_code}",
        ]
        if result.get("crashed"):
            lines.append("")
            lines.append("Captured output (truncated):")
            lines.append(result.get("output", ""))
            lines.append("")
            lines.append(
                "A sanitizer fire on agent-supplied input is a P1 violation. "
                "Read your harness for the bug (unaligned reads, UAF, "
                "missing NULL check, wrong variable, stale state).  Call "
                "run_gdb if you need the exact backtrace."
            )
        return "\n".join(lines)

    @mcp.tool()
    def get_coverage(binary_name: str, duration: int = 600):
        """Run libFuzzer for `duration` seconds to populate a corpus,
        then rebuild with SANITIZER=coverage and run llvm-cov.  Returns
        line / branch / function / region coverage percentages.

        **DO NOT PASS THE `duration` ARGUMENT.**  The benchmark protocol
        requires exactly 600 seconds of empty-corpus libFuzzer run for
        results to be comparable across cases and against the gold
        baseline.  Default 600 is correct.

        Empty corpus, no seeds, fresh `duration`-second cold-start.
        After the fuzz session the project is rebuilt under coverage
        instrumentation; this overwrites the address-sanitized binary,
        so AP_Run_check / AP_Reach_check / run_gdb must NOT be called
        after get_coverage.

        Call order should always be:
          1. build_harness(source, filename)
          2. AP_Run_check(generator_code, binary_name)    ← uses ASan binary
          3. AP_Reach_check(generator_code, target, ...)  ← uses ASan binary
          4. (optional) run_gdb(...)                      ← uses ASan binary
          5. get_coverage(binary_name)                    ← libFuzzer + llvm-cov
          6. submit_harness

        Use this to (P4) quantify whether the harness reaches its
        intended logic cone.  Low line coverage (< 1%) on a well-chosen
        entry usually means the fuzzer is bailing out early — often a
        P2 setup bug rather than a P1 logic bug.

        Args:
            binary_name: Name of the built fuzzer.
            duration: Seconds for the libFuzzer pre-run.  Default 600.
                Do not change unless benchmarking a different scenario.
        """
        if runner is None:
            return unavailable
        try:
            metrics = runner.coverage(binary_name=binary_name, fuzz_seconds=duration)
        except Exception as exc:
            logger.exception("get_coverage failed unexpectedly")
            return f"COVERAGE ERROR (infrastructure): {exc}"
        if metrics.error:
            return f"COVERAGE ERROR: {metrics.error}"
        runner.checks["coverage_lines"] = metrics.lines_covered
        fuzz = getattr(runner, "last_fuzz_run", None)
        note = ""
        if fuzz is not None and fuzz.crash:
            note = (
                f"\n\nWARNING: the fuzzer crashed and was restarted {fuzz.restarts} time(s) "
                f"during the {duration} s (as in the evaluation, it continues on the kept corpus "
                f"after a crash, leak, OOM or timeout, but every restart costs fuzzing time).\n"
                f"Distinct crashes, with how often each occurred:\n{fuzz.crash}\n"
                "A crash found this quickly is most likely a bug in your harness (P1): fix it "
                "and rebuild. If you are confident it is a real library bug deep in the library "
                "code, leave it: do not steer the harness around it or silence the sanitizer."
            )
        elif fuzz is not None and fuzz.elapsed_seconds < 0.9 * duration:
            note = (
                f"\n\nWARNING: the fuzzer stopped after {fuzz.elapsed_seconds:.0f} s of {duration} s: "
                f"{fuzz.error or 'no crash report was printed'}"
            )
        speed = ""
        if fuzz is not None and fuzz.total_executions:
            speed = (f"  executions:  {fuzz.total_executions} in {fuzz.elapsed_seconds:.0f} s "
                     f"({fuzz.total_executions / max(1.0, fuzz.elapsed_seconds):.0f}/s), "
                     f"corpus {fuzz.corpus_size} inputs\n")
        return (
            f"COVERAGE OK  binary={binary_name}  fuzz_seconds={duration}\n"
            + speed
            + f"  lines:     {metrics.lines_covered:>8}/{metrics.lines_total:<8}  ({metrics.lines_pct:5.2f}%)\n"
            f"  branches:  {metrics.branches_covered:>8}/{metrics.branches_total:<8}  ({metrics.branches_pct:5.2f}%)\n"
            f"  functions: {metrics.functions_covered:>8}/{metrics.functions_total:<8}  ({metrics.functions_pct:5.2f}%)\n"
            f"  regions:   {metrics.regions_covered:>8}/{metrics.regions_total:<8}  ({metrics.regions_pct:5.2f}%)"
            + note
        )

    @mcp.tool()
    def run_gdb(generator_code: str, gdb_commands: str = "", binary_name: str = ""):
        """Run the ASan fuzzer binary under gdb on a crafted input.

        Write a Python function ``generate() -> bytes`` in
        *generator_code*; the system runs it in the project's runtime
        container (no network) and writes the bytes to
        ``/tmp/input.bin``.  The binary is ``/out/<binary_name>``.

        *gdb_commands* is a gdb script, one command per line, e.g.::

            break png_read_info
            run /tmp/input.bin
            bt
            info args
            finish

        Breakpoints, ``commands`` blocks, ``print``, ``x``, ``finish``
        and ``info locals`` all work.  If the script has no ``run``
        line, ``run /tmp/input.bin`` then ``bt`` are appended, so an
        empty script reproduces a crash and prints its backtrace.
        ASan aborts on the first error, so gdb stops at the faulting
        frame; leak detection is off under gdb.  As in any gdb command
        file, a command that fails (e.g. `break` on an unknown symbol)
        stops the rest of the script.

        Use it to tell a harness bug (P1: backtrace in harness code)
        from a library bug, or to inspect the arguments the harness
        actually passes to a library call.  Output is truncated to
        ~8KB.

        Args:
            generator_code: Python source defining generate() -> bytes.
            gdb_commands: gdb script (may be empty).
            binary_name: Fuzzer binary name (default: last built).
        """
        if runner is None:
            return unavailable
        try:
            output = runner.gdb(
                generator_code,
                gdb_commands,
                binary_name=binary_name or None,
                max_output_bytes=_MAX_OUTPUT_CHARS,
            )
        except Exception as exc:
            logger.exception("run_gdb failed unexpectedly")
            return f"GDB ERROR (infrastructure): {exc}"
        return f"GDB OUTPUT  binary={binary_name or '(last built)'}\n\n{output}"

    @ap_tool
    def AP_Reach_check(
        generator_code: str,
        target_function: str,
        binary_name: str = "",
    ):
        """**AP Probe 1 (reach check).**  Check whether a specific
        library function is reached by a crafted input.

        Write a Python function ``generate() -> bytes`` in
        *generator_code* that produces an input blob.  The system
        executes it, feeds the blob to the ASan harness binary under
        GDB with a breakpoint on *target_function*, and reports
        whether the breakpoint was hit.

        Use this after a successful build to verify that your harness
        actually exercises the intended target function.  If the
        function is not reached, your P2 setup (init sequence,
        parameter mapping) likely needs fixing.

        Example generator_code::

            def generate() -> bytes:
                # minimal valid YAML document
                return b"key: value\\n"

        Args:
            generator_code: Python source defining generate() -> bytes.
            target_function: Library function name to check (e.g.
                "yaml_parser_load").
            binary_name: Fuzzer binary name (default: last built).
        """
        if runner is None:
            return unavailable
        try:
            result = runner.check_target_reached(
                generator_code,
                target_function,
                binary_name=binary_name or None,
            )
        except Exception as exc:
            logger.exception("AP_Reach_check failed unexpectedly")
            return f"CHECK ERROR (infrastructure): {exc}"
        if result.get("error"):
            tail = result.get("gdb_output", "")[-1500:]
            return f"CHECK ERROR: {result['error']}" + (f"\n\n{tail}" if tail else "")
        hit = result["hit"]
        if hit:
            runner.checks["reached"].add(target_function)
        counts = result.get("hit_counts", {})
        status = "HIT" if hit else "MISS"
        return (
            f"TARGET {status}: {target_function}"
            + (f" (hit {counts.get(target_function, 0)} times)" if hit else "")
            + "\n"
        )
