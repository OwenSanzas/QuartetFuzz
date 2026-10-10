#!/usr/bin/env python3
"""Covered-entity sets for (a) each case's historical corpus and (b) each finished trial's
24 h snapshot. usage: cov_sets_batch.py <campaign>... [--parallel N]"""
import sys, os, glob, json, subprocess, time
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__)); DS = os.path.dirname(HERE)
camps = [a for a in sys.argv[1:] if not a.startswith('--')]
par = int(sys.argv[sys.argv.index('--parallel') + 1]) if '--parallel' in sys.argv else 4
jobs = []; seen = set()
for camp in camps:
    for trial in sorted(glob.glob(f"{camp}/*/*/trial_*")):
        if 'end_unix' not in json.load(open(f"{trial}/meta.json")): continue
        proj, fz = trial.split('/')[-3], trial.split('/')[-2]; case = f"{DS}/projects/{proj}/{fz}"
        if case not in seen and not os.path.exists(f"{case}/report/covsets_hist/covsets.json.gz"):
            jobs.append((case, f"{case}/global_corpus/seeds", f"{case}/report/covsets_hist")); seen.add(case)
        snap = f"{trial}/snaps/snap_24h"
        if os.path.isdir(snap) and not os.path.exists(f"{trial}/cov/sets_24h/covsets.json.gz"):
            jobs.append((case, snap, f"{trial}/cov/sets_24h"))
print(f"{len(jobs)} set exports, {par} at a time", flush=True)
def run(j):
    t0 = time.time(); r = subprocess.run([f"{HERE}/cov_sets.sh", *j], capture_output=True, text=True)
    return f"{time.strftime('%FT%TZ', time.gmtime())} {'ok' if r.returncode == 0 else 'FAIL'} {int(time.time()-t0)}s {(r.stdout.strip().splitlines() or [r.stderr.strip()[-200:]])[-1]}"
with ThreadPoolExecutor(par) as ex:
    for line in ex.map(run, jobs): print(line, flush=True)
print("done", flush=True)
