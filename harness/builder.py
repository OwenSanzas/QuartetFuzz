"""Build validation for generated harnesses.

Provides ``BuildValidator``, which compiles a generated harness via the
OSS-Fuzz Docker infrastructure and returns structured feedback on failure.
"""

from __future__ import annotations

import getpass as _getpass
import json
import logging
import subprocess
import tempfile
import threading
from dataclasses import dataclass
from pathlib import Path

from infra.ossfuzz.build import (
    build_project,
    list_fuzzers,
    load_gold_source_paths,
    swap_gold_source,
)
from infra.ossfuzz.models import BuildResult
from infra.ossfuzz.sanitize import sanitize_harness_source
from infra.ossfuzz.workspace import (
    DEFAULT_OSS_FUZZ_DIR as _DEFAULT_OSS_FUZZ_DIR,
)
from infra.ossfuzz.workspace import (
    OssFuzzPaths,
    checkout_source_oss_fuzz_to_commit,
    ensure_pinned_oss_fuzz_commit_available,
    force_rmtree,
    oss_fuzz_subprocess_env,
    prepare_pinned_oss_fuzz_workspace,
)

logger = logging.getLogger(__name__)

_USER = _getpass.getuser()
_DEFAULT_REPO_DIR = Path(f"/tmp/agf-{_USER}/repos")
_OSS_FUZZ_REPO = "https://github.com/google/oss-fuzz.git"
_BENCHMARK_CASES = (
    Path(__file__).resolve().parent.parent / "benchmark" / "oss_fuzz_harness" / "data" / "benchmark_cases.jsonl"
)
_NEEDS_SG_DOCKER: bool | None = None

# ---------------------------------------------------------------------------
# Project-specific build.sh patches
# ---------------------------------------------------------------------------
# Some projects hardcode each fuzzer in build.sh instead of using globs.
# For fuzzers that exist in the source repo but aren't in oss-fuzz's build.sh,
# we append the necessary compile commands after each reset.

_BUILD_SH_EXTRA: dict[str, str] = {
    "iperf": """
# Re-link cjson_fuzzer with libiperf and friends so LLM-written harnesses
# that reach beyond the bundled cJSON.c (e.g. call iperf_set_test_role)
# still resolve at link time.
$CXX $CXXFLAGS $LIB_FUZZING_ENGINE cjson_fuzzer.o cjson.o \\
    src/.libs/libiperf.a -lm -lpthread \\
    -o $OUT/cjson_fuzzer
""",
    "openssh": """
# sntrup761 fuzzers — COPY'd to $SRC/ root
$CXX $CXXFLAGS -std=c++11 $EXTRA_CFLAGS -I. -L. -Lopenbsd-compat -g \\
    $SRC/sntrup761_dec_fuzz.cc -o $OUT/sntrup761_dec_fuzz \\
    $STATIC_CRYPTO $LIB_FUZZING_ENGINE
$CXX $CXXFLAGS -std=c++11 $EXTRA_CFLAGS -I. -L. -Lopenbsd-compat -g \\
    $SRC/sntrup761_enc_fuzz.cc -o $OUT/sntrup761_enc_fuzz \\
    $STATIC_CRYPTO $LIB_FUZZING_ENGINE
""",
}


def _patch_build_sh(project_dir: Path, project: str) -> None:
    """Append project-specific compile commands to build.sh if needed."""
    extra = _BUILD_SH_EXTRA.get(project)
    if not extra:
        return
    build_sh = project_dir / "build.sh"
    content = build_sh.read_text(errors="replace")
    if extra.strip().splitlines()[0] in content:
        return  # already patched
    build_sh.write_text(content.rstrip() + "\n" + extra)
    logger.debug("Patched build.sh for %s", project)


# ---------------------------------------------------------------------------
# Docker access helpers
# ---------------------------------------------------------------------------


def _needs_sg_docker() -> bool:
    """Return True if Docker commands must be wrapped with ``sg docker``.

    Caches the result after the first check.
    """
    global _NEEDS_SG_DOCKER
    if _NEEDS_SG_DOCKER is not None:
        return _NEEDS_SG_DOCKER

    # Direct access?
    if subprocess.run(["docker", "info"], capture_output=True, timeout=10).returncode == 0:
        _NEEDS_SG_DOCKER = False
        return False

    # Group exists but session not refreshed?
    result = subprocess.run(["sg", "docker", "-c", "docker info"], capture_output=True, timeout=10)
    _NEEDS_SG_DOCKER = result.returncode == 0
    if _NEEDS_SG_DOCKER:
        logger.info("Docker requires 'sg docker' — wrapping commands automatically")
    else:
        logger.warning("Docker not accessible even via 'sg docker'")
    return _NEEDS_SG_DOCKER


# ---------------------------------------------------------------------------
# Environment setup
# ---------------------------------------------------------------------------


def ensure_oss_fuzz(oss_fuzz_dir: Path | None = None, *, pin_worktree: bool = False) -> Path:
    """Return a usable oss-fuzz checkout, cloning if necessary."""
    oss_fuzz_dir = oss_fuzz_dir or _DEFAULT_OSS_FUZZ_DIR
    if (oss_fuzz_dir / "infra" / "helper.py").exists():
        ensure_pinned_oss_fuzz_commit_available(oss_fuzz_dir)
        if pin_worktree:
            checkout_source_oss_fuzz_to_commit(oss_fuzz_dir)
        return oss_fuzz_dir

    logger.info("Cloning oss-fuzz to %s", oss_fuzz_dir)
    result = subprocess.run(
        ["git", "clone", _OSS_FUZZ_REPO, str(oss_fuzz_dir)],
        capture_output=True,
        text=True,
        timeout=300,
    )
    if result.returncode != 0:
        raise RuntimeError(f"Failed to clone oss-fuzz: {result.stderr.strip()[:500]}")
    ensure_pinned_oss_fuzz_commit_available(oss_fuzz_dir)
    if pin_worktree:
        checkout_source_oss_fuzz_to_commit(oss_fuzz_dir)
    return oss_fuzz_dir


def ensure_repo(
    project: str,
    repo_dir: Path | None = None,
    repo_url: str | None = None,
) -> Path:
    """Return a cloned project repo, cloning if necessary."""
    project_dir = (repo_dir or _DEFAULT_REPO_DIR) / project
    if project_dir.exists() and any(project_dir.iterdir()):
        return project_dir

    url = repo_url or _lookup_repo_url(project)
    if not url:
        raise RuntimeError(
            f"No repo URL for project '{project}'. "
            f"Pass --repo explicitly or ensure the project is in benchmark_cases.jsonl."
        )

    logger.info("Cloning %s to %s", url, project_dir)
    project_dir.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        ["git", "clone", "--depth", "1", url, str(project_dir)],
        capture_output=True,
        text=True,
        timeout=600,
    )
    if result.returncode != 0:
        raise RuntimeError(f"Failed to clone {url}: {result.stderr.strip()[:500]}")
    return project_dir


def _lookup_repo_url(project: str) -> str | None:
    """Look up the repo URL for a project from the benchmark cases."""
    if not _BENCHMARK_CASES.is_file():
        return None
    with open(_BENCHMARK_CASES) as f:
        for line in f:
            case = json.loads(line)
            if case.get("project") == project:
                return case.get("repo_url")
    return None


# ---------------------------------------------------------------------------
# Build validation
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class BuildValidationResult:
    """Outcome of a build validation attempt."""

    success: bool
    error: str = ""
    build_log: str = ""
    binary_name: str = ""  # fuzzy-matched binary name (if different from expected)

    @property
    def feedback(self) -> str:
        """Formatted feedback suitable for injection into a generation prompt."""
        if self.success:
            return ""
        parts = [f"Build failed: {self.error}"]
        if self.build_log:
            parts.append(f"\nBuild log (last 2000 chars):\n```\n{self.build_log[-2000:]}\n```")
        return "\n".join(parts)


class BuildValidator:
    """Compile a generated harness via OSS-Fuzz and report build errors.

    Usage::

        bv = BuildValidator(project="libyaml")
        result = bv.validate(harness_code, source_filename="fuzz_scanner.c")
    """

    def __init__(
        self,
        project: str,
        *,
        oss_fuzz_dir: Path | None = None,
        workspace_root: Path | None = None,
        sanitizer: str = "address",
        container_path: str | None = None,
        fuzzer_name: str | None = None,
        docker_lock: threading.Lock | None = None,
        language: str = "c",
    ) -> None:
        self.project = project
        self.oss_fuzz_dir = ensure_oss_fuzz(oss_fuzz_dir)
        self.workspace_root = (
            Path(workspace_root) if workspace_root else Path(tempfile.mkdtemp(prefix="agf-build-validate-"))
        )
        self.sanitizer = sanitizer
        self._container_path = container_path
        self._fuzzer_name = fuzzer_name
        self._gold_paths = load_gold_source_paths()
        self._docker_lock = docker_lock
        self._language = language

    def validate(
        self,
        harness_code: str,
        source_filename: str,
    ) -> BuildValidationResult:
        """Compile *harness_code* as part of the OSS-Fuzz project.

        Returns a :class:`BuildValidationResult` indicating success or failure
        with a formatted feedback string for the generator.
        """
        container_path = self._resolve_container_path(source_filename)
        workspace_dir, _ = prepare_pinned_oss_fuzz_workspace(self.oss_fuzz_dir, self.workspace_root)

        cleaned_source, _ = sanitize_harness_source(harness_code, source_filename, language=self._language)

        project_dir = workspace_dir / "projects" / self.project
        swap_gold_source(project_dir, source_filename, cleaned_source, container_path)
        _patch_build_sh(project_dir, self.project)

        if self._docker_lock:
            with self._docker_lock:
                build_result = self._build(workspace_dir)
        else:
            build_result = self._build(workspace_dir)
        self._clean_build_artifacts(workspace_dir)

        if not build_result.success and not build_result.fuzzers:
            return BuildValidationResult(
                success=False,
                error=build_result.error or "Unknown build error",
                build_log=build_result.build_log,
            )

        # Check that our fuzzer binary was actually produced.  Some projects
        # use glob patterns (e.g. $SRC/*_fuzzer.c) in build.sh, so a misnamed
        # file will be silently skipped.
        expected_binary = Path(source_filename).stem
        if expected_binary not in build_result.fuzzers:
            # Fuzzy match: some projects rename binaries (e.g. php adds
            # "php-fuzz-" prefix, binutils may rename).  Check if any
            # produced binary contains a significant portion of our stem.
            stem_parts = expected_binary.replace("-", "_").split("_")
            matched = None
            for fuzz_bin in build_result.fuzzers:
                fuzz_norm = fuzz_bin.replace("-", "_").lower()
                # Match if the binary contains the most significant keyword
                # from our filename (longest non-trivial part).
                keywords = [p for p in stem_parts if len(p) > 3]
                if keywords and any(kw.lower() in fuzz_norm for kw in keywords):
                    matched = fuzz_bin
                    break
            if matched:
                logger.info(
                    "Build succeeded for %s/%s (binary name fuzzy-matched: %s → %s)",
                    self.project, source_filename, expected_binary, matched,
                )
                return BuildValidationResult(success=True, binary_name=matched)

            return BuildValidationResult(
                success=False,
                error=(
                    f"Build succeeded but fuzzer binary '{expected_binary}' was not produced. "
                    f"The project's build.sh likely uses a glob pattern to find fuzzer source files "
                    f"(e.g. $SRC/*_fuzzer.c). Your filename '{source_filename}' may not match. "
                    f"Available binaries: {', '.join(build_result.fuzzers[:10])}"
                ),
                build_log=build_result.build_log,
            )

        logger.info("Build succeeded for %s/%s", self.project, source_filename)
        return BuildValidationResult(success=True)

    def cleanup_workspace(self) -> None:
        """Remove the entire .build_validate workspace tree."""
        if self.workspace_root.exists():
            force_rmtree(self.workspace_root)
            logger.debug("Removed build-validation workspace %s", self.workspace_root)

    # -- Private helpers ----------------------------------------------------

    def _clean_build_artifacts(self, workspace_dir: Path) -> None:
        """Remove build/out and build/work for this project to reclaim disk."""
        for subdir in ("out", "work"):
            p = workspace_dir / "build" / subdir / self.project
            if p.is_dir():
                force_rmtree(p)

    def _build(self, workspace_dir: Path) -> BuildResult:
        """Run the OSS-Fuzz build, wrapping with ``sg docker`` if needed."""
        if _needs_sg_docker():
            return self._build_via_sg_docker(workspace_dir)
        return build_project(workspace_dir, self.project, sanitizer=self.sanitizer)

    def _build_via_sg_docker(self, workspace_dir: Path, max_retries: int = 2) -> BuildResult:
        """Run build_project inside ``sg docker`` for Docker group permissions."""
        paths = OssFuzzPaths(workspace_dir)
        cmd = [
            "sg",
            "docker",
            "-c",
            f"python3 {paths.helper_py} build_fuzzers --sanitizer {self.sanitizer} {self.project}",
        ]
        output_dir = paths.build_out(self.project)
        timeout = 7200

        for attempt in range(1, max_retries + 1):
            try:
                result = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    timeout=timeout,
                    cwd=str(workspace_dir),
                    env=oss_fuzz_subprocess_env(workspace_dir),
                )
            except subprocess.TimeoutExpired:
                if attempt < max_retries:
                    logger.warning(
                        "sg-docker build for %s timed out on retry %d/%d within this build invocation; retrying...",
                        self.project,
                        attempt,
                        max_retries,
                    )
                    continue
                return BuildResult(
                    output_dir=str(output_dir),
                    fuzzers=list_fuzzers(output_dir),
                    error=f"Build timed out ({timeout}s) after {max_retries} attempts",
                )
            if result.returncode == 0:
                break
            if attempt < max_retries:
                logger.warning(
                    "sg-docker build for %s failed on retry %d/%d within this build invocation; retrying...",
                    self.project,
                    attempt,
                    max_retries,
                )

        fuzzers = list_fuzzers(output_dir)
        combined = result.stdout + "\n" + result.stderr
        if result.returncode != 0:
            return BuildResult(
                output_dir=str(output_dir),
                fuzzers=fuzzers,
                error=f"Build failed (exit {result.returncode})",
                build_log=combined[-3000:],
            )
        return BuildResult(
            success=True,
            output_dir=str(output_dir),
            fuzzers=fuzzers,
            build_log=combined[-1000:],
        )

    def _resolve_container_path(self, source_filename: str) -> str:
        """Determine where the harness should be placed inside the Docker container."""
        if self._container_path:
            return self._container_path
        lookup_stem = self._fuzzer_name or Path(source_filename).stem
        key = f"{self.project}/{lookup_stem}"
        if key in self._gold_paths:
            return self._gold_paths[key]
        return f"$SRC/{source_filename}"
