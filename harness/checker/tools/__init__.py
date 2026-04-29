"""MCP tool registry for the system-check agent.

Tools are grouped by responsibility into per-file modules::

    code_view         read_file, list_directory, search_files
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
) -> None:
    """Mount the requested tool categories onto *mcp*.

    Each category module's ``register(mcp, ctx)`` decides internally
    whether to register full tools or degraded stubs (for example,
    ``static_analysis`` returns an "unavailable" message when
    ``ctx.static_index`` is ``None``).
    """
    for cat in categories:
        if cat not in ALL_CATEGORIES:
            raise ValueError(f"Unknown tool category: {cat}")
        module = importlib.import_module(f"harness.checker.tools.{cat}")
        register_fn = getattr(module, "register", None)
        if register_fn is None:
            raise RuntimeError(f"Tool category {cat} has no register(mcp, ctx) function")
        register_fn(mcp, ctx)
        logger.debug("tools: registered category %s", cat)
