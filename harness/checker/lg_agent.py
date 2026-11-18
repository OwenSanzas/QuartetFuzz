"""LogicGroupAgent — discovers a LogicGroup from (project, prompt).

A BaseAgent subclass symmetric to :class:`SystemCheckAgent`, but with a
narrower responsibility:

    Input:  a natural-language description of what the user wants to fuzz
    Output: a :class:`LogicGroup` describing the feature — name,
            description (agent's own summary), entry candidates, core set

Mounted tools:

    * ``code_view``      — read files, list directories, ripgrep
    * ``static_analysis``— call graph queries (callers, callees, reach,
                           lookup, is_public_api)
    * ``submit_logic_group`` — terminator, ends the loop with a LogicGroup

Dynamic and quality_check tools are NOT mounted — LG generation does
not need to build or run the project and does not need to self-audit
P1-P4 (that's the downstream agent's job).
"""

from __future__ import annotations

import os
import json
import logging
from pathlib import Path
from typing import Any

from jinja2 import Environment, FileSystemLoader, StrictUndefined
from mcp.server.fastmcp import FastMCP

from agent import BaseAgent
from agent.llm_client import LLMResponse
from harness.checker.logic_group import LogicGroup
from harness.checker.tools import register_all
from harness.checker.tools.context import DEFAULT_LLM_MODEL, CheckerContext

logger = logging.getLogger(__name__)

# QF_PROMPTS_DIR selects a frozen prompt version (qf-prompts/<v>); default: the live prompts.
_PROMPTS_DIR = Path(os.environ.get("QF_PROMPTS_DIR") or Path(__file__).parent / "prompts")
_SUBMIT_TOOL = "submit_logic_group"


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


class LogicGroupAgent(BaseAgent):
    """BaseAgent that produces a LogicGroup for a given project + prompt."""

    agent_type = "logic_group"
    max_turns = 300  # high enough that the agent finishes; the paper's Table 5 cap (50) cut runs short
    temperature = 0.0
    max_tool_output_tokens = 8000
    enable_compression = True
    artifact_tool_names = frozenset({"submit_logic_group"})
    model = DEFAULT_LLM_MODEL

    def __init__(
        self,
        *,
        ctx: CheckerContext,
        functionality_prompt: str = "",
        target_function: str = "",
        target_declaration: str = "",
        target_location: str = "",
        fuzzer_name: str = "",
    ) -> None:
        # With a target function (the benchmark's Mode C) the LG is built
        # around it (lg_target_*.md); otherwise it is discovered from the prompt.
        if not (functionality_prompt or target_function):
            raise ValueError("LogicGroupAgent needs a functionality_prompt or a target_function")
        self._ctx = ctx
        self._functionality_prompt = functionality_prompt
        self._target_function = target_function
        self._target_declaration = target_declaration
        self._target_location = target_location
        self._fuzzer_name = fuzzer_name
        self._submitted_lg: LogicGroup | None = None
        self._submit_error: str = ""

    # ---- BaseAgent interface ------------------------------------------

    def create_mcp_server(self) -> FastMCP:
        mcp = FastMCP("logic-group")

        # Narrow tool surface: code_view + static_analysis only.  No
        # dynamic (LG gen doesn't build), no quality_check (those are for
        # the downstream agent auditing a written harness, not for
        # choosing what to fuzz).
        register_all(
            mcp, self._ctx,
            categories=("code_view", "static_analysis"),
            code_view_profile="research",
        )

        @mcp.tool()
        def submit_logic_group(
            name: str,
            description: str,
            entries: list[str],
            core: list[str] | None = None,
        ):
            """Submit the final LogicGroup.  Ends the exploration loop.

            Call this ONCE, as your last action, only after you have:
            (a) identified a coherent feature matching the user's prompt,
            (b) selected 1-5 concrete public-API entry functions,
            (c) listed the core internal functions they reach,
            (d) written your own one-paragraph description of what the
                feature does based on reading the source — do NOT copy
                the user's prompt verbatim.

            Args:
                name: Short label for the feature (e.g. "libyaml document loader").
                description: Your own summary after reading the code.
                entries: Public API entry point function names (at least 1).
                core: Internal functions reachable from entries (optional but
                      recommended — used for P4 verification downstream).
            """
            if self._target_function and (not entries or entries[0] != self._target_function):
                return (f"ERROR: entries[0] must be the target function "
                        f"'{self._target_function}'.  Fix and call submit_logic_group again.")
            try:
                self._submitted_lg = LogicGroup(
                    name=name,
                    description=description,
                    entries=list(entries or []),
                    core=list(core or []),
                    project=self._ctx.project,
                )
                return f"LogicGroup submitted: {self._submitted_lg.summary()}"
            except ValueError as exc:
                self._submit_error = str(exc)
                return f"ERROR: {exc}.  Fix and call submit_logic_group again."

        return mcp

    def get_system_prompt(self, **kwargs: Any) -> str:
        if self._target_function:
            return _render(
                "lg_target_system.md",
                project=self._ctx.project,
                project_path=str(self._ctx.project_path),
                oss_fuzz_project_dir=str(self._ctx.oss_fuzz_project_dir) if self._ctx.oss_fuzz_project_dir else "",
                static_available=self._ctx.static_index is not None or self._ctx.callgraph is not None,
            )
        return _render(
            "lg_system.md",
            project=self._ctx.project,
            project_path=str(self._ctx.project_path),
            functionality_prompt=self._functionality_prompt,
        )

    def get_initial_message(self, **kwargs: Any) -> str:
        if self._target_function:
            return _render(
                "lg_target_initial.md",
                project=self._ctx.project,
                project_path=str(self._ctx.project_path),
                target_function=self._target_function,
                target_declaration=self._target_declaration,
                target_location=self._target_location,
                fuzzer_name=self._fuzzer_name,
                oss_fuzz_project_dir=str(self._ctx.oss_fuzz_project_dir) if self._ctx.oss_fuzz_project_dir else "",
            )
        return _render(
            "lg_initial.md",
            project=self._ctx.project,
            project_path=str(self._ctx.project_path),
            functionality_prompt=self._functionality_prompt,
        )

    def get_urgency_message(self) -> str | None:
        return (
            "You are close to running out of turns. Submit your best-known "
            "LogicGroup NOW via submit_logic_group. A partial LG with only "
            "the entries you've verified is more useful than no LG at all."
        )

    def should_stop(self, response: LLMResponse) -> bool:
        for tc in response.tool_calls:
            if tc["function"]["name"] == _SUBMIT_TOOL:
                # Only stop if the submission actually succeeded (not if
                # it returned a validation error — in which case the
                # agent should retry with corrected args).
                if self._submitted_lg is not None:
                    return True
        return False

    def parse_result(self, content: str) -> LogicGroup | None:
        return self._submitted_lg
