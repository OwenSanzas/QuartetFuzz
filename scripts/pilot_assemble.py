#!/usr/bin/env python3
"""Lay out a pilot comparison: one case dir per (system, case) in the shape
dataset_v2/tools/fuzz_trial.sh and snap_cov.sh take (the gold scripts):

    <pilot>/<system>/<p>-cN/<fuzzer>/asan_build/<fuzzer>_asan   (+ lib/, llvm-symbolizer)
                                     cov_build/<fuzzer>_cov     (+ lib/)
                                     report/{coverage.json,_scope.py}   scope = the gold case's

Every system gets the same scope (the gold case's: the project's own
sources), the same runner script, image and flags.  Systems:

    ofg        ofg-work/results-nofi/<p>/final_{asan,cov}/out/<fuzzer>
    promefuzz  hprc_baselines/bins/promefuzz/<p>/<fuzzer>/  (ASan, as sent to HPRC)
               pf-work/results/<p>/final_cov/fuzz_driver_N
    gold       dataset_v2/projects/<project>/<fuzzer>/{asan,cov}_build  (prebuilt, reference)
    <name>     QuartetFuzz, one per --qf <name>=<run-id>:
               qf-final/<run-id>/<p>/final_{asan,cov}/out/<fuzzer>   (scripts/build_final_qf.sh)

Existing system dirs are kept (their trials stay valid).

usage: pilot_assemble.py <pilot dir> [--qf <name>=<run-id>]... <case>...   (case = <p>-cN)
"""
import glob
import json
import os
import shutil
import sys

PUB = "/home/ze/agf-public"
BASE = "/home/ze/baselines"
args = sys.argv[1:]
pilot = args.pop(0)
qf = {}
while "--qf" in args:
    i = args.index("--qf")
    name, run_id = args[i + 1].split("=", 1)
    qf[name] = run_id
    del args[i:i + 2]
names = args
cases = json.load(open(f"{PUB}/qf-work/cases.json"))
by_p = {c["oss_fuzz_project"]: dict(c, case_id=k) for k, c in cases.items()}


def put(src_bin, out_dir, dst_dir, name):
    """Copy a fuzzer binary as <name>, with the runtime libs next to it."""
    os.makedirs(dst_dir, exist_ok=True)
    shutil.copy2(src_bin, f"{dst_dir}/{name}")
    for extra in ("lib", "llvm-symbolizer"):
        s = f"{out_dir}/{extra}"
        if os.path.isdir(s):
            shutil.copytree(s, f"{dst_dir}/{extra}", dirs_exist_ok=True)
        elif os.path.isfile(s):
            shutil.copy2(s, f"{dst_dir}/{extra}")


for p in names:
    c = by_p[p]
    fz = c["fuzzer_name"]
    project, gold_fz = c["case_id"].split("/", 1)
    report = f"{PUB}/QuartetFuzz/dataset_v2/projects/{project}/{gold_fz}/report"
    gold = f"{PUB}/QuartetFuzz/dataset_v2/projects/{project}/{gold_fz}"
    srcs = {
        "ofg": (f"{BASE}/ofg-work/results-nofi/{p}/final_asan/out",
                f"{BASE}/ofg-work/results-nofi/{p}/final_cov/out", fz, fz),
        "gold": (f"{gold}/asan_build", f"{gold}/cov_build", f"{fz}_asan", f"{fz}_cov"),
    }
    pf_bundle = f"{BASE}/hprc_baselines/bins/promefuzz/{p}/{fz}"
    pf_cov = sorted(glob.glob(f"{BASE}/pf-work/results/{p}/final_cov/fuzz_driver_*"))
    pf_cov = [x for x in pf_cov if os.path.isfile(x) and os.access(x, os.X_OK)]
    if os.path.isdir(pf_bundle) and pf_cov:
        srcs["promefuzz"] = (pf_bundle, os.path.dirname(pf_cov[0]), f"{fz}_asan",
                             os.path.basename(pf_cov[0]))
    for name, run_id in qf.items():
        f = f"{PUB}/qf-final/{run_id}/{p}"
        srcs[name] = (f"{f}/final_asan/out", f"{f}/final_cov/out", fz, fz)
    for system, (adir, cdir, abin, cbin) in srcs.items():
        d = f"{pilot}/{system}/{p}/{fz}"
        if os.path.isfile(f"{d}/cov_build/{fz}_cov"):
            continue
        if not (os.path.isfile(f"{adir}/{abin}") and os.path.isfile(f"{cdir}/{cbin}")):
            print(f"{system}/{p}: not delivered")
            continue
        put(f"{adir}/{abin}", adir, f"{d}/asan_build", f"{fz}_asan")
        put(f"{cdir}/{cbin}", cdir, f"{d}/cov_build", f"{fz}_cov")
        os.makedirs(f"{d}/report", exist_ok=True)
        scope = json.load(open(f"{report}/coverage.json"))["scope"]
        json.dump({"scope": scope}, open(f"{d}/report/coverage.json", "w"))
        shutil.copy2(f"{report}/_scope.py", f"{d}/report/_scope.py")
        print(f"{system}/{p}: ok")
