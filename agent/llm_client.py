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

# Fallback pricing ($/1M tokens) when litellm has no info.
_FALLBACK_PRICE_INPUT = 3.0
_FALLBACK_PRICE_OUTPUT = 15.0

# Default model
_DEFAULT_MODEL = "gemini/gemini-3.1-flash-lite-preview"


# ── Model metadata ───────────────────────────────────────────────────────────


def get_context_window(model: str) -> int:
    """Return the context window size (tokens) for *model*."""
    try:
        info = litellm.get_model_info(model)
        return info.get("max_input_tokens") or 128_000
    except Exception:
        return 128_000


def estimate_cost(model: str, input_tokens: int, output_tokens: int) -> float:
    """Return estimated USD cost for a single LLM call."""
    price_in = _FALLBACK_PRICE_INPUT
    price_out = _FALLBACK_PRICE_OUTPUT
    try:
        info = litellm.get_model_info(model)
        if info.get("input_cost_per_token"):
            price_in = info["input_cost_per_token"] * 1_000_000
        if info.get("output_cost_per_token"):
            price_out = info["output_cost_per_token"] * 1_000_000
    except Exception:
        pass
    return (input_tokens / 1_000_000) * price_in + (output_tokens / 1_000_000) * price_out


# ── LLM Response ─────────────────────────────────────────────────────────────


@dataclass
class LLMResponse:
    """Standardised response from a single LLM call."""

    content: str = ""
    tool_calls: list[dict[str, Any]] = field(default_factory=list)
    stop_reason: str = ""
    input_tokens: int = 0
    output_tokens: int = 0
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

        model_lower = model.lower()
        if ("gemini-3" in model_lower or "gpt-5" in model_lower) and temperature < 1.0:
            if model not in self._temp_override_logged:
                log.info("Overriding temperature to 1.0 (model requires it)", model=model)
                self._temp_override_logged.add(model)
            temperature = 1.0

        kwargs: dict[str, Any] = {
            "model": model,
            "messages": full_messages,
            "temperature": temperature,
            "max_tokens": max_tokens,
        }
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

        return LLMResponse(
            content=message.content or "",
            tool_calls=tool_calls,
            stop_reason=choice.finish_reason or "",
            input_tokens=usage.prompt_tokens or 0,
            output_tokens=usage.completion_tokens or 0,
            latency_ms=latency_ms,
            reasoning_content=getattr(message, "reasoning_content", None) or "",
        )
