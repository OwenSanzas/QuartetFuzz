"""Three-stage pipeline: LG → P2 → Harness.

This module orchestrates the full AGF pipeline described in the paper:

    Stage 1 (LG Discovery):
        LogicGroupAgent runs multiple rounds, each producing one Logic Group.
        Previously generated LGs and existing project fuzzers are passed as
        context to avoid duplication.  After all rounds, entries are ranked
        by danger score and the top-K LGs are selected.

    Stage 2 (API Research):
        P2Agent investigates the entry functions of each selected LG and
        produces a structured P2 protocol report (P2.1-P2.8).

    Stage 3 (Harness Generation):
        SystemCheckAgent (existing, in flow.py) generates and refines
        a harness for each LG using the P2 report as guidance.

The pipeline can be run as:
    - Part 1 only: project → multiple LGs with danger scores
    - Part 2 only: LG → P2 → harness (for each LG)
    - Full: project → LGs → P2 → harnesses

Usage::

    # Full pipeline: generate 5 harnesses for a project
    python -m harness.checker.pipeline \\
        --project icu --prompt "Fuzz ICU's text processing features"

    # Stage 1 only: generate LGs
    python -m harness.checker.pipeline --mode lg \\
        --project icu --prompt "Fuzz ICU's text processing features"

    # Stage 2+3: generate harness from existing LG
    python -m harness.checker.pipeline --mode harness \\
        --lg-path /path/to/lg.json
"""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import os
import secrets
import time
from pathlib import Path
from typing import Any

from harness.checker.danger_score import compute_danger, rank_entries
from harness.checker.flow import (
    SystemCheckCase,
    SystemCheckResult,
    run_system_check,
    _DEFAULT_P2_STORAGE,
)
from harness.checker.lg_agent import LogicGroupAgent
from harness.checker.logic_group import LogicGroup
from harness.checker.p2_agent import P2Agent
from harness.checker.static import StaticIndex
from harness.checker.tools.context import CheckerContext

logger = logging.getLogger(__name__)

_DEFAULT_RUNS_ROOT = Path(
    os.environ.get(
        "AGF_RUNS_ROOT",
        f"/tmp/agf-{os.environ.get('USER', 'anon')}/pipeline-runs",
    )
)


def _ensure_repo(project: str, repo_url: str | None = None) -> str:
    """Clone or locate the project source.  Delegates to flow.ensure_repo."""
    from harness.checker.flow import ensure_repo
    return ensure_repo(project=project, repo_url=repo_url)


def _get_existing_fuzzers(static_index: StaticIndex | None) -> list[str]:
    """Return names of fuzzers already registered for this project."""
    if static_index is None:
        return []
    return list(static_index.fuzzer_names) if hasattr(static_index, "fuzzer_names") else []


# ─────────────────────────────────────────────────────────────────────
# Stage 1: Project → multiple Logic Groups (with danger scores)
# ─────────────────────────────────────────────────────────────────────

def run_lg_stage(
    project: str,
    prompt: str,
    *,
    repo_url: str | None = None,
    output_dir: Path | None = None,
    num_lgs: int = 10,
    top_k: int = 5,
) -> list[LogicGroup]:
    """Run Stage 1: generate multiple Logic Groups and rank by danger score.

    The LG Agent runs *num_lgs* rounds.  Each round receives context about
    previously generated LGs and existing project fuzzers so it avoids
    duplication.  After all rounds, each LG's entries are scored and the
    top *top_k* LGs (by max danger score among their entries) are returned.

    Returns a list of up to *top_k* LogicGroups, each with
    ``danger_scores`` populated, sorted by highest danger.
    """
    logger.info(
        "pipeline.lg_stage.start",
        extra={"project": project, "num_lgs": num_lgs, "top_k": top_k},
    )

    project_path = _ensure_repo(project, repo_url)
    static_index = StaticIndex.load(project)

    ctx = CheckerContext(
        project=project,
        project_path=Path(project_path),
        static_index=static_index,
    )

    existing_fuzzers = _get_existing_fuzzers(static_index)
    generated_lgs: list[LogicGroup] = []

    for round_idx in range(num_lgs):
        # Build exclusion context for the LG Agent
        exclusion_lines = []
        if existing_fuzzers:
            exclusion_lines.append(
                "The project already has these fuzzers (do NOT duplicate): "
                + ", ".join(existing_fuzzers)
            )
        if generated_lgs:
            exclusion_lines.append(
                "You have already generated these Logic Groups (do NOT repeat):"
            )
            for prev in generated_lgs:
                exclusion_lines.append(
                    f"  - {prev.name}: entries=[{', '.join(prev.entries)}]"
                )

        exclusion_block = "\n".join(exclusion_lines)
        augmented_prompt = prompt
        if exclusion_block:
            augmented_prompt = (
                f"{prompt}\n\n"
                f"## Already covered (generate something DIFFERENT):\n"
                f"{exclusion_block}"
            )

        logger.info(
            "pipeline.lg_stage.round",
            extra={"round": round_idx + 1, "total": num_lgs},
        )

        try:
            lg_agent = LogicGroupAgent(ctx=ctx, functionality_prompt=augmented_prompt)
            result = asyncio.run(lg_agent.run())
            lg = result.parsed
        except Exception:
            logger.error("pipeline.lg_stage.round_failed", exc_info=True)
            lg = None

        if lg is not None:
            lg.project = project
            generated_lgs.append(lg)
            logger.info(
                "pipeline.lg_stage.lg_produced",
                extra={"round": round_idx + 1, "lg": lg.summary()},
            )
        else:
            logger.warning(
                "pipeline.lg_stage.lg_none",
                extra={"round": round_idx + 1},
            )

    if not generated_lgs:
        raise RuntimeError(
            f"All {num_lgs} LG generation rounds failed for {project}"
        )

    # ── Rank LGs by danger score ──
    for lg in generated_lgs:
        if static_index is not None:
            rankings = rank_entries(lg.entries, static_index)
            lg.danger_scores = {r["function"]: r["danger"] for r in rankings}
            # Reorder entries by danger (highest first)
            lg.entries = [r["function"] for r in rankings]
        # else: danger_scores stays empty, entries keep LLM order

    # Sort LGs by their top danger score (highest first)
    def lg_max_danger(lg: LogicGroup) -> float:
        return max(lg.danger_scores.values()) if lg.danger_scores else 0.0

    generated_lgs.sort(key=lg_max_danger, reverse=True)
    selected = generated_lgs[:top_k]

    logger.info(
        "pipeline.lg_stage.selected",
        extra={
            "total_generated": len(generated_lgs),
            "selected": len(selected),
            "top_dangers": [
                (lg.name, lg_max_danger(lg)) for lg in selected
            ],
        },
    )

    # Persist
    if output_dir:
        output_dir = Path(output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)
        for i, lg in enumerate(selected):
            lg.save_json(output_dir / f"lg_{i+1}.json")
        # Also save all generated (for audit)
        all_lgs_data = [lg.to_dict() for lg in generated_lgs]
        (output_dir / "all_lgs.json").write_text(
            json.dumps(all_lgs_data, indent=2)
        )

    return selected


# ─────────────────────────────────────────────────────────────────────
# Stage 2 + 3: Single LG → P2 → Harness
# ─────────────────────────────────────────────────────────────────────

def _write_p2_to_storage(case_id: str, report: str) -> None:
    """Write P2 report where flow.py's _load_p2_report() can find it."""
    storage_dir = _DEFAULT_P2_STORAGE / case_id.replace("/", "_")
    storage_dir.mkdir(parents=True, exist_ok=True)
    (storage_dir / "info.md").write_text(report)


def run_harness_for_lg(
    lg: LogicGroup,
    *,
    lg_index: int = 1,
    repo_url: str | None = None,
    output_dir: Path | None = None,
    enable_dynamic: bool = True,
    source_oss_fuzz_dir: Path | None = None,
    top_k_entries: int = 3,
    gold_case_id: str | None = None,
) -> SystemCheckResult:
    """Run Stage 2 (P2 research) + Stage 3 (harness generation) for one LG.

    The P2 report is written to the P2 storage directory so that
    ``run_system_check`` (flow.py) picks it up automatically.

    When ``gold_case_id`` (format ``"<project>/<gold_fuzzer>"``) is given,
    the harness is built/run under the gold fuzzer's name so swap-and-rebuild
    targets the correct gold source file and produces the expected binary.
    """
    project = lg.project
    case_id = gold_case_id or f"{project}/lg_{lg_index}"

    logger.info(
        "pipeline.harness_for_lg.start",
        extra={
            "project": project,
            "lg": lg.name,
            "entries": lg.entries[:top_k_entries],
        },
    )

    project_path = _ensure_repo(project, repo_url)
    static_index = StaticIndex.load(project)

    ctx = CheckerContext(
        project=project,
        project_path=Path(project_path),
        static_index=static_index,
    )

    # ── Stage 2: P2 Research ──
    entries_to_research = lg.entries[:top_k_entries]
    p2_report = ""

    try:
        p2_agent = P2Agent(
            ctx=ctx,
            entries=entries_to_research,
            lg_description=lg.description,
        )
        p2_result = asyncio.run(p2_agent.run())
        p2_report = p2_result.parsed or ""

        if p2_report:
            logger.info(
                "pipeline.harness_for_lg.p2_done",
                extra={"report_len": len(p2_report), "lg": lg.name},
            )
            # Write to P2 storage so flow.py picks it up
            _write_p2_to_storage(case_id, f"# P2 Report: {lg.name}\n\n{p2_report}")
        else:
            logger.warning(
                "pipeline.harness_for_lg.p2_empty",
                extra={"lg": lg.name},
            )
    except Exception:
        logger.error("pipeline.harness_for_lg.p2_failed", exc_info=True)

    # Save P2 report to output dir
    if output_dir and p2_report:
        lg_dir = Path(output_dir) / f"lg_{lg_index}"
        lg_dir.mkdir(parents=True, exist_ok=True)
        (lg_dir / "p2_report.md").write_text(p2_report)

    # ── Stage 3: Harness Generation ──
    # When gold_case_id is provided, use the gold fuzzer name so that swap-and-rebuild
    # replaces the correct gold source and produces the expected binary; the LLM's
    # filename is decoupled from the actual on-disk binary name.
    if gold_case_id and "/" in gold_case_id:
        fuzzer_name = gold_case_id.split("/", 1)[1]
    else:
        fuzzer_name = f"fuzz_{lg.name.replace(' ', '_').lower()[:30]}"
    case = SystemCheckCase(
        project=project,
        case_id=case_id,
        functionality_prompt=lg.description,
        logic_group=lg,
        fuzzer_name=fuzzer_name,
        repo_url=repo_url or "",
    )

    result = run_system_check(
        case,
        output_root=output_dir if output_dir else None,
        enable_dynamic=enable_dynamic,
        source_oss_fuzz_dir=source_oss_fuzz_dir,
    )

    return result


# ─────────────────────────────────────────────────────────────────────
# Stage 2 + 3 batch: multiple LGs → multiple harnesses (serial)
# ─────────────────────────────────────────────────────────────────────

def run_harness_stage(
    lgs: list[LogicGroup],
    *,
    repo_url: str | None = None,
    output_dir: Path | None = None,
    enable_dynamic: bool = True,
    source_oss_fuzz_dir: Path | None = None,
    top_k_entries: int = 3,
    gold_case_id: str | None = None,
) -> list[SystemCheckResult]:
    """Run Stage 2+3 for each LG in sequence.  Returns one result per LG."""
    results = []
    for i, lg in enumerate(lgs):
        logger.info(
            "pipeline.harness_stage.processing",
            extra={"lg_index": i + 1, "total": len(lgs), "lg": lg.name},
        )
        result = run_harness_for_lg(
            lg,
            lg_index=i + 1,
            repo_url=repo_url,
            output_dir=output_dir,
            enable_dynamic=enable_dynamic,
            source_oss_fuzz_dir=source_oss_fuzz_dir,
            top_k_entries=top_k_entries,
            gold_case_id=gold_case_id,
        )
        results.append(result)

    successes = sum(1 for r in results if r.success)
    logger.info(
        "pipeline.harness_stage.done",
        extra={"total": len(results), "successes": successes},
    )
    return results


# ─────────────────────────────────────────────────────────────────────
# Full pipeline: Project → LGs → P2 → Harnesses
# ─────────────────────────────────────────────────────────────────────

def run_full_pipeline(
    project: str,
    prompt: str,
    *,
    repo_url: str | None = None,
    output_dir: Path | None = None,
    enable_dynamic: bool = True,
    source_oss_fuzz_dir: Path | None = None,
    num_lgs: int = 10,
    top_k: int = 5,
    top_k_entries: int = 3,
    gold_case_id: str | None = None,
) -> list[SystemCheckResult]:
    """Run the complete three-stage pipeline.

    Stage 1: LG Agent × num_lgs rounds → top_k Logic Groups (danger ranked)
    Stage 2: P2 Agent for each LG → API protocol reports
    Stage 3: SystemCheckAgent for each LG → harnesses

    Returns one SystemCheckResult per selected LG.
    """
    run_id = f"pipeline-{int(time.time())}-{secrets.token_hex(3)}"
    if output_dir is None:
        output_dir = _DEFAULT_RUNS_ROOT / run_id / project
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    logger.info(
        "pipeline.full.start",
        extra={
            "project": project,
            "num_lgs": num_lgs,
            "top_k": top_k,
            "output_dir": str(output_dir),
        },
    )

    # Stage 1: generate and rank LGs
    lgs = run_lg_stage(
        project, prompt,
        repo_url=repo_url,
        output_dir=output_dir,
        num_lgs=num_lgs,
        top_k=top_k,
    )

    # Stage 2 + 3: for each LG, run P2 research then harness generation
    results = run_harness_stage(
        lgs,
        repo_url=repo_url,
        output_dir=output_dir,
        enable_dynamic=enable_dynamic,
        source_oss_fuzz_dir=source_oss_fuzz_dir,
        top_k_entries=top_k_entries,
        gold_case_id=gold_case_id,
    )

    successes = sum(1 for r in results if r.success)
    logger.info(
        "pipeline.full.done",
        extra={
            "project": project,
            "lgs": len(lgs),
            "harnesses": len(results),
            "successes": successes,
        },
    )

    return results


# ─────────────────────────────────────────────────────────────────────
# CLI entry point
# ─────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description="AGF three-stage pipeline")
    parser.add_argument("--project", required=True)
    parser.add_argument("--prompt", default="")
    parser.add_argument(
        "--mode", choices=["full", "lg", "harness"], default="full",
    )
    parser.add_argument(
        "--lg-path", type=Path, nargs="+",
        help="Path(s) to lg_N.json files (for --mode harness)",
    )
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--no-dynamic", action="store_true")
    parser.add_argument("--num-lgs", type=int, default=10,
                        help="Number of LG generation rounds")
    parser.add_argument("--top-k", type=int, default=5,
                        help="Number of top LGs to select after ranking")
    parser.add_argument("--top-k-entries", type=int, default=3,
                        help="Number of top entries per LG for P2 research")
    parser.add_argument("--repo-url", default=None,
                        help="Override the project's clone URL "
                        "(needed when --project is not in benchmark_cases.jsonl).")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.INFO,
        format="%(levelname)s %(name)s %(message)s",
    )

    if args.mode == "lg":
        if not args.prompt:
            parser.error("--prompt required for --mode lg")
        lgs = run_lg_stage(
            args.project, args.prompt,
            repo_url=args.repo_url,
            output_dir=args.output_dir,
            num_lgs=args.num_lgs,
            top_k=args.top_k,
        )
        print(f"\nGenerated {len(lgs)} Logic Groups:")
        for i, lg in enumerate(lgs):
            max_d = max(lg.danger_scores.values()) if lg.danger_scores else 0
            print(f"  {i+1}. {lg.name} (max danger={max_d:.1f}, entries={lg.entries[:3]})")

    elif args.mode == "harness":
        if not args.lg_path:
            parser.error("--lg-path required for --mode harness")
        lgs = [LogicGroup.load_json(p) for p in args.lg_path]
        results = run_harness_stage(
            lgs,
            output_dir=args.output_dir,
            enable_dynamic=not args.no_dynamic,
            top_k_entries=args.top_k_entries,
        )
        for i, r in enumerate(results):
            status = "OK" if r.success else "FAIL"
            print(f"  LG {i+1}: {status}")

    else:  # full
        if not args.prompt:
            parser.error("--prompt required for --mode full")
        results = run_full_pipeline(
            args.project, args.prompt,
            repo_url=args.repo_url,
            output_dir=args.output_dir,
            enable_dynamic=not args.no_dynamic,
            num_lgs=args.num_lgs,
            top_k=args.top_k,
            top_k_entries=args.top_k_entries,
        )
        successes = sum(1 for r in results if r.success)
        print(f"\nPipeline complete: {successes}/{len(results)} harnesses succeeded")


if __name__ == "__main__":
    main()
