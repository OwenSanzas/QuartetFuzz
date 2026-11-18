#!/bin/bash
# Replace the gold harness of this case with the stub.
#  - The gold is the file at the declared path; if that path is only created at
#    build time (binutils copies $SRC/fuzz_*.c into the tree), the gold is the
#    same-named file that defines LLVMFuzzerTestOneInput.
#  - Every other file under /src with byte-identical content is a copy of the
#    gold and is replaced too.  Same-named files with other content (a sibling
#    harness of another branch, an unrelated sample program) are left alone.
#  - A harness embedded in library source inside #ifdef FUZZ_TARGET (util-linux)
#    has only that block replaced, so the library code stays.
# Fails the image build if no gold is found.
set -u
D=$1; B=$(basename "$1")
if [ -f "$D" ]; then ref=$D
else ref=$(find /src -name .git -prune -o -type f -name "$B" -print0 | xargs -0 -r grep -l LLVMFuzzerTestOneInput | head -1)
fi
[ -n "$ref" ] && [ -f "$ref" ] || { echo "qf-stub: FAILED, gold $D not found"; exit 1; }
cp "$ref" /qf/gold_original
n=0
while IFS= read -r -d '' f; do
  cmp -s "$f" /qf/gold_original || continue
  if grep -q '^#ifdef FUZZ_TARGET' "$f"; then
    python3 /qf/stub_block.py "$f" /qf/gold_stub.c || exit 1
    echo "qf-stub: $f (FUZZ_TARGET block)"
  else
    cp /qf/gold_stub.c "$f"; echo "qf-stub: $f"
  fi
  n=$((n+1))
done < <(find /src -name .git -prune -o -type f -name "$B" -print0)
rm -f /qf/gold_original
[ "$n" -gt 0 ] || { echo "qf-stub: FAILED, nothing replaced"; exit 1; }
