# QuartetFuzz

This repository accompanies the manuscript *"Quality-Assured Fuzz
Harness Generation via the Four Principles Framework"*.

QuartetFuzz is an autonomous LLM-agent system that produces fuzz
harnesses (the small `LLVMFuzzerTestOneInput`-style C/C++ programs
that connect a fuzzer to a library API) with quality gates derived
from four source-level principles.  Given a project source tree and
a one-paragraph description of the feature to fuzz, the system goes
from "no harness yet" to a built, sanitised, coverage-measured
harness that an OSS-Fuzz maintainer would accept — without a human
in the loop.

## What the pipeline does

Four stages run as three agents plus an inline gate:

1. **Stage 1 — Logic Group Discovery** (`harness/checker/lg_agent.py`,
   Opus 4.6, 50 turns).  The LG agent reads the project source and
   proposes Logic Groups: bundles of `(name, entries, core,
   description)` describing one functional unit worth fuzzing
   (e.g. *"libplist binary parse-serialize round-trip"*).  Candidates
   are ranked by a static danger score (paper Eq. 5: depth-discounted
   unsafe-operation reach over the call graph) and the top-`k`
   advance.  Outputs: `lg_*.json`, `all_lgs.json`.

2. **Stage 2 — API Protocol Research**
   (`harness/checker/p2_agent.py`, Opus 4.6, 30 turns).  For each
   selected LG the P2 agent walks the source and writes a structured
   protocol report: init order, parameter construction, object
   lifecycle, return-value handling, cleanup sequence, API existence,
   co-call constraints, prerequisite state — one claim plus one
   piece of source-level evidence per sub-check.  Output:
   `p2_report.md`.

3. **Stage 3 — Static-Driven Build**
   (`harness/checker/flow.py`, Sonnet 4.6, 50 turns).  The
   SystemCheck agent drafts the harness source, runs a bounded
   build–fix loop using `DynamicRunner.build()` (which
   swap-and-rebuilds against the project's gold OSS-Fuzz Dockerfile),
   and self-reviews each P1 / P2 sub-check semantically against the
   source.  Output: a successfully-built ASan-sanitised binary +
   the harness source.

4. **Stage 4 — Adversarial Validation** (same agent as Stage 3).
   Submission is gated on two adversarial probes (paper §3.4):
   - `AP_Run_check` (Probe 2, P1.x): the agent writes a Python
     `generate() -> bytes` that emits one attack blob; the system
     feeds it to the ASan/LSan binary inside the project's
     OSS-Fuzz container as a single test input.
   - `AP_Reach_check` (Probe 1, P2.x): same machinery but with GDB
     and a breakpoint on the target API to confirm the harness
     actually drives execution into the function it claims to
     fuzz.

   Both probes must fire (no sanitizer crash on agent input;
   target reached on a valid input) before `submit_harness` is
   accepted.  Then `get_coverage` runs libFuzzer for 600 s on
   empty corpus, rebuilds with `SANITIZER=coverage`, and reports
   line / branch / function / region coverage via `llvm-cov`.

## The Four Principles

| | What it asks of the harness |
|---|---|
| **P1** Logic Correctness | The harness is bug-free: no leaks, no UAF, no stale state, fuzz input actually flows to the API, size/buffer/UB-safe. (8 sub-checks.) |
| **P2** API Protocol Compliance | The harness calls the library in the correct order with correct parameters, lifecycles, and cleanup. (8 sub-checks.) |
| **P3** Security Boundary Respect | Entry functions exercise the public API surface, not internal helpers that bypass validation. |
| **P4** Entry Point Adequacy | Entry functions reach a security-relevant attack surface (`unsafeReach > 0`). |

P1 and P2 are gated by Adversarial Probing.  P3 and P4 are gated by
the LG agent's static reachability check plus an inline reach probe
in Stage 4.  See
[`harness/checker/HARNESS_CHECKING_MANUAL.md`](harness/checker/HARNESS_CHECKING_MANUAL.md)
for the full per-sub-check table and
[`docs/AP_PROBE_EXAMPLE.md`](docs/AP_PROBE_EXAMPLE.md) for a real
input/output pair captured from a live run.

## How the pieces in this repo fit together

```
┌─────────── agent/ ───────────┐
│  BaseAgent + litellm client  │  reused by all three agents
│  (turn loop, context         │
│   compression that preserves │
│   the latest harness/LG/P2   │
│   artefact across turns)     │
└──────────────┬───────────────┘
               │
┌──────────────┼─────────────────────────────────────────────────┐
│  harness/checker/                                              │
│   lg_agent.py ─── Stage 1 ──▶ lg_*.json, all_lgs.json          │
│   p2_agent.py ─── Stage 2 ──▶ p2_report.md                     │
│   flow.py ─────── Stage 3+4 ─▶ harness source + result.json    │
│                       │                                        │
│                       ▼                                        │
│   tools/                                                       │
│    ├ code_view.py        Read / Glob / Grep                    │
│    ├ static_analysis.py  call-graph queries (8 tools)          │
│    └ dynamic_analysis.py build / AP probes / coverage / GDB    │
│                                                                │
│   dynamic.py ──── DynamicRunner: build / run_blob / coverage / │
│                   gdb — AP probes execute inside OSS-Fuzz      │
│                   Docker for glibc compatibility               │
│   static.py ──── StaticIndex: precomputed call-graph queries   │
│                   with SAF (Static-Analysis Fallback) mode     │
└─────────────────────────────────────┬──────────────────────────┘
                                      │
                                      ▼
                ┌──────────────────────────────────┐
                │  infra/ossfuzz/                  │
                │  helper.py wrapper, build,       │
                │  runner, workspace utilities     │
                └──────────────────────────────────┘
```

The system **does not produce static analysis itself**.  It consumes
a precomputed call graph through `StaticIndex.load(project)`.  Any
SAST backend (Joern, SVF, tree-sitter, hand-written) that emits the
documented JSON schema is a valid producer; missing data degrades
the LG / P2 agents to LLM-only judgement (SAF mode) rather than
aborting.

The remainder of this README is organised around the three things
a reviewer would want to do with the artefact:

- **Dataset** — the final 100 cases in `dataset_v2/`.
- **④ End-to-end demo** — `demo/libplist/`, with all LLM
  trajectories preserved so the agent's reasoning is replayable.

---

## Dataset

The evaluation uses exactly one dataset: **[`dataset_v2/`](dataset_v2/)** — 100 OSS-Fuzz
gold harnesses, frozen in commit 251d983. `dataset_v2/benchmark_100.jsonl` is the
authoritative case list; `dataset_v2/README.md` documents every file.
`dataset_v2/gold_source_paths.json` maps each case to the location of its gold harness
inside the project's build (`$SRC/...`); the build validator places a generated
harness there.

Earlier case sets and their scripts are kept under [`legacy/`](legacy/) for the record
only; no code reads them.

---

## ④ End-to-end demo — `demo/libplist/`

A complete walk-through of the pipeline, with **all LLM trajectories
preserved** so the agent's full reasoning can be replayed.

```
demo/libplist/
├── REPORT.md                    Stage flow + per-LG outcomes + repro.
├── all_lgs.json                 Nine LG candidates the LG agent produced.
├── lg_1.json … lg_5.json        Top-5 selected by danger score.
├── lg_1/ … lg_5/
│   ├── lg_analysis.json         The LG itself.
│   ├── p2_report.md             P2 agent's API protocol report.
│   └── fuzzer.cc                Final harness source the agent submitted.
└── run-<id>/libplist_<slot>/
    ├── trajectory.jsonl         ★ Every LLM turn for P2 + SystemCheck.
    ├── result.json              Flow-level summary.
    ├── coverage/summary.json    llvm-cov line/branch/function metrics.
    └── <slot>.cc                Source compiled into the OSS-Fuzz slot.
```

Re-run from the existing top-5 LGs (skips Stage 1, ~15 min):

```bash
python3 scripts/demo_libplist.py --batch all
```

Re-run **everything from scratch** including LG discovery (~80 min):

```bash
python3 -m harness.checker.pipeline --mode lg \
    --project libplist --output-dir demo/libplist \
    --num-lgs 10 --top-k 5 \
    --prompt "Identify 5 high-value Logic Groups in libplist for fuzzing..."
python3 scripts/demo_libplist.py --batch all
```

See [`demo/libplist/REPORT.md`](demo/libplist/REPORT.md) for full
per-LG cost / turns / coverage and an explanation of the slot-mapping
(libplist's OSS-Fuzz project ships exactly four `*_fuzzer.cc` slots,
so 5 LGs map to 4 slots with one reuse).

A separate input/output example for both Adversarial Probing
operations (paper §3.4), drawn from a different live run, lives at
[`docs/AP_PROBE_EXAMPLE.md`](docs/AP_PROBE_EXAMPLE.md).

---

## Configuration

### Models per stage

| Stage | Default | Override |
|---|---|---|
| LG agent | `claude-opus-4-6` | `harness/checker/lg_agent.py` |
| P2 agent | `claude-opus-4-6` | `harness/checker/p2_agent.py` |
| Harness gen + AP probes | `claude-sonnet-4-6` | `harness/checker/tools/context.py` (`DEFAULT_LLM_MODEL`) |

The artifact ships with the paper's main configuration.  Models are
dispatched through `litellm`; any model `litellm` understands works
as a drop-in (`gpt-5`, `deepseek/deepseek-chat`,
`gemini/gemini-3-flash-preview`, …).

To switch to Gemini Flash for ~10× cheaper reproduction (with the
expected coverage gap from paper Table 7):

```python
# harness/checker/lg_agent.py and p2_agent.py
model = "gemini/gemini-3-flash-preview"
# harness/checker/tools/context.py
DEFAULT_LLM_MODEL = "gemini/gemini-3-flash-preview"
```

### Environment variables

| Variable | Purpose | Default |
|---|---|---|
| `ANTHROPIC_API_KEY` | Default model family. | — |
| `GEMINI_API_KEY` | If you switch to Gemini. | — |
| `OPENAI_API_KEY` | If you switch to GPT-x. | — |
| `AGF_STATIC_ANALYSIS_DIR` | Override static-analysis location. | `dataset_v2/static_analysis/` (not shipped yet; tools report "unavailable") |
| `AGF_RUNS_ROOT` | Per-run output root. | `/tmp/agf-$USER` |

### Fuzz budget

The benchmark protocol uses **600 s** per `get_coverage` call (10
min libFuzzer pre-run + coverage-rebuild + llvm-cov).  Both
`DynamicRunner.coverage(fuzz_seconds=600)` and the
`get_coverage(binary_name, duration=600)` MCP tool default to 600 s.
Lower values (e.g. 10 s) are appropriate only for demos /
smoke-tests where coverage *quality* is not the goal.

---

## RQ-by-RQ matrix

| RQ | Measures | What runs | Subset | Wallclock |
|---|---|---|---|---|
| **RQ3** | Generated-harness coverage vs. gold + LLM baselines | Full pipeline + 600 s libFuzzer + llvm-cov | `dataset_v2/benchmark_100.jsonl` (100) | to be measured on the final set |
| **RQ2** | LG-stage entry-selection match | LG agent only, no fuzz | `dataset_v2/benchmark_100.jsonl` (100) | to be measured on the final set |
| **RQ1** | Production-harness audit (paper §5.2) | Stage 4 only on existing OSS-Fuzz harnesses | external | (long) |
| **RQ5** | Real-world deployment (paper §5.6) | Full pipeline on outside projects | project-dependent | (long) |

RQ1 and RQ5 are out of scope for this artifact and reproduce
against externally-pinned OSS-Fuzz commits.

---

## Repo layout

```
QuartetFuzz/
├── agent/                 LLM agent framework (turn loop, litellm client,
│                          context compression w/ artifact preservation).
├── harness/
│   ├── builder.py         BuildValidator: gold-source swap + OSS-Fuzz build.
│   ├── _env.py            Loads .env at import time.
│   └── checker/
│       ├── lg_agent.py    Stage 1: Logic-Group discovery + danger ranking.
│       ├── p2_agent.py    Stage 2: API-protocol research.
│       ├── flow.py        Stage 3+4: harness gen + adversarial validation.
│       ├── pipeline.py    Top-level orchestrator + CLI.
│       ├── dynamic.py     DynamicRunner: build / run_blob / coverage / gdb
│       │                  — AP probes execute inside OSS-Fuzz Docker.
│       ├── static.py      StaticIndex: call-graph queries + SAF fallback.
│       ├── danger_score.py  Eq. 5 implementation.
│       ├── logic_group.py  LogicGroup + SystemCheckCase dataclasses.
│       ├── prompts/       Jinja2 prompts for each agent.
│       └── tools/
│           ├── code_view.py        Read / Glob / Grep (from agent/tools/)
│           ├── static_analysis.py  get_callers / get_callees / find_definition /
│           │                       forward_reach / reverse_reach /
│           │                       public_entries_for / public_entries_for_batch /
│           │                       is_public_api
│           └── dynamic_analysis.py build_harness / AP_Run_check / AP_Reach_check /
│                                   get_coverage / run_gdb
├── infra/ossfuzz/         OSS-Fuzz integration (build / runner / workspace utils).
├── scripts/
│   └── demo_libplist.py          End-to-end libplist demo driver.
├── dataset_v2/            The final 100-case dataset (see Dataset section above).
├── legacy/                Earlier case sets and scripts; not used.
├── demo/libplist/         (see End-to-end demo section above)
├── docs/AP_PROBE_EXAMPLE.md  Real I/O for both AP probes from a live run.
├── README.md              This file.
├── pyproject.toml         Editable install metadata.
├── .env.example           Template (ANTHROPIC_API_KEY / GEMINI_API_KEY / …).
└── .gitignore
```

---

## Per-case output schema

A live RQ3 / single-case run writes:

```
<output-dir>/<case>/
├── case.json            The dataset entry the run consumed.
├── prompt.txt           The functionality prompt fed to the LG agent.
├── all_lgs.json         Every LG the agent considered.
├── lg_1.json            Selected LG.
├── lg_1/
│   └── p2_report.md     Stage-2 API-protocol report.
├── run-<id>/<case>/
│   ├── result.json      Per-LG SystemCheckResult (build_ok, run_edges,
│   │                    P1–P4 flags, full coverage).
│   ├── trajectory.jsonl Complete agent trace (every tool call + LLM response).
│   └── <fuzzer>.{c|cc}  The QuartetFuzz-generated harness.
├── coverage.json        Aggregated headline (line / branch / function pct,
│                        cost_usd, wall_seconds).
└── fallback.txt         Present only if the live run produced no coverage and
                          was filled from legacy precomputed data (v1 only).
```

---

## Acknowledgements

This is an anonymous submission.  The artifact ships with no author
or institution metadata; once accepted, the authoritative repository
will be linked from the camera-ready paper.

<!-- last-updated: 2026-04-29T21:38:35Z -->
