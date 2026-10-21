#!/bin/bash
# Final builds of QuartetFuzz's delivered harness for one case: ASan
# (final_asan/) and coverage (final_cov/), made exactly like the baselines'
# (ofg-fair/quartet/build_final.sh): the harness source at the case's gold
# path in the case image qf-case/<p>-cN:20261017 (= the image the baselines
# were built in), the project's own build script, OSS-Fuzz `compile` with
# libFuzzer, the whole /out kept (runtime libraries).
#
# usage: build_final_qf.sh <run case dir with delivery.json> <out root>
# writes <out root>/<p>-cN/{final_src,final_asan,final_cov}
set -u
RUN=$(readlink -f "$1"); ROOT=$(readlink -f -m "$2")
HERE=$(dirname "$(readlink -f "$0")")
CASES=$HERE/../../qf-work/cases.json
read -r P TP TN LG HF < <(python3 - "$RUN" "$CASES" <<'E'
import json, sys
run, cases = sys.argv[1:3]
d = json.load(open(f"{run}/delivery.json"))
c = json.load(open(cases))[d["case_id"]]
print(c["oss_fuzz_project"], d["container_path"].replace("$SRC", "/src"), c["fuzzer_name"],
      c["language"], d["harness_file"])
E
)
D=$ROOT/$P; S=$D/final_src
mkdir -p $S
cp "$RUN/$HF" $S/harness.src
cp "$RUN/delivery.json" $S/delivery.json
printf '%s\n%s\n%s\n' "$TP" "$TN" "$LG" > $S/target
IMG=qf-case/$P:20261017
# per-case pipeline fixups (qf-work/src-fixups/<p>-cN.sh), run before compile
FIX=$HERE/../../qf-work/src-fixups/$P.sh; FIXV=(); [ -f $FIX ] && FIXV=(-v $FIX:/qf-fixup.sh:ro)
for SAN in address coverage; do
  [ $SAN = address ] && F=$D/final_asan || F=$D/final_cov
  [ -f $F/status ] && grep -q '^built$' $F/status && { echo "$SAN: built (kept)"; continue; }
  rm -rf $F; mkdir -p $F
  cp $S/harness.src $F/
  docker run --rm --memory 16g \
    -e SANITIZER=$SAN -e FUZZING_ENGINE=libfuzzer -e ARCHITECTURE=x86_64 \
    -e FUZZING_LANGUAGE="$LG" -v $F:/final "${FIXV[@]}" \
    --entrypoint bash "$IMG" -c "
      [ -f /qf-fixup.sh ] && . /qf-fixup.sh
      cp /final/harness.src '$TP'
      compile > /final/build.log 2>&1; echo \"compile rc=\$?\" >> /final/build.log
      rm -rf /final/out; cp -a /out /final/out 2>/dev/null
      [ -f /final/out/$TN ] && echo built > /final/status || echo 'build failed' > /final/status
      chmod -R a+rwX /final" > /dev/null 2>&1
  echo "$SAN: $(cat $F/status)"
done
