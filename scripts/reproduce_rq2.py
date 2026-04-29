"""RQ2 reproduction driver — LG-only on the 100-case prompt dataset.

For every case in dataset/benchmark_cases_with_prompts.jsonl this
script runs Stage 1 (the Logic-Group agent) once and records whether
the gold target_function appears in the agent's selected entry set.
The agent does NOT generate or build a harness — RQ2 measures
LG-stage entry-selection quality, not coverage.

Outputs per case at ``--output-dir/<case_id_safe>/``:
    lg_1.json          The selected Logic Group.
    all_lgs.json       Every LG the agent considered.
    rq2_match.json     {gold_target, lg_entries, direct_match,
                        wrapper_match, overall_match}.

The aggregate match rates printed at the end correspond to paper
Table~6 (RQ2): LG-match (gold target ∈ LG entry set) and Harness-match
(gold target invoked in the first-draft harness). This script reports
LG-match only; harness-match requires the full pipeline run.

Usage:
    export GEMINI_API_KEY=AI...
    python3 scripts/reproduce_rq2.py \\
        --dataset dataset/benchmark_cases_with_prompts.jsonl \\
        --output-dir /tmp/qf-rq2 \\
        --parallel 4
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from harness.checker.pipeline import run_lg_stage  # noqa: E402
from harness.checker.static import StaticIndex  # noqa: E402


def _basename(qualified: str) -> str:
    """Strip C++ namespace prefixes and return the final identifier."""
    s = qualified.replace("(", " ").split()[0]
    if "::" in s:
        s = s.split("::")[-1]
    return s.strip()


def run_one_case(case: dict, output_root: Path, num_lgs: int, top_k: int) -> dict:
    safe = case["case_id"].replace("/", "_")
    case_dir = output_root / safe
    case_dir.mkdir(parents=True, exist_ok=True)

    t_start = time.time()
    try:
        lgs = run_lg_stage(
            project=case["project"],
            prompt=case["prompt"],
            repo_url=case.get("repo_url"),
            output_dir=case_dir,
            num_lgs=num_lgs,
            top_k=top_k,
        )
    except Exception as exc:
        return {
            "case_id": case["case_id"],
            "ok": False,
            "error": str(exc)[:300],
            "wall_seconds": round(time.time() - t_start, 1),
        }

    # gold target match logic (paper §5.3 RQ2 definition).
    gold = case.get("target_function", "")
    gold_bn = _basename(gold)

    selected = lgs[0] if lgs else None
    entries = list(selected.entries) if selected else []
    direct = any(_basename(e) == gold_bn for e in entries)

    # Wrapper match: any entry's single-hop callee in the project call
    # graph equals the gold target. We use StaticIndex for the lookup;
    # if the index is unavailable for this project, we record direct
    # match only.
    wrapper = False
    try:
        idx = StaticIndex.load(case["project"])
        for e in entries:
            for callee in idx.get_callees(e) or []:
                if _basename(callee) == gold_bn:
                    wrapper = True
                    break
            if wrapper:
                break
    except Exception:
        pass

    out = {
        "case_id": case["case_id"],
        "gold_target": gold,
        "lg_entries": entries,
        "direct_match": direct,
        "wrapper_match": wrapper,
        "overall_match": direct or wrapper,
        "wall_seconds": round(time.time() - t_start, 1),
        "ok": True,
    }
    (case_dir / "rq2_match.json").write_text(json.dumps(out, indent=2))
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--dataset", type=Path, required=True)
    p.add_argument("--output-dir", type=Path, required=True)
    p.add_argument("--num-lgs", type=int, default=1)
    p.add_argument("--top-k", type=int, default=1)
    p.add_argument("--parallel", type=int, default=4)
    p.add_argument("--limit", type=int, default=0,
                   help="Run only the first N cases (0 = all).")
    args = p.parse_args()

    cases = [json.loads(l) for l in args.dataset.open() if l.strip()]
    if args.limit > 0:
        cases = cases[: args.limit]
    args.output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Loaded {len(cases)} cases from {args.dataset}")

    results = []
    with ProcessPoolExecutor(max_workers=args.parallel) as pool:
        futures = {
            pool.submit(run_one_case, c, args.output_dir, args.num_lgs, args.top_k): c
            for c in cases
        }
        for fut in as_completed(futures):
            r = fut.result()
            results.append(r)
            tag = "OK" if r.get("ok") else "ERR"
            match = r.get("overall_match")
            print(f"[{tag}] {r['case_id']:50s} match={match}")

    # Aggregate, split by prompt style (60 w/-target vs 40 w/o).
    def _has_target(c):
        return c.get("target_function", "") in c.get("prompt", "")

    cases_by_id = {c["case_id"]: c for c in cases}
    w_target = [r for r in results if r.get("ok") and _has_target(cases_by_id[r["case_id"]])]
    wo_target = [r for r in results if r.get("ok") and not _has_target(cases_by_id[r["case_id"]])]

    print()
    print(f"  w/ target: {sum(1 for r in w_target if r['overall_match'])}/{len(w_target)} match")
    print(f"  w/o target: {sum(1 for r in wo_target if r['overall_match'])}/{len(wo_target)} match")
    print(f"  All:       {sum(1 for r in results if r.get('ok') and r['overall_match'])}/{len(results)} match")

    summary_path = args.output_dir / "rq2_summary.json"
    summary_path.write_text(json.dumps({
        "total": len(results),
        "w_target_total": len(w_target),
        "w_target_match": sum(1 for r in w_target if r["overall_match"]),
        "wo_target_total": len(wo_target),
        "wo_target_match": sum(1 for r in wo_target if r["overall_match"]),
        "results": results,
    }, indent=2))
    print(f"Summary -> {summary_path}")


if __name__ == "__main__":
    main()
