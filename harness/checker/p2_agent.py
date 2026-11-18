"""P2Agent — investigates API protocol for entry functions.

A BaseAgent subclass that produces a structured P2 protocol report
for a set of entry functions.  The report covers P2.1-P2.8 (init
sequence, parameter construction, object lifecycle, return value
handling, cleanup, API existence, co-call constraints, prerequisite
state) with claim+evidence format citing file:line from the source.

Mounted tools:
    * ``code_view``       — read files, list directories, ripgrep
    * ``static_analysis`` — call graph queries
    * ``submit_p2_report``— terminator, ends the loop with a report

Dynamic and quality_check tools are NOT mounted — P2 research does
not need to build or run the project.
"""

from __future__ import annotations

import os
import logging
from pathlib import Path
from typing import Any

from jinja2 import Environment, FileSystemLoader, StrictUndefined
from mcp.server.fastmcp import FastMCP

from agent import BaseAgent
from agent.llm_client import LLMResponse
from harness.checker.tools import register_all
from harness.checker.tools.context import DEFAULT_LLM_MODEL, CheckerContext

logger = logging.getLogger(__name__)

# QF_PROMPTS_DIR selects a frozen prompt version (qf-prompts/<v>); default: the live prompts.
_PROMPTS_DIR = Path(os.environ.get("QF_PROMPTS_DIR") or Path(__file__).parent / "prompts")
_SUBMIT_TOOL = "submit_p2_report"


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


class P2Agent(BaseAgent):
    """BaseAgent that produces a P2 protocol report for given entry functions."""

    agent_type = "p2_research"
    max_turns = 300  # high enough that the agent finishes; the paper's Table 5 cap (30) cut runs short
    temperature = 0.0
    max_tool_output_tokens = 16000
    enable_compression = True
    artifact_tool_names = frozenset({"submit_p2_report"})
    model = DEFAULT_LLM_MODEL

    def __init__(
        self,
        *,
        ctx: CheckerContext,
        entries: list[str],
        target_declaration: str = "",
        target_location: str = "",
        lg_description: str = "",
        lg_entries: list[str] | None = None,
        lg_core: list[str] | None = None,
    ) -> None:
        if not entries:
            raise ValueError("P2Agent needs at least one entry function")
        self._ctx = ctx
        self._entries = entries
        self._target_declaration = target_declaration
        self._target_location = target_location
        self._lg_description = lg_description
        self._lg_entries = list(lg_entries or [])
        self._lg_core = list(lg_core or [])
        self._submitted_report: str | None = None
        self._report_parts: list[str] = []

    # ---- BaseAgent interface ------------------------------------------

    def create_mcp_server(self) -> FastMCP:
        mcp = FastMCP("p2-research")

        # Narrow tool surface: code_view + static_analysis only.
        register_all(
            mcp, self._ctx,
            categories=("code_view", "static_analysis"),
            code_view_profile="research",
        )

        @mcp.tool()
        def append_p2_findings(section: str) -> str:
            """Append a section to the P2 report being built.

            Call this after investigating each entry function or P2 sub-check.
            You can call it multiple times.  When all findings are appended,
            call ``finish_p2_report`` to finalize.

            Args:
                section: Markdown text for one section of the P2 report
                         (e.g., findings for one entry function, or one
                         P2 sub-check).
            """
            if not section or len(section.strip()) < 10:
                return "ERROR: Section too short, provide meaningful content."
            self._report_parts.append(section.strip())
            return f"Appended ({len(section)} chars). Total sections: {len(self._report_parts)}. Call finish_p2_report when done."

        @mcp.tool()
        def finish_p2_report() -> str:
            """Finalize and submit the P2 report.  Ends the research loop.

            Call this AFTER appending all sections via ``append_p2_findings``.
            The report is assembled from all appended sections.
            """
            if not self._report_parts:
                return (
                    "ERROR: No sections appended yet.  Use append_p2_findings "
                    "to add your investigation results first."
                )
            self._submitted_report = "\n\n".join(self._report_parts)
            return f"P2 report finalized ({len(self._submitted_report)} chars, {len(self._report_parts)} sections)."

        @mcp.tool()
        def submit_p2_report(report: str) -> str:
            """Submit the entire P2 report at once.  Ends the research loop.

            Alternative to append_p2_findings + finish_p2_report.
            Use this ONLY if your report is short enough to fit in one call.
            For longer reports, use append_p2_findings instead.

            Args:
                report: Complete P2 protocol report in markdown.
            """
            if not report or len(report.strip()) < 50:
                return (
                    "ERROR: Report is too short.  Investigate the entry "
                    "functions and produce a detailed P2.1-P2.11 report "
                    "before submitting."
                )
            self._submitted_report = report.strip()
            return "P2 report submitted successfully."

        return mcp

    def get_system_prompt(self, **kwargs: Any) -> str:
        return _render(
            "p2_agent_system.md",
            project=self._ctx.project,
            project_path=str(self._ctx.project_path),
            static_available=self._ctx.static_index is not None or self._ctx.callgraph is not None,
        )

    def get_initial_message(self, **kwargs: Any) -> str:
        return _render(
            "p2_agent_initial.md",
            project=self._ctx.project,
            project_path=str(self._ctx.project_path),
            entries=self._entries,
            target_declaration=self._target_declaration,
            target_location=self._target_location,
            lg_description=self._lg_description,
            lg_entries=self._lg_entries,
            lg_core=self._lg_core,
            oss_fuzz_project_dir=(
                str(self._ctx.oss_fuzz_project_dir)
                if self._ctx.oss_fuzz_project_dir
                else ""
            ),
            static_available=self._ctx.static_index is not None or self._ctx.callgraph is not None,
        )

    def get_urgency_message(self) -> str | None:
        return (
            "You are close to running out of turns.  Submit your P2 report "
            "NOW via submit_p2_report.  A partial report with the entry "
            "functions you've investigated is better than no report at all."
        )

    def should_stop(self, response: LLMResponse) -> bool:
        for tc in response.tool_calls:
            name = tc["function"]["name"]
            if name in (_SUBMIT_TOOL, "finish_p2_report"):
                if self._submitted_report is not None:
                    return True
        return False

    def parse_result(self, content: str) -> str | None:
        if self._submitted_report:
            return self._submitted_report
        # Fallback 1: assemble from appended parts even if finish was never called
        if self._report_parts:
            logger.warning(
                "p2_agent.fallback_parts",
                extra={"msg": f"Assembling P2 report from {len(self._report_parts)} appended parts (finish was not called)"},
            )
            return "\n\n".join(self._report_parts)
        # Fallback 2: extract from last LLM text content
        if content and ("P2.1" in content or "Init Sequence" in content):
            logger.warning(
                "p2_agent.fallback_content",
                extra={"msg": "Extracting P2 report from LLM content (submit was not called)"},
            )
            return content.strip()
        return None
