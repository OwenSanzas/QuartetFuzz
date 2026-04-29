"""Shared dependency container for tool modules.

Every tool module's ``register(mcp, ctx)`` receives a :class:`CheckerContext`
and pulls only the fields it needs.  Using a single context object avoids
threading a dozen parameters through every tool signature, and gives the
system-check agent a single place to mutate shared state (e.g. the
``DynamicRunner`` becoming populated after the first build).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from harness.checker.dynamic import DynamicRunner
    from harness.checker.static import StaticIndex


DEFAULT_LLM_MODEL = "claude-sonnet-4-6"


@dataclass
class CheckerContext:
    """Dependency bag passed to every tool category module.

    Fields
    ------
    project
        OSS-Fuzz project name (e.g. ``libyaml``).  Required.
    project_path
        Path to the cloned project source tree.  ``code_view`` tools use
        this as a sandbox root.
    oss_fuzz_project_dir
        Optional path to the ``projects/<project>`` directory of a
        reference OSS-Fuzz checkout — useful for reading ``build.sh`` /
        ``Dockerfile`` to understand how the project compiles.  Added to
        ``allowed_roots`` automatically if set.
    allowed_roots
        Whitelist for code_view tools.  Derived automatically from
        ``project_path`` and ``oss_fuzz_project_dir`` if left empty.
    static_index
        Pre-loaded :class:`StaticIndex` for this project, or ``None`` if
        static analysis data is not available.
    dynamic_runner
        :class:`DynamicRunner` for build/run/coverage/gdb, or ``None`` if
        dynamic analysis is disabled for this flow.
    llm_model
        LiteLLM model string for every LLM-backed tool.  Defaults to
        ``claude-sonnet-4-6`` per project policy.
    github_token
        Optional GitHub API token for the ``github`` category.  Not used
        unless that category is registered.
    """

    project: str
    project_path: Path
    oss_fuzz_project_dir: Path | None = None
    allowed_roots: list[Path] = field(default_factory=list)
    static_index: "StaticIndex | None" = None
    dynamic_runner: "DynamicRunner | None" = None
    llm_model: str = DEFAULT_LLM_MODEL
    github_token: str | None = None

    def __post_init__(self) -> None:
        resolved_roots: list[Path] = list(self.allowed_roots)
        if self.project_path:
            pp = Path(self.project_path).resolve()
            if pp not in resolved_roots:
                resolved_roots.append(pp)
        if self.oss_fuzz_project_dir:
            ofp = Path(self.oss_fuzz_project_dir).resolve()
            if ofp not in resolved_roots:
                resolved_roots.append(ofp)
        self.allowed_roots = resolved_roots
        self.project_path = Path(self.project_path).resolve()
        if self.oss_fuzz_project_dir:
            self.oss_fuzz_project_dir = Path(self.oss_fuzz_project_dir).resolve()
