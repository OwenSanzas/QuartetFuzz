#!/usr/bin/env bash
# llvm-cov (lines / functions / branches / regions, scoped to project sources) for one
# corpus directory, e.g. a fuzzing snapshot. Same procedure as run_cov.sh.
# usage: snap_cov.sh <case_dir> <corpus_dir> <out_dir>
set -uo pipefail
CASE_DIR=$(readlink -f "$1"); CORPUS=$(readlink -f "$2"); OUT=$(readlink -f -m "$3")
IMG=gcr.io/oss-fuzz-base/base-runner@sha256:d2a23fde396b83aaa3d34dbfc0b1d73f0a2b114a90014ba07b32f9e723ed950b
FUZZER=$(basename "$CASE_DIR"); BIN="${FUZZER}_cov"
SCOPE=$(python3 -c "import json;print(json.load(open('$CASE_DIR/report/coverage.json'))['scope'])")
[ -x "$CASE_DIR/cov_build/$BIN" ] || { echo "no coverage build $CASE_DIR/cov_build/$BIN" >&2; exit 2; }
mkdir -p "$OUT"; cp "$CASE_DIR/report/_scope.py" "$OUT/_scope.py"
N=$(find "$CORPUS" -type f | wc -l)
docker run --rm -v "$CASE_DIR/cov_build:/out:ro" -v "$CORPUS:/corpus:ro" -v "$OUT:/report" \
  -e SRC_SCOPE="$SCOPE" -e BIN="$BIN" "$IMG" /bin/bash -c '
    set -u; mkdir -p /tmp/dumps /tmp/empty
    export LLVM_PROFILE_FILE=/tmp/dumps/cov.%1m.profraw
    /out/$BIN -merge=1 -timeout=100 /tmp/empty /corpus > /report/cov_run.log 2>&1; echo "target exit: $?" >> /report/cov_run.log
    llvm-profdata merge -sparse /tmp/dumps/*.profraw -o /tmp/cov.profdata 2>>/report/cov_run.log
    llvm-cov export -summary-only -instr-profile=/tmp/cov.profdata /out/$BIN > /report/coverage_export.json 2>/report/llvmcov_err.log
    python3 /report/_scope.py' > "$OUT/run.log" 2>&1
python3 - "$OUT" "$N" "$CORPUS" <<'PY'
import json,sys,time,os
out,n,corpus=sys.argv[1],int(sys.argv[2]),sys.argv[3]
p=f"{out}/coverage_scoped.json"
if not os.path.exists(p): print("no coverage produced"); sys.exit(1)
d=json.load(open(p)); r=dict(d["scoped"]); r.update(scope=d["scope"],files_in_scope=d["files_in_scope"],corpus=corpus,corpus_files=n,
  timestamp=time.strftime("%Y-%m-%dT%H:%M:%SZ",time.gmtime()),method="-merge=1 replay of snapshot, llvm-cov -summary-only, scoped")
json.dump(r,open(f"{out}/coverage.json","w"),indent=1)
print(f"{os.path.basename(os.path.dirname(out))}/{os.path.basename(out)}: files={n} lines={r['lines']['percent']}% functions={r['functions']['percent']}% branches={r['branches']['percent']}%")
PY
