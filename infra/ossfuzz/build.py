from __future__ import annotations

import json
import logging
from pathlib import Path

from rich.console import Console

from infra.ossfuzz.models import BuildResult
from infra.ossfuzz.subprocess_utils import run_command_with_streaming
from infra.ossfuzz.workspace import OssFuzzPaths, _git, oss_fuzz_subprocess_env

logger = logging.getLogger(__name__)

_NON_FUZZER_FILES = frozenset({"llvm-symbolizer", "jazzer_driver", "jazzer_driver_with_sanitizer"})
_NON_FUZZER_EXTENSIONS = frozenset({".zip", ".dict", ".options", ".cfg", ".profraw"})

_GOLD_SOURCE_PATHS = (
    Path(__file__).resolve().parent.parent.parent
    / "dataset"
    / "gold_source_paths.json"
)


# ---------------------------------------------------------------------------
# Gold source swap (replaces old pattern-detection / add-fuzzer machinery)
# ---------------------------------------------------------------------------


def load_gold_source_paths(path: Path | None = None) -> dict[str, str]:
    p = path or _GOLD_SOURCE_PATHS
    if p.is_file():
        with open(p) as f:
            return json.load(f)
    return {}


def swap_gold_source(
    project_dir: Path,
    source_filename: str,
    generated_source: str,
    container_path: str,
) -> None:
    """Replace gold harness source with generated source in the Docker build context.

    Copies generated source into the project directory and adds a Dockerfile
    COPY line to overlay it at the correct container path (after git clone).
    """
    dest = project_dir / source_filename
    dest.write_text(generated_source)

    dockerfile = project_dir / "Dockerfile"
    content = dockerfile.read_text(errors="replace")
    copy_line = f"COPY {source_filename} {container_path}"
    if copy_line in content:
        return

    lines = content.rstrip().splitlines()
    insert_idx = len(lines)
    for i, line in enumerate(lines):
        if line.strip().startswith("WORKDIR"):
            insert_idx = i
    lines.insert(insert_idx, copy_line)
    dockerfile.write_text("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------
# Build project via oss-fuzz infra/helper.py
# ---------------------------------------------------------------------------


def build_project(
    oss_fuzz_dir: Path,
    project: str,
    sanitizer: str = "address",
    timeout: int = 7200,
    max_retries: int = 2,
    stream_to: Console | None = None,
    stream_prefix: str = "",
) -> BuildResult:
    paths = OssFuzzPaths(oss_fuzz_dir)
    cmd = [
        "python3",
        str(paths.helper_py),
        "build_fuzzers",
        "--sanitizer",
        sanitizer,
        project,
    ]

    output_dir = paths.build_out(project)
    env = oss_fuzz_subprocess_env(oss_fuzz_dir)
    result = None
    for attempt in range(1, max_retries + 1):
        result = run_command_with_streaming(
            cmd,
            cwd=oss_fuzz_dir,
            timeout=timeout,
            env=env,
            stream_to=stream_to,
            stream_prefix=stream_prefix,
        )
        if result.timed_out:
            if attempt == max_retries:
                return BuildResult(
                    output_dir=str(output_dir),
                    fuzzers=list_fuzzers(output_dir),
                    error=f"Build timed out ({timeout}s) after {max_retries} attempts",
                    build_log=result.combined_output[-3000:],
                )
            logger.warning(
                "OSS-Fuzz build for %s timed out on retry %d/%d within this build invocation; retrying...",
                project,
                attempt,
                max_retries,
            )
            continue
        if result.returncode == 0:
            break
        if attempt < max_retries:
            logger.warning(
                "OSS-Fuzz build for %s failed on retry %d/%d within this build invocation; retrying...",
                project,
                attempt,
                max_retries,
            )

    if result is None:
        return BuildResult(error="No build attempts completed")

    fuzzers = list_fuzzers(output_dir)
    combined_log = result.combined_output

    if result.returncode != 0:
        log_tail = combined_log[-3000:]
        return BuildResult(
            output_dir=str(output_dir),
            fuzzers=fuzzers,
            error=f"Build failed (exit {result.returncode})",
            build_log=log_tail,
        )

    if not fuzzers and not output_dir.is_dir():
        logger.warning(
            "Build exited 0 but output directory does not exist: %s. "
            "This usually means helper.py resolved OSS_FUZZ_DIR to a different "
            "path than expected (check for leaked OSS_FUZZ_DIR env var).",
            output_dir,
        )
        return BuildResult(
            output_dir=str(output_dir),
            fuzzers=[],
            error=f"Build output directory missing: {output_dir}",
            build_log=combined_log[-3000:],
        )

    return BuildResult(success=True, output_dir=str(output_dir), fuzzers=fuzzers, build_log=combined_log[-1000:])


# ---------------------------------------------------------------------------
# List fuzzers in build output
# ---------------------------------------------------------------------------


def list_fuzzers(output_dir: Path) -> list[str]:
    if not output_dir.is_dir():
        return []
    return sorted(
        f.name
        for f in output_dir.iterdir()
        if f.is_file()
        and f.stat().st_mode & 0o111
        and f.name not in _NON_FUZZER_FILES
        and f.suffix not in _NON_FUZZER_EXTENSIONS
    )


# ---------------------------------------------------------------------------
# Project directory management
# ---------------------------------------------------------------------------


def reset_oss_fuzz_project(oss_fuzz_dir: Path, project: str) -> None:
    checkout = _git(
        "checkout",
        "--",
        f"projects/{project}/",
        capture_output=True,
        text=True,
        cwd=str(oss_fuzz_dir),
    )
    if checkout.returncode != 0:
        detail = (checkout.stderr or checkout.stdout or "").strip()
        raise RuntimeError(f"Failed to reset OSS-Fuzz project {project}: {detail}")

    clean = _git(
        "clean",
        "-fd",
        f"projects/{project}/",
        capture_output=True,
        text=True,
        cwd=str(oss_fuzz_dir),
    )
    if clean.returncode != 0:
        detail = (clean.stderr or clean.stdout or "").strip()
        raise RuntimeError(f"Failed to clean OSS-Fuzz project {project}: {detail}")
