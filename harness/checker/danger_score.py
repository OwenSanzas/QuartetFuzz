"""Compute danger scores for entry function candidates.

The danger score quantifies how many unsafe operations are reachable from
a given entry function, discounted by call-graph distance:

    danger(f) = sum( unsafe(g) / d(f, g) )  for g in reachable(f, D)

where D=20 is the maximum BFS depth, unsafe(g) is 1 if g calls a
memory-unsafe operation (memcpy, malloc, free, ...), and d(f,g) is
the shortest call-graph distance from f to g.

This module is a pure computation layer that reads from a StaticIndex
instance.  It does NOT run Joern or any other analyzer -- it assumes
pre-computed call-graph data is already loaded into StaticIndex.
"""

from __future__ import annotations

from collections import deque
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from harness.checker.static import StaticIndex

# Operations commonly associated with memory-safety vulnerabilities.
UNSAFE_KEYWORDS: frozenset[str] = frozenset({
    # Memory copy / move
    "memcpy", "memmove", "memset",
    # Heap allocation
    "malloc", "calloc", "realloc", "free",
    # String operations
    "strcpy", "strncpy", "strcat", "strncat",
    "sprintf", "snprintf", "sscanf",
    # Dangerous input
    "gets", "fgets",
    # File / network I/O
    "fread", "fwrite", "recv", "send", "read", "write",
})


def _is_unsafe_callee(name: str) -> bool:
    """Return True if *name* looks like an unsafe libc/POSIX function."""
    lower = name.lower()
    return any(kw in lower for kw in UNSAFE_KEYWORDS)


def compute_danger(
    entry: str,
    static_index: "StaticIndex",
    *,
    max_depth: int = 20,
) -> dict:
    """Compute the danger score for a single entry function.

    Returns a dict with keys:
        function  -- the (possibly resolved) function name
        danger    -- the aggregated score (float)
        reachable -- number of reachable functions within *max_depth*
        unsafe_ops -- number of unsafe call-sites found
    """
    # BFS forward through the call graph
    visited: dict[str, int] = {entry: 0}  # func -> shortest distance
    queue: deque[tuple[str, int]] = deque([(entry, 0)])
    danger = 0.0
    reachable = 0
    unsafe_ops = 0

    while queue:
        func, dist = queue.popleft()
        if dist > max_depth:
            continue
        reachable += 1

        # Check callees for unsafe operations.
        # The callee is at distance dist+1 from the entry.
        callee_dist = dist + 1
        for callee in static_index.callees_of(func):
            if _is_unsafe_callee(callee):
                danger += 1.0 / callee_dist
                unsafe_ops += 1

        # Expand BFS to callees
        for callee in static_index.callees_of(func):
            if callee not in visited:
                visited[callee] = dist + 1
                queue.append((callee, dist + 1))

    return {
        "function": entry,
        "danger": round(danger, 2),
        "reachable": reachable,
        "unsafe_ops": unsafe_ops,
    }


def rank_entries(
    entries: list[str],
    static_index: "StaticIndex",
    *,
    max_depth: int = 20,
) -> list[dict]:
    """Rank a list of candidate entry functions by danger score (desc).

    Each element in the returned list is the dict produced by
    :func:`compute_danger`, augmented with a ``rank`` key (1-based).
    """
    results = []
    for entry in entries:
        # Try exact match; fall back to partial if needed
        if static_index.lookup(entry):
            results.append(compute_danger(entry, static_index, max_depth=max_depth))
        else:
            # Attempt partial match on function name
            candidates = [
                f.name
                for f in static_index.lookup(entry)  # already empty
            ]
            # Broader search: check all known functions
            all_funcs = list(static_index.functions_by_name.keys())
            matches = [f for f in all_funcs if entry in f]
            if matches:
                best = matches[0]
                results.append(compute_danger(best, static_index, max_depth=max_depth))
            else:
                results.append({
                    "function": entry,
                    "danger": 0.0,
                    "reachable": 0,
                    "unsafe_ops": 0,
                })

    results.sort(key=lambda r: -r["danger"])

    for i, r in enumerate(results):
        r["rank"] = i + 1

    return results
