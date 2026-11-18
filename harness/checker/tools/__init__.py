"""MCP tool registry for the system-check agent.

Tools are grouped by responsibility into per-file modules::

    code_view         Read, Glob, Grep (Claude Code-style, from agent.tools)
    web_fetch         fetch_url (stub)
    github            search_issues, search_prs, fetch_file (stub)
    static_analysis   get_callers, get_callees, lookup_symbol,
                      reachable_from, is_public_api
    dynamic_analysis  build_harness, AP_Run_check, get_coverage, run_gdb
    quality_check     check_p1, check_p2, check_p3, check_p4

Each module exposes a top-level ``register(mcp, ctx)`` function which
mounts its tools on the FastMCP server.  The top-level :func:`register_all`
here dispatches to whichever categories the caller wants — by default the
four load-bearing ones (no web / github).
"""

from __future__ import annotations

import importlib
import logging
from collections.abc import Iterable

from mcp.server.fastmcp import FastMCP

from harness.checker.tools.context import CheckerContext

logger = logging.getLogger(__name__)

DEFAULT_CATEGORIES: tuple[str, ...] = (
    "code_view",
    "static_analysis",
    "dynamic_analysis",
)

ALL_CATEGORIES: tuple[str, ...] = (
    "code_view",
    "static_analysis",
    "dynamic_analysis",
)


def register_all(
    mcp: FastMCP,
    ctx: CheckerContext,
    *,
    categories: Iterable[str] = DEFAULT_CATEGORIES,
    code_view_profile: str = "generator",
) -> None:
    """Mount the requested tool categories onto *mcp*.

    Each category module's ``register(mcp, ctx)`` decides internally
    whether to register full tools or degraded stubs (for example,
    ``static_analysis`` returns an "unavailable" message when
    ``ctx.static_index`` is ``None``).  ``code_view_profile`` selects the
    ``Read``/``Glob``/``Grep`` limits (see :mod:`harness.checker.tools.code_view`).
    """
    for cat in categories:
        if cat not in ALL_CATEGORIES:
            raise ValueError(f"Unknown tool category: {cat}")
        if cat == "static_analysis" and ctx.static_index is None and ctx.callgraph is not None:
            # +Static ablation: the three call-graph tools only.
            importlib.import_module("harness.checker.tools.callgraph").register(mcp, ctx)
            logger.debug("tools: registered call graph tools")
            continue
        if cat == "static_analysis" and ctx.static_index is None:
            # Every call would answer "unavailable"; the prompts point the
            # agent at Grep/Read instead (see their ``static_available``).
            logger.debug("tools: no static index, static_analysis not mounted")
            continue
        module = importlib.import_module(f"harness.checker.tools.{cat}")
        register_fn = getattr(module, "register", None)
        if register_fn is None:
            raise RuntimeError(f"Tool category {cat} has no register(mcp, ctx) function")
        if cat == "code_view":
            register_fn(mcp, ctx, profile=code_view_profile)
        else:
            register_fn(mcp, ctx)
        logger.debug("tools: registered category %s", cat)
