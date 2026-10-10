#!/usr/bin/env bash
# One fuzzing trial of one gold case under Apptainer (HPRC). Mirrors tools/fuzz_trial.sh:
# empty corpus, -rss_limit_mb=2560 -timeout=25, image ASAN_OPTIONS (leak detection on),
# restart on process exit, corpus snapshots at 5,10,20,30,60,120,240,480,720,1440 min.
# MODE=gate  : stop at the first crash-/leak- artifact (door two); oom-/timeout- restart.
# MODE=full  : always restart until the budget is spent (main experiment).
# usage: fuzz_trial_apptainer.sh <sif> <bin_dir> <fuzzer> <trial_dir> <hours> <seed> [extra_asan_options]
set -uo pipefail
SIF=$1; BINDIR=$(readlink -f "$2"); FUZZER=$3; TRIAL=$(readlink -f -m "$4"); HOURS=$5; SEED=$6; EXTRA_ASAN=${7:-}
MODE=${MODE:-gate}
BIN="${FUZZER}_asan"; [ -x "$BINDIR/$BIN" ] || { echo "no binary $BINDIR/$BIN" >&2; exit 2; }
BUDGET=$(python3 -c "print(int(float('$HOURS')*3600))")
SNAP_MIN=$(python3 -c "print(' '.join(str(m) for m in (5,10,20,30,60,120,240,480,720,1440) if m<=float('$HOURS')*60))")
# corpus on node-local disk, everything else on the shared trial dir
LOCAL=${TMPDIR:-/tmp}/qf-$$; mkdir -p "$LOCAL/corpus" "$TRIAL"/{artifacts,snaps,log}
START=$(date +%s)
cat > "$TRIAL/meta.json" <<JSON
{"fuzzer":"$FUZZER","binary":"$BIN","binary_sha256":"$(sha256sum "$BINDIR/$BIN" | cut -d' ' -f1)",
 "image":"$(basename "$SIF")","mode":"$MODE","budget_seconds":$BUDGET,"seed":$SEED,"extra_asan_options":"$EXTRA_ASAN",
 "start_unix":$START,"start":"$(date -u +%FT%TZ)","host":"$(hostname)","slurm_job":"${SLURM_JOB_ID:-}",
 "flags":"-rss_limit_mb=2560 -timeout=25 -detect_leaks=1 (image default) -print_final_stats=1","corpus":"empty","dictionary":"none"}
JSON
( for m in $SNAP_MIN; do t=$(( START + m*60 )); while [ $(date +%s) -lt $t ]; do sleep 10; done
    cp -r "$LOCAL/corpus" "$TRIAL/snaps/snap_$(printf %05d $m)m" 2>/dev/null
    echo "$(date +%s) snapshot ${m}m files=$(ls "$LOCAL/corpus" | wc -l)" >> "$TRIAL/log/events.log"; done ) &
SNAP_PID=$!
run=0; stop_reason="budget"
while :; do
  now=$(date +%s); remain=$(( START + BUDGET - now )); [ $remain -le 60 ] && break
  run=$((run+1)); rseed=$(( SEED + run ))
  echo "$now run $run start remain=${remain}s seed=$rseed" >> "$TRIAL/log/events.log"
  apptainer exec --cleanenv --containall \
    --bind "$BINDIR:/out:ro" --bind "$LOCAL/corpus:/corpus" --bind "$TRIAL/artifacts:/artifacts" \
    --env EXTRA_ASAN="$EXTRA_ASAN" --env BIN="$BIN" --env REMAIN="$remain" --env RSEED="$rseed" \
    "$SIF" /bin/bash -c '
      [ -n "$EXTRA_ASAN" ] && export ASAN_OPTIONS="$ASAN_OPTIONS:$EXTRA_ASAN"
      exec /out/$BIN -rss_limit_mb=2560 -timeout=25 -max_total_time=$REMAIN -seed=$RSEED \
           -artifact_prefix=/artifacts/ -print_final_stats=1 /corpus < /dev/null' \
    2>&1 | PYTHONIOENCODING=utf-8:surrogateescape python3 -X utf8 -u -c 'import sys,time,re,collections
# Reads BYTES and never dies on content: a harness that prints raw input bytes must not be able to
# kill the filter (a dead filter gives the fuzzer SIGPIPE, exit 141, and a restart storm).
LF=re.compile(r"^(#\d+\s|INFO:|==\d+==|SUMMARY:|MS: |artifact_prefix|\s+#\d+ 0x|NEW_FUNC|\s*To change|.*Test unit written|.*: Assertion |.*ERROR: libFuzzer|.*libFuzzer: |stat::|.*deadly signal|.*out-of-memory|.*timeout after)")
tail=collections.deque(maxlen=200); out=sys.stdout
try:
    for raw in sys.stdin.buffer:
        try: l=raw.decode("utf-8","replace")
        except Exception: l=repr(raw)+"\n"
        if len(l)>4000: l=l[:4000]+"...\n"
        try:
            if LF.match(l):
                if tail and ("==ERROR" in l or "Assertion" in l or "ERROR: libFuzzer" in l or "deadly signal" in l):
                    for t in tail: out.write(t)
                    tail.clear()
                out.write("%d %s" % (time.time(), l))
            else: tail.append("%d %s" % (time.time(), l))
        except Exception as e: out.write("%d [filter error: %r]\n" % (time.time(), e))
except Exception as e: out.write("%d [filter aborted: %r]\n" % (time.time(), e))
for t in tail: out.write(t)' > "$TRIAL/log/fuzz.$(printf %03d $run).log"
  rc=${PIPESTATUS[0]}
  art=$(ls -t "$TRIAL/artifacts" 2>/dev/null | head -1)
  echo "$(date +%s) run $run exit rc=$rc artifact=${art:-none}" >> "$TRIAL/log/events.log"
  [ $rc -eq 0 ] && break
  if [ "$MODE" = gate ] && [[ "${art:-}" == crash-* || "${art:-}" == leak-* ]]; then stop_reason="fault:$art"; break; fi
  sleep 2
done
kill $SNAP_PID 2>/dev/null; wait $SNAP_PID 2>/dev/null
cp -r "$LOCAL/corpus" "$TRIAL/corpus"; rm -rf "$LOCAL"
END=$(date +%s)
python3 - "$TRIAL" "$END" "$run" "$stop_reason" <<'PY'
import json,sys,os,glob,re,time
t,end,runs,reason=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),sys.argv[4]
m=json.load(open(f"{t}/meta.json")); arts=sorted(os.listdir(f"{t}/artifacts"))
kinds={}
for a in arts: kinds[a.split('-')[0]]=kinds.get(a.split('-')[0],0)+1
execs=0
for f in sorted(glob.glob(f"{t}/log/fuzz.*.log")):
    for l in open(f,errors="replace"):
        mm=re.search(r"stat::number_of_executed_units:\s*(\d+)",l)
        if mm: execs+=int(mm.group(1))
m.update(end_unix=end,end=time.strftime("%Y-%m-%dT%H:%M:%SZ",time.gmtime(end)),wall_seconds=end-m["start_unix"],
         restarts=runs-1,stop_reason=reason,corpus_files=len(os.listdir(f"{t}/corpus")),artifact_kinds=kinds,artifacts=arts,
         executed_units_total=execs,gate2_pass=(not any(k in kinds for k in ("crash","leak"))))
json.dump(m,open(f"{t}/meta.json","w"),indent=1)
print(f"done {t}: gate2_pass={m['gate2_pass']} wall={m['wall_seconds']}s restarts={m['restarts']} kinds={kinds} execs={execs}")
PY
