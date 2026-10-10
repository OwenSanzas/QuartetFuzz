#!/usr/bin/env bash
# Covered-entity sets (lines / functions / branches) for one corpus directory, scoped to the
# project's sources, via llvm-cov export -format=lcov. Writes <out>/covsets.json.gz.
# usage: cov_sets.sh <case_dir> <corpus_dir> <out_dir>
set -uo pipefail
CASE_DIR=$(readlink -f "$1"); CORPUS=$(readlink -f "$2"); OUT=$(readlink -f -m "$3")
IMG=gcr.io/oss-fuzz-base/base-runner@sha256:d2a23fde396b83aaa3d34dbfc0b1d73f0a2b114a90014ba07b32f9e723ed950b
FUZZER=$(basename "$CASE_DIR"); BIN="${FUZZER}_cov"
SCOPE=$(python3 -c "import json;print(json.load(open('$CASE_DIR/report/coverage.json'))['scope'])")
mkdir -p "$OUT"
docker run --rm -v "$CASE_DIR/cov_build:/out:ro" -v "$CORPUS:/corpus:ro" -v "$OUT:/report" -e BIN="$BIN" "$IMG" /bin/bash -c '
  set -u; mkdir -p /tmp/dumps /tmp/empty; export LLVM_PROFILE_FILE=/tmp/dumps/cov.%1m.profraw
  /out/$BIN -merge=1 -timeout=100 /tmp/empty /corpus > /report/cov_run.log 2>&1
  llvm-profdata merge -sparse /tmp/dumps/*.profraw -o /tmp/cov.profdata 2>>/report/cov_run.log
  llvm-cov export -format=lcov -instr-profile=/tmp/cov.profdata /out/$BIN > /report/cov.lcov 2>/report/llvmcov_err.log' > "$OUT/run.log" 2>&1
python3 - "$OUT" "$SCOPE" "$CORPUS" <<'PY'
import sys,gzip,json,os,time
out,scope,corpus=sys.argv[1:4]
lines=set();funcs=set();branches=set(); tl=tf=tb=0; sf=None; inscope=False
for raw in open(f"{out}/cov.lcov",errors="replace"):
    raw=raw.rstrip("\n")
    if raw.startswith("SF:"): sf=raw[3:]; inscope=sf.startswith(scope); continue
    if not inscope: continue
    if raw.startswith("DA:"):
        ln,cnt=raw[3:].split(",")[:2]; tl+=1
        if int(cnt)>0: lines.add(f"{sf}:{ln}")
    elif raw.startswith("FNDA:"):
        cnt,name=raw[5:].split(",",1); tf+=1
        if int(cnt)>0: funcs.add(f"{sf}:{name}")
    elif raw.startswith("BRDA:"):
        p=raw[5:].split(","); tb+=1
        if len(p)>=4 and p[3] not in ("-","0"): branches.add(f"{sf}:{p[0]}:{p[1]}:{p[2]}")
json.dump({"scope":scope,"corpus":corpus,"corpus_files":sum(len(f) for _,_,f in os.walk(corpus)),"timestamp":time.strftime("%Y-%m-%dT%H:%M:%SZ",time.gmtime()),
           "totals":{"lines":tl,"functions":tf,"branches":tb},"covered":{"lines":sorted(lines),"functions":sorted(funcs),"branches":sorted(branches)}},
          gzip.open(f"{out}/covsets.json.gz","wt"))
os.remove(f"{out}/cov.lcov")
print(f"{out}: lines {len(lines)}/{tl} functions {len(funcs)}/{tf} branches {len(branches)}/{tb}")
PY
