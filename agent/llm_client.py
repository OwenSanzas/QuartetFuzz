"""Thin async wrapper around litellm.acompletion() — supports Anthropic, OpenAI, Google (Gemini), DeepSeek."""

from __future__ import annotations

import logging
import os
import time
from dataclasses import dataclass, field
from typing import Any

import litellm
import structlog

litellm.suppress_debug_info = True
logging.getLogger("LiteLLM").setLevel(logging.WARNING)
logging.getLogger("asyncio").setLevel(logging.CRITICAL)

log = structlog.get_logger("agent")

# ── Provider → env-var mapping ────────────────────────────────────────────────

_API_KEY_ENV: dict[str, str] = {
    "anthropic": "ANTHROPIC_API_KEY",
    "openai": "OPENAI_API_KEY",
    "deepseek": "DEEPSEEK_API_KEY",
    "google": "GEMINI_API_KEY",
}

# model-id prefix → provider
_PROVIDER_HINTS: list[tuple[str, str]] = [
    ("claude", "anthropic"),
    ("deepseek", "deepseek"),
    ("gemini", "google"),
    ("gpt", "openai"),
    ("o3", "openai"),
    ("o4", "openai"),
    ("o1", "openai"),
    ("anthropic", "anthropic"),
    ("openai", "openai"),
    ("google", "google"),
]

# Default model: the one every system in the evaluation runs on.
_DEFAULT_MODEL = "claude-sonnet-5"

# Pinned model facts, so cost and the compression threshold do not depend on
# the installed litellm's price table.  $/1M tokens; cache_write is the
# 5-minute rate.  Source: platform.claude.com/docs/en/about-claude/pricing,
# checked 2026-10-17 (Sonnet 5's $2/$10 made permanent 2026-08-10).  The same
# rates priced the OFG and PromeFuzz runs.
_MODEL_FACTS: dict[str, dict[str, float]] = {
    "claude-sonnet-5": {
        "context_window": 1_000_000,
        "max_output": 128_000,
        # Sonnet 5 returns 400 for any non-default temperature/top_p/top_k.
        "sampling_params": 0,
        "input": 2.0,
        "output": 10.0,
        "cache_read": 0.20,
        "cache_write": 2.50,
    },
}

# Fallback pricing ($/1M tokens) for a model in neither table.
_FALLBACK_PRICE_INPUT = 3.0
_FALLBACK_PRICE_OUTPUT = 15.0


def _is_anthropic(model: str) -> bool:
    m = model.lower()
    return m.startswith("anthropic/") or m.startswith("claude")


def _with_cache_breakpoints(messages: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Mark the system prompt and the newest message as Anthropic prompt-cache
    breakpoints.

    The system breakpoint caches tools + system; the one on the newest
    message caches the whole conversation so far, so the next turn pays
    the cache-read rate for everything but its own new messages.  Only the
    request is changed: the content the model sees is identical.
    """
    def mark(msg: dict[str, Any]) -> dict[str, Any]:
        content = msg.get("content")
        if isinstance(content, str) and content:
            return {**msg, "content": [{"type": "text", "text": content,
                                        "cache_control": {"type": "ephemeral"}}]}
        if isinstance(content, list) and content and isinstance(content[-1], dict):
            blocks = [*content[:-1], {**content[-1], "cache_control": {"type": "ephemeral"}}]
            return {**msg, "content": blocks}
        return msg

    out = list(messages)
    if out and out[0].get("role") == "system":
        out[0] = mark(out[0])
    # The newest message that has text content (a tool result or user turn).
    for i in range(len(out) - 1, 0, -1):
        if out[i].get("content"):
            out[i] = mark(out[i])
            break
    return out


def _facts(model: str) -> dict[str, float] | None:
    return _MODEL_FACTS.get(model.lower().removeprefix("anthropic/"))


# ── Model metadata ───────────────────────────────────────────────────────────


def get_context_window(model: str) -> int:
    """Return the context window size (tokens) for *model*."""
    f = _facts(model)
    if f:
        return int(f["context_window"])
    try:
        info = litellm.get_model_info(model)
        return info.get("max_input_tokens") or 128_000
    except Exception:
        return 128_000


def get_max_output_tokens(model: str) -> int:
    """Return the most output tokens *model* can produce in one response.

    The Messages API requires ``max_tokens``; passing the model's own maximum
    means a response is never cut short by our setting.
    """
    f = _facts(model)
    if f and f.get("max_output"):
        return int(f["max_output"])
    try:
        info = litellm.get_model_info(model)
        return info.get("max_output_tokens") or 4096
    except Exception:
        return 4096


def estimate_cost(
    model: str,
    input_tokens: int,
    output_tokens: int,
    cache_read_tokens: int = 0,
    cache_write_tokens: int = 0,
) -> float:
    """Return USD cost for a single LLM call.

    *input_tokens* counts every prompt token; the cached and cache-written ones
    among them are billed at their own rates.
    """
    f = _facts(model)
    if f is None:
        f = {"input": _FALLBACK_PRICE_INPUT, "output": _FALLBACK_PRICE_OUTPUT}
        try:
            info = litellm.get_model_info(model)
            if info.get("input_cost_per_token"):
                f["input"] = info["input_cost_per_token"] * 1_000_000
            if info.get("output_cost_per_token"):
                f["output"] = info["output_cost_per_token"] * 1_000_000
            if info.get("cache_read_input_token_cost"):
                f["cache_read"] = info["cache_read_input_token_cost"] * 1_000_000
            if info.get("cache_creation_input_token_cost"):
                f["cache_write"] = info["cache_creation_input_token_cost"] * 1_000_000
        except Exception:
            log.warning("No price for model, using fallback", model=model)
    plain = max(input_tokens - cache_read_tokens - cache_write_tokens, 0)
    return (
        plain * f["input"]
        + cache_read_tokens * f.get("cache_read", f["input"])
        + cache_write_tokens * f.get("cache_write", f["input"])
        + output_tokens * f["output"]
    ) / 1_000_000


# ── LLM Response ─────────────────────────────────────────────────────────────


@dataclass
class LLMResponse:
    """Standardised response from a single LLM call."""

    content: str = ""
    tool_calls: list[dict[str, Any]] = field(default_factory=list)
    stop_reason: str = ""
    input_tokens: int = 0  # all prompt tokens, cached ones included
    output_tokens: int = 0
    cache_read_tokens: int = 0
    cache_write_tokens: int = 0
    latency_ms: int = 0
    reasoning_content: str = ""

    @property
    def has_tool_calls(self) -> bool:
        return bool(self.tool_calls)


# ── LLM Client ───────────────────────────────────────────────────────────────


class LLMClient:
    """Async wrapper around litellm.acompletion().

    Reads API keys from environment variables.
    Supports Anthropic, OpenAI, Google (Gemini), and DeepSeek.
    """

    _temp_override_logged: set[str] = set()

    def resolve_model(self, model: str | None = None) -> str:
        return model or _DEFAULT_MODEL

    @staticmethod
    def _get_api_key(model_id: str) -> str | None:
        model_lower = model_id.lower()
        for prefix, provider in _PROVIDER_HINTS:
            if prefix in model_lower:
                env_var = _API_KEY_ENV.get(provider)
                if env_var:
                    return os.environ.get(env_var)
                break
        return None

    async def create(
        self,
        *,
        model: str,
        system: str,
        messages: list[dict[str, Any]],
        tools: list[dict[str, Any]] | None = None,
        temperature: float = 0.0,
        max_tokens: int = 4096,
        response_format: dict[str, Any] | None = None,
    ) -> LLMResponse:
        full_messages: list[dict[str, Any]] = [
            {"role": "system", "content": system},
            *messages,
        ]
        if _is_anthropic(model):
            full_messages = _with_cache_breakpoints(full_messages)

        model_lower = model.lower()
        if ("gemini-3" in model_lower or "gpt-5" in model_lower) and temperature < 1.0:
            if model not in self._temp_override_logged:
                log.info("Overriding temperature to 1.0 (model requires it)", model=model)
                self._temp_override_logged.add(model)
            temperature = 1.0

        kwargs: dict[str, Any] = {
            "model": model,
            "messages": full_messages,
            "max_tokens": max_tokens,
        }
        facts = _facts(model)
        if facts is None or facts.get("sampling_params", 1):
            kwargs["temperature"] = temperature
        if tools:
            kwargs["tools"] = tools
        if response_format:
            kwargs["response_format"] = response_format

        api_key = self._get_api_key(model)
        if api_key:
            kwargs["api_key"] = api_key

        t0 = time.monotonic()
        raw = await litellm.acompletion(**kwargs)
        latency_ms = int((time.monotonic() - t0) * 1000)

        choice = raw.choices[0]
        message = choice.message

        tool_calls: list[dict[str, Any]] = []
        if getattr(message, "tool_calls", None):
            for tc in message.tool_calls:
                tool_calls.append(
                    {
                        "id": tc.id,
                        "type": "function",
                        "function": {
                            "name": tc.function.name,
                            "arguments": tc.function.arguments,
                        },
                    }
                )

        usage = raw.usage or litellm.Usage()
        details = getattr(usage, "prompt_tokens_details", None)
        cache_read = getattr(details, "cached_tokens", None) or 0
        cache_write = getattr(usage, "cache_creation_input_tokens", None) or 0

        return LLMResponse(
            content=message.content or "",
            tool_calls=tool_calls,
            stop_reason=choice.finish_reason or "",
            input_tokens=usage.prompt_tokens or 0,
            output_tokens=usage.completion_tokens or 0,
            cache_read_tokens=cache_read,
            cache_write_tokens=cache_write,
            latency_ms=latency_ms,
            reasoning_content=getattr(message, "reasoning_content", None) or "",
        )
