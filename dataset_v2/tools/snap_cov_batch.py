#!/usr/bin/env python3
"""Replay every corpus snapshot of every finished trial on the coverage binary.
usage: snap_cov_batch.py <campaign_dir>... [--parallel N]
Writes <trial>/cov/<snap>/coverage.json; skips snapshots already measured."""
import sys, os, glob, subprocess, json, time
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__)); DS = os.path.dirname(HERE)
args = [a for a in sys.argv[1:] if not a.startswith('--')]
par = int(sys.argv[sys.argv.index('--parallel') + 1]) if '--parallel' in sys.argv else 4
jobs = []
for camp in args:
    for trial in sorted(glob.glob(f"{camp}/*/*/trial_*")):
        if not os.path.exists(f"{trial}/meta.json") or 'end_unix' not in json.load(open(f"{trial}/meta.json")): continue
        proj, fz = trial.split('/')[-3], trial.split('/')[-2]
        for snap in sorted(glob.glob(f"{trial}/snaps/snap_*")):
            out = f"{trial}/cov/{os.path.basename(snap)}"
            if os.path.exists(f"{out}/coverage.json"): continue
            jobs.append((f"{DS}/projects/{proj}/{fz}", snap, out))
print(f"{len(jobs)} snapshot replays to do, {par} at a time", flush=True)
def run(j):
    case_dir, snap, out = j; t0 = time.time()
    r = subprocess.run([f"{HERE}/snap_cov.sh", case_dir, snap, out], capture_output=True, text=True)
    return f"{time.strftime('%FT%TZ', time.gmtime())} {'ok' if r.returncode == 0 else 'FAIL'} {int(time.time()-t0)}s {r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip()[-200:]}"
with ThreadPoolExecutor(par) as ex:
    for line in ex.map(run, jobs): print(line, flush=True)
print("snapshot coverage batch done", flush=True)
