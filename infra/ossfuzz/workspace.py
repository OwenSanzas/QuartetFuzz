from __future__ import annotations

import logging
import os
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

logger = logging.getLogger(__name__)

PINNED_OSS_FUZZ_COMMIT = "4aeb97ef9658244abc844831afacd204afdb6fca"
DEFAULT_OSS_FUZZ_DIR = Path("/tmp/oss-fuzz")


def _git_env() -> dict[str, str]:
    """Build env dict that marks all directories as safe for git.

    Git's ``safe.directory`` check rejects repos owned by other users
    (common with shared /tmp clones).  Neither ``-c safe.directory=*``
    nor ``GIT_CONFIG_COUNT`` env vars bypass the ownership check for
    ``clone --shared`` because the check fires before runtime config
    is loaded.  The only reliable workaround is to point
    ``GIT_CONFIG_GLOBAL`` at a file containing ``safe.directory = *``.
    """
    env = os.environ.copy()
    gitconfig = Path.home() / ".agf-gitconfig-safe"
    if not gitconfig.exists():
        gitconfig.write_text("[safe]\n\tdirectory = *\n")
    env["GIT_CONFIG_GLOBAL"] = str(gitconfig)
    return env


def _git(*args: str, **kwargs) -> subprocess.CompletedProcess:
    """Run a git command with safe.directory=* to handle cross-user /tmp repos."""
    kwargs.setdefault("env", _git_env())
    return subprocess.run(["git", *args], **kwargs)


@dataclass(frozen=True)
class OssFuzzPaths:
    """Encapsulates the standard OSS-Fuzz directory layout.

    Centralises path assumptions so callers don't sprinkle
    ``oss_fuzz_dir / "build" / "out" / project`` in half a dozen modules.
    """

    root: Path

    @property
    def helper_py(self) -> Path:
        return self.root / "infra" / "helper.py"

    def project_dir(self, project: str) -> Path:
        return self.root / "projects" / project

    def build_out(self, project: str) -> Path:
        return self.root / "build" / "out" / project

    def corpus_dir(self, project: str, target: str) -> Path:
        return self.root / "build" / "corpus" / project / target

    def coverage_summary(self, project: str, fuzz_target: str) -> Path:
        return self.build_out(project) / "report_target" / fuzz_target / "linux" / "summary.json"


def oss_fuzz_subprocess_env(oss_fuzz_dir: Path) -> dict[str, str]:
    """Build a subprocess ``env`` dict that pins ``OSS_FUZZ_DIR``.

    OSS-Fuzz's ``infra/common_utils.py`` reads ``OSS_FUZZ_DIR`` from the
    environment to locate the repository root.  If the calling process (or a
    parent shell script) exports a *different* ``OSS_FUZZ_DIR``, helper.py
    silently uses that path -- writing Docker volume mounts, build output,
    etc. to the wrong tree.  This helper prevents the mismatch by always
    setting ``OSS_FUZZ_DIR`` to the ``oss_fuzz_dir`` the caller intends.
    """
    return {**os.environ, "OSS_FUZZ_DIR": str(Path(oss_fuzz_dir).resolve())}


def prepare_pinned_oss_fuzz_workspace(
    source_oss_fuzz_dir: Path,
    workspace_root: Path,
    *,
    pinned_commit: str | None = None,
    workspace_name: str = "oss-fuzz",
) -> tuple[Path, str]:
    pinned_commit = pinned_commit or PINNED_OSS_FUZZ_COMMIT
    source_oss_fuzz_dir = source_oss_fuzz_dir.resolve()
    workspace_root = workspace_root.resolve()
    ensure_pinned_oss_fuzz_commit_available(source_oss_fuzz_dir, pinned_commit=pinned_commit)

    workspace_dir = workspace_root / workspace_name
    if _is_usable_git_workspace(workspace_dir):
        if _reset_workspace_to_commit(workspace_dir, pinned_commit):
            return workspace_dir, pinned_commit
        logger.warning("Workspace reset failed; recreating from scratch")
        force_rmtree(workspace_dir)

    if workspace_dir.exists():
        force_rmtree(workspace_dir)

    workspace_root.mkdir(parents=True, exist_ok=True)
    _clone_oss_fuzz_workspace(source_oss_fuzz_dir, workspace_dir, workspace_root, pinned_commit)
    return workspace_dir, pinned_commit


def ensure_pinned_oss_fuzz_commit_available(
    source_oss_fuzz_dir: Path,
    *,
    pinned_commit: str | None = None,
) -> str:
    pinned_commit = pinned_commit or PINNED_OSS_FUZZ_COMMIT
    source_oss_fuzz_dir = source_oss_fuzz_dir.resolve()

    if _has_commit(source_oss_fuzz_dir, pinned_commit):
        return pinned_commit

    logger.info(
        "Pinned commit %s not found locally in %s; fetching from origin...",
        pinned_commit[:12],
        source_oss_fuzz_dir,
    )
    _git(
        "fetch",
        "origin",
        pinned_commit,
        capture_output=True,
        text=True,
        cwd=str(source_oss_fuzz_dir),
    )

    if _has_commit(source_oss_fuzz_dir, pinned_commit):
        return pinned_commit

    detail = _cat_file_detail(source_oss_fuzz_dir, pinned_commit)
    raise RuntimeError(
        f"Pinned OSS-Fuzz commit {pinned_commit} is not available in {source_oss_fuzz_dir} "
        f"even after `git fetch origin {pinned_commit}`. Reclone OSS-Fuzz and retry. {detail}"
    )


def _has_commit(repo_dir: Path, commit: str) -> bool:
    result = _git(
        "cat-file",
        "-e",
        f"{commit}^{{commit}}",
        capture_output=True,
        cwd=str(repo_dir),
    )
    return result.returncode == 0


def _cat_file_detail(repo_dir: Path, commit: str) -> str:
    result = _git(
        "cat-file",
        "-e",
        f"{commit}^{{commit}}",
        capture_output=True,
        text=True,
        cwd=str(repo_dir),
    )
    return (result.stderr or result.stdout or "").strip()


def checkout_source_oss_fuzz_to_commit(
    source_oss_fuzz_dir: Path,
    *,
    pinned_commit: str | None = None,
) -> None:
    pinned_commit = pinned_commit or PINNED_OSS_FUZZ_COMMIT
    source_oss_fuzz_dir = source_oss_fuzz_dir.resolve()
    ensure_pinned_oss_fuzz_commit_available(source_oss_fuzz_dir, pinned_commit=pinned_commit)
    checkout_result = _git(
        "checkout",
        "--detach",
        pinned_commit,
        capture_output=True,
        text=True,
        cwd=str(source_oss_fuzz_dir),
    )
    if checkout_result.returncode != 0:
        detail = (checkout_result.stderr or checkout_result.stdout or "").strip()
        raise RuntimeError(f"Failed to pin OSS-Fuzz source checkout to {pinned_commit}: {detail}")


def force_rmtree(path: Path) -> None:
    if not path.exists():
        return
    shutil.rmtree(path, ignore_errors=True)
    if not path.exists():
        return

    parent = path.parent.resolve()
    result = subprocess.run(
        ["docker", "run", "--rm", "-v", f"{parent}:/mnt", "alpine", "rm", "-rf", f"/mnt/{path.name}"],
        capture_output=True,
        timeout=120,
    )
    if path.exists():
        detail = (result.stderr or b"").decode(errors="replace").strip()[:200]
        logger.warning("Failed to remove %s even with Docker fallback: %s", path, detail)


def _is_usable_git_workspace(workspace_dir: Path) -> bool:
    if not (workspace_dir / ".git").is_dir():
        return False
    result = _git(
        "rev-parse",
        "--git-dir",
        capture_output=True,
        cwd=str(workspace_dir),
    )
    return result.returncode == 0


def _reset_workspace_to_commit(workspace_dir: Path, commit: str) -> bool:
    checkout = _git(
        "checkout",
        "--detach",
        commit,
        capture_output=True,
        text=True,
        cwd=str(workspace_dir),
    )
    if checkout.returncode != 0:
        return False

    _git(
        "checkout",
        "--",
        "projects/",
        capture_output=True,
        cwd=str(workspace_dir),
    )
    _git(
        "clean",
        "-fd",
        "--",
        "projects/",
        capture_output=True,
        cwd=str(workspace_dir),
    )
    return True


def _clone_oss_fuzz_workspace(source_dir: Path, workspace_dir: Path, workspace_root: Path, commit: str) -> None:
    clone_result = _git(
        "clone",
        "--shared",
        str(source_dir),
        str(workspace_dir),
        capture_output=True,
        text=True,
        cwd=str(workspace_root),
    )
    if clone_result.returncode != 0:
        detail = (clone_result.stderr or clone_result.stdout or "").strip()
        raise RuntimeError(f"Failed to prepare clean OSS-Fuzz workspace: {detail}")

    checkout_result = _git(
        "checkout",
        "--detach",
        commit,
        capture_output=True,
        text=True,
        cwd=str(workspace_dir),
    )
    if checkout_result.returncode != 0:
        detail = (checkout_result.stderr or checkout_result.stdout or "").strip()
        raise RuntimeError(f"Failed to pin OSS-Fuzz workspace to {commit}: {detail}")
