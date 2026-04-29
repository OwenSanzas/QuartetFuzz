"""MCP tools backed by a pre-computed call graph.

Thin ``@mcp.tool()`` wrappers around :class:`harness.checker.static.StaticIndex`.
Every tool returns human-readable text (never JSON) — the agent consumes the
result as context and makes decisions in natural language.

If ``ctx.static_index`` is ``None`` (project has no static data), each tool
returns a ``"Static analysis unavailable for project X..."`` message rather
than failing, so the agent can degrade gracefully to LLM-only checks.

Principle mapping (for the agent's workflow):
    P4 (entry adequacy)   → reachable_from, get_callers
    P3 (security bdry)    → is_public_api, lookup_symbol
    P2 (API protocol)     → get_callees, get_callers, lookup_symbol
    P1 (harness logic)    → lookup_symbol (symbol existence sanity check)
"""

from __future__ import annotations

import logging

from mcp.server.fastmcp import FastMCP

from harness.checker.tools.context import CheckerContext

logger = logging.getLogger(__name__)

_MAX_LIST_ITEMS = 50  # cap long caller/callee lists for readability
_MAX_REACH_SAMPLE = 30  # cap names shown from a reachability set


def register(mcp: FastMCP, ctx: CheckerContext) -> None:
    """Mount the 5 static_analysis tools onto *mcp*."""
    idx = ctx.static_index
    project = ctx.project
    unavailable = (
        f"Static analysis unavailable for project '{project}'. "
        f"No call graph data found under AGF_STATIC_ANALYSIS_DIR. "
        f"Fall back to code_view tools (read_file / search_files) to "
        f"reason about the call graph manually."
    )

    @mcp.tool()
    def get_callers(name: str):
        """List functions that call `name` in this project's call graph.

        Use this to verify (P4) whether a function is in the real attack
        surface: if the only callers are under test/ or fuzz/ directories,
        the function is probably not reachable from external input in
        production.  Also use it (P2) to find real usage examples — read
        the top caller via read_file to see how the function is actually
        invoked in practice.
        """
        if idx is None:
            return unavailable
        callers = idx.callers_of(name)
        if not callers:
            return (
                f"No callers of '{name}' found in {project}'s call "
                f"graph.  This means either (a) the function is not in the "
                f"graph at all, (b) it is a top-level entry point, or "
                f"(c) it is only invoked via function pointers / indirect "
                f"calls which the call-graph backend did not resolve."
            )
        shown = callers[:_MAX_LIST_ITEMS]
        header = f"{len(callers)} callers of '{name}' in {project}:"
        lines = [header]
        # Attach file_path for each caller where available.
        for name in shown:
            recs = idx.lookup(name)
            if recs:
                lines.append(f"  {name}   ({recs[0].file_path}:{recs[0].start_line})")
            else:
                lines.append(f"  {name}")
        if len(callers) > _MAX_LIST_ITEMS:
            lines.append(f"  ... ({len(callers) - _MAX_LIST_ITEMS} more omitted)")
        return "\n".join(lines)

    @mcp.tool()
    def get_callees(name: str):
        """List functions called by `name` in this project's call graph.

        Use this to (P2) understand what internal setup a function depends
        on — every callee in this list is something you may need to
        initialise, mock, or account for in your harness.  Also useful to
        (P4) verify an entry function actually reaches the logic you care
        about rather than bailing out early.
        """
        if idx is None:
            return unavailable
        callees = idx.callees_of(name)
        if not callees:
            return (
                f"No callees of '{name}' found in {project}'s call "
                f"graph.  Either the function is a leaf (makes no calls "
                f"that the backend resolved) or it's not in the graph."
            )
        shown = callees[:_MAX_LIST_ITEMS]
        header = f"{len(callees)} callees of '{name}' in {project}:"
        lines = [header]
        for name in shown:
            recs = idx.lookup(name)
            if recs:
                lines.append(f"  {name}   ({recs[0].file_path}:{recs[0].start_line})")
            else:
                lines.append(f"  {name}   [external / not-in-graph]")
        if len(callees) > _MAX_LIST_ITEMS:
            lines.append(f"  ... ({len(callees) - _MAX_LIST_ITEMS} more omitted)")
        return "\n".join(lines)

    @mcp.tool()
    def lookup_symbol(name: str):
        """Look up a function by name and return its definition location(s).

        Returns file_path, start_line, end_line, language, is_external, and
        cyclomatic_complexity for every matching FunctionRecord.  Multiple
        matches indicate overloads or same-name functions across translation
        units.

        Use this to (P1) sanity-check that a symbol you plan to call
        actually exists in the project (no LLM hallucinations), and (P2/P3)
        to locate the source file so you can read_file the definition.
        """
        if idx is None:
            return unavailable
        recs = idx.lookup(name)
        if not recs:
            return (
                f"Symbol '{name}' not found in {project}'s call graph.  "
                f"Possible causes: (a) it's a libc / stdlib function not "
                f"indexed, (b) it's a macro not a function, (c) it's a "
                f"compile-time-generated symbol, or (d) the LLM hallucinated "
                f"the name.  Try search_files to look for it textually "
                f"before assuming it exists."
            )
        header = f"{len(recs)} definition(s) of '{name}' in {project}:"
        lines = [header]
        for i, r in enumerate(recs, 1):
            ext = " [external]" if r.is_external else ""
            cc = f" cc={r.cyclomatic_complexity}" if r.cyclomatic_complexity else ""
            lines.append(
                f"  [{i}] {r.file_path}:{r.start_line}-{r.end_line} "
                f"lang={r.language or '?'}{ext}{cc}"
            )
        return "\n".join(lines)

    @mcp.tool()
    def reachable_from(entry: str, max_depth: int = 20):
        """BFS forward over the call graph from `entry`.

        Returns the count of functions reachable within `max_depth` hops
        (default 20 — deep enough to map realistic logic cones) and a
        sample of 30 reached names.

        Use this for (P4): if the reachable set is small (< 20) or all
        lives under util/ / helper/ directories, the entry point is
        probably not testing real library logic.  Use it for the Logic
        Group step to filter entry candidates.
        """
        if idx is None:
            return unavailable
        reached = idx.reachable_from(entry, max_depth=max_depth)
        if not reached:
            return (
                f"'{entry}' reaches no other functions (within depth "
                f"{max_depth}) in {project}'s call graph.  Either it's a "
                f"leaf function, or it's not in the graph."
            )
        sample = sorted(reached)[:_MAX_REACH_SAMPLE]
        lines = [
            f"'{entry}' reaches {len(reached)} functions within depth "
            f"{max_depth} in {project}.",
            "Sample of reached functions:",
        ]
        for name in sample:
            lines.append(f"  {name}")
        if len(reached) > _MAX_REACH_SAMPLE:
            lines.append(f"  ... ({len(reached) - _MAX_REACH_SAMPLE} more)")
        return "\n".join(lines)

    @mcp.tool()
    def reverse_reachable_from(target: str, max_depth: int = 20):
        """BFS backward over the call graph from `target`.

        Mirror of `reachable_from` walking the callers index instead of
        the callees index.  Returns the count of functions that can
        reach `target` within `max_depth` hops and a sample of names.

        Use this for paper §3.6 Step 3 fallback: when
        `public_apis_reaching` returns nothing useful (path heuristic
        too coarse), pull the full ancestor list here, then use
        `is_public_api` and `read_file` to judge ambiguous symbols
        yourself.
        """
        if idx is None:
            return unavailable
        ancestors = idx.reverse_reachable_from(target, max_depth=max_depth)
        if not ancestors:
            return (
                f"'{target}' has no callers within depth {max_depth} in "
                f"{project}'s call graph.  Either it is a top-level "
                f"entry, or the graph backend did not resolve any of "
                f"its callers (e.g. indirect dispatch through function "
                f"pointers)."
            )
        sample = sorted(ancestors)[:_MAX_REACH_SAMPLE]
        lines = [
            f"'{target}' is reached by {len(ancestors)} function(s) "
            f"within depth {max_depth} in {project}.",
            "Sample of ancestors (regardless of public/internal):",
        ]
        for name in sample:
            verdict = idx.is_public_api(name)
            lines.append(f"  [{verdict:8s}] {name}")
        if len(ancestors) > _MAX_REACH_SAMPLE:
            lines.append(
                f"  ... ({len(ancestors) - _MAX_REACH_SAMPLE} more)"
            )
        return "\n".join(lines)

    @mcp.tool()
    def is_public_api(name: str):
        """Classify a function as public / internal / unknown based on where
        it is defined in the project.

        Returns one of:
            public   — lives under include/, public/, or api/
            internal — lives under internal/, private/, src/, core/, util/,
                       common/, impl/, or detail/
            unknown  — cannot tell (LLM layer should judge)

        Use this for (P3 — security boundary): if a function is 'internal',
        do NOT call it directly in your harness; find the corresponding
        public entry point instead.  Note: 'unknown' is the common case for
        projects that don't use standard directory conventions (e.g. openssl
        puts its public symbols in crypto/).  Treat 'unknown' as "read the
        header yourself to decide".
        """
        if idx is None:
            return unavailable
        verdict = idx.is_public_api(name)
        recs = idx.lookup(name)
        if not recs:
            return (
                f"'{name}' not found in {project}'s call graph "
                f"(verdict: unknown).  Cannot judge public/internal from "
                f"static data — if you're planning to call this symbol, "
                f"verify it exists by reading its declaration with "
                f"read_file / search_files."
            )
        header = f"'{name}' verdict: {verdict.upper()}"
        lines = [header, f"  Based on {len(recs)} definition(s):"]
        for r in recs[:5]:
            lines.append(f"    {r.file_path}:{r.start_line}")
        if len(recs) > 5:
            lines.append(f"    ... ({len(recs) - 5} more)")
        if verdict == "unknown":
            lines.append(
                "  Note: 'unknown' means the file path doesn't match any "
                "public/internal convention.  Judge from the header file "
                "itself."
            )
        return "\n".join(lines)

    @mcp.tool()
    def public_apis_reaching(target: str, max_depth: int = 20):
        """Reverse-walk callers from `target` and return public-API ancestors
        (paper §3.6 Step~3, E_pub).

        Combines a backward BFS over the call graph with the path-based
        ``is_public_api`` heuristic: starting from `target`, walks callers
        up to `max_depth` hops and keeps only ancestors classified as
        ``public``.  This is the natural-direction implementation of
        E_pub = {f in A_pub | C ∩ callees*(f) ≠ ∅} for a single core
        member; the LG agent should union the result across the core
        members of a Logic Group.

        Use this for (P3/P4 — entry selection): when you have identified
        an interesting internal function and want to find the public-API
        entry points that reach it, call this once instead of
        per-candidate ``reachable_from`` sweeps.  An empty result means
        no public ancestor was found (within depth and the public
        heuristic), in which case the agent must either widen
        `max_depth`, fall back to internal entries, or read source to
        judge ambiguous symbols.
        """
        if idx is None:
            return unavailable
        ancestors = idx.reverse_reachable_from(target, max_depth=max_depth)
        if not ancestors:
            return (
                f"'{target}' has no callers within depth {max_depth} in "
                f"{project}'s call graph.  Either it is a top-level entry "
                f"already, or the graph backend did not resolve any of its "
                f"callers (e.g. indirect dispatch through function pointers)."
            )
        publics = idx.public_apis_reaching(target, max_depth=max_depth)
        lines = [
            f"'{target}' has {len(ancestors)} ancestor(s) within depth "
            f"{max_depth}; {len(publics)} classified as PUBLIC.",
        ]
        if publics:
            lines.append("Public-API ancestors (E_pub for this target):")
            for name in publics[:_MAX_LIST_ITEMS]:
                recs = idx.lookup(name)
                if recs:
                    lines.append(
                        f"  {name}   ({recs[0].file_path}:{recs[0].start_line})"
                    )
                else:
                    lines.append(f"  {name}")
            if len(publics) > _MAX_LIST_ITEMS:
                lines.append(
                    f"  ... ({len(publics) - _MAX_LIST_ITEMS} more public "
                    f"ancestors omitted)"
                )
        else:
            lines.append(
                "  No ancestor classified as public by the path heuristic. "
                "Either the project uses a non-standard layout (treat the "
                "ancestor list as 'unknown' and read headers to decide), "
                "or the target really has no public-API entry; in that case "
                "fall back to an internal entry that preserves boundaries."
            )
        return "\n".join(lines)

    @mcp.tool()
    def public_apis_reaching_batch(targets: list[str], max_depth: int = 20):
        """Batch reverse-search for E_pub over a whole core set C
        (paper §3.6 Step~3).

        For each `t` in `targets`, returns the public-API ancestors of
        `t` reachable within `max_depth` hops.  Use this once per LG
        candidate after you have settled on a core C: pass the full
        list, take the union of the returned values as E_pub, then
        decide whether to LLM-defer (read header to judge ambiguous
        ancestors that the path heuristic returns as "unknown").

        Output format: a per-target listing with the public-ancestor
        count and up to 10 names per target.  When all targets return
        empty lists, the path heuristic has nothing for this core --
        fall back to `reverse_reachable_from` per target plus
        `read_file` on the unknown ancestors.
        """
        if idx is None:
            return unavailable
        if not targets:
            return "No targets provided."
        result = idx.public_apis_reaching_batch(list(targets), max_depth=max_depth)

        non_empty = sum(1 for v in result.values() if v)
        union = sorted({name for names in result.values() for name in names})
        lines = [
            f"public_apis_reaching for {len(targets)} target(s) "
            f"({non_empty} non-empty); union = {len(union)} distinct "
            f"public-API ancestor(s).",
        ]
        for t in targets:
            publics = result.get(t, [])
            lines.append(f"\n  target: {t}  ({len(publics)} public ancestor(s))")
            if not publics:
                lines.append(
                    "    [empty — try reverse_reachable_from + LLM defer]"
                )
                continue
            for name in publics[:10]:
                recs = idx.lookup(name)
                loc = (
                    f"{recs[0].file_path}:{recs[0].start_line}"
                    if recs else "?"
                )
                lines.append(f"    {name:30s} {loc}")
            if len(publics) > 10:
                lines.append(f"    ... ({len(publics) - 10} more)")
        return "\n".join(lines)
