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

_PROMPTS_DIR = Path(__file__).parent / "prompts"
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
    Path(__file__).parent.parent.parent
    / "benchmark" / "oss_fuzz_harness" / "coverage"
    / "data" / "gold_source_paths_v2.json"
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
    max_turns = 50  # paper Table 5 production cap (Harness Generator)
    temperature = 0.0
    max_tool_output_tokens = 8000
    max_context_tokens = 500_000
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
        @mcp.tool()
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
                    f"See 'Submission preconditions' in your system prompt."
                )
            return "Harness submitted."

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
            oss_fuzz_project_dir=str(self._ctx.oss_fuzz_project_dir)
            if self._ctx.oss_fuzz_project_dir
            else "",
            tool_count=tool_count,
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
        missing = self.REQUIRED_TOOLS_BEFORE_SUBMIT - self._seen_tools
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
            self._harness_code = args.get("harness_code", "")
            self._harness_filename = args.get("filename", "")
        except (json.JSONDecodeError, TypeError):
            self._harness_code = ""
            self._harness_filename = ""
        return True

    def parse_result(self, content: str) -> str | None:
        return self._harness_code or None


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
) -> SystemCheckResult:
    """Run the full system-check flow for one case.

    Thread-safe when invoked on different workers — uses per-project
    ``_clone_locks`` and ``_docker_locks`` shared at module level.
    """
    t0 = time.monotonic()

    # ── 1. Clone the project source (cached, per-project lock) ───────────
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

    # ── 4. Build DynamicRunner if enabled ───────────────────────────────
    dynamic_runner: DynamicRunner | None = None
    if enable_dynamic:
        try:
            dynamic_runner = DynamicRunner(
                project=case.project,
                source_oss_fuzz_dir=source_oss_fuzz_dir or _DEFAULT_PINNED_OSS_FUZZ,
                worker_workspace=workspace_dir,
                docker_lock=_docker_locks[case.project],
                language=case.language,
                fuzzer_name=case.fuzzer_name or None,
            )
        except Exception as exc:
            log.warning("flow.dynamic_init_failed", error=str(exc))
            dynamic_runner = None

    # ── 4b. Stub gold harness so agent cannot read it ───────────────────
    gold_stub_path, gold_original = _stub_gold_harness(
        case_id, oss_fuzz_project_dir, Path(project_path) if project_path else None,
    )

    # ── 5. Build CheckerContext ─────────────────────────────────────────
    ctx = CheckerContext(
        project=case.project,
        project_path=Path(project_path),
        oss_fuzz_project_dir=oss_fuzz_project_dir,
        static_index=static_index,
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

    agent = SystemCheckAgent(
        ctx=ctx,
        functionality_prompt=effective_prompt,
        target_function=effective_target,
        p2_report=_load_p2_report(case_id),
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
        )
    finally:
        _restore_gold_harness(gold_stub_path, gold_original)

    # ── 7. Persist trajectory ───────────────────────────────────────────
    trajectory_path = case_dir / "trajectory.jsonl"
    if agent_result.trajectory:
        agent_result.save_trajectory(trajectory_path)

    harness_code = agent_result.parsed or ""
    harness_filename = agent._harness_filename or f"{default_fuzzer_name}.{case.language}"
    if "." not in harness_filename:
        harness_filename += f".{case.language}"

    harness_path = case_dir / harness_filename
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
            / "oss-fuzz" / "build" / "out" / case.project
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
        corpus_src = paths.corpus_dir(case.project, f"{dynamic_runner._built_binary}_systemcheck")
        corpus_dir_path = _copy_tree_if_exists(corpus_src, case_dir / "corpus")

        coverage_summary_src = paths.coverage_summary(case.project, dynamic_runner._built_binary)
        coverage_summary_path = _copy_tree_if_exists(
            coverage_summary_src,
            case_dir / "coverage" / "summary.json",
        )

        coverage_report_src = (
            paths.build_out(case.project) / "report_target" / dynamic_runner._built_binary / "linux"
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
    if lg is not None and case.functionality_prompt and lg.description:
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

    # Determine target_source_method.
    if case.target_function:
        tsm = "given"
    elif lg is not None:
        tsm = "lg_selection"
    else:
        tsm = "prompt_search"

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
        **signals,
    )

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
