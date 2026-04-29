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
        tail = result.build_log_tail or ""
        if len(tail) > _MAX_OUTPUT_CHARS:
            tail = tail[-_MAX_OUTPUT_CHARS:]
        return (
            f"BUILD FAILED\n"
            f"Error: {result.build_error}\n\n"
            f"Compiler output (tail):\n{tail}"
        )

    @mcp.tool()
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
        return (
            f"COVERAGE OK  binary={binary_name}  fuzz_seconds={duration}\n"
            f"  lines:     {metrics.lines_covered:>8}/{metrics.lines_total:<8}  ({metrics.lines_pct:5.2f}%)\n"
            f"  branches:  {metrics.branches_covered:>8}/{metrics.branches_total:<8}  ({metrics.branches_pct:5.2f}%)\n"
            f"  functions: {metrics.functions_covered:>8}/{metrics.functions_total:<8}  ({metrics.functions_pct:5.2f}%)\n"
            f"  regions:   {metrics.regions_covered:>8}/{metrics.regions_total:<8}  ({metrics.regions_pct:5.2f}%)"
        )

    @mcp.tool()
    def run_gdb(binary_name: str, crash_input_b64: str):
        """Reproduce a crash under gdb and return the backtrace.

        Decodes `crash_input_b64` (base64-encoded bytes) into a crash
        input file, mounts the fuzzer binary in the project's Docker
        image, and runs ``gdb --batch -ex run -ex 'bt full' -ex
        'info locals' -ex 'info registers'``.  Output is truncated to
        ~4KB.

        Use this only after AP_Run_check reports a crash to decide
        whether the crash is a P1 harness bug (backtrace lives in the
        harness code or libFuzzer runtime) or a real library bug
        (backtrace reaches library internals).

        Args:
            binary_name: Name of the built fuzzer.
            crash_input_b64: Base64-encoded crash input bytes (libFuzzer
                writes these as 'crash-<hash>' files in the corpus_dir
                after a crash).
        """
        if runner is None:
            return unavailable
        try:
            crash_bytes = base64.b64decode(crash_input_b64)
        except (binascii.Error, ValueError) as exc:
            return f"GDB ERROR: crash_input_b64 is not valid base64 ({exc})"
        try:
            output = runner.gdb(
                crash_bytes,
                binary_name=binary_name,
                max_output_bytes=_MAX_OUTPUT_CHARS,
            )
        except Exception as exc:
            logger.exception("run_gdb failed unexpectedly")
            return f"GDB ERROR (infrastructure): {exc}"
        return f"GDB OUTPUT  binary={binary_name}\n\n{output}"

    @mcp.tool()
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
            return f"CHECK ERROR: {result['error']}"
        hit = result["hit"]
        fns = result.get("functions_hit", [])
        status = "HIT" if hit else "MISS"
        return (
            f"TARGET {status}: {target_function}\n"
            f"Functions hit: {', '.join(fns) if fns else '(none)'}\n"
        )
