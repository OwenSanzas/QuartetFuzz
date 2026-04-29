"""LLM-judge based similarity scorer for prompt ↔ LG.description.

Uses claude-sonnet-4-6 as a semantic judge: given the original user
prompt and the LG description the agent wrote after exploring the
project, score how closely the LG description captures the same intent
as the prompt, on a 0.0–1.0 scale.

This is NOT a typical embedding similarity — we want to measure
"did the agent understand the prompt and reflect that understanding
in the description it wrote", which includes things like:
    * same target feature
    * same abstraction level
    * no drift to adjacent/unrelated functionality

Output:
    {
        "similarity": 0.85,       # float in [0, 1]
        "reasoning": "...",       # one short paragraph
        "verdict": "aligned" | "partial" | "drifted",
    }
"""

from __future__ import annotations

import asyncio
import json
import re
from dataclasses import dataclass

from agent.llm_client import LLMClient

_llm = LLMClient()
_DEFAULT_MODEL = "claude-sonnet-4-6"


_JUDGE_PROMPT = """You are a semantic similarity judge for fuzz-testing
functional descriptions.

Given two descriptions of what a fuzz harness should target, decide
how well they describe the SAME functional target at the SAME level
of abstraction.

## Description A (user prompt — what they asked for)
{prompt}

## Description B (agent-written summary after reading the source)
{description}

## Instructions

Score the pair on a similarity scale from 0.0 to 1.0:

    1.0 — Both describe exactly the same feature at the same level.
          Near-paraphrases.
    0.7–0.9 — Same feature, description adds or omits minor details,
              but the target is unambiguously the same.
    0.4–0.6 — Overlapping but clearly different scope (e.g. user asked
              for the "loader", agent described the "scanner").  Agent
              drifted to adjacent functionality.
    0.1–0.3 — Different feature entirely, only tangentially related.
    0.0 — Unrelated.

Then pick a verdict:
    "aligned"  — score ≥ 0.7
    "partial"  — 0.4 ≤ score < 0.7
    "drifted"  — score < 0.4

## Output format (JSON, no other text)

{{
  "similarity": <float>,
  "reasoning": "<one short paragraph, ≤3 sentences>",
  "verdict": "<aligned|partial|drifted>"
}}
"""


@dataclass
class DeltaResult:
    similarity: float
    reasoning: str
    verdict: str  # "aligned" | "partial" | "drifted"
    raw: str


_JSON_RE = re.compile(r"\{.*\}", re.DOTALL)


async def score_description_delta(
    prompt: str,
    description: str,
    *,
    model: str = _DEFAULT_MODEL,
) -> DeltaResult:
    """Judge how semantically aligned the agent-written description
    is with the original user prompt.  Returns a :class:`DeltaResult`.

    Robust to LLM output variance: looks for the first JSON object in
    the response, falls back to defaults on parse error.
    """
    content = _JUDGE_PROMPT.format(prompt=prompt.strip(), description=description.strip())
    resp = await _llm.create(
        model=model,
        system="You are a precise semantic similarity judge. Output only JSON.",
        messages=[{"role": "user", "content": content}],
        max_tokens=600,
        temperature=0.0,
    )
    raw = resp.content

    match = _JSON_RE.search(raw)
    if not match:
        return DeltaResult(
            similarity=0.0,
            reasoning=f"judge output not parseable: {raw[:200]}",
            verdict="drifted",
            raw=raw,
        )
    try:
        data = json.loads(match.group(0))
    except json.JSONDecodeError:
        return DeltaResult(
            similarity=0.0,
            reasoning=f"judge output invalid JSON: {match.group(0)[:200]}",
            verdict="drifted",
            raw=raw,
        )

    similarity = float(data.get("similarity", 0.0))
    reasoning = str(data.get("reasoning", ""))
    verdict = str(data.get("verdict", ""))
    if verdict not in ("aligned", "partial", "drifted"):
        verdict = (
            "aligned" if similarity >= 0.7
            else "partial" if similarity >= 0.4
            else "drifted"
        )
    return DeltaResult(
        similarity=similarity,
        reasoning=reasoning,
        verdict=verdict,
        raw=raw,
    )


def score_description_delta_sync(
    prompt: str, description: str, *, model: str = _DEFAULT_MODEL
) -> DeltaResult:
    return asyncio.run(score_description_delta(prompt, description, model=model))


if __name__ == "__main__":
    # Simple CLI smoke: compare two strings
    import sys
    if len(sys.argv) < 3:
        print("usage: description_delta.py <prompt> <description>")
        raise SystemExit(2)
    r = score_description_delta_sync(sys.argv[1], sys.argv[2])
    print(json.dumps({
        "similarity": r.similarity,
        "verdict": r.verdict,
        "reasoning": r.reasoning,
    }, indent=2))
