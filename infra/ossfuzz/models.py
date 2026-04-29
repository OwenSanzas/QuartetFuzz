from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class BuildResult:
    success: bool = False
    output_dir: str = ""
    fuzzers: list[str] = field(default_factory=list)
    error: str | None = None
    build_log: str = ""


@dataclass(frozen=True)
class FuzzerRunResult:
    """Stats from a single libFuzzer corpus generation run."""

    corpus_dir: str = ""
    corpus_size: int = 0
    edges_covered: int = 0
    features: int = 0
    total_executions: int = 0
    exec_per_second: int = 0
    peak_rss_mb: int = 0
    elapsed_seconds: float = 0.0
    error: str | None = None


@dataclass(frozen=True)
class CoverageMetrics:
    """Coverage metrics from a single llvm-cov run."""

    lines_covered: int = 0
    lines_total: int = 0
    lines_pct: float = 0.0
    branches_covered: int = 0
    branches_total: int = 0
    branches_pct: float = 0.0
    functions_covered: int = 0
    functions_total: int = 0
    functions_pct: float = 0.0
    regions_covered: int = 0
    regions_total: int = 0
    regions_pct: float = 0.0
    corpus_size: int = 0
    duration_seconds: int = 0
    elapsed_seconds: float = 0.0
    error: str | None = None
