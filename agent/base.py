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
from .llm_client import LLMClient, LLMResponse, get_context_window, get_max_output_tokens
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
    # Compress only when the prompt nears the window: at 80% of the model's
    # context window or at this many tokens, whichever comes first.
    # Compression is observation masking (Lindenbauer et al., "The
    # Complexity Trap", 2025): old tool outputs are replaced by a short
    # placeholder, the agent's own reasoning and calls stay verbatim, and
    # no LLM summary is involved.
    # (fbv2's mechanical eviction is the same idea, with the recall store.)
    compression_threshold_tokens: int = 100_000
    # Tool outputs kept verbatim when masking: the most recent ones.
    keep_recent_observations: int = 10
    # Only above this estimated size are the agent's own old inputs and
    # reasoning also replaced (stage 2/3).
    shrink_threshold_tokens: int = 400_000
    # Pure reads whose result is stable: a masked one is restored by calling
    # the tool again.  Every other masked text (dynamic probes, builds,
    # coverage, old harness inputs, old reasoning) is kept in memory and
    # restored verbatim with recall(ref).
    idempotent_read_tools: frozenset[str] = frozenset({
        "Read", "Glob", "Grep", "get_callers", "get_callees", "lookup_symbol",
        "reachable_from", "reverse_reachable_from", "is_public_api",
        "public_apis_reaching", "public_apis_reaching_batch", "find_path",
    })
    max_tool_output_tokens: int = 8000

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

    def get_pinned_status(self) -> str | None:
        """Facts the agent must keep seeing after old tool outputs are
        masked; shown in one pinned message, refreshed at each compression."""
        return None

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

        if self.enable_compression:
            tools.append(_RECALL_TOOL)
        self._recall_store: dict[int, str] = {}

        system = self.get_system_prompt(**kwargs)
        messages: list[dict[str, Any]] = [
            {"role": "user", "content": self.get_initial_message(**kwargs)},
        ]

        last_content = ""

        while ctx.turn < self.max_turns:
            if ctx.cancelled:
                log.info("agent.cancelled", turn=ctx.turn)
                break

            turn = ctx.increment_turn()

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
                max_tokens=get_max_output_tokens(ctx.model),
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
                    if tool_name == "recall" and self.enable_compression:
                        result = self._recall(tool_input)
                    else:
                        result = await mcp.call_tool(tool_name, tool_input)
                    if isinstance(result, str):
                        output_text = result
                    elif isinstance(result, dict):
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
                token_threshold = min(
                    int(get_context_window(ctx.model) * 0.8),
                    self.compression_threshold_tokens,
                )
                if ctx.last_input_tokens >= token_threshold:
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
        """Observation masking.  Every tool output except the most recent
        ``keep_recent_observations`` (and the latest artifact result) is
        replaced by a one-line placeholder naming the call; the initial task,
        the agent's reasoning and every tool call (with its arguments) stay
        verbatim.  If that is still over half the threshold, superseded
        artifact inputs and then old reasoning text are replaced by
        placeholders.  No LLM call, no message is ever removed."""
        names: dict[str, str] = {}
        for m in messages:
            for tc in m.get("tool_calls") or []:
                names[tc.get("id")] = (tc.get("function") or {}).get("name", "?")

        keep: set[int] = set()
        artifact = self._find_latest_artifact_pair(messages)
        if artifact is not None and artifact[1] >= 0:
            keep.add(artifact[1])
        tool_idx = [i for i, m in enumerate(messages) if m.get("role") == "tool"]
        keep.update(tool_idx[-self.keep_recent_observations:])

        masked = 0
        out: list[dict[str, Any]] = []
        for i, m in enumerate(messages):
            content = m.get("content")
            if (m.get("role") == "tool" and i not in keep and isinstance(content, str)
                    and content and not content.startswith("[Output of ")):
                name = names.get(m.get("tool_call_id"), "?")
                if name in self.idempotent_read_tools:
                    how = f"call {name} again with the same arguments to see it"
                else:
                    how = f"recall({self._store(content)}) restores it verbatim"
                m = dict(m, content=(
                    f"[Output of {name} omitted to save context ({len(content)} chars); {how}.]"))
                masked += 1
            out.append(m)

        # Still too large: shrink, oldest first, what the agent itself wrote,
        # never dropping a message.  Every tool call keeps its name and its
        # place in the history; the system prompt is passed separately and
        # the initial task (messages[0]) is never touched.
        #   1. arguments of superseded artifact calls (old harness versions);
        #   2. the agent's reasoning text in old turns.
        budget_chars = self.shrink_threshold_tokens * 4  # ~4 chars/token
        def _size(ms: list[dict[str, Any]]) -> int:
            return sum(len(str(m.get("content") or "")) + len(str(m.get("tool_calls") or ""))
                       for m in ms)
        latest = artifact[0] if artifact is not None else -1
        recent = tool_idx[-self.keep_recent_observations] if len(tool_idx) >= self.keep_recent_observations else 0
        shrunk = 0
        for i in range(1, recent):
            if _size(out) <= budget_chars:
                break
            m = out[i]
            if m.get("role") == "assistant" and i != latest and m.get("tool_calls"):
                calls = []
                for tc in m["tool_calls"]:
                    fn = tc.get("function") or {}
                    if fn.get("name") in self.artifact_tool_names and not str(fn.get("arguments", "")).startswith('{"_omitted"'):
                        ref = self._store(str(fn.get("arguments")))
                        tc = dict(tc, function=dict(fn, arguments=json.dumps({"_omitted": (
                            f"superseded {fn.get('name')} input ({len(str(fn.get('arguments')))} chars); "
                            f"the latest version is kept; recall({ref}) restores this one")})))
                        shrunk += 1
                    calls.append(tc)
                out[i] = dict(m, tool_calls=calls)
        for i in range(1, recent):
            if _size(out) <= budget_chars:
                break
            m = out[i]
            text = m.get("content")
            if m.get("role") == "assistant" and isinstance(text, str) and len(text) > 200 \
                    and not text.startswith("[Reasoning of an earlier turn omitted"):
                out[i] = dict(m, content=(
                    f"[Reasoning of an earlier turn omitted to save context ({len(text)} chars); "
                    f"recall({self._store(text)}) restores it.]"))
                shrunk += 1
        status = self.get_pinned_status()
        if status:
            pinned = {"role": "user", "content": (
                f"{_PINNED_HEADER} (as of turn {ctx.turn}; older tool outputs "
                f"in this conversation are masked)\n{status}")}
            if len(out) > 1 and str(out[1].get("content") or "").startswith(_PINNED_HEADER):
                out[1] = pinned
            else:
                out.insert(1, pinned)
        log.info("agent.context_masked", masked=masked, shrunk=shrunk, stored=len(self._recall_store))
        return out

    def _store(self, text: str) -> int:
        store = self.__dict__.setdefault("_recall_store", {})
        ref = len(store) + 1
        store[ref] = text
        return ref

    def _recall(self, args: dict[str, Any]) -> str:
        try:
            ref = int(args.get("ref"))
        except (TypeError, ValueError):
            return f"Error: invalid ref {args.get('ref')!r}"
        text = getattr(self, "_recall_store", {}).get(ref)
        if text is None:
            return f"Error: no omitted output #{ref}"
        return text


_PINNED_HEADER = "## STATUS (kept across context compression)"

_RECALL_TOOL: dict[str, Any] = {
    "type": "function",
    "function": {
        "name": "recall",
        "description": (
            "Restore, verbatim, an earlier tool output (or your own earlier input "
            "or reasoning) that was omitted to save context. The placeholder "
            "names the ref: '[... recall(7) restores it ...]' -> recall(ref=7)."
        ),
        "parameters": {
            "type": "object",
            "properties": {"ref": {"type": "integer", "description": "the number in the placeholder"}},
            "required": ["ref"],
        },
    },
}


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
    # FastMCP returns ``(content blocks, structured result)`` for a tool
    # with a return annotation (the Read/Glob/Grep tools) and the bare
    # block list otherwise.
    if isinstance(content, tuple) and len(content) == 2 and isinstance(content[1], dict):
        blocks, structured = content
        text = _extract_mcp_text(blocks)
        if not text and "result" in structured:
            text = str(structured["result"])
        return text
    parts: list[str] = []
    for block in content:
        if isinstance(block, mcp_types.TextContent):
            parts.append(block.text)
        elif isinstance(block, mcp_types.EmbeddedResource):
            if isinstance(block.resource, mcp_types.TextResourceContents):
                parts.append(block.resource.text)
    return "\n".join(parts)
