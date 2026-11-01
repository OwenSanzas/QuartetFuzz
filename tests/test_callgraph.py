from __future__ import annotations

import json
import sqlite3

import pytest

from harness.checker.callgraph import CallGraph, short_name


@pytest.mark.parametrize(
    "raw, expected",
    [
        ("plist_from_bin", "plist_from_bin"),
        ("ns::Cls::fn(int) const", "fn"),
        ("Cls::fn<T>", "fn"),
        ("std::map<int, std::vector<int>>::at", "at"),
        ("  padded ( void )  ", "padded"),
    ],
)
def test_short_name(raw, expected):
    assert short_name(raw) == expected


# id, name, file, line, line_end
FUNCS = [
    (1, "entry", "src/api.c", 10, 30),
    (2, "parse", "src/parse.c", 5, 50),
    (3, "parse", "src/other/parse.c", 5, 50),  # same short name, different file
    (4, "emit", "src/emit.c", 1, 9),
    (5, "unrelated", "src/x.c", 1, 2),
]
CALLS = [(1, 2), (2, 4), (4, 2)]  # entry -> parse -> emit -> parse (cycle)


@pytest.fixture
def graph_path(tmp_path):
    path = tmp_path / "toy.callgraph.sqlite"
    db = sqlite3.connect(path)
    db.execute("CREATE TABLE functions(id INTEGER, uid TEXT, name TEXT, file TEXT, line INTEGER, line_end INTEGER)")
    db.execute("CREATE TABLE calls(caller INTEGER, callee INTEGER)")
    rows = [(i, str(i), n, f, a, b) for i, n, f, a, b in FUNCS]
    db.executemany("INSERT INTO functions VALUES (?, ?, ?, ?, ?, ?)", rows)
    db.executemany("INSERT INTO calls VALUES (?, ?)", CALLS + [(1, 2)])  # duplicate edge is deduped
    db.commit()
    db.close()
    return path


@pytest.fixture
def graph(graph_path) -> CallGraph:
    return CallGraph(graph_path)


def test_duplicate_edges_are_deduped(graph):
    assert graph.callees[1] == [2]


def test_resolve_by_name_and_file(graph):
    assert [f.id for f in graph.resolve("parse")] == [3, 2]  # sorted by file
    assert [f.id for f in graph.resolve("parse", "parse.c")] == [3, 2]  # path-suffix match hits both
    assert [f.id for f in graph.resolve("parse", "src/parse.c")] == [2]
    assert [f.id for f in graph.resolve("parse", "./src/other/parse.c")] == [3]
    assert [f.id for f in graph.resolve("ns::parse(int)", "src/parse.c:20")] == [2]
    assert graph.resolve("parse", "src/parse.c:99") == []


def test_shortest_path(graph):
    chain = graph.shortest_path(graph.resolve("entry"), graph.resolve("emit"))
    assert [f.name for f in chain] == ["entry", "parse", "emit"]


def test_shortest_path_unreachable_and_depth(graph):
    assert graph.shortest_path(graph.resolve("entry"), graph.resolve("unrelated")) is None
    assert graph.shortest_path(graph.resolve("entry"), graph.resolve("emit"), max_depth=1) is None


def test_similar(graph):
    assert graph.similar("pars") == ["parse"]


def test_load_is_off_without_env(monkeypatch):
    monkeypatch.delenv("AGF_CALLGRAPH_DIR", raising=False)
    assert CallGraph.load("case", "toy") is None


def test_load_uses_index_then_project_default(monkeypatch, tmp_path, graph_path):
    monkeypatch.setenv("AGF_CALLGRAPH_DIR", str(tmp_path))
    assert CallGraph.load("case", "toy").path == graph_path  # <project>.callgraph.sqlite
    (tmp_path / "index.json").write_text(json.dumps({"case": "toy.callgraph.sqlite"}))
    assert CallGraph.load("case", "elsewhere").path == graph_path
    assert CallGraph.load("other", "missing") is None
