"""Pure-Python loader for pre-computed static analysis call graphs.

Reads the JSON exports produced by an upstream Joern/SVF pipeline. Performs
NO analysis of its own — every query is an in-memory dict lookup or a small
BFS over an adjacency list. Queries needed by the system-check flow:

    callers_of / callees_of / lookup / reachable_from / is_public_api

Data source
-----------
Default root:  /home/ze/agf-data/static_analysis/
Override:      AGF_STATIC_ANALYSIS_DIR environment variable.

Per-project layout expected:
    <root>/<project>/
        functions.json   — list of {name, file_path, start_line, end_line,
                                    language, is_external, ...}
        edges.json       — list of {caller_name, caller_file,
                                    callee_name, callee_file, ...}
        fuzzers.json     — list of fuzzer-harness function names (best-effort)

If a project directory is missing, :meth:`StaticIndex.load` returns ``None``.
The flow layer decides whether to fall back to LLM-only checks.
"""

from __future__ import annotations

import json
import logging
import os
import re
import threading
from collections import defaultdict, deque
from dataclasses import dataclass, field
from pathlib import Path

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Paths & config
# ---------------------------------------------------------------------------

# The static-analysis call graphs ship with the artifact at
# ``dataset/static_analysis/`` (covering the 25-case subset). The
# ``AGF_STATIC_ANALYSIS_DIR`` env var overrides this for users with
# a fuller call graph elsewhere.
_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
_DEFAULT_STATIC_DIR = Path(os.environ.get(
    "AGF_STATIC_ANALYSIS_DIR",
    str(_REPO_ROOT / "dataset" / "static_analysis"),
))

# File-path prefixes used by is_public_api() heuristics.  Conservative:
# anything not matching either list returns "unknown" and is deferred to the
# LLM layer.
_INTERNAL_PATH_MARKERS: tuple[str, ...] = (
    "internal/",
    "private/",
    "src/",
    "core/",
    "util/",
    "common/",
    "impl/",
    "detail/",
)
_PUBLIC_PATH_MARKERS: tuple[str, ...] = (
    "include/",
    "public/",
    "api/",
)

# Symbols to ignore when extracting called names from a harness source.
# These are C/libFuzzer machinery, stdlib helpers, or keywords that look
# like calls in a naive regex scan.
_IGNORED_SYMBOLS: frozenset[str] = frozenset({
    # libFuzzer / sanitizer
    "LLVMFuzzerTestOneInput", "LLVMFuzzerInitialize", "LLVMFuzzerCleanup",
    "LLVMFuzzerCustomMutator", "LLVMFuzzerCustomCrossOver",
    # C keywords / operators that look like calls
    "if", "while", "for", "switch", "return", "sizeof", "alignof",
    "static_cast", "reinterpret_cast", "const_cast", "dynamic_cast",
    # libc staples (too noisy to flag)
    "malloc", "calloc", "realloc", "free", "memcpy", "memset", "memcmp",
    "memmove", "strcpy", "strncpy", "strcat", "strncat", "strlen", "strcmp",
    "strncmp", "strdup", "strchr", "strstr", "sprintf", "snprintf", "printf",
    "fprintf", "fputs", "fopen", "fclose", "fread", "fwrite", "fseek",
    "ftell", "perror", "exit", "abort", "atoi", "atol", "atof",
    "assert", "abs", "min", "max",
})

# Regex that matches a C identifier immediately followed by ``(``.
# Good enough for harness-sized code; doesn't handle function pointers or
# C++ templated calls — that's fine, symbol existence check is advisory.
_CALL_SITE_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class FunctionRecord:
    """One node in the call graph — a function definition."""

    name: str
    file_path: str
    start_line: int = 0
    end_line: int = 0
    language: str = ""
    is_external: bool = False
    return_type: str = ""
    cyclomatic_complexity: int = 0

    @classmethod
    def from_json(cls, d: dict) -> FunctionRecord:
        return cls(
            name=d.get("name", ""),
            file_path=d.get("file_path", ""),
            start_line=int(d.get("start_line") or 0),
            end_line=int(d.get("end_line") or 0),
            language=d.get("language", ""),
            is_external=bool(d.get("is_external", False)),
            return_type=d.get("return_type", ""),
            cyclomatic_complexity=int(d.get("cyclomatic_complexity") or 0),
        )

    def short(self) -> str:
        """One-line human summary."""
        loc = f"{self.file_path}:{self.start_line}-{self.end_line}" if self.file_path else ""
        tag = " [external]" if self.is_external else ""
        return f"{self.name}  {loc}{tag}".strip()


# ---------------------------------------------------------------------------
# StaticIndex
# ---------------------------------------------------------------------------


_index_cache: dict[str, StaticIndex | None] = {}
_cache_lock = threading.Lock()


@dataclass
class StaticIndex:
    """Per-project call graph loaded once into memory.

    Queries are dict lookups (O(1)) or BFS over an adjacency list.  No
    analysis is performed — the graph is simply read from the JSON exports.

    Create via :meth:`load`, which caches by project name so repeated
    constructions on different workers reuse the same in-memory graph.
    """

    project: str
    root_dir: Path
    functions_by_name: dict[str, list[FunctionRecord]] = field(default_factory=dict)
    callers_index: dict[str, set[str]] = field(default_factory=dict)  # callee -> {caller names}
    callees_index: dict[str, set[str]] = field(default_factory=dict)  # caller -> {callee names}
    fuzzer_names: list[str] = field(default_factory=list)

    # ---- loading -----------------------------------------------------------

    @classmethod
    def load(
        cls,
        project: str,
        *,
        root: Path | None = None,
        force_reload: bool = False,
    ) -> StaticIndex | None:
        """Load the static analysis data for *project*.

        Returns ``None`` if the project directory does not exist or is
        missing required files.  Cached across calls within the same process.
        """
        cache_key = f"{root or _DEFAULT_STATIC_DIR}::{project}"
        if not force_reload:
            with _cache_lock:
                if cache_key in _index_cache:
                    return _index_cache[cache_key]

        root = Path(root) if root else _DEFAULT_STATIC_DIR
        project_dir = root / project
        if not project_dir.is_dir():
            logger.info("static: no data for project=%s at %s", project, project_dir)
            with _cache_lock:
                _index_cache[cache_key] = None
            return None

        functions_path = project_dir / "functions.json"
        edges_path = project_dir / "edges.json"
        if not functions_path.is_file() or not edges_path.is_file():
            logger.warning(
                "static: incomplete data for %s (missing functions.json or edges.json)",
                project,
            )
            with _cache_lock:
                _index_cache[cache_key] = None
            return None

        index = cls(project=project, root_dir=project_dir)
        index._load_functions(functions_path)
        index._load_edges(edges_path)
        index._load_fuzzers(project_dir / "fuzzers.json")

        logger.info(
            "static: loaded project=%s functions=%d edges=%d fuzzers=%d",
            project,
            sum(len(v) for v in index.functions_by_name.values()),
            sum(len(v) for v in index.callees_index.values()),
            len(index.fuzzer_names),
        )

        with _cache_lock:
            _index_cache[cache_key] = index
        return index

    def _load_functions(self, path: Path) -> None:
        data = json.loads(path.read_text())
        by_name: dict[str, list[FunctionRecord]] = defaultdict(list)
        for item in data:
            rec = FunctionRecord.from_json(item)
            if rec.name:
                by_name[rec.name].append(rec)
        self.functions_by_name = dict(by_name)

    def _load_edges(self, path: Path) -> None:
        data = json.loads(path.read_text())
        callers: dict[str, set[str]] = defaultdict(set)
        callees: dict[str, set[str]] = defaultdict(set)
        for edge in data:
            caller = edge.get("caller_name")
            callee = edge.get("callee_name")
            if not caller or not callee:
                continue
            callers[callee].add(caller)
            callees[caller].add(callee)
        self.callers_index = dict(callers)
        self.callees_index = dict(callees)

    def _load_fuzzers(self, path: Path) -> None:
        if not path.is_file():
            return
        try:
            data = json.loads(path.read_text())
        except (json.JSONDecodeError, OSError):
            return
        # fuzzers.json may be either a list of names or a list of dicts.
        names: list[str] = []
        for item in data:
            if isinstance(item, str):
                names.append(item)
            elif isinstance(item, dict):
                name = item.get("name") or item.get("fuzzer") or ""
                if name:
                    names.append(name)
        self.fuzzer_names = names

    # ---- queries -----------------------------------------------------------

    def lookup(self, name: str) -> list[FunctionRecord]:
        """Return all FunctionRecord entries matching *name* exactly.

        Multiple entries are possible for C++ overloads or same-name
        functions in different files.
        """
        return list(self.functions_by_name.get(name, ()))

    def callers_of(self, name: str) -> list[str]:
        """Return the names of functions that call *name* in this project."""
        return sorted(self.callers_index.get(name, ()))

    def callees_of(self, name: str) -> list[str]:
        """Return the names of functions called by *name* in this project."""
        return sorted(self.callees_index.get(name, ()))

    def reachable_from(self, entry: str, max_depth: int = 20) -> set[str]:
        """BFS forward over the call graph from *entry*.

        Returns the set of function names reachable within *max_depth*
        hops, NOT including *entry* itself (so a leaf function returns an
        empty set and a proper entry returns its dependency cone).

        Defaults to depth 20 to map out real logic cones rather than just
        immediate neighbours.
        """
        if entry not in self.callees_index:
            return set()
        seen: set[str] = set()
        queue: deque[tuple[str, int]] = deque([(entry, 0)])
        while queue:
            current, depth = queue.popleft()
            if depth >= max_depth:
                continue
            for nxt in self.callees_index.get(current, ()):
                if nxt in seen or nxt == entry:
                    continue
                seen.add(nxt)
                queue.append((nxt, depth + 1))
        return seen

    def reverse_reachable_from(
        self, target: str, max_depth: int = 20,
    ) -> set[str]:
        """BFS backward over the call graph from *target*.

        Mirror of ``reachable_from`` walking ``callers_index`` instead of
        ``callees_index``.  Returns the set of function names that can
        reach *target* within *max_depth* hops (not including *target*
        itself).

        Used to answer paper §3.6 Step~3's E_pub query in the natural
        direction: starting from a core function, walk callers upward
        and collect all ancestors.  Pair with ``is_public_api`` (or
        ``public_apis_reaching``) to obtain the public-API ancestor
        set.
        """
        if target not in self.callers_index:
            return set()
        seen: set[str] = set()
        queue: deque[tuple[str, int]] = deque([(target, 0)])
        while queue:
            current, depth = queue.popleft()
            if depth >= max_depth:
                continue
            for prev in self.callers_index.get(current, ()):
                if prev in seen or prev == target:
                    continue
                seen.add(prev)
                queue.append((prev, depth + 1))
        return seen

    def public_apis_reaching(
        self, target: str, max_depth: int = 20,
    ) -> list[str]:
        """Return the public-API ancestors of *target* (paper §3.6 E_pub).

        Combines ``reverse_reachable_from`` with the path-based
        ``is_public_api`` heuristic: walks callers up from *target* and
        keeps only ancestors classified as ``public``.  Result is sorted
        by ascending name for stable output.

        Note that ``is_public_api`` itself is a coarse heuristic over
        directory markers (``include/`` etc.); ambiguous symbols return
        ``unknown`` and are excluded here.  The LG agent layer can
        defer those to LLM judgment when the strict filter rejects too
        much.
        """
        ancestors = self.reverse_reachable_from(target, max_depth)
        return sorted(
            a for a in ancestors if self.is_public_api(a) == "public"
        )

    def public_apis_reaching_batch(
        self, targets: list[str], max_depth: int = 20,
    ) -> dict[str, list[str]]:
        """Batch version of ``public_apis_reaching`` for a whole core C.

        Computes ``public_apis_reaching(t)`` for each ``t in targets``
        and returns ``{target: [public_ancestors]}``.  The LG agent
        passes its full core C in one call, takes the union of values
        as E_pub, and avoids the per-target tool round-trip cost.

        Targets not present in ``callers_index`` map to an empty list,
        same as the single-target method.
        """
        return {
            t: self.public_apis_reaching(t, max_depth=max_depth)
            for t in targets
        }

    # ------------------------------------------------------------------
    # Danger score (paper §3.5, Eq. 5)
    # ------------------------------------------------------------------
    #
    # Memory-safety-relevant callees treated as "unsafe operations" when
    # counting unsafe(g) for a function g.  Mirrors the paper's wording:
    # "pointer dereferences and memory operations (memcpy, malloc, free,
    # strcpy, sprintf, etc.) in g's implementation".  We use callees as a
    # backend-agnostic proxy because the system does not parse source
    # itself.
    _UNSAFE_API: frozenset[str] = frozenset({
        # raw allocation / free
        "malloc", "calloc", "realloc", "free", "alloca",
        "aligned_alloc", "posix_memalign", "valloc", "reallocarray",
        # bulk memory ops
        "memcpy", "memmove", "memset", "memcmp", "bcopy", "bzero",
        # unsafe string ops
        "strcpy", "strncpy", "strcat", "strncat",
        "sprintf", "snprintf", "vsprintf", "vsnprintf", "asprintf",
        "strdup", "strndup", "strerror",
        # IO into buffers
        "gets", "fgets", "fread", "fwrite", "read", "write", "recv", "send",
        # casts and pointer arithmetic helpers
        "memchr", "memrchr", "strstr", "strtok",
    })

    def _reachable_with_depth(
        self, entry: str, max_depth: int,
    ) -> dict[str, int]:
        """Like reachable_from(), but also returns the shortest path
        depth d(entry, g) for every reachable g."""
        if entry not in self.callees_index:
            return {}
        depths: dict[str, int] = {}
        queue: deque[tuple[str, int]] = deque([(entry, 0)])
        while queue:
            current, depth = queue.popleft()
            if depth >= max_depth:
                continue
            for nxt in self.callees_index.get(current, ()):
                if nxt == entry:
                    continue
                if nxt in depths:
                    continue  # BFS — already at shortest depth
                depths[nxt] = depth + 1
                queue.append((nxt, depth + 1))
        return depths

    def _unsafe_count(self, fn_name: str) -> int:
        """unsafe(g) — number of memory-safety-relevant callees in g.

        Counts how many of g's direct callees match the _UNSAFE_API set.
        This is a backend-agnostic proxy for "pointer derefs / memory
        ops in g's implementation" (paper §3.5, Eq. 5).
        """
        callees = self.callees_index.get(fn_name, ())
        return sum(1 for c in callees if c in self._UNSAFE_API)

    def compute_danger(self, function_name: str, depth: int = 20) -> float:
        """Paper Eq. 5: ``danger(f) = sum_{g in reachable(f, D)}
        unsafe(g) / d(f, g)``.

        Computes the depth-discounted unsafe-operation reach of
        *function_name* over the static call graph up to *depth* hops.
        Returns 0.0 for unknown / leaf entries.

        ``unsafe(g)`` counts memory-safety-relevant callees of g (see
        ``_UNSAFE_API``).  ``d(f, g)`` is the shortest-path distance
        from f to g in the call graph.

        Defaults to depth 20 — the paper's RQ5 ``D=20`` plateau.  See
        Appendix B (``sec:appendix:danger_sensitivity``) for the
        sensitivity analysis showing top-of-ranking selections are
        invariant for ``D in [10, 30]``.
        """
        if function_name not in self.callees_index:
            return 0.0
        depths = self._reachable_with_depth(function_name, depth)
        score = 0.0
        for g, d in depths.items():
            if d == 0:  # safety: BFS guarantees d >= 1, but be explicit
                continue
            score += self._unsafe_count(g) / d
        return score

    def rank_by_danger(
        self, candidates: list[str], depth: int = 20,
    ) -> list[tuple[str, float]]:
        """Score and sort *candidates* by danger (descending).

        Convenience wrapper for the LG agent's top-5 selection step.
        Returns ``[(name, score), ...]`` in descending score order.
        Functions outside the call graph score 0.0 and sort to the end.
        """
        scored = [(c, self.compute_danger(c, depth=depth)) for c in candidates]
        scored.sort(key=lambda kv: (-kv[1], kv[0]))
        return scored

    def is_public_api(self, name: str) -> str:
        """Classify *name* as ``public`` / ``internal`` / ``unknown``.

        Heuristic based on the file_path prefix of the function's
        FunctionRecord.  If the symbol is not in the call graph, returns
        ``unknown`` (the symbol may still exist as a library function we
        didn't index — the LLM layer handles this).
        """
        records = self.lookup(name)
        if not records:
            return "unknown"
        verdicts: list[str] = []
        for rec in records:
            path = rec.file_path.replace("\\", "/")
            if any(marker in path for marker in _PUBLIC_PATH_MARKERS):
                verdicts.append("public")
            elif any(marker in path for marker in _INTERNAL_PATH_MARKERS):
                verdicts.append("internal")
            else:
                verdicts.append("unknown")
        # Public wins: if ANY record lives under a public header, the
        # symbol is public (some projects vendor a copy of another library
        # whose path trips internal markers — don't let that shadow the real
        # public definition).
        if "public" in verdicts:
            return "public"
        # Internal only if ALL matching records live under internal paths.
        if all(v == "internal" for v in verdicts):
            return "internal"
        return "unknown"

    def extract_called_symbols(self, harness_src: str) -> list[str]:
        """Scan *harness_src* for ``ident(`` call sites.

        Returns a deduplicated, sorted list of identifier names, excluding
        libc staples and libFuzzer machinery.  Good enough for C harnesses;
        C++ overloads/templates are reported by base name.
        """
        names: set[str] = set()
        for match in _CALL_SITE_RE.finditer(harness_src):
            ident = match.group(1)
            if ident in _IGNORED_SYMBOLS:
                continue
            names.add(ident)
        return sorted(names)

    def fuzzer_entries(self) -> list[FunctionRecord]:
        """Return FunctionRecord entries for functions in fuzzers.json."""
        out: list[FunctionRecord] = []
        for name in self.fuzzer_names:
            out.extend(self.lookup(name))
        return out

    # ---- introspection -----------------------------------------------------

    def stats(self) -> dict[str, int]:
        return {
            "functions": sum(len(v) for v in self.functions_by_name.values()),
            "unique_names": len(self.functions_by_name),
            "callers_edges": sum(len(v) for v in self.callers_index.values()),
            "callees_edges": sum(len(v) for v in self.callees_index.values()),
            "fuzzer_names": len(self.fuzzer_names),
        }
