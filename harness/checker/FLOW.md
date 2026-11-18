# System Check — Full Flow

Single source of truth for how the `harness/checker/` pipeline is
supposed to work end-to-end. Separate from the implementation report
(which documents what IS built) and from `KNOWN_ISSUES.md` (which
documents what's broken). This doc is the **target design**.

Authored: 2026-04-15. Keep in sync with actual code as it lands.

---

## 1. Goal

Take a **C/C++ project** and a **natural-language functionality
description** (the "prompt"), and produce a **P1–P4-verified fuzz
harness** — source code, compiled binary, coverage binary, N-second
fuzzing report, per-principle audit report, and a functional summary.

The pipeline has two agentic stages chained together:

```
(project, prompt)
       │
       ▼
  ┌────────────────────┐
  │  LogicGroupAgent   │   Explores the project to identify a
  │                    │   coherent feature matching the prompt.
  │  tools: code_view  │   Produces a LogicGroup with entries + core.
  │  tools: static     │
  └─────────┬──────────┘
            │  LogicGroup
            ▼
  ┌────────────────────┐
  │ SystemCheckAgent   │   Picks target from LG.entries, writes a
  │                    │   harness, self-audits P4→P3→P2→P1, builds,
  │  tools: code_view  │   runs, collects coverage.
  │  tools: static     │
  │  tools: dynamic    │   Submits when all checks pass.
  │  tools: quality    │
  └─────────┬──────────┘
            │  harness.c + binaries + reports
            ▼
        case_dir
```

Each stage is an independent BaseAgent subclass; each can be run in
isolation. The flow wires them together plus the scaffolding
(ensure_repo, static index load, per-worker workspace).

---

## 2. Data model

### `LogicGroup`

A coherent functional unit worth fuzzing as one thing. Mirrors the
paper's definition but we add fields incrementally as we need them.

```python
@dataclass
class LogicGroup:
    name: str                        # human-readable feature name
    description: str                 # agent's own summary after reading code
    entries: list[str]               # candidate public API entry points
    core: list[str]                  # internal functions reachable from entries
    project: str                     # owning project
    risk_level: str = ""             # low/medium/high — unpopulated for now
```

`description` is produced by the agent after exploring, NOT a copy of
the input prompt. The delta between `prompt` and `LG.description` is an
intrinsic ablation signal for "did the agent understand the prompt".

### `SystemCheckCase`

One task description fed into the flow.

```python
@dataclass
class SystemCheckCase:
    project: str                     # required
    functionality_prompt: str = ""   # natural-language description
    target_function: str = ""        # optional direct target
    logic_group: LogicGroup | None = None  # optional pre-computed LG

    repo_url: str = ""
    case_id: str = ""
    fuzzer_name: str = ""
    language: str = "c"
    gold_source_file: str = ""
```

Exactly one of `{functionality_prompt, target_function, logic_group}`
is the "driver" input. Others are optional context. The driver
determines which mode the flow runs in (see §3).

### `SystemCheckResult`

Flat record written as `result.json` per case. Must carry every field
needed to reconstruct the case without re-reading the trajectory.

```python
@dataclass
class SystemCheckResult:
    case_id: str
    project: str
    success: bool                    # submit_harness fired

    # Output artifacts
    harness_code: str
    harness_filename: str
    harness_path: str
    harness_binary_path: str         # address-sanitized binary
    coverage_binary_path: str        # coverage-sanitized binary
    trajectory_path: str

    # Stage 1 output (if LG mode)
    logic_group: dict | None         # serialized LogicGroup
    lg_description_delta: float      # similarity vs input prompt

    # Stage 2 output
    target_function: str             # what was picked
    target_source_method: str        # "given" | "lg_selection" | "prompt_search"

    # Quality audit
    p1_violation: bool
    p2_violation: bool
    p3_violation: bool
    p4_violation: bool
    p1_report: str                   # full check_p1 output
    p2_report: str
    p3_report: str
    p4_report: str

    # Dynamic
    build_ok: bool
    build_error: str
    run_edges: int
    run_features: int
    run_crashed: bool
    coverage_lines_pct: float
    coverage_branches_pct: float
    coverage_functions_pct: float
    coverage_regions_pct: float

    # Bookkeeping
    total_turns: int
    duration_ms: int
    input_tokens: int
    output_tokens: int
    estimated_cost: float
    llm_model: str
    tool_call_counts: dict           # {tool_name: count}
    static_index_available: bool
    error: str
```

Anything not applicable to a particular run (e.g. `lg_description_delta`
in target_function mode) is left at default, not None-vs-empty
ambiguity.

---

## 3. Three entry points (input modes)

### Mode A — `prompt` mode (used by Exp 1 and real-world deployment)

```
input:  SystemCheckCase(project, functionality_prompt="...")
path:   ensure_repo → static_index → LogicGroupAgent → SystemCheckAgent
```

LogicGroupAgent produces the LG, SystemCheckAgent consumes it.

### Mode B — `logic_group` mode (re-use cached LG, or external LG)

```
input:  SystemCheckCase(project, logic_group=LogicGroup(...))
path:   ensure_repo → static_index → SystemCheckAgent
```

Skips LogicGroupAgent. Useful for replaying a previous run, or for
experiments that want to control the LG exactly.

### Mode C — `target_function` mode (used by Exp 2 big baseline table)

```
input:  SystemCheckCase(project, target_function="yaml_parser_load")
path:   ensure_repo → static_index → SystemCheckAgent (Phase 1 = verify)
```

Skips LogicGroupAgent AND Phase 1 candidate search. Agent goes directly
to verifying the given target and then writing the harness.

Modes A, B, C share the same output schema so results can be compared
directly across modes.

---

## 4. Pipeline stages (detailed)

```
┌──────────────────────────────────────────────────────────┐
│ run_system_check(case)                                   │
└──────────────────────────────────────────────────────────┘
   │
   ├─ Stage 0 — Prepare
   │   ├─ ensure_repo(project, repo_url)       → clone to /tmp/agf-ze/repos/<p>
   │   ├─ per-worker workspace dir              → /tmp/agf-ze/system-check-runs/<run>/<case>/
   │   ├─ StaticIndex.load(project)             → may return None
   │   ├─ DynamicRunner(workspace, project)     → lazy build
   │   └─ CheckerContext(...)
   │
   ├─ Stage 1 — Logic Group
   │   if case.logic_group is not None:
   │       lg = case.logic_group                ← Mode B
   │   elif case.target_function:
   │       lg = None                            ← Mode C: skip
   │   else:
   │       lg = LogicGroupAgent(ctx, prompt=case.functionality_prompt).run()  ← Mode A
   │       write lg.json to case_dir
   │
   ├─ Stage 2 — Harness generation
   │   SystemCheckAgent(ctx, case, lg=lg).run()
   │     Phase 1 — target selection
   │       Mode A/B: choose from lg.entries via static signals + LLM
   │       Mode C:   verify case.target_function, no search
   │     Phase 2 — protocol research
   │       get_callers → read caller source → learn usage pattern
   │     Phase 3 — write draft
   │     Phase 4 — self-audit
   │       check_p4 → check_p3 → check_p2 → check_p1
   │       each violation ⇒ agent revises and re-runs the failing check
   │     Phase 5 — dynamic verification
   │       build_harness (address sanitizer)
   │       AP_Run_check (600s, empty corpus — benchmark convention)
   │       optional run_gdb on any crash
   │     Phase 6 — submit_harness
   │
   ├─ Stage 3 — Dynamic artifacts finalization
   │   ├─ copy build/out/<p>/<fuzzer> → case_dir/binaries/<fuzzer>
   │   ├─ _rebuild_with_coverage (ISSUE-1 fix; see KNOWN_ISSUES.md)
   │   ├─ copy build/out/<p>/<fuzzer> → case_dir/binaries/<fuzzer>.cov
   │   ├─ collect_coverage → 4-metric report
   │   └─ parse P1–P4 reports from trajectory → structured fields
   │
   └─ Stage 4 — Persist
       ├─ result.json (SystemCheckResult)
       ├─ trajectory.jsonl (LogicGroupAgent + SystemCheckAgent merged)
       ├─ lg.json (if Stage 1 ran)
       └─ reports/{p1,p2,p3,p4}.md (structured audit reports)
```

---

## 5. Per-case output artifacts

```
/tmp/agf-ze/system-check-runs/<run-id>/<case-id>/
├── result.json                         flat record, primary output
├── trajectory.jsonl                    every LLM + tool event
├── lg.json                             LogicGroup (if Mode A)
├── <fuzzer>.c                          generated harness source
├── gold_<fuzzer>.c                     copy of gold, for diff convenience
├── binaries/
│   ├── <fuzzer>                        address-sanitized binary
│   └── <fuzzer>.cov                    coverage-sanitized binary
├── corpus/
│   └── <...>                           libFuzzer corpus from 600s run
├── coverage/
│   └── summary.json                    llvm-cov raw output
└── reports/
    ├── functionality.md                agent-written one-paragraph summary
    ├── p1.md                           P1 audit text + verdict
    ├── p2.md
    ├── p3.md
    └── p4.md
```

---

## 6. Batch-level artifacts

```
/tmp/agf-ze/system-check-runs/<run-id>/
├── run_manifest.json                   run_id, timestamps, git SHA,
│                                       model, mode, case count, CLI args
├── prompts.jsonl                       exact input used
├── results.jsonl                       one flattened result per case
└── summary.json                        aggregates: success rate, means,
                                        tool frequency, per-project breakdown
```

---

## 7. Experiments

### Exp 1 — prompt → LG → target → fuzzer

**Dataset**: `harness/checker/experiments/benchmark_cases_with_prompts.jsonl`
— a 100-case fork of `benchmark_cases_gold_buildable.jsonl`, with each
row extended by a `prompt` field auto-generated by a reverse-engineering
script that reads the gold harness source and produces a natural-language
description via LLM.

**Input**: `SystemCheckCase(project, functionality_prompt=<derived>)`
→ Mode A.

**Metrics**:
- **Recall** — is `gold_target_function` in `LG.entries`? Proportion
  over 100 cases.
- **Precision** — did SystemCheckAgent pick `gold_target_function`
  from `LG.entries`? Proportion over 100 cases.
- **Downstream fuzzer quality** — edges + 4-metric coverage, compared
  head-to-head against gold under identical conditions (600s empty
  corpus).
- **Description delta** — semantic similarity between the input prompt
  and `LG.description`. Low delta ⇒ agent understood the prompt
  accurately.

**Baselines**: none — this is a novel evaluation of prompt→fuzzer.
Validity comes from real-world efficacy (vulnerabilities found), not
cross-system comparison.

### Exp 2 — target_function → fuzzer (big table)

**Dataset**: `benchmark_cases_gold_buildable.jsonl` unchanged.

**Input**: `SystemCheckCase(project, target_function=<from benchmark>)`
→ Mode C.

**Metrics**: edges + line/branch/function/region % after 600s.

**Baselines**: Gold (human-written), OSS-Fuzz-Gen, PromeFuzz,
CKGFuzzer — anything that accepts a target function and produces a
harness.

### Ablation

Run after Exp 1 and Exp 2 complete. Dimensions to ablate:

| Dim | Variant | Question |
|---|---|---|
| LG stage | on vs off | Does going through an LG help final fuzzer quality? (compare Mode A vs Mode C on the same cases) |
| Static layer | on vs off | Does P3/P4 static verdict improve anything over LLM alone? |
| Tool category | quality_check off | How much does self-audit matter? |
| Workflow order | P4→P3→P2→P1 vs reverse | Does reverse-order matter? |
| LLM model | sonnet vs haiku | Cost-quality tradeoff |

### Real-world deployment

Uses existing 33 vulnerabilities + 3 CVEs + 5 upstream adoptions data
from the semi-automated prototype. The flow here is:

1. Take a project where the prototype found a real bug
2. Feed the same feature prompt through Mode A of `run_system_check`
3. Show end-to-end trajectory + final harness
4. Case study: 1–2 projects walked through in detail
5. Argument: the pipeline reproduces the finding path, validating the
   architecture on real security-relevant targets

This is not a fresh experiment — it's formalization of prior results
into the new pipeline structure.

---

## 8. What exists today vs what still needs building

### Exists
- Mode C (target_function) end-to-end through SystemCheckAgent
- Static analysis via StaticIndex (though `is_public_api` heuristic is
  weak — see KNOWN_ISSUES ISSUE-2)
- Dynamic analysis for build + run (coverage broken — ISSUE-1)
- P1–P4 quality_check tools (static + LLM)
- Parallel batch + per-worker workspace
- Per-case `result.json` + `trajectory.jsonl`

### Needs building
- `LogicGroup` dataclass
- `LogicGroupAgent` — BaseAgent subclass with code_view + static tools
- Reverse-engineering script that derives prompts from gold harnesses
- `SystemCheckCase.logic_group` field + Mode A/B branching in flow
- LG → target selection logic in SystemCheckAgent Phase 1
- Coverage-sanitizer rebuild path in DynamicRunner (ISSUE-1 fix)
- Structured P1–P4 reports parsed from trajectory into `result.json`
- Binary copy to `case_dir/binaries/`
- Coverage binary copy to `case_dir/binaries/<fuzzer>.cov`
- Functionality summary generation (one paragraph per case)
- `run_manifest.json` / `results.jsonl` / `summary.json` batch outputs
- Gold harness copy into case_dir for diff
- LLM delta scorer for description-vs-prompt similarity

### Intentionally deferred
- `web_fetch` and `github` tool categories remain stubs
- Full project-only auto-discovery of multiple LGs from existing
  fuzzers (only relevant if we add a non-prompted discovery experiment
  later)
- `is_public_api` innovation beyond path-prefix heuristics
- Joern re-run for libyaml to fix shallow call graph (ISSUE-3)

---

## 9. Decisions still pending

- Final reverse-engineering prompt template: how we describe a gold
  harness's functionality in natural language (affects Exp 1 quality
  ceiling)
- LG → target selection algorithm inside SystemCheckAgent Phase 1:
  pure static scoring, pure LLM, or weighted combination
- Description similarity metric for the prompt/LG.description delta
- Ablation matrix: which axes to prioritize if we run out of compute
- Whether Exp 1 and Exp 2 reuse the same run artifacts (same case_id,
  different mode) or live in separate run directories
