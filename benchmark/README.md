# Benchmark: 100 harness-generation tasks

Each task asks a system to write a libFuzzer harness for one public function
of an OSS-Fuzz C/C++ project.  The task is taken from an existing OSS-Fuzz
harness (the *reference*, or gold, harness): its target function, its fuzzer
slot in the project's build, and its official corpus.  The reference harness
is hidden from every system during generation.

## Layout

```
benchmark/
├── cases.tsv                 task, case id, target function, LG version
├── subset_25.txt             the 25-task subset (see the top-level README)
├── benchmark_100.jsonl       the 100 cases with selection evidence
│                             (public-API proof, prompt, pinned revisions)
├── gold_source_paths.json    case -> location of the reference harness in the build
├── gold_coverage.json        reference coverage summary per case (corpus replay)
├── GOLD_COVERAGE.md          how the reference coverage was measured
├── corpus_manifest.json      official OSS-Fuzz corpus per case (URL, SHA-256)
├── image_digests.json        base images used for the measurements
├── SELECTION.md              sampling frame, exclusions, admission gates
├── tools/                    fuzzing-trial and coverage scripts (below)
├── callgraphs/               call graph per project (SQLite, xz) + index.json
└── <project>-cN/             one task
    ├── case.json             target function, declaration, location, prompt,
    │                         reference harness path, pinned revisions
    ├── lg.json               Logic Group used by the full QuartetFuzz
    ├── p2_report.md          P2 report built on that Logic Group
    ├── image/                Docker build context of the task image
    ├── qf-project/           OSS-Fuzz build project used by QuartetFuzz
    ├── fixup.sh              (libidn2 only) version-file fix applied before builds
    └── gold/                 the reference harness's measurement
        ├── run_asan.sh       replay the official corpus under ASan
        ├── run_cov.sh        replay it on the coverage build
        ├── global_corpus/manifest.json   corpus URL, seed count, digest
        ├── report/coverage.json          scoped coverage of the replay
        └── reference_covsets.json.gz     covered lines / branches of the replay
```

## Task image

`<task>/image/` builds the image every system works in:

1. the upstream OSS-Fuzz project (OSS-Fuzz commit 955c686);
2. every source repository checked out at the revision recorded in
   `case.json` (`version_pins`, from OSS-Fuzz's published `srcmap`);
3. the reference harness replaced by a stub that keeps the target function
   linked (`qf/stub.sh`), including byte-identical copies elsewhere in `/src`;
4. the reference harness removed from git history (`qf_scrub_git.sh`).

`docker build -t qf-case/<task>:20261017 benchmark/<task>/image` builds it;
`scripts/setup_case.py` does this for you.

## Reference coverage

Reference coverage (paper, Reference Coverage section) measures how much of
the code the reference harness exercises a generated harness also exercises.

- **Reference set.** The reference harness replays its entire official
  OSS-Fuzz corpus at the pinned revision on its coverage build; the covered
  project-source lines and branches (harness code excluded) form the set
  `R`, stored in `<task>/gold/reference_covsets.json.gz`.  `run_cov.sh`
  reproduces the replay; `report/coverage.json` and `gold_coverage.json`
  hold the summary numbers; `GOLD_COVERAGE.md` documents the procedure.
- **Run set.** The final corpus of a fuzzing run is replayed on the coverage
  build of that run's harness (`tools/cov_sets.sh`), giving `G`.
- **Metric.** `RefCov = |G ∩ R| / |R| × 100` for lines and for branches:

  ```bash
  python3 benchmark/tools/refcov.py zlib-c1 <run>/covsets.json.gz
  ```

## Tools

| Script | Purpose |
|---|---|
| `tools/fuzz_trial.sh` | one fuzzing trial: empty corpus, wall-clock budget, libFuzzer with an outer restart loop (ClusterFuzz behaviour), corpus snapshots at 1, 2, 4, 8, 12, 24 h |
| `tools/fuzz_batch.py` | many trials, one physical core each |
| `tools/cov_sets.sh` | covered-entity sets of a corpus on a coverage build (input to `refcov.py`) |
| `tools/snap_cov.sh`, `snap_cov_batch.py` | llvm-cov summary of corpus snapshots |
| `tools/refcov.py`, `recall.py` | reference coverage of one run / of a campaign |
| `tools/asan_overrides.json` | per-case ASan options used in every run |
| `tools/mem_guard.sh`, `mem_watchdog.sh` | refuse or stop container work when memory runs low |

The trial scripts take a *case directory* holding `asan_build/` and
`cov_build/` (the `out/` directories of the ASan and coverage builds), e.g.
`work/final/<run-id>/<task>/final_asan/out` and `final_cov/out` from
`reproduce.sh`.
