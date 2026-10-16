# v1 reproduction instructions (legacy — do not use)

Moved out of the top-level README. These sections describe the RQ2/RQ3 runs on the
earlier case set (`dataset_v1`, `subset_25_v1`) and the scripts now in `scripts_v1/`;
paths below refer to the old locations (`dataset/`, `subset_25/`, `scripts/`).
The final dataset is [`../dataset_v2/`](../dataset_v2/).

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

