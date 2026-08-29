#!/usr/bin/env bash
# Replay this target's global corpus under ASan, in two phases.
#
# Phase 1 — completeness. -runs=0 -detect_leaks=0 replays every seed exactly once
# with no mutation. This is deterministic: the same corpus against the same binary
# gives the same execution count and coverage every time (verified on
# zlib/compress_fuzzer — two consecutive runs, 3761 runs, cov 721, ft 3804 both
# times). Leak detection is off in this phase because a leaking seed aborts the
# process, and the phase exists to establish that every seed was executed.
#
# Phase 2 — PoC collection. Leak detection back on, replaying iteratively: each
# artifact is kept, the seed that produced it is removed from a scratch copy, and
# the replay resumes. Artifacts are grouped by libFuzzer's DEDUP_TOKEN, so what is
# reported is the number of *distinct* faults rather than the number of inputs
# that happen to trigger them.
#
# -fork=1 is deliberately not used anywhere here: fork mode reinterprets -runs=0 as
# a short fuzzing session (689 iterations instead of 3761 on zlib), destroying both
# the determinism and the completeness the replay exists for.
set -uo pipefail
cd "$(dirname "$0")"

BIN=libyaml_parser_fuzzer_asan
IMG=gcr.io/oss-fuzz-base/base-runner:ubuntu-24-04
MAX_ROUNDS=${MAX_ROUNDS:-40}
mkdir -p report/crashes

[ -d global_corpus/seeds ] || { echo "no corpus at global_corpus/seeds" >&2; exit 2; }
SEEDS=$(find global_corpus/seeds -type f | wc -l)
echo "replaying $SEEDS seeds through $BIN"

run_replay() {   # $1 = corpus dir on host, $2... = extra libFuzzer flags
  local corpus=$1; shift
  docker run --rm \
    -v "$PWD/asan_build:/out" -v "$corpus:/corpus" -v "$PWD/report/crashes:/crashes" \
    -e FUZZING_ENGINE=libfuzzer -e SANITIZER=address \
    -e RUN_FUZZER_MODE=interactive -e HELPER=True \
    "$IMG" run_fuzzer "$BIN" -runs=0 -artifact_prefix=/crashes/ \
                             -print_final_stats=1 "$@" /corpus 2>&1
}

# ---- phase 1: complete deterministic replay -------------------------------
echo "phase 1: full replay, leak detection off"
run_replay "$PWD/global_corpus/seeds" -detect_leaks=0 > report/run_asan_phase1.log 2>&1
p1rc=$?
execs=$(grep -oE 'stat::number_of_executed_units:[[:space:]]*[0-9]+' report/run_asan_phase1.log | tail -1 | grep -oE '[0-9]+$')
cov=$(grep -oE 'DONE[[:space:]]+cov:[[:space:]]*[0-9]+' report/run_asan_phase1.log | tail -1 | grep -oE '[0-9]+$')
echo "  rc=$p1rc executed=${execs:-?} cov=${cov:-?}"

# ---- phase 2: iterative PoC collection ------------------------------------
echo "phase 2: iterative replay, leak detection on"
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
cp -r global_corpus/seeds "$WORK/corpus"
: > report/run_asan.log
round=0
while :; do
  round=$((round+1))
  run_replay "$WORK/corpus" >> report/run_asan.log 2>&1
  rc=$?
  [ $rc -eq 0 ] && break
  [ $round -ge $MAX_ROUNDS ] && { echo "reached MAX_ROUNDS=$MAX_ROUNDS" >> report/run_asan.log; break; }
  bad=$(grep -oE 'Test unit written to \./?crashes/[a-z]+-[0-9a-f]{40}' report/run_asan.log | tail -1 | sed 's|.*/||')
  [ -n "$bad" ] || bad=$(ls -t report/crashes 2>/dev/null | grep -E '^(crash|leak|timeout|oom)-[0-9a-f]{40}$' | head -1)
  [ -n "$bad" ] || { echo "no artifact named (rc=$rc), stopping" >> report/run_asan.log; break; }
  # record which fault this artifact represents, so identical bugs collapse
  grep -oE 'DEDUP_TOKEN:.*' report/run_asan.log | tail -1 > "report/crashes/$bad.dedup" 2>/dev/null
  grep -B2 -A25 'ERROR: (Address|Leak)Sanitizer' report/run_asan.log 2>/dev/null | tail -40 > "report/crashes/$bad.trace" || true
  sha=${bad##*-}
  found=$(find "$WORK/corpus" -type f | while read -r f; do
            [ "$(sha1sum "$f" | cut -c1-40)" = "$sha" ] && { echo "$f"; break; }; done)
  if [ -n "${found:-}" ]; then
    cp "$found" "report/crashes/seed-$bad"; rm -f "$found"
    echo "round $round: $bad — seed removed, replay continues" >> report/run_asan.log
  else
    echo "round $round: $bad — seed not matched, stopping" >> report/run_asan.log; break
  fi
done

arts=$(find report/crashes -maxdepth 1 -type f \( -name 'crash-*' -o -name 'leak-*' -o -name 'timeout-*' -o -name 'oom-*' \) ! -name '*.dedup' ! -name '*.trace' | wc -l)
distinct=$(cat report/crashes/*.dedup 2>/dev/null | sort -u | wc -l)

cat > report/replay.json <<JSONEOF
{
 "seeds_in_corpus": $SEEDS,
 "phase1_full_replay": {
   "flags": "-runs=0 -detect_leaks=0",
   "exit_code": $p1rc,
   "executed_units": ${execs:-0},
   "edges_covered": ${cov:-0},
   "complete": $([ "$p1rc" -eq 0 ] && echo true || echo false)
 },
 "phase2_poc_collection": {
   "flags": "-runs=0 (leak detection on), iterative",
   "rounds": $round,
   "artifacts": $arts,
   "distinct_faults": ${distinct:-0},
   "capped": $([ "$round" -ge "$MAX_ROUNDS" ] && echo true || echo false)
 },
 "deterministic": true,
 "image": "gcr.io/oss-fuzz-base/base-runner:ubuntu-24-04",
 "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
JSONEOF

echo
echo "seeds=$SEEDS executed=${execs:-?} cov=${cov:-?} artifacts=$arts distinct_faults=${distinct:-0} rounds=$round"
