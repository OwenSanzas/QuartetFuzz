#!/bin/bash
# Run QuartetFuzz on a list of cases, JOBS at a time (default 3: the machine's
# heavy-job budget), each started only while MemAvailable >= 16 GB.  After a
# case finishes its delivered harness gets the final ASan + coverage builds
# (build_final_qf.sh) and one audit line goes to <runs>/<run-id>.audit.tsv.
#
# usage: qf_queue.sh <run-id> <case>...      (case = <p>-cN)
set -u
RUN_ID=$1; shift
HERE=$(dirname "$(readlink -f "$0")")
PUB=$(readlink -f "$HERE/../..")
RUNS=$PUB/qf-runs; FINAL=$PUB/qf-final/$RUN_ID
JOBS=${JOBS:-3}
mkdir -p $RUNS $FINAL
cd "$HERE/.."
set -a; . <(grep '^ANTHROPIC_API_KEY=' /home/ze/agf/.env); set +a
export RUNS FINAL RUN_ID PUB HERE

one() {
  p=$1
  until [ $(awk '/MemAvailable/{print int($2/1048576)}' /proc/meminfo) -ge 16 ]; do sleep 60; done
  echo "$(date +%T) start $p"
  /home/ze/AGF-new/agf/.venv/bin/python scripts/run_qf_case.py $p --run-id $RUN_ID > $RUNS/$RUN_ID-$p.log 2>&1
  rc=$?
  cid=$(python3 -c "import json;print([k for k,c in json.load(open('$PUB/qf-work/cases.json')).items() if c['oss_fuzz_project']=='$p'][0].replace('/','_'))")
  d=$RUNS/$RUN_ID/$cid
  fb="-"
  if [ -f $d/delivery.json ]; then
    fb=$($HERE/build_final_qf.sh $d $FINAL 2>&1 | tr '\n' ' ')
  fi
  line=$(/home/ze/AGF-new/agf/.venv/bin/python - "$d" <<'E'
import json, sys, os
d = sys.argv[1]
try:
    r = json.load(open(f"{d}/result.json"))
except Exception:
    print("no result.json"); sys.exit()
dv = json.load(open(f"{d}/delivery.json")) if os.path.exists(f"{d}/delivery.json") else {}
ev = [json.loads(l) for l in open(f"{d}/trajectory.jsonl")] if os.path.exists(f"{d}/trajectory.jsonl") else []
res = [e for e in ev if e.get("event_type") == "tool_result"]
builds = [e["content"][:12] for e in res if e.get("tool_name") == "build_harness"]
reach = [e["content"][:16] for e in res if e.get("tool_name") == "AP_Reach_check"]
# Silent tool failures: code-view calls that came back empty, tool errors.
cv = [e for e in res if e.get("tool_name") in ("Read", "Glob", "Grep")]
empty = sum(not (e.get("content") or "").strip() for e in cv)
errs = sum(bool(e.get("is_error")) for e in res)
print("\t".join(map(str, [dv.get("delivered_from", "none"), r["total_turns"], round(r["estimated_cost"], 2),
      f"builds={sum(b.startswith('BUILD OK') for b in builds)}/{len(builds)}",
      f"reach_hit={sum(x.startswith('TARGET HIT') for x in reach)}/{len(reach)}",
      f"cov_lines={r['coverage_lines_pct']}", f"codeview_empty={empty}/{len(cv)}", f"tool_errors={errs}",
      r.get("error", "")[:80]])))
E
)
  printf '%s\t%s\trc=%s\t%s\tfinal: %s\n' "$(date +%T)" "$p" "$rc" "$line" "$fb" >> $RUNS/$RUN_ID.audit.tsv
  echo "$(date +%T) end $p rc=$rc"
}
export -f one
printf '%s\n' "$@" | xargs -P $JOBS -I{} bash -c 'one {}'
echo QUEUE DONE
