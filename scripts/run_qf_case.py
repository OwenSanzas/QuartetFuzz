#!/usr/bin/env python3
"""Run QuartetFuzz on one dataset_v2 case and deliver its harness.

    python scripts/run_qf_case.py zlib-c1 [--out DIR] [--dry-run]

The case is named by its per-case OSS-Fuzz project (``<project>-cN``, see
``qf-work/cases.json``).  Everything comes from ``../qf-work`` (prepared
once, shared by every case):

    src/<p>-cN/             /src exported from qf-case/<p>-cN:20261017
                            (gold stubbed, git history scrubbed), read-only
    ossfuzz_view/<p>-cN/    upstream projects/<project> at the pinned commit,
                            gold copies replaced by the stub
    oss-fuzz/               OSS-Fuzz at QF_OSS_FUZZ_COMMIT with
                            projects/qf-<p>-cN = FROM qf-case/<p>-cN:20261017
                            (helper.py tags the build image
                            gcr.io/oss-fuzz/<project>; the qf- prefix keeps
                            it off the case image the baselines use)
    qf-gdb/<p>-cN image     the case image plus gdb, for probes

Input is the same as the baselines got: the target function (OFG's
function-level input) with the case's prompt.  The harness replaces the
gold source at its path in the image, so the build produces the case's
fuzzer name.  Output: ``<out>/<run-id>/<case>/`` with the harness,
``delivery.json``, ``result.json``, the trajectory and the binaries the
agent built.
"""

from __future__ import annotations

import argparse
import json
import re
import os
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO))

QF_WORK = REPO.parent / "qf-work"
QF_OSS_FUZZ_COMMIT = "d0ded9cef64f3e93f8f701ee3a4f1c32e8a695d1"  # bafc300 + libidn2 version fix (only qf-libidn2-c1/c2 differ)
IMAGE_TAG = "20261017"
DEFAULT_OUT = REPO.parent / "qf-runs"


def load_case(name: str) -> dict:
    cases = json.loads((QF_WORK / "cases.json").read_text())
    by_project = {c["oss_fuzz_project"]: dict(c, case_id=cid) for cid, c in cases.items()}
    if name in by_project:
        case = by_project[name]
    elif name in cases:
        case = dict(cases[name], case_id=name)
    else:
        raise SystemExit(f"unknown case {name!r}")
    bench = {
        json.loads(l)["case_id"]: json.loads(l)
        for l in (REPO / "dataset_v2" / "benchmark_100.jsonl").read_text().splitlines()
        if l.strip()
    }
    case["bench"] = bench[case["case_id"]]
    # The evaluation's per-case ASan options; get_coverage's fuzz run applies them too.
    overrides = json.loads((REPO / "dataset_v2" / "tools" / "asan_overrides.json").read_text())
    os.environ["QF_EXTRA_ASAN"] = overrides.get(case["case_id"], "")
    return case


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("case", help="per-case project, e.g. zlib-c1 (or the dataset case id)")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--run-id", default="")
    ap.add_argument("--dry-run", action="store_true", help="check inputs, no LLM / docker")
    args = ap.parse_args()

    case = load_case(args.case)
    oss_project = case["oss_fuzz_project"]
    build_project = f"qf-{oss_project}"
    bench = case["bench"]
    src_root = QF_WORK / "src" / oss_project
    upstream = bench["version_pins"]["upstream_path"]  # e.g. /src/zlib
    project_path = src_root / upstream.removeprefix("/src/")
    view = QF_WORK / "ossfuzz_view" / oss_project
    gold_paths = json.loads((REPO / "dataset_v2" / "gold_source_paths.json").read_text())
    container_path = gold_paths[case["case_id"]]
    runtime_image = f"qf-gdb/{oss_project}:{IMAGE_TAG}"

    plan = {
        "case_id": case["case_id"],
        "oss_fuzz_project": oss_project,
        "target_function": case["target_function"],
        "fuzzer_name": case["fuzzer_name"],
        "language": case["language"],
        "project_path": str(project_path),
        "extra_roots": [str(src_root)],
        "oss_fuzz_project_dir": str(view),
        "oss_fuzz": f"{QF_WORK / 'oss-fuzz'} @ {QF_OSS_FUZZ_COMMIT}, project {build_project}",
        "container_path": container_path,
        "runtime_image": runtime_image,
    }
    print(json.dumps(plan, indent=2))

    problems = []
    for p in (project_path, src_root, view, QF_WORK / "oss-fuzz" / "projects" / build_project):
        if not p.is_dir():
            problems.append(f"missing {p}")
    if os.system(f"docker image inspect {runtime_image} >/dev/null 2>&1") != 0:
        problems.append(f"missing image {runtime_image}")
    if not os.environ.get("ANTHROPIC_API_KEY") and not args.dry_run:
        problems.append("ANTHROPIC_API_KEY is not set")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 2
    if args.dry_run:
        print("dry run ok")
        return 0

    # "Declared in: cjson/cJSON.h:158" in the benchmark prompt, relative to /src.
    m = re.search(r"^Declared in: (\S+)$", bench.get("prompt", ""), re.M)
    target_location = str(src_root / m.group(1)) if m else ""

    from harness.checker.flow import SystemCheckCase, run_system_check

    sc = SystemCheckCase(
        project=case["project"],
        target_function=case["target_function"],
        target_declaration=bench.get("target_declaration", ""),
        target_location=target_location,
        functionality_prompt=bench.get("prompt", ""),
        case_id=case["case_id"],
        fuzzer_name=case["fuzzer_name"],
        # The harness takes the gold file's place, so its language is that file's.
        language="c" if Path(container_path).suffix == ".c" else "cpp",
        gold_source_file=Path(container_path).name,
    )
    result = run_system_check(
        sc,
        output_root=args.out,
        # QF_PREPARE_ONLY runs only the LG and P2 stages: nothing is built.
        enable_dynamic=not os.environ.get("QF_PREPARE_ONLY"),
        run_id=args.run_id or f"qf-{time.strftime('%Y%m%d-%H%M%S')}",
        source_oss_fuzz_dir=QF_WORK / "oss-fuzz",
        oss_fuzz_project_dir=view,
        project_path=project_path,
        extra_roots=[src_root],
        build_project=build_project,
        pinned_commit=QF_OSS_FUZZ_COMMIT,
        container_path=container_path,
        runtime_image=runtime_image,
    )
    print(json.dumps({
        "case_id": result.case_id,
        "success": result.success,
        "harness_path": result.harness_path,
        "asan_binary": result.harness_binary_path,
        "cov_binary": result.coverage_binary_path,
        "turns": result.total_turns,
        "cost_usd": round(result.estimated_cost, 2),
        "error": result.error,
    }, indent=2))
    return 0 if result.success else 1


if __name__ == "__main__":
    raise SystemExit(main())
