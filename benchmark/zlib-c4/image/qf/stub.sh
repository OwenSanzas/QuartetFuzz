#!/bin/bash
# Replace every copy of the gold harness under /src (any directory, .git
# excluded) with the stub; a harness is often both COPY'd into $SRC and kept in
# the upstream tree.  Fails the image build if no copy is found.
set -u
n=0
while IFS= read -r -d '' f; do
  cp /qf/gold_stub.c "$f"; echo "qf-stub: $f"; n=$((n+1))
done < <(find /src -name .git -prune -o -type f -name "$1" -print0)
[ "$n" -gt 0 ] || { echo "qf-stub: FAILED, no file named $1 under /src"; exit 1; }
