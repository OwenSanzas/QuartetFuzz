#!/usr/bin/env python3
"""Cross-check every benchmark case against the invariants the artifact claims."""
import json, os, sys, glob
D = "/home/ze/agf-public/QuartetFuzz/dataset_v2"
REQUIRED = ["run_asan.sh", "run_cov.sh", "report/coverage.json",
            "report/replay.json", "global_corpus/manifest.json"]
bad, rows = [], []
for line in open(f"{D}/benchmark_100.jsonl"):
    c = json.loads(line); cid = f'{c["project"]}/{c["fuzzer_name"]}'
    base = f'{D}/projects/{cid}'; probs = []
    for r in REQUIRED:
        if not os.path.exists(f"{base}/{r}"): probs.append(f"missing {r}")
    cov = {}
    try:
        cov = json.load(open(f"{base}/report/coverage.json"))
        if not cov.get("files_in_scope"): probs.append("no_coverage")
        elif cov["lines"]["covered"] == 0: probs.append("zero_lines")
    except Exception as e: probs.append(f"coverage unreadable: {e}")
    rp = {}
    try:
        rp = json.load(open(f"{base}/report/replay.json"))
        if rp.get("phase1_full_replay", {}).get("exit_code") != 0: probs.append("phase1 exit!=0")
        if not rp.get("phase1_full_replay", {}).get("complete"): probs.append("phase1 incomplete")
        if rp.get("phase2_poc_collection", {}).get("distinct_faults", 0): probs.append(
            f'phase2 faults={rp["phase2_poc_collection"]["distinct_faults"]}')
        seeds, ex = rp.get("seeds_in_corpus", 0), rp.get("phase1_full_replay", {}).get("executed_units", 0)
        if seeds and not (0.9 <= ex / seeds <= 1.6): probs.append(f"exec/seeds={ex/seeds:.2f}")
    except Exception as e: probs.append(f"replay unreadable: {e}")
    for d in ("asan_build", "cov_build"):
        junk = [p for p in glob.glob(f"{base}/{d}/*") if p.endswith("_out")]
        if junk: probs.append(f"{d} junk x{len(junk)}")
    rows.append((cid, cov.get("lines", {}).get("percent"), rp.get("seeds_in_corpus"), probs))
    if probs: bad.append((cid, probs))
print(f"=== {len(rows)-len(bad)}/{len(rows)} 通过")
for cid, p in bad: print(f"  {cid:45s} {'; '.join(p)}")
ok = [r for r in rows if not r[3]]
if ok:
    pct = sorted(r[1] for r in ok if r[1] is not None)
    print(f"\n覆盖率(scoped lines %): min {pct[0]:.2f}  中位 {pct[len(pct)//2]:.2f}  max {pct[-1]:.2f}")
    print(f"种子总数: {sum(r[2] or 0 for r in rows):,}")
sys.exit(1 if bad else 0)
