"""Read-only filesystem tools shared by every agent: ``Read`` / ``Glob`` / ``Grep``.

Ported from AGF (``agent/tools/``; only change: ``Grep`` passes ``--no-ignore``), which mirrors Claude Code's
read-only trio. Built as closures over a :class:`Workspace` that says what the
agent may reach and how much it may return.
"""

from __future__ import annotations

from agent.tools.filesystem import build_filesystem_tools, format_numbered_lines
from agent.tools.workspace import ReadHook, Workspace

__all__ = [
    "ReadHook",
    "Workspace",
    "build_filesystem_tools",
    "format_numbered_lines",
]
