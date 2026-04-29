"""Data classes returned by agent runs."""

from __future__ import annotations

import json
import uuid
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any


@dataclass
class TrajectoryEvent:
    """Single event in an agent trajectory."""

    turn: int
    event_type: str  # "llm_response" | "tool_call" | "tool_result"
    timestamp_ms: int
    content: str = ""
    tool_name: str = ""
    tool_input: dict[str, Any] = field(default_factory=dict)
    tool_call_id: str = ""
    input_tokens: int = 0
    output_tokens: int = 0
    duration_ms: int = 0
    is_error: bool = False


@dataclass
class ToolCallRecord:
    """Single tool invocation metadata."""

    turn: int
    seq: int
    tool_name: str
    tool_input: dict[str, Any]
    output_chars: int = 0
    duration_ms: int = 0
    is_error: bool = False


@dataclass
class AgentResult:
    """Return value of BaseAgent.run()."""

    run_id: uuid.UUID
    content: str = ""
    parsed: Any = None
    tool_calls: list[ToolCallRecord] = field(default_factory=list)
    trajectory: list[TrajectoryEvent] = field(default_factory=list)

    status: str = "completed"  # completed | failed | timeout
    error: str | None = None

    input_tokens: int = 0
    output_tokens: int = 0
    total_turns: int = 0
    estimated_cost: float = 0.0
    duration_ms: int = 0

    def save_trajectory(self, path: Path) -> None:
        """Write trajectory as JSONL."""
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w") as f:
            for event in self.trajectory:
                f.write(json.dumps(asdict(event), default=str) + "\n")
