"""Logic Group — a functional unit of a library worth fuzzing as a whole.

A Logic Group groups together:
    * one or more **entry functions** (public API surface to hit the feature)
    * the **core** set of reachable internal functions
    * a human-readable name + description
    * an optional risk level (currently unpopulated; kept as a placeholder
      field for future use)

Logic Groups are produced by :class:`LogicGroupAgent` (see ``lg_agent.py``)
when the pipeline is in Mode A (prompt → LG → target → fuzzer).  They can
also be handed to the pipeline directly (Mode B) — e.g. from a file, a
previous cached run, or an external tool.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any


@dataclass
class LogicGroup:
    """A coherent functional unit to fuzz as a single target.

    Fields
    ------
    name
        Short human-readable feature label.  Examples:
        ``"libyaml document loader"``, ``"openssl TLS handshake"``.
    description
        One-paragraph natural-language summary of what the feature does.
        Produced by :class:`LogicGroupAgent` after exploring the source —
        this is the agent's own understanding, NOT a copy of the input
        prompt.  The delta between the input prompt and this field is an
        intrinsic signal of how well the agent understood the task.
    entries
        Candidate public API entry points.  At least one must be present.
        The downstream :class:`SystemCheckAgent` picks one from this list
        in its Phase 1.
    core
        Internal functions reachable from the entries that implement the
        feature.  Used by downstream P4 verification to confirm the chosen
        entry actually reaches this core set.
    project
        Owning project name (e.g. ``"libyaml"``).
    risk_level
        ``"low"`` / ``"medium"`` / ``"high"``.  Unpopulated in the current
        pipeline; kept as a placeholder for future scoring work.
    """

    name: str
    description: str
    entries: list[str]
    core: list[str] = field(default_factory=list)
    project: str = ""
    risk_level: str = ""
    danger_scores: dict[str, float] = field(default_factory=dict)

    def __post_init__(self) -> None:
        if not self.name:
            raise ValueError("LogicGroup.name must be non-empty")
        if not self.entries:
            raise ValueError(
                f"LogicGroup('{self.name}').entries must contain at least "
                f"one entry function name"
            )

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent)

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> LogicGroup:
        return cls(
            name=d.get("name", ""),
            description=d.get("description", ""),
            entries=list(d.get("entries", [])),
            core=list(d.get("core", [])),
            project=d.get("project", ""),
            risk_level=d.get("risk_level", ""),
            danger_scores=dict(d.get("danger_scores", {})),
        )

    @classmethod
    def load_json(cls, path: Path) -> LogicGroup:
        return cls.from_dict(json.loads(Path(path).read_text()))

    def save_json(self, path: Path) -> None:
        Path(path).write_text(self.to_json())

    def summary(self) -> str:
        """One-line human summary for logs / CLI output."""
        return (
            f"LogicGroup({self.name!r}, entries={len(self.entries)}, "
            f"core={len(self.core)}, project={self.project!r})"
        )
