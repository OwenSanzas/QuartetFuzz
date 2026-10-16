#!/usr/bin/env bash
#
# QuartetFuzz: 100-case reproducibility script (RQ3, paper §5.3).
#
# Runs the full three-stage pipeline (LG → P2 → harness) on the
# 100-case gold-standard dataset and produces a coverage report
# comparable to the numbers in Table 4 of the paper:
#
#     Avg. line     17.8%
#     Avg. branch   17.5%
#     Avg. function 18.7%
#     Non-zero rate 97/100
#     Avg. cost     $1.14
#
# Hardware assumed: 32 cores, 62 GB RAM, ten parallel workers.
# Each case runs LibFuzzer for 10 × 600 s with empty corpus and ASan.
#
# Wall-clock with parallelism = 10:  ~30–40 hours
# Mean cost / case (Sonnet 4.6):     ~$1.14   →  ~$114 total
#
# Usage:
#     export ANTHROPIC_API_KEY=sk-ant-...
#     ./scripts/reproduce_100_cases.sh                 # full run
#     ./scripts/reproduce_100_cases.sh --dry-run       # plan only, no API
#     ./scripts/reproduce_100_cases.sh --cases 5       # smoke test on 5
#     ./scripts/reproduce_100_cases.sh --case mongoose/fuzz_match  # one
#     ./scripts/reproduce_100_cases.sh --resume        # skip done cases
#
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# ------------ defaults ------------
DATASET="${DATASET:-dataset/benchmark_cases_with_prompts.jsonl}"
OUT_DIR="${OUT_DIR:-/tmp/agf-${USER}/repro_100}"
PARALLEL="${PARALLEL:-10}"
FUZZ_BUDGET="${FUZZ_BUDGET:-600}"
FUZZ_REPS="${FUZZ_REPS:-10}"
DRY_RUN=0
RESUME=0
CASES_LIMIT=
SINGLE_CASE=
SKIP_LG=0

# ------------ args ------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run) DRY_RUN=1; shift ;;
    --resume)  RESUME=1; shift ;;
    --cases)   CASES_LIMIT="$2"; shift 2 ;;
    --case)    SINGLE_CASE="$2"; shift 2 ;;
    --out)     OUT_DIR="$2"; shift 2 ;;
    --parallel) PARALLEL="$2"; shift 2 ;;
    --skip-lg) SKIP_LG=1; shift ;;
    -h|--help) sed -n '3,30p' "$0"; exit 0 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

# ------------ checks ------------
if [[ "$DRY_RUN" == "0" && -z "${ANTHROPIC_API_KEY:-}" ]]; then
  echo "ERROR: ANTHROPIC_API_KEY is unset. Run with --dry-run to plan only."
  exit 1
fi
if [[ ! -f "$DATASET" ]]; then
  echo "ERROR: dataset not found: $DATASET"
  exit 1
fi

mkdir -p "$OUT_DIR"
LOG="$OUT_DIR/run.log"
SUMMARY="$OUT_DIR/summary.csv"
echo "case_id,build_ok,line_pct,branch_pct,function_pct,cost_usd,turns,iter,status" > "$SUMMARY"

# ------------ select cases ------------
if [[ -n "$SINGLE_CASE" ]]; then
  CASE_LIST=$(python3 -c "
import json, sys
target = '$SINGLE_CASE'
with open('$DATASET') as f:
    for line in f:
        d = json.loads(line)
        if d['case_id'] == target:
            print(d['case_id'])
            sys.exit(0)
print('NOT_FOUND', file=sys.stderr); sys.exit(1)")
elif [[ -n "$CASES_LIMIT" ]]; then
  CASE_LIST=$(python3 -c "
import json
with open('$DATASET') as f:
    for i, line in enumerate(f):
        if i >= $CASES_LIMIT: break
        print(json.loads(line)['case_id'])")
else
  CASE_LIST=$(python3 -c "
import json
with open('$DATASET') as f:
    for line in f:
        print(json.loads(line)['case_id'])")
fi

N_CASES=$(echo "$CASE_LIST" | wc -l | tr -d ' ')
echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] Reproducing $N_CASES cases (parallel=$PARALLEL)" | tee -a "$LOG"

# ------------ run one case ------------
run_one_case() {
  local case_id="$1"
  local case_dir="$OUT_DIR/cases/${case_id//\//_}"
  mkdir -p "$case_dir"

  if [[ "$RESUME" == "1" && -f "$case_dir/DONE" ]]; then
    echo "  [skip resume] $case_id"
    return 0
  fi

  if [[ "$DRY_RUN" == "1" ]]; then
    echo "  [dry-run] would run pipeline for $case_id" | tee -a "$LOG"
    echo "$case_id,DRY,,,,,,,DRY" >> "$SUMMARY"
    return 0
  fi

  # The driver loads the case from $DATASET, derives a prompt
  # (target-named style; see paper §5.3), and invokes
  # run_full_pipeline (LG -> P2 -> harness gen -> build -> fuzz).
  PY="$REPO_ROOT/.venv/bin/python"
  [[ -x "$PY" ]] || PY="python3"
  EXTRA_ARGS=()
  if [[ "$SKIP_LG" == "1" ]]; then
    EXTRA_ARGS+=(--skip-lg)
  fi
  "$PY" "$REPO_ROOT/scripts/reproduce_one_case.py" \
    --case-id "$case_id" \
    --dataset "$DATASET" \
    --output-dir "$case_dir" \
    --num-lgs 10 \
    --top-k 5 \
    --top-k-entries 3 \
    "${EXTRA_ARGS[@]}" \
    >"$case_dir/pipeline.log" 2>&1 || {
      echo "$case_id,FAIL,,,,,,,FAIL" >> "$SUMMARY"
      return 0
    }

  # Coverage results land in $case_dir/coverage.json
  python3 - <<EOF
import json, os, csv
case_dir = "$case_dir"
case_id = "$case_id"
cov_path = os.path.join(case_dir, "coverage.json")
summary = "$SUMMARY"
if os.path.exists(cov_path):
    cov = json.load(open(cov_path))
    line = cov.get("line_pct", "")
    branch = cov.get("branch_pct", "")
    func = cov.get("function_pct", "")
    cost = cov.get("cost_usd", "")
    turns = cov.get("turns", "")
    iters = cov.get("iter", "")
    status = "OK" if (line or branch or func) else "ZERO"
    with open(summary, "a") as f:
        f.write(f"{case_id},1,{line},{branch},{func},{cost},{turns},{iters},{status}\n")
    open(os.path.join(case_dir, "DONE"), "w").close()
EOF
}

# ------------ parallel orchestration ------------
export -f run_one_case
export OUT_DIR DATASET DRY_RUN RESUME LOG SUMMARY FUZZ_BUDGET FUZZ_REPS

if command -v parallel >/dev/null 2>&1; then
  echo "$CASE_LIST" | parallel -j "$PARALLEL" --line-buffer run_one_case {}
else
  i=0
  for case_id in $CASE_LIST; do
    run_one_case "$case_id" &
    i=$((i+1))
    if (( i % PARALLEL == 0 )); then wait; fi
  done
  wait
fi

# ------------ aggregate ------------
python3 - <<'EOF'
import csv, statistics, sys, os
summary = os.path.join(os.environ.get("OUT_DIR", "/tmp/agf-${USER}/repro_100"), "summary.csv")
if not os.path.exists(summary):
    print(f"summary not found: {summary}"); sys.exit(0)

rows = list(csv.DictReader(open(summary)))
if not rows:
    print("no rows in summary"); sys.exit(0)

def col(name):
    return [float(r[name]) for r in rows if r.get(name) and r[name].replace('.','',1).replace('-','',1).isdigit()]

n = len(rows)
nz_line = [v for v in col("line_pct") if v > 0]
print(f"\n=== Reproduction summary ({n} cases) ===")
print(f"Non-zero rate (line>0): {len(nz_line)}/{n}")
if col("line_pct"):
    print(f"Avg line     {statistics.mean(col('line_pct')):.1f}%   (paper: 17.8%)")
if col("branch_pct"):
    print(f"Avg branch   {statistics.mean(col('branch_pct')):.1f}%   (paper: 17.5%)")
if col("function_pct"):
    print(f"Avg function {statistics.mean(col('function_pct')):.1f}%   (paper: 18.7%)")
if col("cost_usd"):
    print(f"Avg cost     ${statistics.mean(col('cost_usd')):.2f} (paper: $1.14)")
EOF

echo
echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] Done. Summary: $SUMMARY"
