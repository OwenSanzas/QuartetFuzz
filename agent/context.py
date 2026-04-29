"""AgentContext — mutable accumulator for a single agent run."""

from __future__ import annotations

import time
import uuid
from datetime import datetime, timezone
from typing import Any

import structlog

from .llm_client import LLMResponse, estimate_cost
from .result import AgentResult, ToolCallRecord, TrajectoryEvent

log = structlog.get_logger("agent")


class AgentContext:
    """Tracks tokens, tool calls, and timing for one agent run."""

    def __init__(
        self,
        *,
        agent_type: str,
        model: str,
        **kwargs: Any,
    ) -> None:
        self.run_id = uuid.uuid4()
        self.agent_type = agent_type
        self.model = model

        self._input_tokens: int = 0
        self._output_tokens: int = 0
        self._last_input_tokens: int = 0
        self._turn: int = 0
        self._tool_calls: list[ToolCallRecord] = []
        self._trajectory: list[TrajectoryEvent] = []
        self._cost: float = 0.0

        self._status: str = "running"
        self._error: str | None = None

        self._started_at = time.monotonic()
        self._started_dt = datetime.now(timezone.utc)
        self._ended_dt: datetime | None = None
        self._cancelled = False

    def add_usage(self, response: LLMResponse) -> None:
        self._input_tokens += response.input_tokens
        self._output_tokens += response.output_tokens
        self._last_input_tokens = response.input_tokens
        self._cost += estimate_cost(self.model, response.input_tokens, response.output_tokens)

    def increment_turn(self) -> int:
        self._turn += 1
        return self._turn

    def record_tool_call(
        self,
        *,
        seq: int,
        tool_name: str,
        tool_input: dict[str, Any],
        output_chars: int = 0,
        duration_ms: int = 0,
        is_error: bool = False,
    ) -> None:
        self._tool_calls.append(
            ToolCallRecord(
                turn=self._turn,
                seq=seq,
                tool_name=tool_name,
                tool_input=tool_input,
                output_chars=output_chars,
                duration_ms=duration_ms,
                is_error=is_error,
            )
        )

    def record_event(self, event: TrajectoryEvent) -> None:
        self._trajectory.append(event)

    @property
    def elapsed_ms(self) -> int:
        return int((time.monotonic() - self._started_at) * 1000)

    def finish(self, status: str = "completed", error: str | None = None) -> None:
        self._status = status
        self._error = error
        self._ended_dt = datetime.now(timezone.utc)

    def cancel(self) -> None:
        self._cancelled = True

    @property
    def cancelled(self) -> bool:
        return self._cancelled

    @property
    def total_input_tokens(self) -> int:
        return self._input_tokens

    @property
    def last_input_tokens(self) -> int:
        """Prompt tokens from the most recent LLM call (approximates current context size)."""
        return self._last_input_tokens

    @property
    def estimated_cost(self) -> float:
        return self._cost

    @property
    def duration_ms(self) -> int:
        return int((time.monotonic() - self._started_at) * 1000)

    @property
    def turn(self) -> int:
        return self._turn

    def to_result(self, *, content: str = "", parsed: Any = None) -> AgentResult:
        return AgentResult(
            run_id=self.run_id,
            content=content,
            parsed=parsed,
            tool_calls=list(self._tool_calls),
            trajectory=list(self._trajectory),
            status=self._status,
            error=self._error,
            input_tokens=self._input_tokens,
            output_tokens=self._output_tokens,
            total_turns=self._turn,
            estimated_cost=self._cost,
            duration_ms=self.duration_ms,
        )
