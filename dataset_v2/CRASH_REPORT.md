# Crash / PoC report — dataset_v2

Generated 2026-08-29.

## Method

Every case replays its full global corpus under ASan in two phases.

**Phase 1 — completeness.** `-runs=0 -detect_leaks=0` replays every seed exactly
once with no mutation. Leak detection is off here because a leaking seed aborts
the process, and there would then be no complete replay at all: quickjs/fuzz_regexp
leaks on so many seeds that 40 successive passes never reached the end of its
7,173-seed corpus.

**Phase 2 — PoC collection.** Leak detection back on, replaying iteratively: each
artifact is kept, the seed that produced it is removed from a scratch copy, and the
replay resumes (up to 40 rounds). Artifacts are grouped by libFuzzer's `DEDUP_TOKEN`,
so what is reported is the number of **distinct faults**, not the number of inputs
that trigger them — quickjs once produced 41 artifacts for 4 distinct faults.

**Attribution.** Whose file appears in the crash or allocation stack decides
library bug vs harness defect. This is the executable form of P1.

## Result

| | |
|---|---|
| Cases | 100 |
| Seeds replayed | 625,891 |
| Units executed | 625,991 |
| Phase 1 complete, exit 0 | 100/100 |
| **Distinct faults** | **0** |
| Fault artifacts (crash/leak/oom/timeout) | 0 |
| `slow-unit-` artifacts (not faults) | 2 |

Every case in the benchmark replays its entire official corpus without a single
distinct fault. That is the property the benchmark is selected to have: these are
the gold harnesses, and a gold harness that crashes on its own project's corpus
would not be a usable reference point.

### `slow-unit-` artifacts

libFuzzer writes these when one input exceeds its time threshold. They record performance, not a fault, and are excluded from the fault count.

| Case | Artifact | Bytes |
|---|---|---:|
| `cmake/cmListFileLexerFuzzer` | `slow-unit-d9538a0d8c720c30fad096bbabd1a81d61ad10ed` | 65,536 |
| `boost/boost_regex_replace_fuzzer` | `slow-unit-a01e1bd3b6f8a345a75932932a92211e580cc481` | 692,866 |

## Scope

`benchmark_100.jsonl` is the authoritative list of cases. Any other directory under
`projects/` is not part of it.
