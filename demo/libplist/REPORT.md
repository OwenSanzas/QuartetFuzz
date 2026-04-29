# libplist End-to-End Demo

A complete walk through of the QuartetFuzz pipeline on `libplist`,
from project exploration to harness generation.  Every artifact in
this directory was produced by the pipeline; the LLM trajectories
under `run-*/` are kept verbatim so the agent's full reasoning can
be replayed.

## Stage 1 — Logic Group Discovery

The LG agent (Opus 4.6, max 50 turns per round, `num_lgs=10`,
`top_k=5`) explored the libplist source tree and proposed candidate
Logic Groups across 10 independent rounds.  Nine LGs were submitted;
they are ranked by max danger score (paper §3.5, Eq. 5):

| Rank | LG name | Top entry | Max danger |
|------|---------|-----------|-----------|
| 1 | libplist cross-format serialization | `plist_to_bin` | 14.8 |
| 2 | binary plist parse-serialize round-trip | `plist_to_bin` | 14.8 |
| 3 | libplist post-parse tree manipulation | `plist_from_memory` | 7.7 |
| 4 | libplist auto-detect format dispatcher | `plist_from_memory` | 7.7 |
| 5 | libplist pretty-print output formatters | `plist_from_memory` | 7.7 |
| 6 | libplist per-format parsers | `plist_from_openstep` | 5.6 |
| 7 | OpenStep plist parse-then-serialize round-trip | `plist_from_openstep` | 5.6 |
| 8 | JSON plist parse-then-serialize round-trip | `plist_from_json` | 3.7 |
| 9 | XML plist parse-then-serialize round-trip | `plist_from_xml` | 3.5 |

The full set is in [`all_lgs.json`](all_lgs.json); the top-5 selected
LGs are in [`lg_1.json`](lg_1.json) through [`lg_5.json`](lg_5.json).

## Stage 2+3 — P2 Research and Harness Generation

Each of the top-5 LGs was processed through the P2 agent (Opus 4.6)
and the SystemCheck harness generator (Sonnet 4.6).  The driver is
[`scripts/demo_libplist.py`](../../scripts/demo_libplist.py).

libplist's OSS-Fuzz project ships exactly four fuzzer slots that
`build.sh` knows how to compile (`bplist`, `jplist`, `oplist`,
`xplist`); the driver therefore maps each LG to one of those slots so
the agent's source actually overwrites a slot known to the build
script and a real binary is produced.  LG 5 reuses the `bplist` slot
in a second batch (the agent's source is distinct, only the
compilation slot is shared with LG 1).

Per-LG layout:

```
lg_<N>/
  lg_analysis.json     ← the LG (copied from lg_<N>.json)
  p2_report.md         ← P2 agent's API protocol report
  fuzzer.cc            ← the harness source the agent submitted
run-<id>/libplist_<slot>/
  trajectory.jsonl     ← every LLM turn for P2 + SystemCheck
  result.json          ← flow-level summary
  binaries/            ← address-sanitised binary snapshot
  corpus/              ← libFuzzer corpus produced during get_coverage
  coverage/            ← llvm-cov reports
```

Note: the `get_coverage` fuzz budget for this demo was lowered to
**10 seconds** (vs. the 600-second RQ3 protocol) so the demo
completes in under an hour.  Coverage numbers below should be read
as a sanity check that the harness compiles, runs, and exercises the
library — not as a benchmarking result.

### Per-LG outcome

| LG | slot | turns | cost | line cov | branch cov | success |
|----|------|------:|-----:|---------:|-----------:|---------|
| lg_1 (cross-format serialization) | bplist | 28 | $1.86 | 42.41% | 36.07% | ✓ |
| lg_2 (binary parse-serialize round-trip) | jplist | 50 | $2.91 | 1.24% | 0.74% | partial |
| lg_3 (post-parse tree manipulation) | oplist | 29 | $1.73 | 24.07% | 22.06% | ✓ |
| lg_4 (auto-detect format dispatcher) | xplist | 24 | $1.30 | 21.21% | 19.95% | ✓ |
| lg_5 (pretty-print output formatters) | bplist (reused) | 34 | $2.01 | 21.72% | 18.11% | ✓ |

`lg_2` exhausted its 50-turn budget without calling `submit_harness`;
the partial harness it produced still compiled (`build_ok=True`) and
touched a small fraction of the library, so we keep the artifact for
inspection.

Total LLM cost across all 5 LGs (excluding the LG-discovery stage):
**~$9.81**.  Wall-clock was ~10 minutes for batch 1 (4 LGs in
parallel) + ~5 minutes for batch 2 (lg_5 alone).

## Reproducing

```
export ANTHROPIC_API_KEY=...
# Skip Stage 1 by reusing the lg_*.json shipped here:
python scripts/demo_libplist.py --batch all
```

The driver loads `demo/libplist/lg_<N>.json`, runs P2 + SystemCheck
for each, and writes back into `demo/libplist/lg_<N>/`.

To regenerate Stage 1 from scratch (LG discovery), use the lower-level
pipeline CLI:

```
python -m harness.checker.pipeline --mode lg \
    --project libplist \
    --prompt "Identify 5 high-value Logic Groups in libplist for fuzzing..." \
    --output-dir demo/libplist \
    --num-lgs 10 --top-k 5
```
