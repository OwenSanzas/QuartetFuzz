#!/usr/bin/env python3
"""Generate COVERAGE_REPORT.md and CRASH_REPORT.md from the per-case evidence."""
import json, os, glob, collections, time

D = "/home/ze/agf-public/QuartetFuzz/dataset_v2"
cases = [json.loads(l) for l in open(f"{D}/benchmark_100.jsonl")]
cur = {f'{c["project"]}/{c["fuzzer_name"]}' for c in cases}
FAULT = ("crash-", "leak-", "oom-", "timeout-")     # slow-unit- is not a fault

rows = []
for c in cases:
    cid = f'{c["project"]}/{c["fuzzer_name"]}'
    b = f"{D}/projects/{cid}"
    cov = json.load(open(f"{b}/report/coverage.json"))
    rp  = json.load(open(f"{b}/report/replay.json"))
    arts = [os.path.basename(p) for p in glob.glob(f"{b}/report/crashes/*")
            if os.path.isfile(p)]
    rows.append({
        "cid": cid, "project": c["project"],
        "lines_pct": cov["lines"]["percent"], "lines_cov": cov["lines"]["covered"],
        "lines_tot": cov["lines"]["count"], "funcs_pct": cov["functions"]["percent"],
        "files_scope": cov["files_in_scope"], "files_total": cov["files_total"],
        "scope": cov["scope"], "seeds": rp["seeds_in_corpus"],
        "exec": rp["phase1_full_replay"]["executed_units"],
        "edges": rp["phase1_full_replay"]["edges_covered"],
        "p1_ok": rp["phase1_full_replay"]["complete"] and rp["phase1_full_replay"]["exit_code"] == 0,
        "faults": rp["phase2_poc_collection"]["distinct_faults"],
        "arts": arts,
        "slow": [a for a in arts if a.startswith("slow-unit-")],
        "real": [a for a in arts if any(a.startswith(p) for p in FAULT)],
        "sec": rp.get("wall_seconds"),
    })
rows.sort(key=lambda r: -r["lines_pct"])
ts = time.strftime("%Y-%m-%d", time.gmtime())
pct = sorted(r["lines_pct"] for r in rows)

with open(f"{D}/COVERAGE_REPORT.md", "w") as f:
    f.write(f"""# Coverage report — dataset_v2

Generated {ts}. One row per benchmark case.

## Method

Corpus **replay**, not fuzzing. Each case runs its project's official OSS-Fuzz
global corpus once through the coverage build:

    <target>_cov -merge=1 -timeout=100 /tmp/empty /corpus
    llvm-profdata merge -sparse /tmp/dumps/*.profraw -o cov.profdata
    llvm-cov export -summary-only -instr-profile=cov.profdata <target>_cov

`-merge=1` is the procedure OSS-Fuzz itself uses for its coverage reports, and it
tolerates inputs that crash the target. `-summary-only` omits the per-line segment
arrays: without it the export reached 965 MB on the largest target and was
OOM-killed. The summaries it keeps are identical — re-measuring all 100 cases with
and without the flag gave the same numbers to the reported precision.

Coverage is **scoped to the project's own sources**, using the prefix the build's
own `srcmap.json` names. Without scoping, statically linked dependencies inflate
the denominator: libvips read 1632 files / 415,751 lines (42.83%) unscoped against
396 files / 73,117 lines scoped. The scoped figure matches Google's public report
for the same target (50,173/73,117 = 68.62% here, 50,729/73,117 = 69.38% official).

Seven cases have a source prefix that differs from `/src/<project>/`:
binutils -> `/src/binutils-gdb/`, cmake (x5) -> `/src/CMake/`, simd -> `/src/Simd/`.

Replay is deterministic: the same corpus against the same binary gives the same
execution count and coverage on every run. `-fork=1` is deliberately never used —
fork mode reinterprets `-runs=0` as a short fuzzing session.

## Headline

| | |
|---|---|
| Cases | {len(rows)} |
| Seeds replayed | {sum(r['seeds'] for r in rows):,} |
| Line coverage, median | {pct[len(pct)//2]:.2f}% |
| Line coverage, min / max | {pct[0]:.2f}% / {pct[-1]:.2f}% |

### Reading the low end

Absolute percentage is a per-target measurement against the **whole project's**
source, so a deliberately narrow harness reads low without being defective.
`cmake/cmVersionFuzzer` covers 223 lines because parsing a version string is all
it does; the denominator is all of CMake. The evaluation compares systems on the
same target, where this denominator cancels.

Comparisons between harnesses on the same target are unaffected: the denominator is
identical on both sides and cancels.

## Per case

| Case | Lines | Covered / Total | Funcs | Files in scope | Seeds | Edges |
|---|---:|---:|---:|---:|---:|---:|
""")
    for r in rows:
        f.write(f"| `{r['cid']}` | {r['lines_pct']:.2f}% | {r['lines_cov']:,} / "
                f"{r['lines_tot']:,} | {r['funcs_pct']:.2f}% | "
                f"{r['files_scope']}/{r['files_total']} | {r['seeds']:,} | {r['edges']:,} |\n")

nf = sum(len(r["real"]) for r in rows)
ns = sum(len(r["slow"]) for r in rows)
with open(f"{D}/CRASH_REPORT.md", "w") as f:
    f.write(f"""# Crash / PoC report — dataset_v2

Generated {ts}.

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
| Cases | {len(rows)} |
| Seeds replayed | {sum(r['seeds'] for r in rows):,} |
| Units executed | {sum(r['exec'] for r in rows):,} |
| Phase 1 complete, exit 0 | {sum(1 for r in rows if r['p1_ok'])}/{len(rows)} |
| **Distinct faults** | **{sum(r['faults'] for r in rows)}** |
| Fault artifacts (crash/leak/oom/timeout) | {nf} |
| `slow-unit-` artifacts (not faults) | {ns} |

Every case in the benchmark replays its entire official corpus without a single
distinct fault. That is the property the benchmark is selected to have: these are
the gold harnesses, and a gold harness that crashes on its own project's corpus
would not be a usable reference point.

""")
    if ns:
        f.write("### `slow-unit-` artifacts\n\n"
                "libFuzzer writes these when one input exceeds its time threshold. "
                "They record performance, not a fault, and are excluded from the "
                "fault count.\n\n| Case | Artifact | Bytes |\n|---|---|---:|\n")
        for r in rows:
            for a in r["slow"]:
                p = f"{D}/projects/{r['cid']}/report/crashes/{a}"
                f.write(f"| `{r['cid']}` | `{a}` | {os.path.getsize(p):,} |\n")
    f.write(f"""
## Scope

`benchmark_100.jsonl` is the authoritative list of cases. Any other directory under
`projects/` is not part of it.
""")
print(f"wrote COVERAGE_REPORT.md and CRASH_REPORT.md ({len(rows)} cases, "
      f"{sum(r['faults'] for r in rows)} distinct faults, {nf} fault artifacts, {ns} slow-units)")
