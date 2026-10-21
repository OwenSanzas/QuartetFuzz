"""Filesystem-reading MCP tools: ``Read`` / ``Glob`` / ``Grep``.

The Claude Code-style trio from :mod:`agent.tools` (ported from AGF), mounted
on a FastMCP server.  Paths are resolved against ``ctx.allowed_roots`` to keep
the agent inside the project source tree (plus the optional oss-fuzz project
dir).

Two profiles, matching the AGF agents they correspond to:

``research`` (LG, P2 — AGF ``discovery/targets``, ``harness/api_research``)
    Results capped at 4000 chars, paths relative to the root.  These agents
    sweep the tree, so a cap keeps one broad search from filling the context.
``generator`` (system-check flow — AGF ``harness/generator``)
    No cap, absolute paths, default glob shown in ``Grep``'s signature.
    Writing a harness means reading whole functions and headers, so a cap
    landing mid-definition costs more than it saves; absolute paths keep a
    project file distinct from its OSS-Fuzz counterpart (``build.sh``).
"""

from __future__ import annotations

import logging
from typing import Literal

from mcp.server.fastmcp import FastMCP

from agent.tools import Workspace, build_filesystem_tools
from harness.checker.tools.context import CheckerContext

logger = logging.getLogger(__name__)

Profile = Literal["research", "generator"]

# Every case in the dataset is C or C++; AGF's C++ profile covers both,
# headers included (declarations are what P2 looks up most).
_SOURCE_GLOB = "*.{c,cc,cpp,cxx,c++,h,hh,hpp,hxx}"

_RESEARCH_MAX_OUTPUT_CHARS = 4000


def build_workspace(ctx: CheckerContext, profile: Profile) -> Workspace:
    roots = tuple(ctx.allowed_roots)
    if profile == "research":
        return Workspace(
            project_path=ctx.project_path,
            extra_roots=roots,
            default_glob=_SOURCE_GLOB,
            max_output_chars=_RESEARCH_MAX_OUTPUT_CHARS,
        )
    if profile == "generator":
        return Workspace(
            project_path=ctx.project_path,
            extra_roots=roots,
            default_glob=_SOURCE_GLOB,
            glob_in_signature=True,
            max_output_chars=0,
            search_path_style="absolute",
        )
    raise ValueError(f"Unknown code_view profile: {profile!r}")


def register(mcp: FastMCP, ctx: CheckerContext, *, profile: Profile = "generator") -> None:
    """Mount ``Read`` / ``Glob`` / ``Grep`` onto *mcp*."""
    for tool in build_filesystem_tools(build_workspace(ctx, profile)):
        mcp.tool()(tool)
