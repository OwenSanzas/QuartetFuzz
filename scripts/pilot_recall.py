#!/usr/bin/env python3
"""Gold-territory coverage of the pilot trials: what share of the code the
gold harness covers (its historical corpus, replayed with its own coverage
build) each system's trial also covers.  Code a system reaches outside the
gold's territory (a wider attack surface) earns nothing here; it is reported
separately as `extra`.

Covered sets come from the gold tool dataset_v2/tools/cov_sets.sh (lcov
export, scoped like the case).  The harness file itself (the case's gold
path) is excluded on every side: only the project's code counts.

    gold reference  cov_sets.sh <pilot>/gold/<p>/<fz>  <case>/global_corpus/seeds
    each trial      cov_sets.sh <pilot>/<s>/<p>/<fz>   the trial's <MIN>-minute snapshot

Per system and case: the median over trials of line and branch recall, and
of the extra lines.

usage: pilot_recall.py <pilot dir> [minutes=10] [parallel=3]
writes <pilot>/gold_ref/<p>/covsets.json.gz, <pilot>/trials/<s>/<p>/t<k>/sets/,
       <pilot>/recall.tsv
"""
import glob
import gzip
import json
import os
import statistics
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

PUB = "/home/ze/agf-public"
DS = f"{PUB}/QuartetFuzz/dataset_v2"
pilot = os.path.abspath(sys.argv[1])
minutes = int(sys.argv[2]) if len(sys.argv) > 2 else 10
par = int(sys.argv[3]) if len(sys.argv) > 3 else 3
cases = {c["oss_fuzz_project"]: dict(c, case_id=k)
         for k, c in json.load(open(f"{PUB}/qf-work/cases.json")).items()}
systems = sorted(os.path.basename(d) for d in glob.glob(f"{pilot}/*")
                 if os.path.isdir(d) and os.path.basename(d) not in ("trials", "gold_ref"))

jobs = []
for gold_dir in sorted(glob.glob(f"{pilot}/gold/*/*/")):
    p, fz = gold_dir.rstrip("/").split("/")[-2:]
    project, gold_fz = cases[p]["case_id"].split("/", 1)
    ref = f"{pilot}/gold_ref/{p}"
    if not os.path.exists(f"{ref}/covsets.json.gz"):
        jobs.append((gold_dir, f"{DS}/projects/{project}/{gold_fz}/global_corpus/seeds", ref))
    for s in systems:
        case_dir = f"{pilot}/{s}/{p}/{fz}"
        for trial in sorted(glob.glob(f"{pilot}/trials/{s}/{p}/t*")):
            snap = f"{trial}/snaps/snap_{minutes:05d}m"
            snap = snap if os.path.isdir(snap) else f"{trial}/corpus"
            if os.path.isdir(case_dir) and os.path.isdir(snap) and \
                    not os.path.exists(f"{trial}/sets/covsets.json.gz"):
                jobs.append((case_dir, snap, f"{trial}/sets"))


def run(j):
    r = subprocess.run([f"{DS}/tools/cov_sets.sh", *j], capture_output=True, text=True)
    return (r.stdout.strip().splitlines() or [f"FAIL {j[2]} {r.stderr.strip()[-200:]}"])[-1]


with ThreadPoolExecutor(par) as ex:
    for line in ex.map(run, jobs):
        print(line, flush=True)


def covered(path, harness):
    d = json.load(gzip.open(path))
    return {m: {k for k in d["covered"][m] if not k.startswith(harness + ":")}
            for m in ("lines", "branches")}


rows = []
for ref in sorted(glob.glob(f"{pilot}/gold_ref/*/covsets.json.gz")):
    p = ref.split("/")[-2]
    G = covered(ref, cases[p]["gold_path"])
    row = [p, str(len(G["lines"]))]
    for s in systems:
        recs = []
        for f in sorted(glob.glob(f"{pilot}/trials/{s}/{p}/t*/sets/covsets.json.gz")):
            R = covered(f, cases[p]["gold_path"])
            recs.append((100 * len(R["lines"] & G["lines"]) / max(1, len(G["lines"])),
                         100 * len(R["branches"] & G["branches"]) / max(1, len(G["branches"])),
                         len(R["lines"] - G["lines"])))
        if not recs:
            row += ["-"] * 3
            continue
        row += [f"{statistics.median(r[0] for r in recs):.1f}({len(recs)})",
                f"{statistics.median(r[1] for r in recs):.1f}",
                f"{statistics.median(r[2] for r in recs):.0f}"]
    rows.append(row)

head = ["case", "gold_lines"] + [f"{s}_{x}" for s in systems
                                 for x in ("line_recall%(n)", "branch_recall%", "extra_lines")]
with open(f"{pilot}/recall.tsv", "w") as out:
    out.write("\t".join(head) + "\n")
    for r in rows:
        out.write("\t".join(r) + "\n")
print(open(f"{pilot}/recall.tsv").read())
