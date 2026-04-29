"""BaseAgent — abstract LLM-tool loop driven by LiteLLM + FastMCP, with skill dispatch."""

from __future__ import annotations

import json
import time
from abc import ABC, abstractmethod
from collections.abc import Sequence
from typing import Any

import structlog
from mcp import types as mcp_types
from mcp.server.fastmcp import FastMCP

from .context import AgentContext
from .llm_client import LLMClient, LLMResponse, get_context_window
from .result import AgentResult, TrajectoryEvent

log = structlog.get_logger("agent")

_llm = LLMClient()


class BaseAgent(ABC):
    """Abstract base for all agents.

    Subclasses must implement:
    - create_mcp_server()  — return a FastMCP with tools registered
    - get_system_prompt()  — return the system prompt string
    - get_initial_message() — return the first user message
    """

    agent_type: str = ""
    max_turns: int = 30
    temperature: float = 0.0
    model: str | None = None
    enable_compression: bool = True
    max_tool_output_tokens: int = 8000
    max_context_tokens: int = 64000

    @abstractmethod
    def create_mcp_server(self) -> FastMCP:
        """Return a FastMCP instance with tools registered."""

    @abstractmethod
    def get_system_prompt(self, **kwargs: Any) -> str:
        """Return the system prompt for this agent."""

    @abstractmethod
    def get_initial_message(self, **kwargs: Any) -> str:
        """Return the first user message that kicks off the loop."""

    def parse_result(self, content: str) -> Any:
        return None

    def get_urgency_message(self) -> str | None:
        return None

    def get_compression_criteria(self) -> str:
        return "Keep all tool results, key findings, code snippets, and decisions."

    def should_stop(self, response: LLMResponse) -> bool:
        return False

    def create_context(self, **kwargs: Any) -> AgentContext:
        return AgentContext(**kwargs)

    async def run(self, **kwargs: Any) -> AgentResult:
        """Execute the LLM-tool loop and return an AgentResult."""
        resolved_model = _llm.resolve_model(self.model)
        ctx = self.create_context(
            agent_type=self.agent_type,
            model=resolved_model,
        )

        structlog.contextvars.bind_contextvars(
            run_id=str(ctx.run_id),
            agent_type=self.agent_type,
        )

        try:
            content = await self._run_loop(ctx, **kwargs)
            parsed = self.parse_result(content)
            ctx.finish("completed")
        except Exception as exc:
            log.error("agent.error", exc_info=True)
            content = ""
            parsed = None
            ctx.finish("failed", error=str(exc))
        finally:
            structlog.contextvars.unbind_contextvars("run_id", "agent_type")

        result = ctx.to_result(content=content, parsed=parsed)
        log.info(
            "agent.done",
            status=result.status,
            turns=result.total_turns,
            cost=result.estimated_cost,
        )
        return result

    async def _run_loop(self, ctx: AgentContext, **kwargs: Any) -> str:
        """LLM call -> tool execution -> repeat."""
        mcp = self.create_mcp_server()
        tools = await self._load_tools(mcp)

        system = self.get_system_prompt(**kwargs)
        messages: list[dict[str, Any]] = [
            {"role": "user", "content": self.get_initial_message(**kwargs)},
        ]

        last_content = ""
        budget_extended = False

        while ctx.turn < self.max_turns:
            if ctx.cancelled:
                log.info("agent.cancelled", turn=ctx.turn)
                break

            turn = ctx.increment_turn()

            if not budget_extended and ctx.total_input_tokens >= self.max_context_tokens:
                log.warning("agent.budget_exceeded", turn=turn, tokens=ctx.total_input_tokens)
                urgency = self.get_urgency_message()
                if urgency:
                    messages.append({"role": "user", "content": urgency})
                budget_extended = True

            if turn == self.max_turns - 2:
                urgency = self.get_urgency_message()
                if urgency:
                    messages.append({"role": "user", "content": urgency})

            response = await _llm.create(
                model=ctx.model,
                system=system,
                messages=messages,
                tools=tools if tools else None,
                temperature=self.temperature,
            )
            ctx.add_usage(response)
            ctx.record_event(
                TrajectoryEvent(
                    turn=turn,
                    event_type="llm_response",
                    timestamp_ms=ctx.elapsed_ms,
                    content=response.content,
                    input_tokens=response.input_tokens,
                    output_tokens=response.output_tokens,
                    duration_ms=response.latency_ms,
                )
            )

            log.debug(
                "llm.response",
                turn=turn,
                stop_reason=response.stop_reason,
                tools=len(response.tool_calls),
            )

            if not response.has_tool_calls:
                last_content = response.content
                messages.append({"role": "assistant", "content": response.content})
                break

            if self.should_stop(response):
                last_content = response.content
                messages.append({"role": "assistant", "content": response.content})
                break

            # NOTE: Some providers (DeepSeek) expect content=None when tool_calls are present.
            # An empty string "" can cause issues. Use None for empty content with tool calls.
            assistant_msg: dict[str, Any] = {
                "role": "assistant",
                "tool_calls": response.tool_calls,
            }
            if response.content:
                assistant_msg["content"] = response.content
            messages.append(assistant_msg)

            for seq, tc in enumerate(response.tool_calls):
                func = tc["function"]
                tool_name = func["name"]
                try:
                    tool_input = json.loads(func["arguments"])
                except (json.JSONDecodeError, TypeError):
                    tool_input = {}

                ctx.record_event(
                    TrajectoryEvent(
                        turn=turn,
                        event_type="tool_call",
                        timestamp_ms=ctx.elapsed_ms,
                        tool_name=tool_name,
                        tool_input=tool_input,
                        tool_call_id=tc["id"],
                    )
                )

                t0 = time.monotonic()
                is_error = False
                output_text = ""
                try:
                    result = await mcp.call_tool(tool_name, tool_input)
                    if isinstance(result, dict):
                        output_text = json.dumps(result, indent=2, default=str)
                    else:
                        output_text = _extract_mcp_text(result)
                except Exception as exc:
                    is_error = True
                    output_text = f"Error: {exc}"
                    log.warning("tool.error", tool=tool_name, error=str(exc))
                duration_ms = int((time.monotonic() - t0) * 1000)

                char_limit = self.max_tool_output_tokens * 4
                if len(output_text) > char_limit:
                    output_text = (
                        output_text[:char_limit]
                        + f"\n\n[truncated — {len(output_text)} chars total, limit {char_limit}]"
                    )

                ctx.record_tool_call(
                    seq=seq,
                    tool_name=tool_name,
                    tool_input=tool_input,
                    output_chars=len(output_text),
                    duration_ms=duration_ms,
                    is_error=is_error,
                )
                ctx.record_event(
                    TrajectoryEvent(
                        turn=turn,
                        event_type="tool_result",
                        timestamp_ms=ctx.elapsed_ms,
                        content=output_text,
                        tool_name=tool_name,
                        tool_call_id=tc["id"],
                        duration_ms=duration_ms,
                        is_error=is_error,
                    )
                )

                messages.append(
                    {
                        "role": "tool",
                        "tool_call_id": tc["id"],
                        "content": output_text,
                    }
                )

            if self.enable_compression and turn > 1:
                token_threshold = int(get_context_window(ctx.model) * 0.8)
                needs_compress = turn % 5 == 0 or ctx.last_input_tokens >= token_threshold
                if needs_compress:
                    messages = await self._compress_context(ctx, system, messages)

        return last_content

    async def _load_tools(self, mcp: FastMCP) -> list[dict[str, Any]]:
        mcp_tools = await mcp.list_tools()
        openai_tools: list[dict[str, Any]] = []
        for tool in mcp_tools:
            schema = tool.inputSchema if tool.inputSchema else {"type": "object", "properties": {}}
            schema = _strip_titles(schema)
            openai_tools.append(
                {
                    "type": "function",
                    "function": {
                        "name": tool.name,
                        "description": tool.description or "",
                        "parameters": schema,
                    },
                }
            )
        return openai_tools

    # Tool names whose latest invocation should be preserved verbatim
    # when the conversation is compressed.  For SystemCheckAgent this is
    # build_harness (carries the harness source in tool_call.arguments);
    # for LogicGroupAgent it is submit_logic_group; for P2Agent it is
    # submit_p2_report.  Subclasses override to opt in.
    artifact_tool_names: frozenset[str] = frozenset()

    def _find_latest_artifact_pair(
        self,
        messages: list[dict[str, Any]],
    ) -> tuple[int, int] | None:
        """Locate the most recent assistant message containing a tool_call
        to one of ``self.artifact_tool_names``, plus its tool_result row.

        Returns (assistant_idx, tool_result_idx).  ``tool_result_idx`` is
        ``-1`` when the call has not produced a result yet (truncated turn).
        """
        if not self.artifact_tool_names:
            return None
        for i in range(len(messages) - 1, -1, -1):
            m = messages[i]
            if m.get("role") != "assistant":
                continue
            for tc in m.get("tool_calls") or []:
                func = tc.get("function") or {}
                if func.get("name") in self.artifact_tool_names:
                    tc_id = tc.get("id")
                    result_idx = -1
                    for j in range(i + 1, len(messages)):
                        if messages[j].get("tool_call_id") == tc_id:
                            result_idx = j
                            break
                    return (i, result_idx)
        return None

    async def _compress_context(
        self,
        ctx: AgentContext,
        system: str,
        messages: list[dict[str, Any]],
    ) -> list[dict[str, Any]]:
        if len(messages) < 6:
            return messages

        head_n, tail_n = 1, 4
        head_end = head_n
        tail_start = len(messages) - tail_n

        # Walk tail_start back so it never lands on a tool_result whose
        # parent assistant tool_call would end up in the summary.  Anthropic
        # rejects orphan ``tool_use_id`` references; OpenAI does not, but
        # the safe rule is the same.  We can also land on an assistant
        # whose tool_calls have results inside the tail — those are
        # whole-pair safe.
        while tail_start > head_end and messages[tail_start].get("role") == "tool":
            tail_start -= 1
        if tail_start <= head_end:
            return messages
        head = messages[:head_end]
        tail = messages[tail_start:]

        # Identify the latest artifact pair (e.g. last build_harness call +
        # its tool result).  Preserve it across compression so the
        # generated harness source is never lost from context.
        artifact = self._find_latest_artifact_pair(messages)
        preserved: set[int] = set()
        if artifact is not None:
            for idx in artifact:
                if 0 <= idx and head_end <= idx < tail_start:
                    preserved.add(idx)

        middle_to_summarise = [
            messages[i]
            for i in range(head_end, tail_start)
            if i not in preserved
        ]
        if not middle_to_summarise:
            return messages

        middle_text = "\n".join(
            f"[{m.get('role', '?')}] {str(m.get('content', ''))[:500]}"
            for m in middle_to_summarise
        )
        criteria = self.get_compression_criteria()
        compress_prompt = (
            f"Summarise the following conversation excerpt concisely.\n"
            f"Criteria: {criteria}\n\n{middle_text}"
        )

        try:
            resp = await _llm.create(
                model=ctx.model,
                system="You are a concise summariser.",
                messages=[{"role": "user", "content": compress_prompt}],
                temperature=0.0,
                max_tokens=1024,
            )
            ctx.add_usage(resp)
            summary_msg: dict[str, Any] = {
                "role": "user",
                "content": (
                    f"[Context summary of {len(middle_to_summarise)} earlier "
                    f"messages]\n{resp.content}"
                ),
            }
            preserved_msgs = [messages[i] for i in sorted(preserved)]
            rebuilt = head + [summary_msg] + preserved_msgs + tail

            # Final safety pass: drop any tool_result whose tool_use_id has
            # no matching assistant tool_call earlier in the rebuilt list.
            # The summary block does not contain tool_use ids, so a result
            # message that referred to a now-summarised assistant becomes
            # orphan; Anthropic's API rejects such payloads with
            # ``unexpected tool_use_id``.
            seen_tc_ids: set[str] = set()
            cleaned: list[dict[str, Any]] = []
            for m in rebuilt:
                if m.get("role") == "assistant":
                    for tc in m.get("tool_calls") or []:
                        tc_id = tc.get("id")
                        if tc_id:
                            seen_tc_ids.add(tc_id)
                if m.get("role") == "tool":
                    if m.get("tool_call_id") not in seen_tc_ids:
                        # orphan — skip
                        continue
                cleaned.append(m)
            return cleaned
        except Exception:
            log.warning("agent.compress_failed")
            return messages


def _strip_titles(schema: dict[str, Any]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for key, value in schema.items():
        if key == "title":
            continue
        if isinstance(value, dict):
            out[key] = _strip_titles(value)
        elif isinstance(value, list):
            out[key] = [_strip_titles(v) if isinstance(v, dict) else v for v in value]
        else:
            out[key] = value
    return out


def _extract_mcp_text(content: Sequence[Any]) -> str:
    parts: list[str] = []
    for block in content:
        if isinstance(block, mcp_types.TextContent):
            parts.append(block.text)
        elif isinstance(block, mcp_types.EmbeddedResource):
            if isinstance(block.resource, mcp_types.TextResourceContents):
                parts.append(block.resource.text)
    return "\n".join(parts)
