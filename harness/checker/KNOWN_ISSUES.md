# Known Issues — `harness/checker/`

Tracking list of **actual bugs** and **architectural gaps** in the
system-check flow. Each entry describes what breaks, why, the concrete
fix, and the resolution status.

**Session log**:
- 2026-04-14: file created with 4 entries discovered during initial
  E2E smoke (ISSUE-1 coverage broken, ISSUE-2 is_public_api conservative,
  ISSUE-3 libyaml shallow, ISSUE-4 web_fetch/github stubs)
- 2026-04-15: P1 fixed ISSUE-1 (coverage rebuild now works end-to-end)

---

## ISSUE-1: `get_coverage` tool is broken (cannot produce llvm-cov metrics)

**Status**: ✅ **FIXED in P1 (2026-04-15)** — see commit `e6dfe7c`.
**Discovered**: 2026-04-14 during E2E smoke run `smoke-dyn-1`.
**Severity**: was blocking the 4-metric coverage comparison against gold.
**Resolution**: `DynamicRunner._rebuild_with_coverage()` mirrors
`eval_gold_coverage.py`'s pattern — reset OSS-Fuzz project, rmtree
build/out + build/work, rebuild via `BuildValidator(sanitizer="coverage")`.
`coverage()` lazy-triggers the rebuild on first call and seeds the
default-location corpus dir from the per-worker `_systemcheck` corpus
so `helper.py coverage` finds the inputs.  Validated on libyaml gold:
lines=75.68% branches=62.13% functions=68.61% regions=67.65%, matching
the gold_coverage_100.md baseline.

**Order constraint** (now documented in the tool docstring): calling
`get_coverage` invalidates the address-sanitized binary, so
`AP_Run_check` / `run_gdb` must finish first.

### Symptom

Agent calls `get_coverage(binary_name)` after `build_harness` + `AP_Run_check`.
Tool returns:

```
COVERAGE ERROR: Coverage failed (exit 1): ... ERROR:__main__:Failed to
generate clang code coverage report.
```

Seen in `/tmp/agf-ze/system-check-runs/e2e-smoke-dyn/smoke-dyn-1/.../trajectory.jsonl`
at turn 16.

### Root cause

`infra.ossfuzz.runner.collect_coverage` shells out to OSS-Fuzz
`helper.py coverage`, which expects the project to be built with
`SANITIZER=coverage`.  Our `BuildValidator.validate()` builds with
`SANITIZER=address` (the default we pass via `DynamicRunner(sanitizer="address")`).
There is no coverage-instrumented binary in `build/out/<project>/`,
so `llvm-cov` cannot generate the report.

**The full chain already supports `sanitizer` as a parameter:**

- `infra/ossfuzz/build.py:78`  — `build_project(sanitizer="address")`
- `harness/builder.py:220`      — `BuildValidator(sanitizer="address")`
- `harness/checker/dynamic.py:140` — `DynamicRunner(sanitizer="address")`

We simply never instantiate with `sanitizer="coverage"`.  The plumbing
is there; we just don't use it.

### Why it's not a one-line fix

`get_coverage` cannot simply swap the sanitizer to coverage, because:

1. **Both builds are needed.**  The fuzz run in `AP_Run_check` uses
   AddressSanitizer for crash detection; the coverage measurement
   needs the coverage-instrumented binary.  We need **two builds,
   sequentially**, over the same workspace.
2. **The second build must start from a clean project state.**
   `eval_gold_coverage.py:151-160` explicitly does
   `reset_oss_fuzz_project` + `_clean_build_artifacts` before the
   coverage build.  Without this, CMake / autoconf projects (icu,
   libjxl, freerdp, etc.) reuse `.o` files from the address build
   and fail to link under the coverage sanitizer.
3. **Order matters and is irreversible.**  Once we rebuild with
   coverage, the address binary is gone — `AP_Run_check` cannot be
   called again after `get_coverage`.
4. **The corpus survives both builds.**  libFuzzer wrote corpus files
   into `build/corpus/<project>/<fuzzer>/` during `AP_Run_check`.
   `collect_coverage` reads that same corpus when running the
   coverage-instrumented binary under `llvm-cov`.

### The designed fix (not yet applied)

In `harness/checker/dynamic.py`, add a new private method
`_rebuild_with_coverage()` that mirrors the pattern in
`benchmark/oss_fuzz_harness/eval_gold_coverage.py:151-180`:

```python
def _rebuild_with_coverage(self) -> BuildResult:
    workspace = self._require_workspace()
    reset_oss_fuzz_project(workspace, self.project)
    self._clean_project_artifacts(workspace)   # rmtree build/out + build/work
    if self.docker_lock:
        with self.docker_lock:
            return build_project(workspace, self.project, sanitizer="coverage")
    return build_project(workspace, self.project, sanitizer="coverage")

def coverage(self, binary_name=None) -> CoverageMetrics:
    if not self._coverage_built:
        r = self._rebuild_with_coverage()
        if not r.success:
            return CoverageMetrics(error=f"coverage rebuild failed: {r.error}")
        self._coverage_built = True
    return collect_coverage(self._workspace_dir, self.project, binary_name or self._built_binary)
```

Approximate size: ~30 lines.  Uses only imports from existing
`infra/ossfuzz/build.py` — no existing file is modified.

The `get_coverage` MCP tool docstring must also be updated to warn
that the tool is one-shot-per-run and irreversible:

> **Order constraint**: `get_coverage` rebuilds the project with
> coverage sanitizer.  This **invalidates the address-sanitized
> binary**.  You cannot call `AP_Run_check` after `get_coverage` in
> the same flow.  Always finish all runtime checks (AP_Run_check,
> run_gdb) before calling `get_coverage`.

### Cost of the fix

- Extra Docker build: ~30-120s per `get_coverage` call
- Code: ~30 lines in `dynamic.py`, ~10 lines of docstring in
  `tools/dynamic_analysis.py`
- No changes to existing files outside `harness/checker/`
- No new dependencies

### Why we haven't fixed it yet

User directive on 2026-04-15: record first, fix later.
This document exists because of that directive.

### Workaround for benchmark runs (until the fix lands)

For any head-to-head comparison that needs the 4 llvm-cov metrics,
use `benchmark/oss_fuzz_harness/eval_gold_coverage.py` (for gold) or
`benchmark/oss_fuzz_harness/eval_coverage.py` (for generated) directly
— both already do the two-build dance correctly.  Feed the generated
harness into `eval_coverage`'s input path instead of relying on the
system-check flow's `get_coverage` tool.

---

## ISSUE-2: `is_public_api` is conservative for projects whose public API lives under `src/`

**Status**: works-as-intended but impacts `check_p3` precision.
**Severity**: low — LLM layer compensates, agent reads headers.

### Symptom

On openssl / libyaml / jq, `is_public_api("SSL_new")` /
`is_public_api("yaml_parser_load")` / `is_public_api("jv_parse")`
return `"unknown"` rather than `"public"`, because the symbol
definition lives under `ssl/`, `src/`, etc. — none of which match
our `_PUBLIC_PATH_MARKERS = ("include/", "public/", "api/")`.

### Root cause

Different projects use different source-tree conventions.  openssl's
public API is declared in `include/openssl/*.h` but *defined* in
`crypto/` and `ssl/` — the FunctionRecord file_path is the definition,
not the declaration.  Our heuristic reads the definition path.

### Why it isn't broken

`check_p3` still works because:
- `"unknown"` is not flagged as a violation (only `"internal"` is)
- The LLM layer (`check_p3` Layer 2) independently reads the `#include`
  directives in the harness and can recognize public API headers
- Observed in trajectories: the agent follows up `"unknown"` with
  `read_file("include/yaml.h")` to verify the declaration by hand

### Fix (not urgent)

Per-project override table, e.g.:

```python
_PROJECT_PUBLIC_PREFIXES = {
    "openssl": ("crypto/", "ssl/", "include/"),
    "libyaml": ("include/", "src/"),
    # ...
}
```

Or parse `Makefile.am` / `pkg-config` files to find the declared
`include_HEADERS`.  The latter is more principled.

### Cost

Dozens of per-project tuning entries or a parser for each build system.
Not worth doing unless we see concrete P3 misclassification damaging
benchmark numbers.

---

## ISSUE-3: libyaml's static call graph is shallow (Joern limitation)

**Status**: data-quality issue, not a code bug.
**Severity**: low — affects `check_p4` static layer false-positive
rate on libyaml specifically.

### Symptom

`reachable_from("yaml_parser_load", max_depth=20)` on libyaml returns
only **4** reached functions.  The static-layer `check_p4` heuristic
is "reach < 20 → SHALLOW", so libyaml's real entry function trips
the flag.

### Root cause

Joern's frontend resolved only 319 edges for 223 functions in libyaml.
Many calls inside libyaml go through `yaml_emitter_state_machine`-style
function-pointer dispatch tables that Joern did not resolve.  The
call graph is technically correct — those edges simply aren't there
as direct calls in the source.

Verified in `/home/ze/agf-data/static_analysis/libyaml/metadata.json` —
the backend used is Joern with default `importCpg` settings (no
reaching-definitions enhancement).

### Why it isn't broken

`check_p4` Layer 2 (LLM) independently reasons about whether the
entry reaches real logic based on what it reads in `src/loader.c`,
and the agent accepts libyaml's answer and continues.  Observed in
trajectories: agent sees the "SHALLOW" flag and writes the harness
anyway based on source reading.

### Fix

Re-run the libyaml static analysis with a fuller Joern pass that
resolves function pointers (e.g.,
`importCode("libyaml", language="c") |> reachingDefs`) and
reconstructs indirect-call edges.  This is an offline data-refresh
task, not a code change.

### Cost

One Joern run per affected project.  Not blocking; affects only
false-positive rate of `check_p4` static layer.

---

## ISSUE-4: `web_fetch` and `github` tool categories are stubs

**Status**: intentional — shipped as placeholder categories.
**Severity**: none — default `register_all` doesn't mount them,
agent never sees them.

### Description

`harness/checker/tools/web_fetch.py` and `github.py` register tools
that return `"not implemented"` strings:

- `fetch_url(url)`
- `search_github_issues(project, query)`
- `search_github_prs(project, query)`
- `fetch_github_file(project, path, ref)`

The category dispatch is real (they register on a FastMCP instance),
but the function bodies are placeholders.  `DEFAULT_CATEGORIES` in
`tools/__init__.py` does **not** include these — you must explicitly
pass `categories=(..., "web_fetch", "github")` to `register_all` to
get them mounted.

### When to implement

- **`fetch_url`** — useful for Phase 2 (P2 API protocol research) so
  the agent can read upstream API docs / man pages / Stack Overflow
  pages instead of only reading the local project source.  Design
  shape: fetch → strip boilerplate → return ~4KB of readable text.
- **`github`** — useful for Phase 4 P1 / P2 audit: the agent can
  look up prior incident reports or upstream fix commits that
  affected the target API.  Needs `ctx.github_token`.

---

## How this file should evolve

Add an entry here as soon as any issue is discovered, even if fixing
it is trivial.  The rule: **if we said "works" but it doesn't, it
belongs in this file**.  Close an entry by deleting it when the fix
lands; keep the git history as the audit trail.
