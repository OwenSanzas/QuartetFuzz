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
│    ├ code_view.py        read_file / list_directory / search   │
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

- **① Reproduce RQ3** — drive the full pipeline on 25 cases.
- **② Datasets** — what we ship and why.
- **③ Reproduce RQ2** — LG-stage entry-selection match, no fuzzing.
- **④ End-to-end demo** — `demo/libplist/`, with all LLM
  trajectories preserved so the agent's reasoning is replayable.

---

## ① Reproduce **RQ3** — generated-harness coverage on the 25-case subset

The flagship reproduction.  Drives the full pipeline (LG → P2 →
harness gen → AP probes → libFuzzer → llvm-cov) on
[`subset_25/subset_25.jsonl`](subset_25/subset_25.jsonl) and writes
per-case `coverage.json` you can compare against the gold baseline
in [`dataset/gold_baseline_100.jsonl`](dataset/gold_baseline_100.jsonl).

```bash
# 0. Setup (one-time)
python3 -m venv .venv && source .venv/bin/activate
pip install -e .
cp .env.example .env
echo "ANTHROPIC_API_KEY=sk-ant-..." >> .env
docker info > /dev/null     # OSS-Fuzz builds run inside Docker

# 1. Run the 25-case subset.
#    Claude Opus 4.6 (LG + P2) + Sonnet 4.6 (harness): ~$120, ~5h, 4 workers.
#    Gemini Flash (cheaper):                            ~$10,  ~45m, 4 workers.
DATASET=subset_25/subset_25.jsonl \
    ./scripts/reproduce_100_cases.sh \
        --cases 25 \
        --out /tmp/qf-rerun \
        --parallel 4
```

Per-case outputs land in `/tmp/qf-rerun/cases/<case>/coverage.json`;
side-by-side reference data is at `subset_25/precomputed/<case>/`.
When a live case produces no coverage (build error, LLM output-token
budget exhausted, transient API failure, …) the runner copies the
precomputed reference into the case directory and writes a
`fallback.txt` marker.  Successful live runs are never overwritten.

**Single-case mode** (handy for debugging one target):

```bash
python3 scripts/reproduce_one_case.py \
    --case-id zlib/zlib_uncompress2_fuzzer \
    --dataset subset_25/subset_25.jsonl \
    --output-dir /tmp/qf-zlib \
    --num-lgs 1 --top-k 1 --top-k-entries 1
```

**Skip the LG-discovery stage** (use the precomputed gold LG and
exercise only P2 + harness gen — much faster):

```bash
DATASET=subset_25/subset_25.jsonl \
    ./scripts/reproduce_100_cases.sh \
        --cases 25 \
        --out /tmp/qf-rerun-skip-lg \
        --parallel 5 \
        --skip-lg
```

> **Cross-fork fairness note.** This 25-case reproduction is run
> identically across all three forks (QuartetFuzz, OSS-Fuzz-Gen,
> PromeFuzz).  Occasionally a case will fail on one fork for reasons
> that have nothing to do with the system under test (build-wrapper
> quirk, transient API quota, …).  For a fair head-to-head we
> recommend, in those cases only, manually extracting the candidate
> harness containing the gold `target_function` from the fork's
> output directory and rebuilding it directly against the project's
> gold OSS-Fuzz Dockerfile.  The candidate harness is the same
> C/C++ source regardless of how it was driven; counting it as a 0
> because the fork's wrapper hit infra noise would understate that
> fork's harness quality.  This is the procedure the paper itself
> follows when reporting RQ3 numbers.

---

## ② Datasets shipped with the artifact

Reproduction depends on four pieces of curated data, all in this
repo:

### `dataset/benchmark_cases_with_prompts.jsonl` — **100-case gold-standard dataset**

The full RQ2 / RQ3 / RQ4 dataset.  One JSON object per line:

| Field | Purpose |
|---|---|
| `case_id` | `<project>/<gold_fuzzer_name>` — swap key. |
| `project`, `fuzzer_name`, `target_function` | OSS-Fuzz target. |
| `repo_url`, `source_file` | Gold harness coordinates. |
| `prompt` | Functionality prompt fed to the LG agent.  **60 cases** name the target function (RQ2 *w/ target* split); **40 cases** describe the feature without naming it (*w/o target*). |

The prompt style is the only difference between the two RQ2 splits;
the underlying targets and gold harnesses are the same.  RQ3 and RQ4
use the same 100 cases.

### `dataset/gold_baseline_100.jsonl` — **gold harness reference coverage**

The gold (human-written) harness's coverage on every case under the
paper's Table 9 protocol (10 × 600 s libFuzzer, empty corpus, ASan;
per-case median).  This is the "G" column underlying paper Table 9
and the `Gold` curve in Figure 8 — use it to compute Δ-vs-gold for
any reproduction.

### `dataset/gold_source_paths.json` — **swap-and-rebuild map**

Maps `<project>/<gold_fuzzer_name>` → the gold harness path inside
OSS-Fuzz (`$SRC/...`).  `swap_gold_source` writes the LLM's output
into this exact path before invoking `build.sh`, ensuring the
project's own glob/link line picks the harness up.

### `dataset/static_analysis/` — **precomputed call graphs** (14 projects)

Joern call graphs for every project in the 25-case subset:

```
dataset/static_analysis/<project>/
├── functions.json    Function records (name, file, lines, parameters).
├── edges.json        Caller → callee edges over the project's source.
└── fuzzers.json      Existing OSS-Fuzz fuzzer entry-points.
```

The pipeline auto-loads through `StaticIndex.load(project)`; override
with `AGF_STATIC_ANALYSIS_DIR=/path/to/static_analysis`.  Missing data
does **not** abort: the pipeline degrades to **SAF mode**
(Static-Analysis Fallback) where the LG agent works from source-only
LLM judgment and `static_analysis` tools return `unknown` instead of
failing.  Same fallback as paper §5 for non-C/C++ targets.

### `subset_25/` — **25-case reproduction package**

```
subset_25/
├── subset_25.jsonl     The 25 cases (identical across forks).
├── precomputed/        Per-case live reference outputs (coverage.json,
│                       harness, lg_*.json, prompt, p2_report).  Used
│                       both as fallback when a live run fails AND as
│                       gold LGs when the runner is invoked --skip-lg.
└── README.md           Schema, provenance, fallback contract.
```

Total dataset footprint: ~9 MB.

---

## ③ Reproduce **RQ2** — LG-stage entry-selection match

```bash
# 25-case subset (same one as RQ3):
python3 scripts/reproduce_rq2.py \
    --dataset    subset_25/subset_25.jsonl \
    --output-dir /tmp/qf-rq2 \
    --parallel   4

# Or the full 100-case dataset (paper Table 6, ~91/100 with Opus 4.6):
python3 scripts/reproduce_rq2.py \
    --dataset    dataset/benchmark_cases_with_prompts.jsonl \
    --output-dir /tmp/qf-rq2-full \
    --parallel   4
```

The script runs Stage 1 (LG agent) once per case and records whether
the gold `target_function` appears in the agent's selected entry
set — no harness generation, no fuzzing.  Per-case outputs:

```
/tmp/qf-rq2/<case_id_safe>/
├── rq2_match.json      {gold_target, lg_entries, direct_match,
│                        wrapper_match, overall_match}
├── lg_1.json           Selected Logic Group.
└── all_lgs.json        Every LG the agent considered.
```

Aggregate match rates print at the end, split by prompt style:

```
w/ target:  X/<n>  match
w/o target: X/<m>  match
All:        X/N    match
```

Wallclock: ~10 min for the 25-subset on 4 workers (Gemini Flash);
~30 min for full 100.  `--limit 5` for a smoke run.

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
| `AGF_STATIC_ANALYSIS_DIR` | Override static-analysis location. | repo's `dataset/static_analysis/` |
| `AGF_RUNS_ROOT` | Per-run output root. | `/tmp/agf-$USER` |
| `DATASET` | Override the dataset jsonl in `reproduce_100_cases.sh`. | `dataset/benchmark_cases_with_prompts.jsonl` |

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
| **RQ3** | Generated-harness coverage vs. gold + LLM baselines | Full pipeline + 600 s libFuzzer + llvm-cov | `subset_25/subset_25.jsonl` (25) | ≈45 min on 4 workers (Flash) / ≈5 h (Claude paper config) |
| **RQ2** | LG-stage entry-selection match | LG agent only, no fuzz | `subset_25` (25) or full 100 | ≈10 min (subset) / ≈30 min (full) on 4 workers |
| **RQ1** | Production-harness audit (paper §5.2) | Stage 4 only on existing OSS-Fuzz harnesses | external | (long) |
| **RQ5** | Real-world deployment (paper §5.6) | Full pipeline on outside projects | project-dependent | (long) |

RQ1 and RQ5 are out of scope for this 25-case artifact and reproduce
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
│           ├── code_view.py        read_file / list_directory / search_files /
│           │                       list_existing_fuzzers
│           ├── static_analysis.py  get_callers / get_callees / find_definition /
│           │                       forward_reach / reverse_reach /
│           │                       public_entries_for / public_entries_for_batch /
│           │                       is_public_api
│           └── dynamic_analysis.py build_harness / AP_Run_check / AP_Reach_check /
│                                   get_coverage / run_gdb
├── infra/ossfuzz/         OSS-Fuzz integration (build / runner / workspace utils).
├── scripts/
│   ├── reproduce_100_cases.sh    Parallel batch driver (RQ3).
│   ├── reproduce_one_case.py     Per-case driver — single entry point.
│   ├── reproduce_rq2.py          RQ2 LG-stage match driver.
│   └── demo_libplist.py          End-to-end libplist demo driver.
├── dataset/               (see Datasets section above)
├── subset_25/             (see Datasets section above)
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
                          was filled from subset_25/precomputed/.
```

---

## Acknowledgements

This is an anonymous submission.  The artifact ships with no author
or institution metadata; once accepted, the authoritative repository
will be linked from the camera-ready paper.
