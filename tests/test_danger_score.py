"""Tests for harness.checker.danger_score -- the scorer pipeline.py uses to rank LG entries."""

from __future__ import annotations

import pytest

from harness.checker.danger_score import compute_danger, rank_entries
from tests.test_static_index import EDGES


@pytest.fixture
def index(make_index):
    return make_index(EDGES)


def test_compute_danger_values(index):
    # Every unsafe call-site found while expanding a node at BFS distance d
    # adds 1 / (d + 1):  b (d=1) -> malloc, free = 2 * 1/2 ;  c (d=2) -> memcpy = 1/3.
    assert compute_danger("entry", index) == {
        "function": "entry",
        "danger": 1.33,
        "reachable": 7,  # entry, a, b, c, malloc, free, memcpy
        "unsafe_ops": 3,
    }


def test_compute_danger_counts_entry_own_callees(make_index):
    assert compute_danger("entry", make_index([("entry", "memcpy")]))["danger"] == 1.0


def test_compute_danger_max_depth(index):
    assert compute_danger("entry", index, max_depth=1) == {
        "function": "entry",
        "danger": 1.0,
        "reachable": 3,
        "unsafe_ops": 2,
    }


def test_compute_danger_unknown_function(index):
    assert compute_danger("missing", index) == {
        "function": "missing",
        "danger": 0.0,
        "reachable": 1,
        "unsafe_ops": 0,
    }


def test_rank_entries_sorts_and_ranks(index):
    ranked = rank_entries(["a", "entry", "b"], index)
    # b's own malloc/free count at 1/1, so b (2.0) outranks entry (1.33), which calls b.
    assert [(r["function"], r["danger"], r["rank"]) for r in ranked] == [
        ("b", 2.0, 1),
        ("entry", 1.33, 2),
        ("a", 1.17, 3),
    ]


def test_rank_entries_resolves_partial_names(index):
    # pipeline.py rewrites lg.entries with these resolved names.
    (ranked,) = rank_entries(["ntr"], index)
    assert ranked["function"] == "entry"
    assert ranked["danger"] == 1.33


def test_rank_entries_unknown_name_scores_zero(index):
    (ranked,) = rank_entries(["zzz"], index)
    assert ranked == {"function": "zzz", "danger": 0.0, "reachable": 0, "unsafe_ops": 0, "rank": 1}


@pytest.mark.xfail(
    strict=True,
    reason="unsafe keywords match as substrings: 'read' in pthread_create / is_ready, 'gets' in set_targets",
)
@pytest.mark.parametrize("callee", ["pthread_create", "thread_once", "is_ready", "set_targets"])
def test_non_memory_callees_are_not_unsafe(make_index, callee):
    assert compute_danger("entry", make_index([("entry", callee)]))["unsafe_ops"] == 0


@pytest.mark.xfail(
    strict=True,
    reason=(
        "pipeline.py ranks with danger_score.compute_danger, which counts f's own callees and "
        "divides by d(f, callee); StaticIndex.compute_danger implements Eq. 5 as documented "
        "(sum over g != f of unsafe(g) / d(f, g)) but nothing calls it"
    ),
)
def test_pipeline_scorer_matches_static_index_eq5(index):
    assert compute_danger("entry", index)["danger"] == pytest.approx(index.compute_danger("entry"), abs=0.01)
