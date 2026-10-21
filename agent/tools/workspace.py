"""What the shared filesystem tools are allowed to do, for one agent run."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal

ReadHook = Callable[[Path, int, int], str | None]
"""Intercepts ``Read`` after the path is resolved and allowed.

Called with ``(resolved_path, offset, limit)``. ``offset`` is a 0-based slice
start: ``Read`` takes the model's 1-based line and converts it before the hook
sees it, so a hook can slice a list of lines directly. Return content to hand
the model, or ``None`` to read from disk as usual.

A hook rather than a flag because the two users need different shapes: ``seed``
annotates every path with coverage, ``ap`` substitutes one path with a harness
it holds in memory.
"""

PathStyle = Literal["relative", "absolute"]
"""How ``Grep`` cites the files it found.

``relative`` strips an allowed-root prefix, so paths are short and no host path
reaches the model. ``absolute`` keeps the full path. Prefer absolute when two
roots hold same-named files: a bare ``build.sh`` is ambiguous once both the
project and its OSS-Fuzz directory are in scope.
"""

MissingRipgrep = Literal["error", "grep"]
"""What ``Grep`` does when ripgrep is not installed.

``error`` says so and stops. ``grep`` retries with ``grep -rn``, which keeps the
run alive but uses POSIX BRE instead of Rust regex, so the same pattern can
match differently. The fallback labels its own output for that reason.
"""


@dataclass(frozen=True)
class Workspace:
    """The roots, limits, and hooks one agent's filesystem tools run under.

    Only genuine choices are fields. Behaviour with one sensible setting, such
    as catching ``OSError`` or treating a non-zero ripgrep exit as an error, is
    not configurable.

    Attributes:
        project_path: Base for resolving relative paths, and always an allowed
            root.
        extra_roots: Additional allowed roots. Usually the project's OSS-Fuzz
            directory, plus the fuzzer source's directory for LG extraction,
            which often sits outside the project tree.
        default_glob: ``--glob`` for ``Grep`` when the model passes none.
            Empty means pass no glob at all, i.e. search every file.
        glob_in_signature: Put :attr:`default_glob` in ``Grep``'s
            signature instead of applying it at call time. Behaviour is the
            same either way; only the schema the model sees differs.
        max_output_chars: Cap on a single tool result; ``0`` disables it. A cap
            protects the context window but can also cut off what the agent was
            looking for.
        search_path_style: See :data:`PathStyle`.
        missing_ripgrep: See :data:`MissingRipgrep`.
        read_hook: See :data:`ReadHook`.
    """

    project_path: Path
    extra_roots: tuple[Path, ...] = ()
    default_glob: str = ""
    glob_in_signature: bool = False
    max_output_chars: int = 0
    search_path_style: PathStyle = "relative"
    missing_ripgrep: MissingRipgrep = "error"
    read_hook: ReadHook | None = None

    _roots: tuple[Path, ...] = field(init=False, repr=False, compare=False, default=())

    def __post_init__(self) -> None:
        base = Path(self.project_path).resolve()
        roots = [base]
        for extra in self.extra_roots:
            if extra is None:
                continue
            resolved = Path(extra).resolve()
            if resolved not in roots:
                roots.append(resolved)
        object.__setattr__(self, "_roots", tuple(roots))

    @property
    def roots(self) -> tuple[Path, ...]:
        """Allowed roots, resolved and deduplicated, ``project_path`` first."""
        return self._roots

    def resolve(self, raw_path: str, *, is_dir: bool = False) -> tuple[Path | None, str | None]:
        """Resolve *raw_path* and check it against :attr:`roots`.

        Returns ``(path, None)`` when allowed, or ``(None, message)`` when the
        path is malformed or outside every root. The message is written for the
        model to read, not for a log.
        """
        p = Path(raw_path)
        if not p.is_absolute():
            p = Path(self.project_path) / p
        try:
            resolved = p.resolve()
        except (OSError, ValueError):
            kind = "directory" if is_dir else "file"
            return None, f"Error: Invalid path: {raw_path} (expected {kind})"
        if not any(resolved == root or root in resolved.parents for root in self.roots):
            return None, f"Error: Access denied -- path is outside the project: {raw_path}"
        return resolved, None

    def truncate(self, text: str) -> str:
        """Apply :attr:`max_output_chars`, marking the cut so the model sees it."""
        limit = self.max_output_chars
        if limit <= 0 or len(text) <= limit:
            return text
        return text[:limit] + f"\n\n[... output truncated at {limit} chars]"
