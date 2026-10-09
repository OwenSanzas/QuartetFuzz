#!/usr/bin/env python3
"""Run fuzz_trial.sh for a list of cases x trials, N at a time, one physical core each.

usage: fuzz_batch.py --cases cases.json --out ROOT [--trials 1] [--hours 24] [--parallel 16]
cases.json: ["project/fuzzer", ...]
Per-case ASan overrides (project .options [asan] section, applied identically to every
system) are read from tools/asan_overrides.json if present: {"project/fuzzer": "detect_leaks=0"}.
"""
import argparse, json, os, subprocess, sys, threading, time, queue
HERE = os.path.dirname(os.path.abspath(__file__)); DS = os.path.dirname(HERE)
ap = argparse.ArgumentParser()
ap.add_argument("--cases", required=True); ap.add_argument("--out", required=True)
ap.add_argument("--trials", type=int, default=1); ap.add_argument("--hours", type=float, default=24)
ap.add_argument("--parallel", type=int, default=16); ap.add_argument("--seed", type=int, default=20261009)
ap.add_argument("--cpus", default="0-15", help="host cpu list for --cpuset, e.g. 0-15 (physical cores; 16-31 are SMT siblings)")
a = ap.parse_args()
cases = json.load(open(a.cases))
over = json.load(open(os.path.join(HERE, "asan_overrides.json"))) if os.path.exists(os.path.join(HERE, "asan_overrides.json")) else {}
def expand(s):
    out = []
    for part in s.split(","):
        if "-" in part: lo, hi = map(int, part.split("-")); out += list(range(lo, hi + 1))
        else: out.append(int(part))
    return out
cpus = queue.Queue()
for c in expand(a.cpus)[: a.parallel]: cpus.put(str(c))
jobs = queue.Queue()
for t in range(1, a.trials + 1):
    for c in cases: jobs.put((c, t))
os.makedirs(a.out, exist_ok=True)
status_path = os.path.join(a.out, "status.json"); status = {"started": time.strftime("%FT%TZ", time.gmtime()), "hours": a.hours, "trials": a.trials, "cases": cases, "running": {}, "done": [], "failed": []}
lock = threading.Lock()
def save():
    with lock: json.dump(status, open(status_path, "w"), indent=1)
def worker():
    while True:
        try: case, trial = jobs.get_nowait()
        except queue.Empty: return
        cpu = cpus.get()
        proj, fz = case.split("/"); case_dir = os.path.join(DS, "projects", proj, fz)
        trial_dir = os.path.join(a.out, proj, fz, f"trial_{trial:02d}")
        seed = a.seed + trial * 1000 + (hash(case) % 997)
        key = f"{case}#{trial}"
        with lock: status["running"][key] = {"cpu": cpu, "start": time.strftime("%FT%TZ", time.gmtime())}
        save()
        with open(os.path.join(a.out, "batch.log"), "a") as lg:
            lg.write(f"{time.strftime('%FT%TZ', time.gmtime())} start {key} cpu={cpu}\n")
        rc = subprocess.call([os.path.join(HERE, "fuzz_trial.sh"), case_dir, trial_dir, str(a.hours), cpu, str(seed), over.get(case, "")],
                             stdout=open(os.path.join(a.out, "batch.log"), "a"), stderr=subprocess.STDOUT)
        with lock:
            status["running"].pop(key, None); (status["done"] if rc == 0 else status["failed"]).append(key)
        save(); cpus.put(cpu)
threads = [threading.Thread(target=worker, daemon=True) for _ in range(a.parallel)]
for t in threads: t.start()
for t in threads: t.join()
status["finished"] = time.strftime("%FT%TZ", time.gmtime()); save()
print(f"batch finished: {len(status['done'])} done, {len(status['failed'])} failed")
