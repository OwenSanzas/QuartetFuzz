#!/usr/bin/env bash
# Report batch state. The [o] bracket keeps this command from matching itself —
# a plain `pgrep -f onboard_batch` matches the very shell running it, which once
# had this pipeline reporting a dead batch as alive twice in a row.
R=/home/ze/agf-public/QuartetFuzz/dataset_v2/_private/review
if ps -eo pid,etime,args | grep -q "[o]nboard_batch.py"; then
  echo "running: $(ps -eo etime,args | grep '[o]nboard_batch.py' | awk '{print $1}')"
else
  echo "not running"
fi
echo "results: $(ls $R/onboard_results/*.json 2>/dev/null | wc -l)/26"
free -g | awk '/^Mem:/{print "memory: "$7"GB available"}'
