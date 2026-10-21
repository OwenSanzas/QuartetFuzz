from __future__ import annotations

import json
import os
import logging
import re
import time
import shutil
import subprocess
from pathlib import Path

from rich.console import Console

from infra.ossfuzz.models import CoverageMetrics, FuzzerRunResult
from infra.ossfuzz.subprocess_utils import run_command_with_streaming
from infra.ossfuzz.workspace import OssFuzzPaths, oss_fuzz_subprocess_env

logger = logging.getLogger(__name__)

_LIBFUZZER_STATS_RE = re.compile(r"cov:\s*(\d+)\s+ft:\s*(\d+).*?exec/s:\s*(\d+).*?rss:\s*(\d+)")


def generate_corpus(
    oss_fuzz_dir: Path,
    project: str,
    fuzz_target: str,
    duration: int = 60,
    corpus_name: str | None = None,
    stream_to: Console | None = None,
    stream_prefix: str = "",
) -> FuzzerRunResult:
    """Run a fuzzer in Docker for *duration* seconds to generate a corpus.

    Mirrors the evaluation runner (dataset_v2/tools/fuzz_trial.sh, ClusterFuzz
    behaviour): libFuzzer flags -rss_limit_mb=2560 -timeout=25, and a crash,
    leak, OOM or timeout restarts the fuzzer on the kept corpus until the
    budget is spent.  The case's extra ASan options (QF_EXTRA_ASAN, from
    dataset_v2/tools/asan_overrides.json) apply as in the evaluation.

    Args:
        corpus_name: Override corpus directory name. Defaults to fuzz_target.
            Use this to keep gold and generated corpora separate when the
            binary name is the same (swap-and-rebuild approach).
    """
    paths = OssFuzzPaths(oss_fuzz_dir)
    corpus_dir = paths.corpus_dir(project, corpus_name or fuzz_target)
    if corpus_dir.exists():
        shutil.rmtree(corpus_dir, ignore_errors=True)
    corpus_dir.mkdir(parents=True, exist_ok=True)
    extra_asan = os.environ.get("QF_EXTRA_ASAN", "")

    stderr_lines: list[str] = []
    crashes: list[str] = []      # one excerpt per run that ended early
    error: str | None = None
    start = time.monotonic()
    total_execs = 0
    runs = 0

    while True:
        remain = int(duration - (time.monotonic() - start))
        if remain < 5 or runs >= 200:
            break
        runs += 1
        cmd = ["docker", "run", "--rm",
               "-v", f"{paths.build_out(project)}:/out",
               "-v", f"{corpus_dir}:/corpus"]
        if extra_asan:
            cmd += ["-e", f"ASAN_OPTIONS={extra_asan}"]
        cmd += [f"gcr.io/oss-fuzz/{project}", f"/out/{fuzz_target}", "/corpus",
                f"-max_total_time={remain}", "-rss_limit_mb=2560", "-timeout=25",
                f"-seed={runs}"]
        run_lines: list[str] = []
        try:
            proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                    text=True, errors="replace")
            assert proc.stderr is not None
            for raw_line in proc.stderr:
                line = raw_line.rstrip()
                run_lines.append(line)
                if stream_to and line:
                    stream_to.print(f"         [dim]{stream_prefix}{line}[/dim]")
            proc.wait(timeout=remain + 120)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            error = f"Fuzzer timed out after {remain + 120}s"
            break
        except Exception as exc:
            error = f"Fuzzer error: {exc}"
            break
        stderr_lines = run_lines
        total_execs += _parse_libfuzzer_stats(run_lines).get("execs", 0)
        excerpt = _crash_excerpt(run_lines)
        if excerpt:
            crashes.append(excerpt)
        elif proc.returncode != 0:
            crashes.append(f"Fuzzer exited with code {proc.returncode}\n" + "\n".join(run_lines[-15:]))
        else:
            break  # ran to the end of its budget
        if runs == 1 and count_corpus(corpus_dir) == 0 and total_execs == 0:
            error = f"Fuzzer exited with code {proc.returncode} before running any input"
            break

    stats = _parse_libfuzzer_stats(stderr_lines)
    elapsed = time.monotonic() - start
    return FuzzerRunResult(
        corpus_dir=str(corpus_dir),
        corpus_size=count_corpus(corpus_dir),
        edges_covered=stats.get("cov", 0),
        features=stats.get("ft", 0),
        total_executions=total_execs,
        exec_per_second=stats.get("exec_s", 0),
        peak_rss_mb=stats.get("rss", 0),
        elapsed_seconds=elapsed,
        error=error,
        crash=_summarize_crashes(crashes),
        restarts=max(0, runs - 1) if crashes else 0,
    )


def _summarize_crashes(crashes: list[str]) -> str | None:
    """The first report of each distinct crash (by its SUMMARY line), with a count."""
    if not crashes:
        return None
    seen: dict[str, list] = {}
    for c in crashes:
        key = next((l for l in c.splitlines() if l.startswith("SUMMARY:")), c.splitlines()[0])
        seen.setdefault(key, [0, c])[0] += 1
    parts = [f"[{n}x] {key}\n{c}" for key, (n, c) in list(seen.items())[:3]]
    if len(seen) > 3:
        parts.append(f"... and {len(seen) - 3} more distinct crashes")
    return "\n\n".join(parts)


def _crash_excerpt(stderr_lines: list[str], max_lines: int = 25) -> str | None:
    """The report libFuzzer / a sanitizer printed when it stopped the run
    (first ERROR/deadly-signal line through the SUMMARY line), or None."""
    start = next((i for i, l in enumerate(stderr_lines)
                  if re.search(r"==\d+==\s*ERROR:|deadly signal|ERROR: libFuzzer|"
                               r"out-of-memory|timeout after", l)), None)
    if start is None:
        return None
    lines = stderr_lines[start:start + max_lines]
    summary = next((l for l in stderr_lines[start:] if l.startswith("SUMMARY:")), None)
    if summary and summary not in lines:
        lines += ["...", summary]
    return "\n".join(lines)


def _parse_libfuzzer_stats(stderr_lines: list[str]) -> dict[str, int]:
    """Extract final coverage stats from libFuzzer stderr output.

    Scans backwards for the last line containing cov/ft/exec_s fields.
    libFuzzer format:
        #1000  DONE   cov: 142 ft: 387 corp: 555/28Kb exec/s: 1250 rss: 45Mb
    """
    for line in reversed(stderr_lines):
        match = _LIBFUZZER_STATS_RE.search(line)
        if match:
            execs_match = re.search(r"#(\d+)", line)
            return {
                "cov": int(match.group(1)),
                "ft": int(match.group(2)),
                "exec_s": int(match.group(3)),
                "rss": int(match.group(4)),
                "execs": int(execs_match.group(1)) if execs_match else 0,
            }
    return {}


def collect_coverage(
    oss_fuzz_dir: Path,
    project: str,
    fuzz_target: str,
    stream_to: Console | None = None,
    stream_prefix: str = "",
) -> CoverageMetrics:
    """Collect llvm-cov coverage for a fuzz target."""
    paths = OssFuzzPaths(oss_fuzz_dir)
    cmd = [
        "python3",
        str(paths.helper_py),
        "coverage",
        "--no-serve",
        "--no-corpus-download",
        "--fuzz-target",
        fuzz_target,
        project,
    ]

    result = run_command_with_streaming(
        cmd,
        cwd=oss_fuzz_dir,
        timeout=300,
        env=oss_fuzz_subprocess_env(oss_fuzz_dir),
        stream_to=stream_to,
        stream_prefix=stream_prefix,
    )
    if result.timed_out:
        return CoverageMetrics(error="Coverage collection timed out")

    # The oss-fuzz coverage helper sometimes exits non-zero even after producing
    # a valid summary.json (e.g. HTML report-generation step fails on a sibling
    # fuzzer while llvm-cov export already succeeded). Try the summary first;
    # only surface the rc!=0 error if no parseable summary exists.
    metrics = parse_coverage_summary(oss_fuzz_dir, project, fuzz_target)
    if not metrics.error:
        return metrics
    if result.returncode != 0:
        return CoverageMetrics(error=f"Coverage failed (exit {result.returncode}): {result.combined_output[-4000:]}")
    return metrics


def parse_coverage_summary(
    oss_fuzz_dir: Path,
    project: str,
    fuzz_target: str,
) -> CoverageMetrics:
    """Parse the llvm-cov summary.json produced by oss-fuzz coverage collection.

    Primary path is ``report_target/<fuzz_target>/linux/summary.json`` (the
    per-target report). For some projects the per-target step fails inside
    helper.py while the overall ``report/linux/summary.json`` still gets
    written. Since each ``collect_coverage`` call invokes helper.py with a
    single ``--fuzz-target`` and parses immediately after the helper exits,
    the overall report at that moment reflects this single target.
    """
    paths = OssFuzzPaths(oss_fuzz_dir)
    primary = paths.coverage_summary(project, fuzz_target)
    fallback = paths.build_out(project) / "report" / "linux" / "summary.json"
    candidates = [p for p in (primary, fallback) if p.is_file()]
    if not candidates:
        return CoverageMetrics(error=f"Summary not found: {primary}")
    try:
        data = json.loads(candidates[0].read_text())
    except (json.JSONDecodeError, KeyError) as exc:
        return CoverageMetrics(error=f"Failed to parse summary: {exc}")

    return _extract_metrics_from_summary(data)


def _extract_metrics_from_summary(data: dict) -> CoverageMetrics:
    """Extract CoverageMetrics from llvm-cov export JSON structure."""
    try:
        totals = data["data"][0]["totals"]
    except (KeyError, IndexError) as exc:
        return CoverageMetrics(error=f"Unexpected summary structure: {exc}")

    return CoverageMetrics(
        lines_covered=totals["lines"]["covered"],
        lines_total=totals["lines"]["count"],
        lines_pct=totals["lines"]["percent"],
        branches_covered=totals["branches"]["covered"],
        branches_total=totals["branches"]["count"],
        branches_pct=totals["branches"]["percent"],
        functions_covered=totals["functions"]["covered"],
        functions_total=totals["functions"]["count"],
        functions_pct=totals["functions"]["percent"],
        regions_covered=totals["regions"]["covered"],
        regions_total=totals["regions"]["count"],
        regions_pct=totals["regions"]["percent"],
    )


def count_corpus(corpus_dir: Path) -> int:
    if not corpus_dir.is_dir():
        return 0
    return sum(1 for entry in corpus_dir.iterdir() if entry.is_file())
