"""Static call graph for the +Static ablation (sqlite, one per project).

Built offline by ``scripts/callgraph/build_graph.sh`` (Joern + tree-sitter, the
way fbbench's callgraph-tools does it) from the project source with every fuzz
harness removed.  Tables::

    functions(id, uid, name, file, line, line_end)   name: short name, no class
    calls(caller, callee)                             functions.id

Off unless ``AGF_CALLGRAPH_DIR`` is set: :meth:`CallGraph.load` then returns
``None``, no tool is mounted and the prompts say no call graph is available --
exactly the main (w/o static) run.  ``<dir>/index.json`` maps a case id to its
graph file (capstone has one graph per source tree); without an entry the
project's ``<project>.callgraph.sqlite`` is used.
"""

from __future__ import annotations

import difflib
import json
import logging
import os
import re
import sqlite3
from collections import deque
from dataclasses import dataclass
from pathlib import Path

logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class Func:
    id: int
    name: str
    file: str
    line: int
    line_end: int

    def loc(self) -> str:
        return f"{self.file}:{self.line}"


def short_name(name: str) -> str:
    """'ns::Cls::fn(int) const' / 'Cls::fn<T>' -> 'fn' (the graph's names)."""
    s = name.strip()
    s = re.sub(r"\(.*\)\s*(const)?\s*$", "", s)
    while re.search(r"<[^<>]*>", s):
        s = re.sub(r"<[^<>]*>", "", s)
    return s.split("::")[-1].strip()


class CallGraph:
    def __init__(self, path: Path) -> None:
        self.path = path
        db = sqlite3.connect(f"file:{path}?mode=ro", uri=True, check_same_thread=False)
        self.funcs: dict[int, Func] = {
            r[0]: Func(*r) for r in db.execute("SELECT id, name, file, line, line_end FROM functions")
        }
        self.by_name: dict[str, list[int]] = {}
        for f in self.funcs.values():
            self.by_name.setdefault(f.name, []).append(f.id)
        self.callees: dict[int, list[int]] = {}
        self.callers: dict[int, list[int]] = {}
        for a, b in db.execute("SELECT DISTINCT caller, callee FROM calls"):
            self.callees.setdefault(a, []).append(b)
            self.callers.setdefault(b, []).append(a)
        db.close()

    @classmethod
    def load(cls, case_id: str, project: str) -> CallGraph | None:
        root = os.environ.get("AGF_CALLGRAPH_DIR")
        if not root:
            return None
        root_p = Path(root)
        index = root_p / "index.json"
        name = json.loads(index.read_text()).get(case_id) if index.exists() else None
        path = root_p / (name or f"{project}.callgraph.sqlite")
        if not path.exists():
            logger.warning("callgraph: %s missing for %s", path, case_id)
            return None
        return cls(path)

    def resolve(self, name: str, file: str = "") -> list[Func]:
        """Definitions named *name*; *file* (path suffix, optionally ':line')
        narrows them."""
        found = [self.funcs[i] for i in self.by_name.get(short_name(name), [])]
        if file:
            path, _, line = file.partition(":")
            path = path.lstrip("./")
            found = [f for f in found if f.file == path or f.file.endswith("/" + path)]
            if line.isdigit():
                found = [f for f in found if f.line <= int(line) <= max(f.line, f.line_end)] or \
                        [f for f in found if f.line == int(line)]
        return sorted(found, key=lambda f: (f.file, f.line))

    def similar(self, name: str, n: int = 10) -> list[str]:
        return difflib.get_close_matches(short_name(name), self.by_name.keys(), n=n, cutoff=0.6)

    def shortest_path(self, src: list[Func], dst: list[Func], max_depth: int = 30) -> list[Func] | None:
        """Shortest call chain from any of *src* to any of *dst* (BFS over callees)."""
        goal = {f.id for f in dst}
        prev: dict[int, int | None] = {f.id: None for f in src}
        q = deque((f.id, 0) for f in src)
        while q:
            u, d = q.popleft()
            if u in goal:
                chain = []
                while u is not None:
                    chain.append(self.funcs[u])
                    u = prev[u]
                return chain[::-1]
            if d >= max_depth:
                continue
            for v in self.callees.get(u, ()):
                if v not in prev:
                    prev[v] = u
                    q.append((v, d + 1))
        return None
