#!/usr/bin/env python3
"""Prepare benchmark cases for scripts/run_qf_case.py.

    python3 scripts/setup_case.py zlib-c1 [more cases | all] [--jobs N]

Everything goes to $QF_WORK (default: work/ in the repository).  Per case:

  1. qf-case/<case>:20261017   the case image, built from benchmark/<case>/image:
                               upstream OSS-Fuzz project at OSS-Fuzz 955c686,
                               every source repository pinned (qf/pins.txt),
                               the gold harness replaced by a stub and removed
                               from git history.  Same image the baselines used.
  2. qf-gdb/<case>:20261017    the case image plus gdb, used by the AP probes.
  3. src/<case>/               /src exported from the case image (read-only):
                               the source tree the agent reads.
  4. ossfuzz_view/<case>/      the OSS-Fuzz project files the agent may read
                               (gold copies replaced by the stub).
  5. oss-fuzz/                 google/oss-fuzz at 955c686 plus the build
                               project qf-<case> (benchmark/<case>/qf-project).
  6. callgraphs/               the case's call graph (benchmark/callgraphs).
  7. lg/<case id>/             the case's Logic Group and P2 report
                               (benchmark/<case>/lg.json, p2_report.md); the
                               full system reuses them instead of generating
                               new ones (pass --fresh-lg to generate new ones).
  8. cases.json                case index read by run_qf_case.py.

Steps already done are skipped, so the script can be re-run.
"""

from __future__ import annotations

import argparse
import json
import lzma
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BENCH = REPO / "benchmark"
WORK = Path(os.environ.get("QF_WORK", REPO / "work")).resolve()
TAG = "20261017"
OSS_FUZZ_URL = "https://github.com/google/oss-fuzz.git"
OSS_FUZZ_REV = "955c6866c152471d78c7472c0f1f354343548b9f"


def sh(cmd: list[str] | str, **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, shell=isinstance(cmd, str), text=True, capture_output=True, **kw)


def have_image(name: str) -> bool:
    return sh(["docker", "image", "inspect", name]).returncode == 0


def setup_oss_fuzz() -> None:
    d = WORK / "oss-fuzz"
    if (d / "infra" / "helper.py").exists():
        return
    print(f"cloning OSS-Fuzz {OSS_FUZZ_REV[:7]} -> {d}", flush=True)
    for cmd in (["git", "clone", "-q", OSS_FUZZ_URL, str(d)],
                ["git", "-C", str(d), "checkout", "-q", OSS_FUZZ_REV]):
        r = sh(cmd)
        if r.returncode:
            sys.exit(f"{' '.join(cmd)} failed:\n{r.stderr}")


def add_qf_projects(names: list[str]) -> None:
    """Copy qf-<case> build projects into OSS-Fuzz and commit them, so the
    builder's pinned-commit check sees a clean tree."""
    d = WORK / "oss-fuzz"
    for c in names:
        dst = d / "projects" / f"qf-{c}"
        shutil.rmtree(dst, ignore_errors=True)
        shutil.copytree(BENCH / c / "qf-project", dst)
    sh(["git", "-C", str(d), "add", "projects"])
    if sh(["git", "-C", str(d), "diff", "--cached", "--quiet"]).returncode:
        sh(["git", "-C", str(d), "-c", "user.name=qf", "-c", "user.email=qf@local",
            "commit", "-q", "-m", "QuartetFuzz per-case build projects"])


def one(c: str, fresh_lg: bool) -> str:
    case = json.loads((BENCH / c / "case.json").read_text())
    img, gdb = f"qf-case/{c}:{TAG}", f"qf-gdb/{c}:{TAG}"
    log = WORK / "logs"
    log.mkdir(parents=True, exist_ok=True)

    if not have_image(img):
        r = sh(["docker", "build", "-t", img, str(BENCH / c / "image")])
        (log / f"image-{c}.log").write_text(r.stdout + r.stderr)
        if r.returncode:
            return f"{c}: image build FAILED (see {log}/image-{c}.log)"
    if not have_image(gdb):
        df = (f"FROM {img}\nRUN apt-get update -q && DEBIAN_FRONTEND=noninteractive "
              "apt-get install -y -q --no-install-recommends gdb && rm -rf /var/lib/apt/lists/*\n")
        r = sh(["docker", "build", "-q", "-t", gdb, "-"], input=df)
        if r.returncode:
            return f"{c}: gdb image FAILED\n{r.stderr[-500:]}"

    src = WORK / "src" / c
    if not Path(f"{src}.exported").exists():
        if src.exists():
            sh(["chmod", "-R", "u+w", str(src)])
            shutil.rmtree(src)
        src.mkdir(parents=True)
        cid = sh(["docker", "create", img]).stdout.strip()
        try:
            r = sh(f"docker cp {cid}:/src - | tar -x -C {src} --strip-components=1")
        finally:
            sh(["docker", "rm", cid])
        if r.returncode:
            return f"{c}: exporting /src FAILED\n{r.stderr[-500:]}"
        sh(["chmod", "-R", "a-w", str(src)])
        Path(f"{src}.exported").write_text(img + "\n")

    view = WORK / "ossfuzz_view" / c
    if not view.exists():
        shutil.copytree(BENCH / c / "image", view, ignore=shutil.ignore_patterns("qf", "qf_scrub_git.sh"))
        df = (view / "Dockerfile").read_text()
        (view / "Dockerfile").write_text(df.split("\n# --- QuartetFuzz baseline setup")[0].rstrip() + "\n")
        stub = (BENCH / c / "image" / "qf" / "gold_stub.c").read_bytes()
        gold = Path(case["gold_harness_path"]).name
        for f in view.rglob(gold):
            f.write_bytes(stub)

    cg = WORK / "callgraphs"
    cg.mkdir(parents=True, exist_ok=True)
    shutil.copy(BENCH / "callgraphs" / "index.json", cg / "index.json")
    xz = BENCH / case["callgraph"]
    out = cg / xz.name.removesuffix(".xz")
    if not out.exists():
        with lzma.open(xz) as fi, open(f"{out}.tmp", "wb") as fo:
            shutil.copyfileobj(fi, fo)
        os.replace(f"{out}.tmp", out)

    lg = WORK / "lg" / case["case_id"].replace("/", "_")
    lg.mkdir(parents=True, exist_ok=True)
    if not fresh_lg:
        for f in ("lg.json", "p2_report.md"):
            if not (lg / f).exists():
                text = (BENCH / c / f).read_text().replace("${QF_WORK}", str(WORK))
                (lg / f).write_text(text)
    return f"{c}: ready"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="+", help="case names (e.g. zlib-c1) or 'all'")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--fresh-lg", action="store_true",
                    help="do not seed the released LG / P2 report (the system generates new ones)")
    args = ap.parse_args()
    allc = sorted(p.name for p in BENCH.iterdir() if (p / "case.json").exists())
    names = allc if args.cases == ["all"] else args.cases
    bad = [c for c in names if c not in allc]
    if bad:
        sys.exit(f"unknown case(s): {' '.join(bad)}  (see benchmark/cases.tsv)")
    WORK.mkdir(parents=True, exist_ok=True)

    index = WORK / "cases.json"
    cases = json.loads(index.read_text()) if index.exists() else {}
    for c in names:
        k = json.loads((BENCH / c / "case.json").read_text())
        cases[k["case_id"]] = {
            "oss_fuzz_project": c, "project": k["project"], "fuzzer_name": k["fuzzer_name"],
            "gold_path": k["gold_harness_path"], "language": k["language"],
            "target_function": k["target_function"], "target_declaration": k["target_declaration"],
        }
    index.write_text(json.dumps(cases, indent=1) + "\n")

    setup_oss_fuzz()
    add_qf_projects(names)
    with ThreadPoolExecutor(args.jobs) as ex:
        results = list(ex.map(lambda c: one(c, args.fresh_lg), names))
    for r in results:
        print(r)
    return 0 if all(r.endswith("ready") for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
