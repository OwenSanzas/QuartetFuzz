#!/usr/bin/env bash
# One fuzzing trial of one gold case: empty corpus, wall-clock budget, libFuzzer
# plain mode with an outer restart loop (ClusterFuzz behaviour: a crash, OOM or
# timeout ends the process, the artifact is kept, fuzzing resumes on the same
# corpus until the budget is spent).  Corpus snapshots at 1,2,4,8,12,24 h.
#
# usage: fuzz_trial.sh <case_dir> <trial_dir> <hours> <cpu> <seed> [extra_asan_options]
set -uo pipefail
CASE_DIR=$(readlink -f "$1"); TRIAL=$(readlink -f -m "$2"); HOURS=$3; CPU=$4; SEED=$5; EXTRA_ASAN=${6:-}
IMG=gcr.io/oss-fuzz-base/base-runner@sha256:d2a23fde396b83aaa3d34dbfc0b1d73f0a2b114a90014ba07b32f9e723ed950b
FUZZER=$(basename "$CASE_DIR"); BIN="${FUZZER}_asan"
[ -x "$CASE_DIR/asan_build/$BIN" ] || { echo "no binary $CASE_DIR/asan_build/$BIN" >&2; exit 2; }
BUDGET=$(python3 -c "print(int(float('$HOURS')*3600))")
SNAP_HOURS=$(python3 -c "print(' '.join(str(h) for h in (1,2,4,8,12,24) if h<=float('$HOURS')))")
LAST_SNAP=$(python3 -c "h=float('$HOURS'); print('%02dh'%h if h==int(h) else '%gh'%h)")
mkdir -p "$TRIAL"/{corpus,artifacts,snaps,log}
START=$(date +%s)
NAME="qf-$(echo "$TRIAL" | md5sum | cut -c1-10)"
cat > "$TRIAL/meta.json" <<JSON
{"case_dir":"$CASE_DIR","fuzzer":"$FUZZER","binary":"$BIN",
 "binary_sha256":"$(sha256sum "$CASE_DIR/asan_build/$BIN" | cut -d' ' -f1)",
 "image":"$IMG","budget_seconds":$BUDGET,"cpu":"$CPU","seed":$SEED,
 "extra_asan_options":"$EXTRA_ASAN","start_unix":$START,"start":"$(date -u +%FT%TZ)",
 "flags":"-rss_limit_mb=2560 -timeout=25 -detect_leaks=1 (image default) -print_final_stats=1",
 "corpus":"empty","dictionary":"none","options_file":"not applied","host":"$(hostname)"}
JSON

# ---- snapshot loop (host side; corpus dir is a bind mount) --------------------
( for h in $SNAP_HOURS; do
    t=$(( START + h*3600 ))
    while [ $(date +%s) -lt $t ]; do sleep 20; done
    cp -r "$TRIAL/corpus" "$TRIAL/snaps/snap_$(printf %02d $h)h" 2>/dev/null
    echo "$(date +%s) snapshot ${h}h files=$(ls "$TRIAL/corpus" | wc -l)" >> "$TRIAL/log/events.log"
  done ) &
SNAP_PID=$!

# ---- restart loop --------------------------------------------------------------
run=0
while :; do
  now=$(date +%s); remain=$(( START + BUDGET - now ))
  [ $remain -le 60 ] && break
  run=$((run+1)); rseed=$(( SEED + run ))
  echo "$now run $run start remain=${remain}s seed=$rseed" >> "$TRIAL/log/events.log"
  docker run --rm --name "$NAME" --cpuset-cpus="$CPU" --memory=4g --memory-swap=4g \
    -v "$CASE_DIR/asan_build:/out:ro" -v "$TRIAL:/work" \
    -e EXTRA_ASAN="$EXTRA_ASAN" -e BIN="$BIN" -e REMAIN="$remain" -e RSEED="$rseed" \
    "$IMG" /bin/bash -c '
      [ -n "$EXTRA_ASAN" ] && export ASAN_OPTIONS="$ASAN_OPTIONS:$EXTRA_ASAN"
      exec /out/$BIN -rss_limit_mb=2560 -timeout=25 -max_total_time=$REMAIN -seed=$RSEED \
           -artifact_prefix=/work/artifacts/ -print_final_stats=1 /work/corpus < /dev/null' \
    2>&1 | python3 -u -c 'import sys,time,re,collections
# keep every libFuzzer line; harness stdout/stderr chatter only as a 200-line tail before an error block
LF=re.compile(r"^(#\d+\s|INFO:|==\d+==|SUMMARY:|MS: |artifact_prefix|\s+#\d+ 0x|NEW_FUNC|\s*To change|.*Test unit written|.*: Assertion |.*ERROR: libFuzzer|.*libFuzzer: |stat::|.*deadly signal|.*out-of-memory|.*timeout after)")
tail=collections.deque(maxlen=200); out=sys.stdout
for l in sys.stdin:
    if LF.match(l):
        if tail and ("==ERROR" in l or "Assertion" in l or "ERROR: libFuzzer" in l or "deadly signal" in l):
            for t in tail: out.write(t)
            tail.clear()
        out.write("%d %s" % (time.time(), l))
    else: tail.append("%d %s" % (time.time(), l))
for t in tail: out.write(t)' > "$TRIAL/log/fuzz.$(printf %03d $run).log"
  rc=${PIPESTATUS[0]}
  art=$(ls -t "$TRIAL/artifacts" 2>/dev/null | head -1)
  echo "$(date +%s) run $run exit rc=$rc artifact=${art:-none}" >> "$TRIAL/log/events.log"
  [ $rc -eq 0 ] && break      # budget exhausted normally
  sleep 2
done
kill $SNAP_PID 2>/dev/null; wait $SNAP_PID 2>/dev/null
[ -d "$TRIAL/snaps/snap_$LAST_SNAP" ] || cp -r "$TRIAL/corpus" "$TRIAL/snaps/snap_$LAST_SNAP"
END=$(date +%s)
python3 - "$TRIAL" "$END" "$run" <<'PY'
import json,sys,os,glob,re
t,end,runs=sys.argv[1],int(sys.argv[2]),int(sys.argv[3])
m=json.load(open(f"{t}/meta.json")); m.update(end_unix=end,end=__import__("time").strftime("%Y-%m-%dT%H:%M:%SZ",__import__("time").gmtime(end)),
    wall_seconds=end-m["start_unix"],restarts=runs-1,corpus_files=len(os.listdir(f"{t}/corpus")),
    artifacts=sorted(os.listdir(f"{t}/artifacts")))
execs=0
for f in sorted(glob.glob(f"{t}/log/fuzz.*.log")):
    for l in open(f,errors="replace"):
        mm=re.search(r"stat::number_of_executed_units:\s*(\d+)",l)
        if mm: execs+=int(mm.group(1))
m["executed_units_total"]=execs
json.dump(m,open(f"{t}/meta.json","w"),indent=1)
print(f"done {t}: wall={m['wall_seconds']}s restarts={m['restarts']} corpus={m['corpus_files']} artifacts={len(m['artifacts'])} execs={execs}")
PY
