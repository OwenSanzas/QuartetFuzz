#!/usr/bin/env python3
"""Lay out a delivered harness for the benchmark's trial and coverage tools.

    python3 scripts/make_trial_dir.py <task> <final dir> <out dir>

<final dir> is work/final/<run-id>/<task> from reproduce.sh (final_asan/out,
final_cov/out).  <out dir>/<fuzzer> becomes the case directory
that benchmark/tools/fuzz_trial.sh, cov_sets.sh and snap_cov.sh take (they
read the fuzzer name from the directory name):

    <out dir>/<fuzzer>/asan_build/<fuzzer>_asan   (+ lib/, llvm-symbolizer)
                       cov_build/<fuzzer>_cov     (+ lib/)
                       report/coverage.json       the task's coverage scope
                       report/_scope.py

Every system is measured with the reference harness's scope (the project's
own sources), as in the paper.
"""
import json
import shutil
import sys
from pathlib import Path

BENCH = Path(__file__).resolve().parent.parent / "benchmark"


def put(binary: Path, out_dir: Path, dst: Path, name: str) -> None:
    dst.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, dst / name)
    for extra in ("lib", "llvm-symbolizer"):
        s = out_dir / extra
        if s.is_dir():
            shutil.copytree(s, dst / extra, dirs_exist_ok=True)
        elif s.is_file():
            shutil.copy2(s, dst / extra)


def main() -> int:
    task, final, out = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
    case = json.loads((BENCH / task / "case.json").read_text())
    fz = case["fuzzer_name"]
    a, c = final / "final_asan" / "out", final / "final_cov" / "out"
    if not ((a / fz).is_file() and (c / fz).is_file()):
        sys.exit(f"{final}: no ASan or coverage binary named {fz}")
    out = out / fz
    put(a / fz, a, out / "asan_build", f"{fz}_asan")
    put(c / fz, c, out / "cov_build", f"{fz}_cov")
    (out / "report").mkdir(parents=True, exist_ok=True)
    scope = json.loads((BENCH / task / "gold" / "report" / "coverage.json").read_text())["scope"]
    (out / "report" / "coverage.json").write_text(json.dumps({"scope": scope}) + "\n")
    shutil.copy2(BENCH / "tools" / "templates" / "_scope.py", out / "report" / "_scope.py")
    print(f"{task}: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
