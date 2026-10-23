"""SystemCheckAgent + run_system_check orchestrator.

This is the top-level entry for the tooled-checks system-check flow.  The
agent is a :class:`BaseAgent` subclass that mounts all six tool
categories via :func:`harness.checker.tools.register_all` plus the flow's
own ``submit_harness`` terminator.  The orchestrator function
:func:`run_system_check` prepares the per-worker workspace, the static
index, the dynamic runner, runs the agent, and writes the result
artifacts to disk.

Per-worker isolation:

    /tmp/agf-ze/system-check-runs/<run-id>/<case-id>/
        workspace/               worker-local OSS-Fuzz worktree
        harness.<ext>            final harness source
        trajectory.jsonl         all LLM + tool events
        result.json              flow-level summary
        build_log.txt            (written if build happens during flow)

Nothing in this module modifies existing code.  All existing modules
(detect.py, builder.py, generation_agent.py, generator.py, etc.) are
imported read-only.
"""

from __future__ import annotations

import asyncio
import getpass as _getpass
import json
import logging
import os
import re
import secrets
import shutil
import threading
import time
from collections import defaultdict
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

import structlog
from jinja2 import Environment, FileSystemLoader, StrictUndefined
from mcp.server.fastmcp import FastMCP

from agent import BaseAgent
from agent.llm_client import LLMResponse
from harness.builder import ensure_repo
from harness.checker.dynamic import DynamicRunner
from harness.checker.lg_agent import LogicGroupAgent
from harness.checker.logic_group import LogicGroup
from harness.checker.callgraph import CallGraph
from harness.checker.static import StaticIndex
from harness.checker.tools import DEFAULT_CATEGORIES, register_all
from harness.checker.tools.context import DEFAULT_LLM_MODEL, CheckerContext
from infra.ossfuzz.workspace import OssFuzzPaths

logger = logging.getLogger(__name__)
log = structlog.get_logger("harness.checker.flow")


# ---------------------------------------------------------------------------
# Paths & defaults
# ---------------------------------------------------------------------------

_USER = _getpass.getuser()
_DEFAULT_RUNS_ROOT = Path(f"/tmp/agf-{_USER}/system-check-runs")
_DEFAULT_PINNED_OSS_FUZZ = Path(f"/tmp/agf-{_USER}/oss-fuzz-pinned")
_DEFAULT_P2_STORAGE = Path(__file__).parent / "rq2_workspace" / "P2_storage"

# QF_PROMPTS_DIR selects a frozen prompt version (qf-prompts/<v>); default: the live prompts.
_PROMPTS_DIR = Path(os.environ.get("QF_PROMPTS_DIR") or Path(__file__).parent / "prompts")
_SUBMIT_TOOL = "submit_harness"

# Shared across workers in the same process — per-project locks to avoid
# concurrent clones and concurrent docker builds on the same image tag.
_clone_locks: dict[str, threading.Lock] = defaultdict(threading.Lock)
_docker_locks: dict[str, threading.Lock] = defaultdict(threading.Lock)


def _jinja_env() -> Environment:
    return Environment(
        loader=FileSystemLoader(str(_PROMPTS_DIR)),
        autoescape=False,
        trim_blocks=True,
        lstrip_blocks=True,
        undefined=StrictUndefined,
    )


def _render(template: str, **kwargs: Any) -> str:
    return _jinja_env().get_template(template).render(kwargs).strip()


_GOLD_PATHS_V2 = (
    Path(__file__).resolve().parent.parent.parent
    / "dataset_v2" / "gold_source_paths.json"
)

_MINIMAL_STUB = """\
/* Minimal stub — original harness replaced for evaluation. */
#include <stdint.h>
#include <stddef.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    return 0;
}
"""


def _stub_gold_harness(
    case_id: str,
    oss_fuzz_project_dir: Path | None,
    project_path: Path | None,
) -> tuple[Path | None, str]:
    """Replace the gold harness with a minimal stub so the agent cannot
    read the original.  Returns ``(path, original_content)`` for
    restoration, or ``(None, "")`` if the gold was not found."""
    if not _GOLD_PATHS_V2.exists():
        return None, ""
    import json as _json
    all_paths = _json.loads(_GOLD_PATHS_V2.read_text())
    container_path = all_paths.get(case_id, "")
    if not container_path:
        return None, ""
    rel = container_path.replace("$SRC/", "")

    # Try oss-fuzz project dir first
    if oss_fuzz_project_dir:
        local = Path(oss_fuzz_project_dir) / rel
        if local.exists():
            original = local.read_text(errors="ignore")
            local.write_text(_MINIMAL_STUB)
            return local, original

    # Try project repo (strip first component = repo name)
    if project_path:
        parts = rel.split("/", 1)
        if len(parts) > 1:
            local = Path(project_path) / parts[1]
            if local.exists():
                original = local.read_text(errors="ignore")
                local.write_text(_MINIMAL_STUB)
                return local, original

    return None, ""


def _restore_gold_harness(path: Path | None, content: str) -> None:
    """Restore the original gold harness after the agent finishes."""
    if path is not None and content:
        try:
            path.write_text(content)
        except OSError:
            pass


def _load_p2_report(case_id: str) -> str:
    """Load the upstream P2 report for *case_id* if one has been generated."""
    if not case_id:
        return ""
    report_path = _DEFAULT_P2_STORAGE / case_id.replace("/", "_") / "info.md"
    if not report_path.is_file():
        return ""
    try:
        report = report_path.read_text()
    except OSError:
        return ""
    if report.startswith("# P2") and "(TODO)" not in report:
        return report
    return ""


def _save_stage_cost(path: Path, r: Any, model: str) -> None:
    """Token use and cost of one pre-generation stage (LG or P2)."""
    path.write_text(json.dumps({
        "model": model,
        "status": r.status,
        "turns": r.total_turns,
        "input_tokens": r.input_tokens,
        "output_tokens": r.output_tokens,
        "estimated_cost": r.estimated_cost,
        "duration_ms": r.duration_ms,
        "time": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }, indent=2))


def _copy_tree_if_exists(src: Path, dst: Path) -> str:
    if not src.exists():
        return ""
    if dst.exists():
        shutil.rmtree(dst, ignore_errors=True)
    if src.is_dir():
        shutil.copytree(src, dst)
    else:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    return str(dst)


# ---------------------------------------------------------------------------
# SystemCheckAgent
# ---------------------------------------------------------------------------


class SystemCheckAgent(BaseAgent):
    """BaseAgent that mounts the system-check tool categories.

    Unlike :class:`GenerationAgent`, this agent has access to static and
    dynamic analysis tools and runs its own quality checks inside the
    turn loop rather than relying on external post-hoc validators.
    """

    agent_type = "system_check"
    max_turns = 300  # high enough that the agent delivers; the paper's Table 5 cap (50) ended runs before any build
    temperature = 0.0
    max_tool_output_tokens = 8000
    enable_compression = True
    # Preserve the most recent build_harness / submit_harness call (the
    # harness source the agent has produced so far) when compressing
    # earlier turns away.
    artifact_tool_names = frozenset({"build_harness", "submit_harness"})
    model = DEFAULT_LLM_MODEL

    # Tools that MUST have been called at least once before submit_harness
    # is allowed to terminate the loop.  Enforced via should_stop +
    # the submit_harness tool body.
    #
    # ``AP_Reach_check`` is the adversarial self-validation probe:
    # the agent submits a per-principle attack blob (a generator() that
    # crafts inputs targeting a specific P1.x / P2.x suspicion) and the
    # runner reports whether the target function was reached.  Requiring
    # it here forces every submission to pass through at least one round
    # of attack-style challenge — the harness is not "I built and ran it"
    # but "I tried to break it and it survived".
    REQUIRED_TOOLS_BEFORE_SUBMIT: frozenset[str] = frozenset({
        "build_harness", "AP_Run_check", "get_coverage",
        "AP_Reach_check",
    })

    def __init__(
        self,
        *,
        ctx: CheckerContext,
        functionality_prompt: str = "",
        target_function: str = "",
        p2_report: str = "",
        default_fuzzer_name: str,
        validation_feedback: str | None = None,
        first_harness_only: bool = False,
    ) -> None:
        if not functionality_prompt and not target_function:
            raise ValueError(
                "SystemCheckAgent needs either functionality_prompt or "
                "target_function (or both)."
            )
        self._ctx = ctx
        self._functionality_prompt = functionality_prompt
        self._target_function = target_function
        self._p2_report = p2_report
        self._default_fuzzer_name = default_fuzzer_name
        self._validation_feedback = validation_feedback
        self._first_harness_only = first_harness_only
        self._harness_code: str = ""
        self._harness_filename: str = ""
        # Tools the agent has called so far in this run.  Updated from
        # should_stop on each turn.
        self._seen_tools: set[str] = set()
        self._turn_count: int = 0
        # If the agent attempts submit_harness with missing preconditions,
        # this carries the rejection reason so the submit_harness tool
        # body can surface it to the agent on tool execution.
        self._submit_blocked_reason: str | None = None
        # The first submission that passes the checks while builds remain is
        # answered with a gap review (system_check_gap_review.md) instead of
        # being accepted; the next submission is accepted.
        self._gap_review_done: bool = False
        self._gap_review_pending: str | None = None

    def create_mcp_server(self) -> FastMCP:
        mcp = FastMCP("system-check")

        if self._first_harness_only:
            # In first-harness-only mode, mount only code_view +
            # static_analysis (no Docker needed).  Add a stub
            # build_harness so the agent can call it and trigger our
            # should_stop interceptor.
            register_all(
                mcp, self._ctx,
                categories=("code_view", "static_analysis"),
            )

            @mcp.tool()
            def build_harness(harness_code: str, filename: str = ""):
                """Compile the harness against the project.

                Args:
                    harness_code: Complete C/C++ source with LLVMFuzzerTestOneInput.
                    filename: Filename with extension (e.g. fuzz_parse.c).
                """
                # Stub: first_harness_only mode captures the code in
                # should_stop and terminates; this return is never seen.
                return "BUILD OK (first-harness-only stub)"
        else:
            # Mount the four load-bearing tool categories.  Stubs
            # (web_fetch, github) are intentionally excluded.
            register_all(mcp, self._ctx, categories=DEFAULT_CATEGORIES)

        # Terminator tool that ends the loop.  Enforces that all
        # required verification tools have been called before submission
        # by surfacing a REJECTED message that the agent must respond to.
        def submit_harness(harness_code: str, filename: str = ""):
            """Submit the final harness.

            Call this ONCE, as your last action, only after all required
            verification tools have been invoked: build_harness;
            AP_Run_check (P1 sanitizer probe — agent supplies one
            attack blob via generator_code); AP_Reach_check (P2 reach
            probe — agent supplies a generator_code aimed at the target
            API); get_coverage (libFuzzer + llvm-cov).  Submission in
            turn 1 is impossible: you must complete build → both AP
            probes → coverage before this call is accepted.  If any
            required tool has not been called this run, the submission
            will be REJECTED and you must call the missing tools first.

            Args:
                harness_code: Complete, compilable C/C++ source.
                filename: Filename with extension (e.g. ``yaml_loader_fuzz.c``).
            """
            if self._submit_blocked_reason:
                reason = self._submit_blocked_reason
                self._submit_blocked_reason = None
                return (
                    f"REJECTED: cannot submit yet — {reason}.  Call the "
                    f"missing tools first, then call submit_harness again. "
                    f"See 'Submission rules' in your system prompt."
                )
            if self._gap_review_pending:
                review = self._gap_review_pending
                self._gap_review_pending = None
                return review
            return "Harness submitted."

        if os.environ.get("QF_NO_AP"):
            # w/o AP ablation: same tool, description without the two probes.
            submit_harness.__doc__ = (
                "Submit the final harness.\n\n"
                "Call this ONCE, as your last action, only after build_harness "
                "and get_coverage (libFuzzer + llvm-cov) have run on the code you "
                "submit.  If a required tool has not been called this run, the "
                "submission will be REJECTED and you must call the missing tools "
                "first.\n\n"
                "Args:\n"
                "    harness_code: Complete, compilable C/C++ source.\n"
                "    filename: Filename with extension (e.g. ``yaml_loader_fuzz.c``).\n"
            )
        mcp.tool()(submit_harness)

        return mcp

    def get_system_prompt(self, **kwargs: Any) -> str:
        # Count mounted tools for the "N tools" reference in the prompt.
        # Best-effort: FastMCP list_tools is async, so we hard-code 17
        # (4 quality + 4 dynamic + 5 static + 3 code_view + 1 submit).
        tool_count = 17
        return _render(
            "system_check_system.md",
            project_path=str(self._ctx.project_path),
            functionality_prompt=self._functionality_prompt,
            target_function=self._target_function,
            p2_report=self._p2_report,
            no_ap=bool(os.environ.get("QF_NO_AP")),
            oss_fuzz_project_dir=str(self._ctx.oss_fuzz_project_dir)
            if self._ctx.oss_fuzz_project_dir
            else "",
            tool_count=tool_count,
            static_available=self._ctx.static_index is not None or self._ctx.callgraph is not None,
        )

    def get_initial_message(self, **kwargs: Any) -> str:
        return _render(
            "system_check_initial.md",
            project_path=str(self._ctx.project_path),
            project=self._ctx.project,
            functionality_prompt=self._functionality_prompt,
            target_function=self._target_function,
            default_fuzzer_name=self._default_fuzzer_name,
            oss_fuzz_project_dir=str(self._ctx.oss_fuzz_project_dir)
            if self._ctx.oss_fuzz_project_dir
            else "",
            validation_feedback=self._validation_feedback or "",
            static_available=self._ctx.static_index is not None or self._ctx.callgraph is not None,
        )

    def get_urgency_message(self) -> str | None:
        return _render(
            "system_check_urgency.md",
            turns_used=self._turn_count,
            max_turns=self.max_turns,
            turns_left=max(0, self.max_turns - self._turn_count),
        )

    def should_stop(self, response: LLMResponse) -> bool:
        # Track every tool the agent has called across all turns.
        self._turn_count += 1
        for tc in response.tool_calls:
            self._seen_tools.add(tc["function"]["name"])

        # In first_harness_only mode, stop as soon as the agent calls
        # build_harness — capture the harness code from its arguments
        # without actually building.
        if self._first_harness_only:
            build_tc = next(
                (tc for tc in response.tool_calls
                 if tc["function"]["name"] == "build_harness"),
                None,
            )
            if build_tc is not None:
                try:
                    args = json.loads(build_tc["function"]["arguments"])
                    self._harness_code = args.get("harness_code", "")
                    self._harness_filename = args.get("filename", "")
                except (json.JSONDecodeError, TypeError):
                    pass
                if self._harness_code:
                    return True

        # Look for a submit_harness call this turn.
        submit_tc = next(
            (tc for tc in response.tool_calls
             if tc["function"]["name"] == _SUBMIT_TOOL),
            None,
        )
        if submit_tc is None:
            return False

        # Verify required tools were called before submit.
        required = self.REQUIRED_TOOLS_BEFORE_SUBMIT
        if os.environ.get("QF_NO_AP"):  # w/o AP ablation: no probe gate
            required = required - {"AP_Run_check", "AP_Reach_check"}
        missing = required - self._seen_tools
        try:
            submitted_code = json.loads(submit_tc["function"]["arguments"]).get("harness_code", "")
        except (json.JSONDecodeError, TypeError, AttributeError):
            submitted_code = ""
        unmet = self._unmet_checks(submitted_code)
        runner = self._ctx.dynamic_runner
        if unmet and runner is not None and getattr(runner, "build_attempts", 0) >= 5:
            # Out of builds: the harness cannot be fixed any more, so accept
            # the last build as is (the unmet checks stay in the trajectory).
            unmet = []
        if unmet and not missing:
            self._submit_blocked_reason = (
                "the submitted build has not passed the submission checks: "
                + "; ".join(unmet)
            )
            return False
        if missing:
            # Block this submission attempt — the submit_harness tool body
            # will read this state and return a REJECTED message that the
            # agent will see in the tool result, hopefully prompting it
            # to call the missing tools and try again.
            self._submit_blocked_reason = (
                f"missing required tool calls: {', '.join(sorted(missing))}"
            )
            return False

        # All preconditions met — extract args from the submit_harness
        # tool call and signal the loop to terminate.
        try:
            args = json.loads(submit_tc["function"]["arguments"])
            code = args.get("harness_code", "")
            filename = args.get("filename", "")
        except (json.JSONDecodeError, TypeError):
            code, filename = "", ""

        # The delivered harness must be one that was built and checked (the
        # final binaries are rebuilt from it): the last successful build or
        # an earlier one, character for character.
        runner = self._ctx.dynamic_runner
        history = getattr(runner, "build_history", None) if runner is not None else None
        if history and code.strip() not in history:
            self._submit_blocked_reason = (
                "harness_code is not the code of any successful build_harness call; "
                "submit exactly the code of one of your successful builds, or build "
                "the new code first"
            )
            return False

        builds_left = 5 - getattr(runner, "build_attempts", 0) if runner is not None else 0
        if not self._gap_review_done and builds_left > 0:
            self._gap_review_done = True
            self._gap_review_pending = _render("system_check_gap_review.md", builds_left=builds_left)
            return False

        self._harness_code = code
        self._harness_filename = filename
        return True

    def _unmet_checks(self, code: str | None = None) -> list[str]:
        """The paper's submission rule, on the submitted build (*code*; the
        last successful build when None or unknown): it ran an agent probe,
        reached the target, and got coverage above zero."""
        runner = self._ctx.dynamic_runner
        if runner is None:
            return []
        if not runner._built_binary:
            return ["no successful build_harness yet"]
        unmet = []
        history = getattr(runner, "build_history", {}) or {}
        checks = history.get(code.strip()) if code else None
        if checks is None:
            checks = runner.checks
        no_ap = bool(os.environ.get("QF_NO_AP"))  # w/o AP ablation: build + coverage only
        if not checks["run_done"] and not no_ap:
            unmet.append("call AP_Run_check on it")
        # The binary may name the target differently (inherited method,
        # inline namespace, template instance): match the last name part.
        want = re.split(r"::", self._target_function)[-1] if self._target_function else ""
        if not no_ap and (not any(want in f for f in checks["reached"]) or not checks["reached"]):
            unmet.append(
                f"AP_Reach_check must report TARGET HIT for {self._target_function or 'your entry function'}"
            )
        if checks["coverage_lines"] <= 0:
            unmet.append("get_coverage must report more than 0 covered lines")
        return unmet

    def get_pinned_status(self) -> str | None:
        runner = self._ctx.dynamic_runner
        if runner is None:
            return None
        checks = runner.checks
        lines = [
            f"- target function: {self._target_function or '(none)'}",
            f"- build_harness attempts used: {getattr(runner, 'build_attempts', 0)} of 5",
            f"- last successful build: {runner._built_binary or 'none yet'}"
            + (" (its source is in your latest build_harness call)" if runner._built_binary else ""),
            f"- on that build: AP_Run_check done={checks['run_done']}; "
            f"AP_Reach_check hit={sorted(checks['reached']) or 'none'}; "
            f"get_coverage lines={checks['coverage_lines']}",
        ]
        unmet = self._unmet_checks()
        lines.append("- submission checks on the last build: " + ("all met" if not unmet else "unmet: " + "; ".join(unmet)))
        history = getattr(runner, "build_history", {}) or {}
        if len(history) > 1:
            lines.append("- successful builds so far (get_coverage lines; any build whose checks "
                         "are met can be submitted with its exact code):")
            for i, c in enumerate(history.values(), 1):
                lines.append(f"  - build {i}: coverage lines={c['coverage_lines'] or 'not measured'}"
                             f"{' (the last build)' if c is runner.checks else ''}")
        return "\n".join(lines)

    def parse_result(self, content: str) -> str | None:
        return self._harness_code or None

    @property
    def submitted(self) -> bool:
        return bool(self._harness_code)


# ---------------------------------------------------------------------------
# Orchestrator
# ---------------------------------------------------------------------------


@dataclass
class SystemCheckCase:
    """Input for a single system-check run.

    Two input modes are supported:

    * **Logic-group mode** — ``functionality_prompt`` is set, describing in
      natural language what to fuzz.  The agent searches for candidate
      entry functions in Phase 1.
    * **Target-function mode** — ``target_function`` is set to a specific
      library symbol.  The agent skips the search and verifies the given
      entry directly, then continues P2/P1/build/run as normal.  This is
      the mode used when comparing against a gold harness (same entry
      function on both sides).

    You may set both — ``functionality_prompt`` then acts as additional
    context telling the agent WHY this entry was chosen, which helps it
    shape the harness (e.g. ``target_function=yaml_parser_load`` with
    ``functionality_prompt="multi-document YAML stream"`` tells the agent
    to loop until end-of-stream instead of parsing a single document).
    """

    project: str
    functionality_prompt: str = ""
    target_function: str = ""
    # The target's declaration and "path:line" where it is declared, as the
    # benchmark gives them to every system.
    target_declaration: str = ""
    target_location: str = ""
    logic_group: LogicGroup | None = None
    repo_url: str = ""
    case_id: str = ""
    fuzzer_name: str = ""
    language: str = "c"
    gold_source_file: str = ""  # e.g. "yaml_loader_fuzzer.c" — for filename convention
    gold_source_code: str = ""  # optional full gold source for side-by-side diff

    def __post_init__(self) -> None:
        if not (
            self.functionality_prompt
            or self.target_function
            or self.logic_group is not None
        ):
            raise ValueError(
                "SystemCheckCase needs at least one of: functionality_prompt, "
                "target_function, logic_group."
            )

    def mode(self) -> str:
        """Resolve the input mode from the populated fields.

        Priority: target_function (Mode C) > logic_group (Mode B) >
        functionality_prompt (Mode A).  This mirrors what the agent
        actually does — if you have a concrete target, use it directly;
        if you have an LG, skip search; if you only have a prompt,
        run the LogicGroupAgent first.
        """
        if self.target_function:
            return "C"
        if self.logic_group is not None:
            return "B"
        return "A"


@dataclass
class SystemCheckResult:
    """Flat record written as ``result.json`` per case.

    Fields that don't apply to a given run (e.g. coverage_* when
    enable_dynamic is False) are left at their defaults rather than
    None-vs-empty ambiguity.
    """

    case_id: str
    project: str
    success: bool

    # Output artifacts
    harness_code: str = ""
    harness_filename: str = ""
    harness_path: str = ""
    harness_binary_path: str = ""
    coverage_binary_path: str = ""
    corpus_dir_path: str = ""
    coverage_report_path: str = ""
    coverage_summary_path: str = ""
    trajectory_path: str = ""
    gold_harness_path: str = ""

    # Stage 1 output (logic group mode)
    logic_group: dict | None = None
    lg_description_delta: float = 0.0

    # Stage 2 output
    target_function: str = ""
    target_source_method: str = ""  # "given" | "lg_selection" | "prompt_search"

    # Quality audit (summarised from trajectory)
    p1_violation: bool = False
    p2_violation: bool = False
    p3_violation: bool = False
    p4_violation: bool = False
    p1_report: str = ""
    p2_report: str = ""
    p3_report: str = ""
    p4_report: str = ""

    # Dynamic
    build_ok: bool | None = None
    build_error: str = ""
    run_edges: int = 0
    run_features: int = 0
    run_corpus_size: int = 0
    run_executions: int = 0
    run_exec_per_second: int = 0
    run_crashed: bool = False
    run_error: str = ""
    coverage_lines_pct: float = 0.0
    coverage_branches_pct: float = 0.0
    coverage_functions_pct: float = 0.0
    coverage_regions_pct: float = 0.0
    coverage_error: str = ""

    # Bookkeeping
    total_turns: int = 0
    duration_ms: int = 0
    input_tokens: int = 0
    output_tokens: int = 0
    estimated_cost: float = 0.0
    llm_model: str = ""
    tool_call_counts: dict = field(default_factory=dict)
    static_index_available: bool = False
    callgraph: str = ""
    error: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


# ---------------------------------------------------------------------------
# Trajectory parser — extracts build/run/cov/check_pN signals from events
# ---------------------------------------------------------------------------


_EDGES_RE = re.compile(r"edges_covered:\s*(\d+)")
_FEATURES_RE = re.compile(r"features:\s*(\d+)")
_CORPUS_RE = re.compile(r"corpus_size:\s*(\d+)")
_EXECS_RE = re.compile(r"total_executions:\s*(\d+)")
_EXEC_S_RE = re.compile(r"exec_per_second:\s*(\d+)")
_COV_LINES_RE = re.compile(r"lines:\s*\d+/\d+\s*\(\s*([\d.]+)%\)")
_COV_BRANCHES_RE = re.compile(r"branches:\s*\d+/\d+\s*\(\s*([\d.]+)%\)")
_COV_FUNCS_RE = re.compile(r"functions:\s*\d+/\d+\s*\(\s*([\d.]+)%\)")
_COV_REGIONS_RE = re.compile(r"regions:\s*\d+/\d+\s*\(\s*([\d.]+)%\)")
_VIOLATION_VERDICT_RE = re.compile(r"Combined verdict:\s*(VIOLATION|CLEAN)", re.IGNORECASE)


def _safe_int(match: re.Match | None) -> int:
    return int(match.group(1)) if match else 0


def _safe_float(match: re.Match | None) -> float:
    return float(match.group(1)) if match else 0.0


def extract_signals_from_trajectory(events: list) -> dict[str, Any]:
    """Walk trajectory events and pull out build/run/coverage/P1-P4 info.

    Expects events as TrajectoryEvent-shaped objects/dicts (whatever
    AgentResult.trajectory yields).  Returns a flat dict of signals to
    merge into SystemCheckResult.
    """
    # Last-seen semantics: agent may call build_harness multiple times;
    # we care about the FINAL outcome.
    signals: dict[str, Any] = {
        "build_ok": None,
        "build_error": "",
        "run_edges": 0,
        "run_features": 0,
        "run_corpus_size": 0,
        "run_executions": 0,
        "run_exec_per_second": 0,
        "run_crashed": False,
        "run_error": "",
        "coverage_lines_pct": 0.0,
        "coverage_branches_pct": 0.0,
        "coverage_functions_pct": 0.0,
        "coverage_regions_pct": 0.0,
        "coverage_error": "",
        "p1_violation": False,
        "p2_violation": False,
        "p3_violation": False,
        "p4_violation": False,
        "p1_report": "",
        "p2_report": "",
        "p3_report": "",
        "p4_report": "",
        "tool_call_counts": {},
    }

    counts: dict[str, int] = defaultdict(int)

    # Pair tool_call / tool_result by tool_call_id.
    pending_calls: dict[str, dict] = {}
    for e in events:
        ev = e if isinstance(e, dict) else getattr(e, "__dict__", {})
        event_type = ev.get("event_type")
        if event_type == "tool_call":
            counts[ev.get("tool_name", "")] += 1
            tcid = ev.get("tool_call_id") or ""
            pending_calls[tcid] = ev
        elif event_type == "tool_result":
            tcid = ev.get("tool_call_id") or ""
            call = pending_calls.pop(tcid, None)
            tool_name = ev.get("tool_name") or (call.get("tool_name") if call else "")
            content = ev.get("content") or ""
            is_error = ev.get("is_error", False)

            if tool_name == "build_harness":
                if content.startswith("BUILD OK"):
                    signals["build_ok"] = True
                    signals["build_error"] = ""
                elif content.startswith("BUILD FAILED") or is_error:
                    signals["build_ok"] = False
                    signals["build_error"] = content[:2000]
            elif tool_name == "AP_Run_check":
                # AP_Run_check is now a single-shot blob probe.  The
                # tool returns "RUN PASS" (sanitizer clean) or
                # "RUN FAIL" + sanitizer trace (P1.x violation candidate).
                if content.startswith("RUN PASS"):
                    signals["run_error"] = ""
                    signals["run_crashed"] = False
                elif content.startswith("RUN FAIL") or is_error:
                    signals["run_error"] = content[:2000]
                    signals["run_crashed"] = True
                elif content.startswith("RUN ERROR"):
                    # Infrastructure / generator failure, not a P1 fire.
                    signals["run_error"] = content[:2000]
                    signals["run_crashed"] = False
            elif tool_name == "get_coverage":
                if content.startswith("COVERAGE OK"):
                    signals["coverage_lines_pct"] = _safe_float(_COV_LINES_RE.search(content))
                    signals["coverage_branches_pct"] = _safe_float(_COV_BRANCHES_RE.search(content))
                    signals["coverage_functions_pct"] = _safe_float(_COV_FUNCS_RE.search(content))
                    signals["coverage_regions_pct"] = _safe_float(_COV_REGIONS_RE.search(content))
                    signals["coverage_error"] = ""
                elif "ERROR" in content or is_error:
                    signals["coverage_error"] = content[:2000]
            elif tool_name in ("check_p1", "check_p2", "check_p3", "check_p4"):
                idx = tool_name[-1]  # "1".."4"
                verdict_match = _VIOLATION_VERDICT_RE.search(content)
                if verdict_match:
                    signals[f"p{idx}_violation"] = (
                        verdict_match.group(1).upper() == "VIOLATION"
                    )
                signals[f"p{idx}_report"] = content[:6000]

    signals["tool_call_counts"] = dict(counts)
    return signals


def run_system_check(
    case: SystemCheckCase,
    *,
    output_root: Path | None = None,
    run_id: str | None = None,
    enable_dynamic: bool = True,
    source_oss_fuzz_dir: Path | None = None,
    oss_fuzz_project_dir: Path | None = None,
    llm_model: str = DEFAULT_LLM_MODEL,
    first_harness_only: bool = False,
    project_path: Path | None = None,
    extra_roots: list[Path] | None = None,
    build_project: str | None = None,
    pinned_commit: str | None = None,
    container_path: str | None = None,
    runtime_image: str | None = None,
) -> SystemCheckResult:
    """Run the full system-check flow for one case.

    Thread-safe when invoked on different workers — uses per-project
    ``_clone_locks`` and ``_docker_locks`` shared at module level.

    Prepared-case mode (the dataset run): *project_path* is a read-only
    source tree exported from the case image (gold already stubbed, git
    history scrubbed), *build_project* the per-case OSS-Fuzz project
    (``<project>-cN``, FROM that image) in *source_oss_fuzz_dir* at
    *pinned_commit*, *container_path* where the harness goes in the image,
    and *runtime_image* the case image plus gdb for probes.  Nothing is
    cloned and no file is stubbed or restored.
    """
    t0 = time.monotonic()
    prepared = project_path is not None

    # ── 1. Clone the project source (cached, per-project lock) ───────────
    if not prepared:
        with _clone_locks[case.project]:
            try:
                project_path = ensure_repo(project=case.project, repo_url=case.repo_url or None)
            except Exception as exc:
                return SystemCheckResult(
                    case_id=case.case_id or case.project,
                    project=case.project,
                    success=False,
                    error=f"ensure_repo failed: {exc}",
                    duration_ms=int((time.monotonic() - t0) * 1000),
                )
    oss_project = build_project or case.project

    # ── 2. Prepare per-worker output & workspace ─────────────────────────
    run_id = run_id or f"run-{int(time.time())}-{os.getpid()}-{secrets.token_hex(3)}"
    runs_root = output_root or _DEFAULT_RUNS_ROOT
    case_id = case.case_id or f"{case.project}/{case.fuzzer_name or 'generated'}"
    case_dir = runs_root / run_id / case_id.replace("/", "_")
    case_dir.mkdir(parents=True, exist_ok=True)
    workspace_dir = case_dir / "workspace"
    workspace_dir.mkdir(parents=True, exist_ok=True)

    log.info(
        "flow.start",
        case_id=case_id,
        project=case.project,
        project_path=str(project_path),
        workspace=str(workspace_dir),
        enable_dynamic=enable_dynamic,
    )

    # ── 3. Load static index (may be None) ──────────────────────────────
    static_index = StaticIndex.load(case.project)
    # +Static ablation only: None unless AGF_CALLGRAPH_DIR is set.
    callgraph = CallGraph.load(case_id, case.project)

    # ── 4. Build DynamicRunner if enabled ───────────────────────────────
    dynamic_runner: DynamicRunner | None = None
    if enable_dynamic:
        try:
            dynamic_runner = DynamicRunner(
                project=oss_project,
                source_oss_fuzz_dir=source_oss_fuzz_dir or _DEFAULT_PINNED_OSS_FUZZ,
                worker_workspace=workspace_dir,
                docker_lock=_docker_locks[oss_project],
                language=case.language,
                fuzzer_name=case.fuzzer_name or None,
                pinned_commit=pinned_commit,
                container_path=container_path,
                runtime_image=runtime_image,
            )
        except Exception as exc:
            log.warning("flow.dynamic_init_failed", error=str(exc))
            dynamic_runner = None

    # ── 4b. Stub gold harness so agent cannot read it ───────────────────
    if prepared:
        gold_stub_path, gold_original = None, ""
    else:
        gold_stub_path, gold_original = _stub_gold_harness(
            case_id, oss_fuzz_project_dir, Path(project_path) if project_path else None,
        )

    # ── 5. Build CheckerContext ─────────────────────────────────────────
    ctx = CheckerContext(
        project=case.project,
        project_path=Path(project_path),
        oss_fuzz_project_dir=oss_fuzz_project_dir,
        allowed_roots=list(extra_roots or [])
        + ([dynamic_runner.build_log_dir] if dynamic_runner else []),
        static_index=static_index,
        callgraph=callgraph,
        dynamic_runner=dynamic_runner,
        llm_model=llm_model,
    )

    default_fuzzer_name = (
        case.fuzzer_name or Path(case.gold_source_file).stem or "generated_harness"
    )

    # ── 5a. Stage 1 — Logic Group (Mode A only) ─────────────────────────
    lg: LogicGroup | None = case.logic_group
    lg_agent_result = None
    mode = case.mode()

    if mode == "A":
        # Need to discover an LG from the prompt first.
        log.info("flow.lg_stage", case_id=case_id, prompt_len=len(case.functionality_prompt))
        lg_agent = LogicGroupAgent(
            ctx=ctx,
            functionality_prompt=case.functionality_prompt,
        )
        try:
            lg_agent_result = asyncio.run(lg_agent.run())
            lg = lg_agent_result.parsed
        except Exception as exc:
            log.error("flow.lg_agent_failed", exc_info=True)
            lg = None
        if lg is None:
            # LG generation failed — fall back to plain prompt mode in
            # the downstream agent so we still produce something.
            log.warning("flow.lg_none_falling_back", case_id=case_id)
        else:
            # Persist the LG as lg.json for downstream + audit.
            try:
                lg.save_json(case_dir / "lg.json")
            except OSError as exc:
                log.warning("flow.lg_save_failed", error=str(exc))

    # ── 5a'. Logic Group around the given target (Mode C, full system) ──
    # QF_LG_DIR turns it on.  The LG and the P2 report built on it are kept
    # in QF_LG_DIR/<case> and reused by later runs of the same case.
    lg_store = (Path(os.environ["QF_LG_DIR"]) / case_id.replace("/", "_")
                if os.environ.get("QF_LG_DIR") else None)
    if mode == "C" and lg_store is not None:
        lg_store.mkdir(parents=True, exist_ok=True)
        if (lg_store / "lg.json").is_file():
            lg = LogicGroup.load_json(lg_store / "lg.json")
        else:
            log.info("flow.lg_target_stage", case_id=case_id, target=case.target_function)
            lg_agent = LogicGroupAgent(
                ctx=ctx,
                target_function=case.target_function,
                target_declaration=case.target_declaration,
                target_location=case.target_location,
                fuzzer_name=default_fuzzer_name,
            )
            try:
                lg_agent_result = asyncio.run(lg_agent.run())
                lg = lg_agent_result.parsed
            except Exception:
                log.error("flow.lg_agent_failed", exc_info=True)
            if lg_agent_result is not None:
                if lg_agent_result.trajectory:
                    lg_agent_result.save_trajectory(lg_store / "lg_trajectory.jsonl")
                _save_stage_cost(lg_store / "lg_cost.json", lg_agent_result, llm_model)
            if lg is not None:
                lg.save_json(lg_store / "lg.json")
        if lg is not None:
            lg.save_json(case_dir / "lg.json")

    # ── 6. Stage 2 — Run the system-check agent ────────────────────────
    # If we have an LG, surface it to the agent via a synthetic prompt
    # that lists the candidate entries.  This is the simplest way to
    # plumb LG information through the existing SystemCheckAgent without
    # changing its constructor signature.
    effective_prompt = case.functionality_prompt
    effective_target = case.target_function
    if lg is not None and not effective_target:
        # Inject LG candidates as a *suggestion*.  The agent should prefer
        # these but is not forced to use them — this lets us measure
        # LG adoption rate as a signal of LG stage usefulness.
        lg_block = (
            "\n\n## Logic Group (pre-computed, advisory)\n"
            f"Name: {lg.name}\n"
            f"Description: {lg.description}\n"
            f"Suggested entry functions: {', '.join(lg.entries)}\n"
        )
        if lg.core:
            lg_block += f"Suggested core implementation functions: {', '.join(lg.core[:20])}\n"
        lg_block += (
            "\nThe Logic Group above is an analysis hint from an earlier "
            "stage.  In Phase 1 you should START by verifying one of the "
            "suggested entries (lookup, reachable, callers).  If after "
            "verification you find the suggestion is wrong or a better "
            "entry exists, you MAY search for and pick a different one — "
            "but you must justify the deviation in your reasoning before "
            "doing so.  Prefer the suggestion when it is reasonable."
        )
        effective_prompt = (case.functionality_prompt or "") + lg_block
    elif lg is not None:
        # Mode C: the target stays the entry; the LG adds the rest of the
        # feature around it.
        effective_prompt = (case.functionality_prompt or "") + (
            "\n\n## Logic Group of this feature (from the Logic Group stage)\n"
            f"Name: {lg.name}\n"
            f"Entries (public API of the feature; the first is the target): {', '.join(lg.entries)}\n"
            + (f"Core (internal functions implementing it): {', '.join(lg.core)}\n" if lg.core else "")
            + f"Description:\n{lg.description}\n"
            "\nThe target function stays the entry your harness must call.  "
            "The other entries are the rest of this feature: call them too, "
            "following the P2 report, so the fuzz input reaches all of the core.  "
            "Where the description lists parameters, flags, modes or formats "
            "that select different paths, derive them from the fuzz input "
            "instead of fixing one.  "
            "The Logic Group adds to the P2 report; it is not a boundary.  What "
            "the P2 report shows real callers doing to build the target's input "
            "or with its results stays in scope even when the function is not "
            "in the Logic Group."
        )

    # ── 5b. Stage 2 — P2 research on the given target (Mode C) ─────────
    # Mode A/B get their P2 report from the pipeline; with a given target
    # the P2 agent researches that target here, as in the paper's Stage 2.
    # QF_NO_P2 (w/o P2 ablation): no P2 report at all; the LG is still used.
    no_p2 = bool(os.environ.get("QF_NO_P2"))
    p2_report = "" if prepared or no_p2 else _load_p2_report(case_id)
    if not no_p2 and lg_store is not None and (lg_store / "p2_report.md").is_file():
        p2_report = (lg_store / "p2_report.md").read_text()
    p2_agent_result = None
    if mode == "C" and not p2_report and not first_harness_only and not no_p2:
        from harness.checker.p2_agent import P2Agent

        log.info("flow.p2_stage", case_id=case_id, target=case.target_function)
        p2_agent = P2Agent(
            ctx=ctx,
            entries=[case.target_function],
            target_declaration=case.target_declaration,
            target_location=case.target_location,
            # The benchmark task text is the downstream agent's task, not the
            # researcher's; only a real Logic Group description goes here.
            lg_description=lg.description if lg is not None else "",
            lg_entries=lg.entries if lg is not None else None,
            lg_core=lg.core if lg is not None else None,
        )
        try:
            p2_agent_result = asyncio.run(p2_agent.run())
            p2_report = p2_agent_result.parsed or ""
        except Exception:
            log.error("flow.p2_agent_failed", exc_info=True)
        if p2_agent_result is not None and p2_agent_result.trajectory:
            p2_agent_result.save_trajectory(case_dir / "p2_trajectory.jsonl")
        if p2_report:
            (case_dir / "p2_report.md").write_text(p2_report)
        if lg_store is not None and lg is not None:
            if p2_agent_result is not None:
                if p2_agent_result.trajectory:
                    p2_agent_result.save_trajectory(lg_store / "p2_trajectory.jsonl")
                _save_stage_cost(lg_store / "p2_cost.json", p2_agent_result, llm_model)
            if p2_report:
                (lg_store / "p2_report.md").write_text(p2_report)
    elif lg_store is not None and p2_report:
        (case_dir / "p2_report.md").write_text(p2_report)

    # QF_PREPARE_ONLY: stop after the LG and P2 stages (they are stored).
    if os.environ.get("QF_PREPARE_ONLY"):
        if dynamic_runner:
            dynamic_runner.cleanup()
        cost = sum(r.estimated_cost for r in (lg_agent_result, p2_agent_result) if r is not None)
        return SystemCheckResult(
            case_id=case_id,
            project=case.project,
            success=lg is not None and bool(p2_report),
            logic_group=lg.to_dict() if lg is not None else None,
            target_function=case.target_function,
            p2_report=p2_report,
            estimated_cost=cost,
            llm_model=llm_model,
            error="" if lg is not None and p2_report else "LG or P2 stage produced nothing",
            duration_ms=int((time.monotonic() - t0) * 1000),
            callgraph=str(callgraph.path) if callgraph else "",
        )

    agent = SystemCheckAgent(
        ctx=ctx,
        functionality_prompt=effective_prompt,
        target_function=effective_target,
        p2_report=p2_report,
        default_fuzzer_name=default_fuzzer_name,
        first_harness_only=first_harness_only,
    )

    try:
        agent_result = asyncio.run(agent.run())
    except Exception as exc:
        log.error("flow.agent_failed", exc_info=True)
        _restore_gold_harness(gold_stub_path, gold_original)
        if dynamic_runner:
            dynamic_runner.cleanup()
        return SystemCheckResult(
            case_id=case_id,
            project=case.project,
            success=False,
            error=f"agent.run() failed: {exc}",
            duration_ms=int((time.monotonic() - t0) * 1000),
            static_index_available=static_index is not None,
            callgraph=str(callgraph.path) if callgraph else "",
        )
    finally:
        _restore_gold_harness(gold_stub_path, gold_original)

    # ── 7. Persist trajectory ───────────────────────────────────────────
    trajectory_path = case_dir / "trajectory.jsonl"
    if agent_result.trajectory:
        agent_result.save_trajectory(trajectory_path)

    harness_code = (agent_result.parsed or "") if agent.submitted else ""
    harness_filename = agent._harness_filename or f"{default_fuzzer_name}.{case.language}"
    if "." not in harness_filename:
        harness_filename += f".{case.language}"

    # If the agent never submitted (turn limit, text-only reply), fall back
    # to the last harness that built; delivery.json records which it was.
    delivered_from = "submit" if agent.submitted else ""
    if not harness_code and dynamic_runner is not None and dynamic_runner._last_harness_code:
        harness_code = dynamic_runner._last_harness_code
        harness_filename = dynamic_runner._last_source_filename or harness_filename
        delivered_from = "last_build"

    # The agent may name the harness by its in-tree path (util-linux:
    # libfdisk/src/script.c); the case dir keeps only the basename.
    harness_path = case_dir / Path(harness_filename).name
    if harness_code:
        harness_path.write_text(harness_code)

    # ── 7a. Parse trajectory → build/run/cov/P1-P4 signals ──────────────
    signals = extract_signals_from_trajectory(agent_result.trajectory or [])

    # ── 7b. Copy compiled binaries out of the worker worktree ───────────
    harness_binary_path = ""
    coverage_binary_path = ""
    corpus_dir_path = ""
    coverage_report_path = ""
    coverage_summary_path = ""
    if dynamic_runner is not None and dynamic_runner._built_binary:
        bin_dir = case_dir / "binaries"
        bin_dir.mkdir(parents=True, exist_ok=True)
        address_src_bin = dynamic_runner._address_binary_snapshot
        if address_src_bin is not None and address_src_bin.is_file():
            dst_bin = bin_dir / dynamic_runner._built_binary
            try:
                shutil.copy2(address_src_bin, dst_bin)
                harness_binary_path = str(dst_bin)
            except OSError as exc:
                log.warning("flow.binary_copy_failed", error=str(exc))

        coverage_src_bin = (
            dynamic_runner.worker_workspace
            / "oss-fuzz" / "build" / "out" / dynamic_runner.project
            / dynamic_runner._built_binary
        )
        if coverage_src_bin.is_file():
            suffix = ".cov" if dynamic_runner._coverage_built else ""
            dst_bin = bin_dir / f"{dynamic_runner._built_binary}{suffix}"
            try:
                shutil.copy2(coverage_src_bin, dst_bin)
                if dynamic_runner._coverage_built:
                    coverage_binary_path = str(dst_bin)
                elif not harness_binary_path:
                    harness_binary_path = str(dst_bin)
            except OSError as exc:
                log.warning("flow.binary_copy_failed", error=str(exc))

        paths = OssFuzzPaths(dynamic_runner.worker_workspace / "oss-fuzz")
        corpus_src = paths.corpus_dir(dynamic_runner.project, f"{dynamic_runner._built_binary}_systemcheck")
        corpus_dir_path = _copy_tree_if_exists(corpus_src, case_dir / "corpus")

        coverage_summary_src = paths.coverage_summary(dynamic_runner.project, dynamic_runner._built_binary)
        coverage_summary_path = _copy_tree_if_exists(
            coverage_summary_src,
            case_dir / "coverage" / "summary.json",
        )

        coverage_report_src = (
            paths.build_out(dynamic_runner.project) / "report_target" / dynamic_runner._built_binary / "linux"
        )
        coverage_report_path = _copy_tree_if_exists(
            coverage_report_src,
            case_dir / "coverage" / "report",
        )

    # ── 7c. Copy gold harness for diff convenience ─────────────────────
    gold_harness_path = ""
    if case.gold_source_code and case.gold_source_file:
        gp = case_dir / f"gold_{case.gold_source_file}"
        try:
            gp.write_text(case.gold_source_code)
            gold_harness_path = str(gp)
        except OSError:
            pass

    # ── 7d. Write per-principle audit reports ──────────────────────────
    reports_dir = case_dir / "reports"
    reports_dir.mkdir(parents=True, exist_ok=True)
    for p in ("p1", "p2", "p3", "p4"):
        text = signals.get(f"{p}_report", "")
        if text:
            (reports_dir / f"{p}.md").write_text(text)

    # ── 7e. Compute description delta (if Mode A produced an LG) ───────
    lg_description_delta = 0.0
    if mode == "A" and lg is not None and case.functionality_prompt and lg.description:
        try:
            from harness.checker.scripts.description_delta import (
                score_description_delta_sync,
            )
            delta_result = score_description_delta_sync(
                case.functionality_prompt, lg.description, model=llm_model
            )
            lg_description_delta = delta_result.similarity
        except Exception as exc:
            log.warning("flow.delta_score_failed", error=str(exc))

    # Track LG agent stats merged into the case totals.
    lg_agent_turns = lg_agent_result.total_turns if lg_agent_result else 0
    lg_agent_in_tokens = lg_agent_result.input_tokens if lg_agent_result else 0
    lg_agent_out_tokens = lg_agent_result.output_tokens if lg_agent_result else 0
    lg_agent_cost = lg_agent_result.estimated_cost if lg_agent_result else 0.0
    # P2 agent stats join the LG ones in the case totals.
    if p2_agent_result is not None:
        lg_agent_turns += p2_agent_result.total_turns
        lg_agent_in_tokens += p2_agent_result.input_tokens
        lg_agent_out_tokens += p2_agent_result.output_tokens
        lg_agent_cost += p2_agent_result.estimated_cost

    # Determine target_source_method.
    if case.target_function:
        tsm = "given"
    elif lg is not None:
        tsm = "lg_selection"
    else:
        tsm = "prompt_search"

    if p2_report:
        signals["p2_report"] = p2_report[:20000]

    # ── 8. Build result summary ────────────────────────────────────────
    result = SystemCheckResult(
        case_id=case_id,
        project=case.project,
        success=bool(harness_code),
        harness_code=harness_code,
        harness_filename=harness_filename,
        harness_path=str(harness_path) if harness_code else "",
        harness_binary_path=harness_binary_path,
        coverage_binary_path=coverage_binary_path,
        corpus_dir_path=corpus_dir_path,
        coverage_report_path=coverage_report_path,
        coverage_summary_path=coverage_summary_path,
        trajectory_path=str(trajectory_path) if agent_result.trajectory else "",
        gold_harness_path=gold_harness_path,
        logic_group=lg.to_dict() if lg is not None else None,
        lg_description_delta=lg_description_delta,
        target_function=case.target_function,
        target_source_method=tsm,
        total_turns=agent_result.total_turns + lg_agent_turns,
        duration_ms=int((time.monotonic() - t0) * 1000),
        input_tokens=agent_result.input_tokens + lg_agent_in_tokens,
        output_tokens=agent_result.output_tokens + lg_agent_out_tokens,
        estimated_cost=agent_result.estimated_cost + lg_agent_cost,
        llm_model=llm_model,
        static_index_available=static_index is not None,
        callgraph=str(callgraph.path) if callgraph else "",
        **signals,
    )

    # ── 8a. Delivery record: what the final build compiles ─────────────
    if harness_code:
        import hashlib
        delivery = {
            "case_id": case_id,
            "delivered_from": delivered_from,
            "harness_file": harness_path.name,
            "harness_sha256": hashlib.sha256(harness_code.encode()).hexdigest(),
            "oss_fuzz_project": oss_project,
            "oss_fuzz_commit": pinned_commit or "",
            "container_path": container_path or "",
            "fuzzer_name": dynamic_runner._built_binary if dynamic_runner else case.fuzzer_name,
            "runtime_image": runtime_image or "",
            "agent_binaries": {"asan": harness_binary_path, "coverage": coverage_binary_path},
        }
        (case_dir / "delivery.json").write_text(json.dumps(delivery, indent=2))

    # Persist flow-level result
    (case_dir / "result.json").write_text(
        json.dumps(result.to_dict(), indent=2, default=str)
    )

    log.info(
        "flow.done",
        case_id=case_id,
        success=result.success,
        turns=result.total_turns,
        duration_ms=result.duration_ms,
        cost=result.estimated_cost,
        build_ok=result.build_ok,
        run_edges=result.run_edges,
        cov_lines=result.coverage_lines_pct,
    )

    if dynamic_runner:
        try:
            dynamic_runner.cleanup()
        except Exception:
            pass

    return result


# ---------------------------------------------------------------------------
# Batch helper
# ---------------------------------------------------------------------------


def run_batch(
    cases: list[SystemCheckCase],
    *,
    max_workers: int = 1,
    output_root: Path | None = None,
    run_id: str | None = None,
    enable_dynamic: bool = True,
    source_oss_fuzz_dir: Path | None = None,
    oss_fuzz_project_dir: Path | None = None,
    llm_model: str = DEFAULT_LLM_MODEL,
    mode: str = "auto",
    first_harness_only: bool = False,
    cli_args: dict | None = None,
) -> list[SystemCheckResult]:
    """Run a batch of cases in parallel via ThreadPoolExecutor.

    Respects the module-level ``_clone_locks`` / ``_docker_locks`` so
    workers on the same project serialize cloning and Docker builds.

    Writes three batch-level files into ``<output_root>/<run_id>/``:
        * ``run_manifest.json`` — run_id, start/end time, git SHA, model,
          mode, case count, CLI args
        * ``prompts.jsonl``       — exact input used for this run
        * ``results.jsonl``       — one flattened result per case
        * ``summary.json``        — aggregates (success rate, means,
          tool call frequency, per-project breakdown)
        * ``error_log.jsonl``     — failed cases (append-only, partial
          safe if run is interrupted)
    """
    from concurrent.futures import ThreadPoolExecutor, as_completed

    run_id = run_id or f"run-{int(time.time())}-{os.getpid()}-{secrets.token_hex(3)}"
    runs_root = output_root or _DEFAULT_RUNS_ROOT
    batch_dir = Path(runs_root) / run_id
    batch_dir.mkdir(parents=True, exist_ok=True)

    start_time = time.time()
    results: list[SystemCheckResult] = []

    # Write prompts.jsonl — the exact input.
    with open(batch_dir / "prompts.jsonl", "w") as f:
        for c in cases:
            f.write(json.dumps({
                "case_id": c.case_id,
                "project": c.project,
                "fuzzer_name": c.fuzzer_name,
                "target_function": c.target_function,
                "logic_group": c.logic_group.to_dict() if c.logic_group is not None else None,
                "functionality_prompt": c.functionality_prompt,
                "repo_url": c.repo_url,
                "language": c.language,
                "gold_source_file": c.gold_source_file,
            }) + "\n")

    # Write run_manifest.json before any work starts (so interrupted runs
    # can still be identified).
    run_manifest = {
        "run_id": run_id,
        "start_time": start_time,
        "end_time": None,
        "case_count": len(cases),
        "max_workers": max_workers,
        "mode": mode,
        "enable_dynamic": enable_dynamic,
        "llm_model": llm_model,
        "git_sha": _get_git_sha(),
        "cli_args": cli_args or {},
        "source_oss_fuzz_dir": str(source_oss_fuzz_dir) if source_oss_fuzz_dir else "",
    }
    (batch_dir / "run_manifest.json").write_text(
        json.dumps(run_manifest, indent=2)
    )

    error_log = batch_dir / "error_log.jsonl"
    partial_results = batch_dir / "results.partial.jsonl"

    def _append_result(r: SystemCheckResult) -> None:
        with open(partial_results, "a") as f:
            f.write(json.dumps(r.to_dict(), default=str) + "\n")
        if not r.success or r.error:
            with open(error_log, "a") as f:
                f.write(json.dumps({
                    "case_id": r.case_id,
                    "project": r.project,
                    "error": r.error,
                    "build_error": r.build_error,
                    "run_error": r.run_error,
                    "coverage_error": r.coverage_error,
                }, default=str) + "\n")

    def _maybe_partial_summary(n: int) -> None:
        if n > 0 and n % 20 == 0:
            _write_summary(batch_dir, results, start_time, partial=True)
            log.info("batch.partial_summary", completed=n, total=len(cases))

    if max_workers <= 1 or len(cases) <= 1:
        for case in cases:
            try:
                r = run_system_check(
                    case,
                    output_root=output_root,
                    run_id=run_id,
                    enable_dynamic=enable_dynamic,
                    source_oss_fuzz_dir=source_oss_fuzz_dir,
                    oss_fuzz_project_dir=oss_fuzz_project_dir,
                    llm_model=llm_model,
                    first_harness_only=first_harness_only,
                )
            except Exception as exc:
                log.error("flow.case_crashed", case=case.case_id, exc_info=True)
                r = SystemCheckResult(
                    case_id=case.case_id or case.project,
                    project=case.project,
                    success=False,
                    error=f"case crashed: {exc}",
                )
            results.append(r)
            _append_result(r)
            _maybe_partial_summary(len(results))
    else:
        with ThreadPoolExecutor(max_workers=max_workers) as pool:
            futures = {
                pool.submit(
                    run_system_check,
                    case,
                    output_root=output_root,
                    run_id=run_id,
                    enable_dynamic=enable_dynamic,
                    source_oss_fuzz_dir=source_oss_fuzz_dir,
                    oss_fuzz_project_dir=oss_fuzz_project_dir,
                    llm_model=llm_model,
                    first_harness_only=first_harness_only,
                ): case
                for case in cases
            }
            for fut in as_completed(futures):
                case = futures[fut]
                try:
                    r = fut.result()
                except Exception as exc:
                    log.error("flow.worker_crashed", case=case.case_id, exc_info=True)
                    r = SystemCheckResult(
                        case_id=case.case_id or case.project,
                        project=case.project,
                        success=False,
                        error=f"worker crashed: {exc}",
                    )
                results.append(r)
                _append_result(r)
                _maybe_partial_summary(len(results))

    # Finalise batch-level outputs.
    end_time = time.time()
    run_manifest["end_time"] = end_time
    run_manifest["duration_seconds"] = end_time - start_time
    (batch_dir / "run_manifest.json").write_text(json.dumps(run_manifest, indent=2))

    # Atomic results.jsonl (rename the partial to final).
    if partial_results.exists():
        partial_results.rename(batch_dir / "results.jsonl")

    _write_summary(batch_dir, results, start_time, partial=False)

    return results


def _get_git_sha() -> str:
    """Return current HEAD SHA, or '' if not in a git repo."""
    try:
        import subprocess
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            capture_output=True, text=True, timeout=5,
            cwd=str(Path(__file__).resolve().parent),
        )
        return result.stdout.strip() if result.returncode == 0 else ""
    except Exception:
        return ""


def _write_summary(
    batch_dir: Path,
    results: list[SystemCheckResult],
    start_time: float,
    *,
    partial: bool,
) -> None:
    """Aggregate results into summary.json."""
    if not results:
        return

    total = len(results)
    succ = sum(1 for r in results if r.success)
    build_ok = sum(1 for r in results if r.build_ok is True)
    build_attempted = sum(1 for r in results if r.build_ok is not None)
    total_cost = sum(r.estimated_cost for r in results)
    total_in_tokens = sum(r.input_tokens for r in results)
    total_out_tokens = sum(r.output_tokens for r in results)
    total_turns = sum(r.total_turns for r in results)

    # Tool call frequency across all cases.
    tool_totals: dict[str, int] = defaultdict(int)
    for r in results:
        for tool, cnt in (r.tool_call_counts or {}).items():
            tool_totals[tool] += cnt

    # Per-project breakdown.
    per_project: dict[str, dict] = {}
    for r in results:
        p = per_project.setdefault(
            r.project,
            {"total": 0, "success": 0, "build_ok": 0, "mean_edges": 0, "mean_lines_pct": 0},
        )
        p["total"] += 1
        if r.success:
            p["success"] += 1
        if r.build_ok is True:
            p["build_ok"] += 1
        p["mean_edges"] += r.run_edges
        p["mean_lines_pct"] += r.coverage_lines_pct
    for p in per_project.values():
        if p["total"] > 0:
            p["mean_edges"] /= p["total"]
            p["mean_lines_pct"] /= p["total"]

    mean_edges = (
        sum(r.run_edges for r in results if r.run_edges) / max(1, sum(1 for r in results if r.run_edges))
    )
    mean_lines = (
        sum(r.coverage_lines_pct for r in results if r.coverage_lines_pct)
        / max(1, sum(1 for r in results if r.coverage_lines_pct))
    )

    summary = {
        "partial": partial,
        "start_time": start_time,
        "elapsed_seconds": time.time() - start_time,
        "total_cases": total,
        "successful_submissions": succ,
        "build_ok_cases": build_ok,
        "build_attempted_cases": build_attempted,
        "total_cost_usd": round(total_cost, 4),
        "mean_cost_usd": round(total_cost / total, 4) if total else 0,
        "total_input_tokens": total_in_tokens,
        "total_output_tokens": total_out_tokens,
        "mean_turns": total_turns / total if total else 0,
        "mean_edges_covered": round(mean_edges, 2),
        "mean_lines_coverage_pct": round(mean_lines, 2),
        "tool_call_frequency": dict(tool_totals),
        "per_project": per_project,
    }

    name = "summary.partial.json" if partial else "summary.json"
    (batch_dir / name).write_text(json.dumps(summary, indent=2))
