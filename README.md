# QuartetFuzz

Artifact of *QuartetFuzz: Making LLM-Generated Fuzz Harnesses Reliable with
Four Principles*, IEEE Symposium on Security and Privacy (S&P) 2027.

QuartetFuzz is an LLM-agent system that generates fuzz harnesses
(`LLVMFuzzerTestOneInput` programs for C/C++ libraries) under four
source-level principles, and validates them by adversarial probing before
delivery.  This repository holds the system, the 100-task benchmark used in
the paper, and a one-command reproduction script.

## Branches

Each configuration evaluated in the paper is one branch.  The ablation
branches differ from `main` only in the component they remove
(`git diff main woAP` shows exactly that).

| Branch | Configuration | What is removed |
|---|---|---|
| `main` | **QuartetFuzz (full)** | — |
| `woLG` | w/o LG | the Logic Group stage; generation starts from the project and the target function |
| `woAP` | w/o AP | the two adversarial probes (`AP_Run_check`, `AP_Reach_check`) and their submission requirements |
| `woP2` | w/o P2 report | the API protocol (P2) report |
| `woStatic` | w/o Static | the static call-graph query tools and the Logic Group stage, as in the run reported in the paper |

## Running the experiments

Requirements: Linux x86_64, Docker, git, Python ≥ 3.10, an Anthropic API
key, and about 16 GB of RAM per concurrently running task.

### 1. Generate harnesses (one command)

```bash
git checkout main                 # or woLG / woAP / woP2 / woStatic
export ANTHROPIC_API_KEY=...
./reproduce.sh subset25           # the 25-task subset, recommended first
./reproduce.sh zlib-c1            # one task
JOBS=3 ./reproduce.sh all         # all 100 tasks, three at a time
```

For each task, `reproduce.sh`:

1. builds the task's Docker image from `benchmark/<task>/image/`
   (upstream OSS-Fuzz project, every source repository pinned to the
   revision used in the paper, the reference harness replaced by a stub and
   removed from git history) and a copy with gdb for the probes;
2. prepares the inputs the agent sees (`scripts/setup_case.py`): the
   exported source tree, the OSS-Fuzz project files, the call graph, and the
   released Logic Group and P2 report;
3. runs harness generation (`scripts/run_qf_case.py`);
4. builds the delivered harness with ASan and with coverage instrumentation
   (`scripts/build_final_qf.sh`).

Results go to `work/runs/<run-id>/` (harness, `result.json`, full agent
trajectory) and `work/final/<run-id>/<task>/` (`final_src`, `final_asan`,
`final_cov`).  `./reproduce.sh --setup-only all` builds the images and inputs
without calling the LLM.

By default the full system reuses the Logic Group and P2 report released in
`benchmark/<task>/` (the ones behind the paper's results); `FRESH_LG=1`
makes it generate new ones.  LLM output is not deterministic, so a rerun
gives a different harness.

**The 25-task subset** (`benchmark/subset_25.txt`) has one task from each of
25 projects, the ones with the shortest generation in our runs.  It exercises
every stage of the pipeline at a fraction of the cost of the full benchmark.

### 2. Fuzz the delivered harnesses

Each delivered harness is fuzzed in its task image with libFuzzer, from an
empty corpus, for 24 h per trial (paper: HPC cluster, one core per trial):

```bash
python3 scripts/make_trial_dir.py zlib-c1 work/final/<run-id>/zlib-c1 trials/qf/zlib-c1
bash benchmark/tools/fuzz_trial.sh trials/qf/zlib-c1/compress_fuzzer \
     trials/qf/zlib-c1/trial_01 24 <cpu> <seed> "$(jq -r '."zlib/compress_fuzzer" // ""' benchmark/tools/asan_overrides.json)"
```

`fuzz_trial.sh` restarts libFuzzer after a crash, OOM or timeout (as
ClusterFuzz does), keeps every artifact, and snapshots the corpus at 1, 2, 4,
8, 12 and 24 h.  `benchmark/tools/fuzz_batch.py` runs many trials in parallel.

### 3. Measure reference coverage

```bash
bash benchmark/tools/cov_sets.sh trials/qf/zlib-c1/compress_fuzzer \
     trials/qf/zlib-c1/trial_01/corpus trials/qf/zlib-c1/trial_01/sets
python3 benchmark/tools/refcov.py zlib-c1 trials/qf/zlib-c1/trial_01/sets/covsets.json.gz
```

The final corpus is replayed on the harness's coverage build; reference
line / branch coverage is the share of the lines / branches covered by the
reference harness's corpus replay (`benchmark/<task>/gold/`) that the run
also covers.  The paper averages trials within a task, then averages over
the 100 tasks with undelivered tasks counted as zero.

### 4. Baselines

OSS-Fuzz-Gen and PromeFuzz as we ran them, each an upstream commit plus our
changes for this benchmark, are in their own repositories; their READMEs list
the changes and the scripts used.  Their delivered harnesses go through steps
2 and 3 unchanged.

| Baseline | Repository | Upstream base |
|---|---|---|
| OSS-Fuzz-Gen | https://github.com/OwenSanzas/oss-fuzz-gen | google/oss-fuzz-gen `c0982c5d40a7` |
| PromeFuzz | https://github.com/OwenSanzas/PromeFuzz | pvz122/PromeFuzz `655d2d98fc30` |

## Benchmark

`benchmark/` is self-contained: one directory per task with the target
function (`case.json`), the Logic Group and P2 report, the task image, and
the reference harness's corpus-replay coverage used as the denominator of
reference coverage.  See [`benchmark/README.md`](benchmark/README.md).

## Generated harnesses from the paper

`results/harnesses/` holds the source of the harnesses evaluated in the
paper, one directory per system and task (`index.tsv` lists them).  Only source code is included —
no fuzzing output, crash inputs or reports — and comments are stripped from
the harness sources.  OSS-Fuzz-Gen harnesses are named after the reference
harness they replace and come with the build script when OSS-Fuzz-Gen
changed it; PromeFuzz harnesses come with PromeFuzz's build script.

## Pipeline

1. **Logic Group** (`harness/checker/lg_agent.py`): around the given target
   function, the LG agent reads the project and describes the functional unit
   to test: entry functions, core functions, and the behaviors to exercise.
2. **API protocol research** (`harness/checker/p2_agent.py`): the P2 agent
   writes a report of the API's requirements (initialization order,
   parameter construction, object lifecycle, return-value handling, cleanup),
   each with source evidence.
3. **Generation** (`harness/checker/flow.py`): the generation agent writes
   the harness and builds it in the task's OSS-Fuzz image (at most 5 builds).
4. **Adversarial probing** (same agent): `AP_Run_check` runs an
   agent-crafted input on the ASan build; `AP_Reach_check` runs one under gdb
   and checks that the target function is reached.  Submission requires both
   on the last successful build, plus a 600 s libFuzzer run with coverage
   (`get_coverage`).

All agents use `claude-sonnet-5` (`harness/checker/tools/context.py`).
Call-graph tools (`get_callers`, `get_callees`, `find_path`) are backed by
`benchmark/callgraphs/` (built with `scripts/callgraph/`).

## The four principles

| | What it asks of the harness |
|---|---|
| **P1** Logic correctness | No defects in the harness itself: input handling, memory, ownership, cleanup. |
| **P2** API protocol compliance | Parameters, call order and object lifecycles follow the library's documented requirements. |
| **P3** Security boundary respect | The harness does not bypass the security boundary the test assumes, e.g. by building states the public interface does not allow. |
| **P4** Entry point adequacy | The harness structure and input construction let the intended functionality execute. |

See `harness/checker/HARNESS_CHECKING_MANUAL.md` for the per-check details.

## Layout

```
agent/            agent framework (turn loop, LLM client, context handling)
harness/          the system: LG, P2 and generation agents, tools, builder
infra/ossfuzz/    OSS-Fuzz integration
scripts/          reproduce helpers, final builds, trial layout, call graphs
benchmark/        the 100 tasks, reference coverage, fuzzing/coverage tools
results/          the generated harnesses evaluated in the paper
reproduce.sh      one-command generation
```
