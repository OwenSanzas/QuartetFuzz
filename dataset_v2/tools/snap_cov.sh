#!/usr/bin/env bash
# llvm-cov (lines / functions / branches / regions, scoped to project sources) for one
# corpus directory, e.g. a fuzzing snapshot.
# chunked replay instead of -merge=1: a process that dies (oom, crash, timeout) never writes
# its profile, so a dying -merge=1 child drops the coverage of every input it ran before (tested
# 2026-10-14: wabt/wasm_interp seeds, two merge children oom at the default 2048 MB, lines 47.5%
# vs 48.6%, branches 52.0% vs 58.3%). A dead chunk is replayed one input per process, so only
# inputs that themselves kill the target go uncounted.
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
    cd /corpus; find . -type f | sort > /tmp/list; split -l 500 /tmp/list /tmp/chunk.
    : > /report/cov_run.log; killers=0
    for c in /tmp/chunk.*; do
      xargs -a "$c" -d "\\n" /out/$BIN -timeout=100 >> /report/cov_run.log 2>&1 && continue
      echo "chunk $c died, replaying its inputs one by one" >> /report/cov_run.log
      while IFS= read -r f; do
        /out/$BIN -timeout=100 "$f" >> /report/cov_run.log 2>&1 || { killers=$((killers+1)); echo "input not counted: $f" >> /report/cov_run.log; }
      done < "$c"
    done
    echo "target exit: 0; inputs not counted: $killers of $(wc -l < /tmp/list)" >> /report/cov_run.log
    llvm-profdata merge -sparse /tmp/dumps/*.profraw -o /tmp/cov.profdata 2>>/report/cov_run.log
    llvm-cov export -summary-only -instr-profile=/tmp/cov.profdata /out/$BIN > /report/coverage_export.json 2>/report/llvmcov_err.log
    python3 /report/_scope.py' > "$OUT/run.log" 2>&1
python3 - "$OUT" "$N" "$CORPUS" <<'PY'
import json,sys,time,os
out,n,corpus=sys.argv[1],int(sys.argv[2]),sys.argv[3]
p=f"{out}/coverage_scoped.json"
if not os.path.exists(p): print("no coverage produced"); sys.exit(1)
d=json.load(open(p)); r=dict(d["scoped"]); r.update(scope=d["scope"],files_in_scope=d["files_in_scope"],corpus=corpus,corpus_files=n,
  timestamp=time.strftime("%Y-%m-%dT%H:%M:%SZ",time.gmtime()),method="chunked replay of snapshot (500 inputs per process, dead chunks one input per process), llvm-cov -summary-only, scoped")
json.dump(r,open(f"{out}/coverage.json","w"),indent=1)
print(f"{os.path.basename(os.path.dirname(out))}/{os.path.basename(out)}: files={n} lines={r['lines']['percent']}% functions={r['functions']['percent']}% branches={r['branches']['percent']}%")
PY
