#!/usr/bin/env python3
"""Reference coverage of one fuzzing run (paper, Reference Coverage section).

    python3 benchmark/tools/refcov.py <task> <run_covsets.json.gz>

<run_covsets.json.gz> is the covered-entity set of the run's final corpus,
replayed on the coverage build of the run's harness with
benchmark/tools/cov_sets.sh.  The reference set R is
benchmark/<task>/gold/reference_covsets.json.gz: the lines and branches the
gold harness covers when it replays its entire official OSS-Fuzz corpus.
Harness code is excluded from both sides.

    RefCov^k = |G^k ∩ R^k| / |R^k| * 100,  k in {line, branch}
"""
import gzip
import json
import sys
from pathlib import Path

BENCH = Path(__file__).resolve().parent.parent
# The libyaml gold binary was built from a parser.c with 11 extra lines
# (167-177); shift its line keys onto the pinned source.
GOLD_LINE_MAP = {"libyaml": ("/src/libyaml/src/parser.c:", 167, 177, -11)}


def remap(keys, project):
    if project not in GOLD_LINE_MAP:
        return keys
    pre, first, last, shift = GOLD_LINE_MAP[project]
    out = set()
    for k in keys:
        if not k.startswith(pre):
            out.add(k)
            continue
        line, *rest = k[len(pre):].split(":")
        line = int(line)
        if first <= line <= last:
            continue
        out.add(pre + ":".join([str(line + shift if line > last else line)] + rest))
    return out


def covered(path, harness, project=None):
    d = json.load(gzip.open(path))
    keep = lambda k: not k.startswith(harness + ":") and "fuzz_driver" not in k
    return (remap({k for k in d["covered"]["lines"] if keep(k)}, project),
            remap({k for k in d["covered"]["branches"] if keep(k)}, project))


def main():
    task, run = sys.argv[1], sys.argv[2]
    case = json.loads((BENCH / task / "case.json").read_text())
    harness = case["gold_source_file"]
    R_l, R_b = covered(BENCH / task / "gold" / "reference_covsets.json.gz", harness, case["project"])
    G_l, G_b = covered(run, harness)
    print(json.dumps({
        "task": task,
        "reference_line_coverage": 100 * len(G_l & R_l) / max(1, len(R_l)),
        "reference_branch_coverage": 100 * len(G_b & R_b) / max(1, len(R_b)),
        "reference_lines": len(R_l), "reference_branches": len(R_b),
    }, indent=1))


if __name__ == "__main__":
    main()
