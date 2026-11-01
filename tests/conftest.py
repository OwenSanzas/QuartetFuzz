"""Shared fixtures: tiny on-disk call graphs, so tests need no Joern, Docker or LLM."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from harness.checker.static import StaticIndex


def write_static_project(
    root: Path,
    project: str,
    functions: list[dict],
    edges: list[tuple[str, str]],
    fuzzers: list | None = None,
) -> Path:
    """Write functions.json / edges.json (/ fuzzers.json) in the layout StaticIndex.load expects."""
    project_dir = root / project
    project_dir.mkdir(parents=True)
    (project_dir / "functions.json").write_text(json.dumps(functions))
    (project_dir / "edges.json").write_text(
        json.dumps([{"caller_name": a, "callee_name": b} for a, b in edges])
    )
    if fuzzers is not None:
        (project_dir / "fuzzers.json").write_text(json.dumps(fuzzers))
    return project_dir


@pytest.fixture
def make_index(tmp_path):
    """Build a StaticIndex from an edge list; every caller/callee gets a FunctionRecord.

    ``paths`` maps a function name to its file_path (default ``src/<name>.c``).
    """

    def _make(
        edges: list[tuple[str, str]],
        *,
        paths: dict[str, str] | None = None,
        fuzzers: list | None = None,
        project: str = "toy",
    ) -> StaticIndex:
        paths = paths or {}
        names = sorted({n for edge in edges for n in edge} | set(paths))
        functions = [
            {"name": n, "file_path": paths.get(n, f"src/{n}.c"), "start_line": 1, "end_line": 2}
            for n in names
        ]
        write_static_project(tmp_path, project, functions, edges, fuzzers)
        index = StaticIndex.load(project, root=tmp_path, force_reload=True)
        assert index is not None
        return index

    return _make
