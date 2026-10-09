#!/usr/bin/env python3
"""Shrink fuzz run logs that a harness floods with its own stdout/stderr.

Keeps every libFuzzer line (status, NEW/REDUCE, INFO, sanitizer report, stack frames,
artifact lines) and only the last KEEP_TAIL non-libFuzzer lines before each libFuzzer
error block, so crash context survives but per-input chatter does not.
usage: log_janitor.py <trial_dir>... [--loop]   (--loop: rerun every 60 s until trial done)
"""
import glob, json, os, re, sys, time
KEEP_TAIL = 200
LF = re.compile(r"^\d+ (#\d+\s|INFO:|==\d+==|SUMMARY:|MS: |artifact_prefix|\s+#\d+ 0x|NEW_FUNC|\s*To change|.*Test unit written|.*: Assertion |.*ERROR: libFuzzer|.*libFuzzer: |stat::|.*deadly signal|.*out-of-memory|.*timeout after)")
def shrink(path):
    tmp = path + ".tmp"; kept = 0; dropped = 0; tail = []
    with open(path, errors="replace") as f, open(tmp, "w") as o:
        for line in f:
            if LF.match(line):
                if tail and ("==ERROR" in line or "Assertion" in line or "ERROR: libFuzzer" in line or "deadly signal" in line):
                    o.writelines(tail); kept += len(tail); tail = []
                o.write(line); kept += 1
            else:
                tail.append(line); dropped += 1
                if len(tail) > KEEP_TAIL: tail.pop(0)
        if tail: o.writelines(tail); kept += len(tail)
    os.replace(tmp, path)
    return kept, dropped
def pass_once(trial):
    logs = sorted(glob.glob(f"{trial}/log/fuzz.*.log"))
    done = os.path.exists(f"{trial}/meta.json") and "end_unix" in json.load(open(f"{trial}/meta.json"))
    if not done: logs = logs[:-1]                      # newest is still being written
    marker = f"{trial}/log/.shrunk"; already = set(open(marker).read().split()) if os.path.exists(marker) else set()
    tot_d = 0
    for p in logs:
        if p in already or os.path.getsize(p) < 2_000_000: continue
        k, d = shrink(p); tot_d += d; already.add(p)
    open(marker, "w").write("\n".join(sorted(already)))
    return tot_d, done
if __name__ == "__main__":
    loop = "--loop" in sys.argv; trials = [a for a in sys.argv[1:] if a != "--loop"]
    while True:
        alldone = True
        for t in trials:
            d, done = pass_once(t); alldone &= done
            if d: print(time.strftime("%FT%TZ", time.gmtime()), t, "dropped", d, "lines", flush=True)
        if not loop or alldone: break
        time.sleep(60)
