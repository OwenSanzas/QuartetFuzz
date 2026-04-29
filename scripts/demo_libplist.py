#!/usr/bin/env python3
"""Driver for the libplist end-to-end demo.

Loads the five LGs in ``demo/libplist/lg_*.json`` (already produced by a
prior LG-stage run) and feeds each one through P2 + harness generation,
mapping each LG to a fixed OSS-Fuzz fuzzer slot so that the agent's
source actually overwrites a slot known to ``build.sh`` and gets
compiled.  libplist ships exactly four fuzzer slots
(bplist / jplist / oplist / xplist), so the fifth LG reuses the bplist
slot — only the binary is shared, the lg_5/ artifact directory is
distinct from lg_1/.

Two batches:
  * batch 1: lg_1..lg_4 in parallel, one per slot
  * batch 2: lg_5 alone, mapped to bplist_fuzzer (slot reuse)

Per-LG artifacts written under ``demo/libplist/lg_<N>/``:
  - lg_analysis.json (the LG itself, copied from demo/libplist/lg_<N>.json)
  - p2_report.md
  - fuzzer.cc

The trajectory.jsonl files for both the P2 agent and the SystemCheck
agent land under each worker's ``run-<id>/libplist_lg_<N>/`` directory
(kept by the .gitignore exception for ``demo/libplist/run-*/``).
"""
import argparse
import json
import multiprocessing as mp
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from harness.checker.logic_group import LogicGroup  # noqa: E402
from harness.checker.pipeline import run_harness_for_lg  # noqa: E402

DEMO_DIR = REPO_ROOT / "demo" / "libplist"
REPO_URL = "https://github.com/libimobiledevice/libplist"

# Mapping: lg_N → libplist OSS-Fuzz fuzzer slot.
SLOT_MAP = {
    1: "bplist_fuzzer",
    2: "jplist_fuzzer",
    3: "oplist_fuzzer",
    4: "xplist_fuzzer",
    5: "bplist_fuzzer",  # slot reuse — libplist only has 4 distinct slots
}


def run_one(lg_n: int) -> tuple[int, bool, str]:
    """Worker entry: process a single LG."""
    lg_json = DEMO_DIR / f"lg_{lg_n}.json"
    if not lg_json.exists():
        return (lg_n, False, f"missing {lg_json}")
    lg = LogicGroup.load_json(lg_json)
    slot = SLOT_MAP[lg_n]
    try:
        result = run_harness_for_lg(
            lg,
            lg_index=lg_n,
            repo_url=REPO_URL,
            output_dir=DEMO_DIR,
            enable_dynamic=True,
            top_k_entries=3,
            gold_case_id=f"libplist/{slot}",
        )
        # Copy lg_N.json into lg_N/ as lg_analysis.json so the demo dir is
        # self-contained per LG (matches the layout of the earlier main-branch
        # demo).
        lg_dir = DEMO_DIR / f"lg_{lg_n}"
        lg_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(lg_json, lg_dir / "lg_analysis.json")
        return (lg_n, bool(result.success), str(getattr(result, "failure_reason", "") or "ok"))
    except Exception as exc:
        return (lg_n, False, f"exception: {exc}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--batch", choices=["1", "2", "all"], default="all",
        help="Which batch to run: 1 (lg_1..lg_4 parallel), "
             "2 (lg_5 alone), or all.",
    )
    args = ap.parse_args()

    if args.batch in ("1", "all"):
        print("=== batch 1: lg_1..lg_4 (parallel) ===", flush=True)
        with mp.Pool(processes=4) as pool:
            for n, ok, msg in pool.imap_unordered(run_one, [1, 2, 3, 4]):
                print(f"  lg_{n}: {'OK' if ok else 'FAIL'}  {msg}", flush=True)

    if args.batch in ("2", "all"):
        print("=== batch 2: lg_5 (slot reuse) ===", flush=True)
        n, ok, msg = run_one(5)
        print(f"  lg_{n}: {'OK' if ok else 'FAIL'}  {msg}", flush=True)


if __name__ == "__main__":
    main()
