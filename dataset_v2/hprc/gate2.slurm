#!/bin/bash
# Door two: 24 h empty-corpus run of every gold harness, one core each. Array over cases.txt.
# Fill in --account (and --partition if FASTER needs one) before submitting:
#   sbatch --array=0-$(( $(wc -l < cases.txt) - 1 )) gate2.slurm
#SBATCH --job-name=qf-gate2
#SBATCH --time=25:30:00
#SBATCH --ntasks=1 --cpus-per-task=1 --mem=4G
#SBATCH --output=logs/gate2_%A_%a.out
#SBATCH --account=CHANGE_ME
set -uo pipefail
module load Apptainer 2>/dev/null || module load apptainer 2>/dev/null || true
cd "$(dirname "$0")"
CASE=$(sed -n "$(( SLURM_ARRAY_TASK_ID + 1 ))p" cases.txt)          # project/fuzzer
PROJ=${CASE%/*}; FZ=${CASE#*/}
EXTRA=$(python3 -c "import json,sys;print(json.load(open('asan_overrides.json')).get('$CASE',''))")
MODE=gate ./fuzz_trial_apptainer.sh base-runner.sif "bins/$PROJ/$FZ" "$FZ" "results/gate2/$PROJ/$FZ/trial_01" 24 $(( 20261009 + SLURM_ARRAY_TASK_ID )) "$EXTRA"
