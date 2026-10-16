# Precomputed QuartetFuzz Outputs (Reference)

This directory contains live QuartetFuzz outputs on the 25-case subset
(`benchmark/oss_fuzz_harness/data/subset_25.jsonl` or the copy in the
sibling forks). They serve as a **fallback reference** for reviewers
whose live run cannot complete end-to-end.

## What is included per case

```
precomputed_outputs/<project>__<fuzzer>/
├── coverage.json          Aggregated headline coverage (line_pct/branch_pct/function_pct/cost_usd/wall_seconds)
├── result.json            Per-LG SystemCheckResult (build_ok, run_edges, P1/P2/P3/P4 flags, full coverage)
├── case.json              The dataset entry the live run consumed
├── prompt.txt             The natural-language prompt fed to the LG agent
├── lg_1.json              The Logic Group the agent selected
├── all_lgs.json           All LG candidates considered
├── p2_report.md           The Stage-2 API-protocol report
├── <fuzzer>.{c|cpp}       The QuartetFuzz-generated harness (LLM output)
└── source.txt             "live_gemini_flash" or "no_live_run_added_post_hoc"
```

## Provenance

24 of the 25 cases ship live outputs from a Gemini-3-Flash-Preview run
on the artifact server (`--num-lgs 1 --top-k 1 --top-k-entries 1`,
30 s libFuzzer fuzz, ASan, OSS-Fuzz pinned). Total cost on Gemini Flash
was ~\$8.99 across the 25 cases.

The 25th case (`openssh/privkey_fuzz`) was added to `subset_25.jsonl`
*after* the live run completed (replacing `libical/libical_fuzzer`,
which failed at the C++ template-compile step under Gemini Flash). It
ships only `paper_reference.json` with the headline numbers from paper
Table 9 (Q=6.3 line, Q=4.9 branch); a fresh run is required to populate
the directory.

## When the fallback is used

`scripts/reproduce_one_case.py` (and the `reproduce_100_cases.sh`
wrapper) write each live run's `coverage.json` and `result.json` to the
output directory. To compare against reference numbers, simply diff
the live `coverage.json` against the corresponding
`precomputed_outputs/<key>/coverage.json`. Successful live runs are
**never** overwritten.

## Notes for reviewers

The Gemini Flash numbers in `coverage.json` are intentionally lower
than the Sonnet 4.6 numbers reported in the paper (Table 9), for three
reasons that compound: smaller model, single-trial (`--num-samples 1`
equivalent), and 30-second fuzz versus the paper's 600-second runs.
Three of the 25 cases (`iperf/cjson_fuzzer`, `libical/libical_fuzzer`,
`libpcap/fuzz_both`) report `line_pct: 0.0` because Gemini Flash
either failed to compile a valid harness or hit its output token
budget before emitting one; the `result.json` and live log capture the
exact failure mode for each.
