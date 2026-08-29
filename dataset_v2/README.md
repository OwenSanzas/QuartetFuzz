# dataset_v2 — 100 OSS-Fuzz gold harnesses

A fixed set of 100 human-written fuzz harnesses from OSS-Fuzz, each paired with the
evidence that it runs: a complete replay of its official global corpus under
AddressSanitizer, and line coverage measured over that same corpus.

`benchmark_100.jsonl` is the authoritative list — one JSON record per case.

## Layout

    benchmark_100.jsonl              the 100 cases
    COVERAGE_REPORT.md               coverage per case, and how it was measured
    CRASH_REPORT.md                  replay results per case, and how they were collected
    projects/<project>/<fuzzer>/
        run_asan.sh                  replay the corpus under ASan (two phases)
        run_cov.sh                   replay it against the coverage build
        report/coverage.json         scoped coverage for this case
        report/replay.json           replay result for this case
        global_corpus/manifest.json  corpus URL, seed count, digest
    tools/validate.py                re-check every case against its recorded evidence

Binaries and corpora are not committed — 29 GB. Both scripts rebuild the
measurements from the official OSS-Fuzz runner image, and `global_corpus/manifest.json`
records the exact corpus URL and its SHA-256 so the same seeds can be fetched.

## Reproducing a case

    cd projects/zlib/compress_fuzzer
    ./run_asan.sh
    ./run_cov.sh

Both go through `gcr.io/oss-fuzz-base/base-runner:ubuntu-24-04`. Coverage follows
OSS-Fuzz's own procedure (`-merge=1`, `llvm-profdata`, `llvm-cov`) and is scoped to
the project's own sources, so statically linked dependencies do not inflate the
denominator. Replay is deterministic: the same corpus against the same binary gives
the same execution count every time.

## Verifying the set

    python3 tools/validate.py

## What the numbers say

All 100 cases replay their entire official corpus at exit 0 with zero distinct
faults, and all 100 yield non-zero coverage within their project's own sources.
617,000+ seeds in total.
