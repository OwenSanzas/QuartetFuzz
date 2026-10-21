#!/bin/bash
# Pilot comparison: every (system, case) dir that pilot_assemble.py laid out
# gets TRIALS empty-corpus libFuzzer trials with the gold script (fuzz_trial.sh:
# base-runner image digest, -rss_limit_mb=2560 -timeout=25, restarts on
# crash), seed 20261017+k for trial k, and each trial's coverage with the
# system's own coverage build (snap_cov.sh: chunked replay, llvm-cov, scoped
# to the gold case's project sources).  Identical for every system.  The
# case's ASan options (dataset_v2/tools/asan_overrides.json) apply to every
# system.  Finished trials are kept.
#
# usage: [TRIALS=1] pilot_run.sh <pilot dir> [minutes=10] [parallel=3]
# writes <pilot>/trials/<system>/<p>/t<k>/{cov/coverage.json,snaps/,trial.log}
set -u
PILOT=$(readlink -f "$1"); MIN=${2:-10}; PAR=${3:-3}; TRIALS=${TRIALS:-1}
TOOLS=$(dirname "$(readlink -f "$0")")/../dataset_v2/tools
HOURS=$(python3 -c "print($MIN/60)")
export PILOT MIN HOURS TOOLS
ls -d $PILOT/*/*/*/ | grep -v -e '/trials/' -e '/gold_ref/' > $PILOT/dirs.txt

one() {
  d=${1%/}; k=$2; i=$3
  rel=${d#$PILOT/}; system=${rel%%/*}; rest=${rel#*/}; p=${rest%%/*}
  out=$PILOT/trials/$system/$p/t$k
  [ -f $out/cov/coverage.json ] && return
  until [ $(awk '/MemAvailable/{print int($2/1048576)}' /proc/meminfo) -ge 16 ]; do sleep 30; done
  cid=$(python3 -c "import json;print([k for k,c in json.load(open('$TOOLS/../../../qf-work/cases.json')).items() if c['oss_fuzz_project']=='$p'][0])")
  extra=$(python3 -c "import json;print(json.load(open('$TOOLS/asan_overrides.json')).get('$cid',''))")
  cpu=$(( 8 + i % 24 ))
  rm -rf $out; mkdir -p $out
  $TOOLS/fuzz_trial.sh $d $out $HOURS $cpu $(( 20261017 + k )) "$extra" > $out/trial.log 2>&1
  snap=$(ls -d $out/snaps/snap_$(printf %05d $MIN)m 2>/dev/null || echo $out/corpus)
  $TOOLS/snap_cov.sh $d $snap $out/cov >> $out/trial.log 2>&1
  echo "$(date +%T) $rel t$k $(tail -n 1 $out/trial.log)"
}
export -f one
i=0
while read -r d; do
  for k in $(seq 1 $TRIALS); do echo "$d $k $i"; i=$((i+1)); done
done < $PILOT/dirs.txt | xargs -P $PAR -n 3 bash -c 'one "$0" "$1" "$2"'

python3 - "$PILOT" <<'PY'
import json, sys, glob, statistics
pilot = sys.argv[1]
rows = {}
for f in glob.glob(f"{pilot}/trials/*/*/t*/cov/coverage.json"):
    system, p = f.split("/")[-5], f.split("/")[-4]
    rows.setdefault(p, {}).setdefault(system, []).append(json.load(open(f))["lines"]["percent"])
systems = sorted({s for r in rows.values() for s in r})
with open(f"{pilot}/summary.tsv", "w") as out:
    out.write("case\t" + "\t".join(f"{s}_lines_median(n)" for s in systems) + "\n")
    for p in sorted(rows):
        cells = [f"{statistics.median(rows[p][s]):.1f}({len(rows[p][s])})" if s in rows[p]
                 else "not delivered" for s in systems]
        out.write(p + "\t" + "\t".join(cells) + "\n")
print(open(f"{pilot}/summary.tsv").read())
PY
