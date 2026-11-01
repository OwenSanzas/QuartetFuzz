from __future__ import annotations

import pytest

from harness.checker.static import StaticIndex
from tests.conftest import write_static_project

# entry -> a -> c -> memcpy
#       -> b -> {malloc, free}
# a -> entry closes a cycle.
EDGES = [
    ("entry", "a"),
    ("entry", "b"),
    ("a", "c"),
    ("a", "entry"),
    ("c", "memcpy"),
    ("b", "malloc"),
    ("b", "free"),
]


@pytest.fixture
def index(make_index) -> StaticIndex:
    return make_index(EDGES)


# ---- loading ---------------------------------------------------------------


def test_load_missing_project_returns_none(tmp_path):
    assert StaticIndex.load("nope", root=tmp_path, force_reload=True) is None


def test_load_without_edges_returns_none(tmp_path):
    project_dir = write_static_project(tmp_path, "half", [{"name": "f", "file_path": "f.c"}], [])
    (project_dir / "edges.json").unlink()
    assert StaticIndex.load("half", root=tmp_path, force_reload=True) is None


def test_load_is_cached_per_root(tmp_path, make_index):
    first = make_index(EDGES)
    assert StaticIndex.load("toy", root=tmp_path) is first


@pytest.mark.parametrize(
    "fuzzers",
    [["fuzz_a", "fuzz_b"], [{"name": "fuzz_a"}, {"fuzzer": "fuzz_b"}, {"other": "x"}]],
)
def test_fuzzers_json_accepts_names_or_dicts(make_index, fuzzers):
    assert make_index(EDGES, fuzzers=fuzzers).fuzzer_names == ["fuzz_a", "fuzz_b"]


# ---- graph queries ---------------------------------------------------------


def test_callers_and_callees_are_sorted(index):
    assert index.callees_of("entry") == ["a", "b"]
    assert index.callees_of("b") == ["free", "malloc"]
    assert index.callers_of("entry") == ["a"]
    assert index.callees_of("unknown") == []


def test_reachable_from_excludes_entry_and_handles_cycles(index):
    assert index.reachable_from("entry") == {"a", "b", "c", "memcpy", "malloc", "free"}


def test_reachable_from_respects_depth(index):
    assert index.reachable_from("entry", max_depth=1) == {"a", "b"}
    assert index.reachable_from("entry", max_depth=2) == {"a", "b", "c", "malloc", "free"}


def test_reachable_from_leaf_is_empty(index):
    assert index.reachable_from("memcpy") == set()


def test_reverse_reachable_from(index):
    assert index.reverse_reachable_from("memcpy") == {"c", "a", "entry"}
    assert index.reverse_reachable_from("memcpy", max_depth=1) == {"c"}


# ---- public / internal heuristic ------------------------------------------


@pytest.fixture
def api_index(make_index) -> StaticIndex:
    return make_index(
        [("pub", "core"), ("pub2", "core"), ("helper", "core"), ("odd", "core")],
        paths={
            "pub": "include/lib.h",
            "pub2": "lib/src/pub2.c",  # overridden by a second, public record below
            "helper": "src/helper.c",
            "odd": "tools/odd.c",
            "core": "src/core.c",
        },
    )


def test_is_public_api_by_path(api_index):
    assert api_index.is_public_api("pub") == "public"
    assert api_index.is_public_api("helper") == "internal"
    assert api_index.is_public_api("odd") == "unknown"
    assert api_index.is_public_api("not_in_graph") == "unknown"


def test_is_public_api_public_record_wins(api_index):
    from harness.checker.static import FunctionRecord

    api_index.functions_by_name["pub2"].append(FunctionRecord(name="pub2", file_path="include/pub2.h"))
    assert api_index.is_public_api("pub2") == "public"


def test_public_apis_reaching(api_index):
    assert api_index.public_apis_reaching("core") == ["pub"]
    assert api_index.public_apis_reaching_batch(["core", "missing"]) == {"core": ["pub"], "missing": []}


# ---- harness source scan ---------------------------------------------------


def test_extract_called_symbols_skips_libc_and_libfuzzer(index):
    src = """
    int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
        if (size < 4) return 0;
        char *buf = malloc(size);
        memcpy(buf, data, size);
        plist_t root = NULL;
        plist_from_bin(buf, size, &root);
        plist_free(root);
        plist_free(root);
        free(buf);
        return 0;
    }
    """
    assert index.extract_called_symbols(src) == ["plist_free", "plist_from_bin"]


# ---- danger score (paper Eq. 5) -------------------------------------------


def test_compute_danger_eq5(index):
    # Reachable g with d(entry, g): a=1, b=1, c=2, (leaves skipped: unsafe=0).
    # unsafe(b) = 2 (malloc, free) -> 2/1 ; unsafe(c) = 1 (memcpy) -> 1/2.
    assert index.compute_danger("entry") == pytest.approx(2.5)


def test_compute_danger_ignores_entry_own_callees(make_index):
    # Eq. 5 sums over reachable(f) \ {f}, so f's own unsafe calls do not count.
    assert make_index([("entry", "memcpy")]).compute_danger("entry") == 0.0


def test_rank_by_danger_orders_desc_then_name(index):
    assert index.rank_by_danger(["c", "entry", "b", "missing"]) == [
        ("entry", 2.5),
        ("b", 0.0),
        ("c", 0.0),
        ("missing", 0.0),
    ]
