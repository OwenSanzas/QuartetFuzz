#!/bin/bash
# Run only the Logic Group and P2 stages (QF_PREPARE_ONLY) on a list of cases,
# JOBS at a time, each started only while MemAvailable >= 16 GB.  The LG and
# the P2 report are kept in $QF_LG_DIR/<case> (with trajectories and cost);
# a case whose lg.json and p2_report.md already exist costs nothing.  One
# line per case goes to <runs>/<run-id>.prep.tsv.
#
# usage: QF_LG_DIR=... AGF_CALLGRAPH_DIR=... qf_prep_queue.sh <run-id> <case>...
set -u
RUN_ID=$1; shift
HERE=$(dirname "$(readlink -f "$0")")
PUB=$(readlink -f "$HERE/../..")
RUNS=$PUB/qf-runs
JOBS=${JOBS:-6}
: "${QF_LG_DIR:?}" "${AGF_CALLGRAPH_DIR:?}"
cd "$HERE/.."
set -a; . <(grep '^ANTHROPIC_API_KEY=' /home/ze/agf/.env); set +a
export RUNS RUN_ID PUB HERE QF_LG_DIR AGF_CALLGRAPH_DIR QF_PREPARE_ONLY=1

one() {
  p=$1
  until [ $(awk '/MemAvailable/{print int($2/1048576)}' /proc/meminfo) -ge 16 ]; do sleep 60; done
  echo "$(date +%T) start $p"
  /home/ze/AGF-new/agf/.venv/bin/python scripts/run_qf_case.py $p --run-id $RUN_ID > $RUNS/$RUN_ID-$p.log 2>&1
  rc=$?
  cid=$(python3 -c "import json;print([k for k,c in json.load(open('$PUB/qf-work/cases.json')).items() if c['oss_fuzz_project']=='$p'][0].replace('/','_'))")
  line=$(python3 - "$QF_LG_DIR/$cid" <<'E'
import json, sys, os
d = sys.argv[1]
def cost(f):
    try:
        return round(json.load(open(f"{d}/{f}"))["estimated_cost"], 2)
    except Exception:
        return "-"
try:
    lg = json.load(open(f"{d}/lg.json"))
    lgs = f"entries={len(lg['entries'])} core={len(lg['core'])}"
except Exception:
    lgs = "no lg.json"
p2 = os.path.getsize(f"{d}/p2_report.md") if os.path.exists(f"{d}/p2_report.md") else 0
print("\t".join(map(str, [lgs, f"p2_chars={p2}", f"lg_cost={cost('lg_cost.json')}", f"p2_cost={cost('p2_cost.json')}"])))
E
)
  printf '%s\t%s\trc=%s\t%s\n' "$(date +%T)" "$p" "$rc" "$line" >> $RUNS/$RUN_ID.prep.tsv
  echo "$(date +%T) end $p rc=$rc"
}
export -f one
printf '%s\n' "$@" | xargs -P $JOBS -I{} bash -c 'one {}'
echo QUEUE DONE
