#!/bin/bash
# One-command reproduction of QuartetFuzz harness generation on the benchmark.
#
#   export ANTHROPIC_API_KEY=...
#   ./reproduce.sh zlib-c1            # one case
#   ./reproduce.sh subset25           # the 25-task subset (benchmark/subset_25.txt)
#   ./reproduce.sh all                # all 100 cases (JOBS at a time, default 3)
#   ./reproduce.sh --setup-only all   # build images and inputs, no LLM calls
#
# The configuration is the branch you are on: main = full QuartetFuzz;
# woLG / woAP / woP2 / woStatic = the four ablations of the paper.
#
# Per case it: builds the case image from benchmark/<case>/image (pinned
# sources, gold harness stubbed), prepares the inputs (scripts/setup_case.py),
# runs the generator (scripts/run_qf_case.py), then builds the delivered
# harness with ASan and with coverage (scripts/build_final_qf.sh).
#
# Output: $QF_WORK/runs/<run-id>/<case id>/   generation (harness, result.json, trajectory)
#         $QF_WORK/final/<run-id>/<case>/     final_src, final_asan, final_cov
# $QF_WORK defaults to ./work.  Requirements: Linux x86_64, Docker, git,
# Python >= 3.10; about 16 GB of RAM per concurrent case.
#
# Options (environment): JOBS (default 3), RUN_ID (default <branch>-<date>),
#   FRESH_LG=1 to let the system generate new Logic Groups and P2 reports
#   instead of reusing the ones released in benchmark/<case>/.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
export QF_WORK=${QF_WORK:-$PWD/work}
JOBS=${JOBS:-3}
SETUP_ONLY=0
[ "${1:-}" = "--setup-only" ] && { SETUP_ONLY=1; shift; }
[ $# -ge 1 ] || { sed -n '2,26p' "$0"; exit 1; }
case "$1" in
  all) CASES=$(tail -n +2 benchmark/cases.tsv | cut -f1) ;;
  subset25) CASES=$(cat benchmark/subset_25.txt) ;;
  *) CASES="$*" ;;
esac
BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo main)
RUN_ID=${RUN_ID:-$BRANCH-$(date +%Y%m%d-%H%M%S)}

# Python environment
if [ ! -x .venv/bin/python ]; then
  python3 -m venv .venv
  .venv/bin/pip install -q -r requirements.txt
fi
PY=$PWD/.venv/bin/python

# Inputs (images, sources, call graphs, LG / P2 reports)
FL=(); [ "${FRESH_LG:-0}" = 1 ] && FL=(--fresh-lg)
$PY scripts/setup_case.py --jobs "$JOBS" "${FL[@]}" $CASES
[ $SETUP_ONLY = 1 ] && exit 0

[ -n "${ANTHROPIC_API_KEY:-}" ] || { echo "set ANTHROPIC_API_KEY" >&2; exit 1; }
code=$(curl -s -o /dev/null -w '%{http_code}' https://api.anthropic.com/v1/models \
  -H "x-api-key: $ANTHROPIC_API_KEY" -H 'anthropic-version: 2023-06-01' || true)
[ "$code" = 200 ] || { echo "ANTHROPIC_API_KEY rejected by the API (HTTP $code)" >&2; exit 1; }
export AGF_CALLGRAPH_DIR=$QF_WORK/callgraphs QF_LG_DIR=$QF_WORK/lg
mkdir -p "$QF_WORK/runs" "$QF_WORK/final/$RUN_ID" "$QF_WORK/logs"
echo "configuration: $BRANCH   run id: $RUN_ID   cases: $(echo $CASES | wc -w)"

one() {
  c=$1
  $PY scripts/run_qf_case.py "$c" --run-id "$RUN_ID" > "$QF_WORK/logs/$RUN_ID-$c.log" 2>&1 || true
  cid=$($PY -c "import json,sys;print(json.load(open('benchmark/$c/case.json'))['case_id'].replace('/','_'))")
  d=$QF_WORK/runs/$RUN_ID/$cid
  if [ -f "$d/delivery.json" ]; then
    fb=$(scripts/build_final_qf.sh "$d" "$QF_WORK/final/$RUN_ID" 2>&1 | tr '\n' ' ')
    echo "$c: delivered; $fb"
  else
    echo "$c: no harness delivered (log: $QF_WORK/logs/$RUN_ID-$c.log)"
  fi
}
export -f one; export PY RUN_ID
printf '%s\n' $CASES | xargs -P "$JOBS" -I{} bash -c 'one {}'
echo "done: $QF_WORK/runs/$RUN_ID  $QF_WORK/final/$RUN_ID"
