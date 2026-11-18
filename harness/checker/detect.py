#!/usr/bin/env python3
"""Core detection and fix logic for HarnessChecker.

Shared by the test suite (tests/test_detection.py) and the user-facing CLI (check.py).
"""

from __future__ import annotations

import re
from dataclasses import dataclass

from agent.llm_client import LLMClient

_llm = LLMClient()

# ── Prompts ──────────────────────────────────────────────────────────────────

DETECTION_PROMPT = """You are an expert fuzz harness auditor. Analyze the following fuzzer source code \
and detect ALL violations of the Four Principles of fuzz harness quality.

## The Four Principles

**P1 — Fuzzer Logic Must Be Correct**: The harness code itself must be bug-free.
Common P1 violations:
- Unaligned memory reads (casting uint8_t* to wider type pointer and dereferencing)
- Stale state across calls (static/global variables not reset between LLVMFuzzerTestOneInput calls)
- Input space collapse (hardcoded constants where fuzz bytes should drive variation)
- Dead code paths (conditions that always evaluate the same way)
- Missing NULL checks after allocation/creation functions THAT CAN FAIL FOR INPUT-DEPENDENT REASONS
  (not OOM-only — see Non-violations below)
- Resource leaks (memory, file handles, library objects not freed on all paths)
- Incorrect variable usage (using wrong variable, e.g. always x when y should sometimes be used)
- Buffer size mismatches (telling API the buffer is larger than it actually is)
- Missing null-termination (passing non-null-terminated data to APIs requiring C strings)
- Inverted return value checks (treating success as failure or vice versa)
- memcmp/memcpy confusion (using memcmp where memcpy was intended, or vice versa)
- Wrong character encoding (using raw integer where ASCII character is needed, e.g. 5 vs '5')
- Dangerous initialization (memset with non-zero values filling pointer fields with garbage)
- longjmp bypassing cleanup (setjmp/longjmp skipping free() calls)
- Double-free / ownership confusion (freeing objects whose ownership was transferred to a library)
- reinterpret_cast reading pointer address instead of pointed-to data

**P2 — Follow Target API Protocol**: The harness must call target library APIs correctly.
Common P2 violations:
- Wrong initialization order (calling setup functions before required init)
- Incomplete API usage / create-and-discard: A resource is created and the ONLY operation
  performed on it is destruction/cleanup — no processing function ever receives it.
  Example: cmsCreateInkLimitingDeviceLink() → cmsCloseProfile() with ZERO calls in between
  that use the profile (no cmsCreateTransform, no cmsDoTransform, nothing).
  IMPORTANT: If ANY function is called with the resource as argument (other than free/close/destroy),
  it is NOT create-and-discard. For example: create → set_metadata → paint → destroy is NOT
  create-and-discard because the resource IS used (set_metadata, paint).
- Missing required setup steps (skipping mandatory API calls)
- Wrong cleanup order (freeing resources in wrong order)
- Parameter constraint violations (calling API with out-of-range or wrong-type parameters)
- API level mixing (mixing low-level and high-level APIs that shouldn't be combined)
- Wrong parameter types (passing FILE* where BZFILE* is expected, etc.)
- Calling functions on wrong object type (e.g., PDF functions on image surface)
- Same object as both source and destination when API requires different objects
- Missing status/error reset between API phases
- No-op fuzzer (core API call commented out or disabled)
- Wrong number of arguments (function signature changed, caller not updated)

**P3 — Respect Security Boundaries**: The harness should only test public API, not internals.
Common P3 violations:
- Including internal/private headers instead of public API headers. Look at ALL #include directives:
  headers from paths like `internal/`, `private/`, `core/`, `src/`, `common/`, `util/` are often internal.
  If the project has a documented public API header (e.g., `wasm_export.h`, `openssl/evp.h`, `zlib.h`)
  but the fuzzer also includes headers from implementation directories, that is P3.
  Example: `#include "wasm_runtime_common.h"` (internal) when `#include "wasm_export.h"` (public) suffices.
  Example: `#include "util/regional.h"` and `#include "sldns/sbuffer.h"` — internal unbound headers.
- Calling internal functions not meant for external use \
(e.g., `parse_packet()` instead of public API like `ub_resolve()`)
- Dangerous initialization creating unrealistic testing conditions (memset with 0xFE fills pointer fields with garbage)
- Testing through internal data structures instead of public API
- Bypassing the public interface to directly access internal implementation

IMPORTANT for P3: ALWAYS check ALL #include lines. If ANY included header is from an internal/implementation
directory rather than the public API, flag it as P3 even if the code also includes the public header.

**P4 — Use Correct Entry Points**: The harness should operate on the right objects and have essential setup.
Common P4 violations:
- Missing essential setup that makes the entire tested path unreachable:
  e.g., QUIC/TLS server without certificate+private key — TLS 1.3 mandates server cert,
  so SSL_accept_connection() always fails, making the entire server path dead code.
  This is P4 because the fuzzer never reaches its intended entry point.
- I/O on wrong object (reading/writing on listener instead of accepted connection)
- Operating on wrong abstraction level
- Stale static state that makes fuzz input irrelevant to control flow (e.g., static flow struct
  reused across calls, making protocol state depend on previous iterations not current input)

## Important notes
- OOM-only NULL checks (malloc/calloc returning NULL) are low-priority but still flag them.
- If no public API alternative exists for a function, it is NOT a P3 violation.
- Passing fuzz-derived values (size, stride, format) to library functions that validate parameters
  internally and return error/nil is NOT a P1/P2 bug — if the code checks the return status after
  the call, the harness correctly handles invalid parameters. This is correct fuzzer behavior.
- Extracting a property from a resource (get_format, get_size, get_type) COUNTS as using the resource.
  A resource that has get_* called on it is NOT create-and-discard.

## Instructions

You MUST check ALL four principles independently. Do NOT stop after finding P1 issues.

1. Read the ENTIRE source code carefully, starting with #include directives for P3 checks
2. Check P1: scan for logic bugs (leaks, UB, wrong values, etc.)
3. Check P2: trace EVERY API call — verify call order, parameter types, src!=dest constraints,
   argument counts. Also trace the LIFECYCLE of every created resource: is it actually USED
   before being freed? If a resource is created and immediately closed/freed without being
   passed to any processing function, that is a P2 create-and-discard violation.
   If the same variable appears as both source and destination parameter in
   a function call (e.g., `func(obj, obj, ...)` where first arg is src and second is dest),
   that is a P2 violation.
4. Check P3: verify all #include headers are public API
5. Check P4: verify the fuzzer can actually reach its intended test target
6. For EACH violation found, output a structured block:
   ```
   VIOLATION: P<N> — <brief title>
   LINES: <key line numbers, max 5>
   DESCRIPTION: <what's wrong and why>
   CONSEQUENCE: <observable effect: crash, reduced coverage, UB, leak, dead code, false positive>
   ```
3. After all violations, output:
   ```
   PRINCIPLES_VIOLATED: P1, P2  (or whichever apply)
   BUG_TYPES: <comma-separated bug types>
   ```
4. If the fuzzer is CLEAN (no violations), output:
   ```
   CLEAN: No violations found
   PRINCIPLES_VIOLATED: NONE
   ```

## Fuzzer to analyze

Project: {project}
Fuzzer: {fuzzer}

```
{source_code}
```
"""


FIX_PROMPT = """You are an expert fuzz harness fixer. Given the original buggy fuzzer \
and the violations found, generate a MINIMAL fixed version.

## Rules
1. Fix ONLY P1 and P2 violations — these are the actionable correctness bugs
2. Do NOT fix P3 (boundary) or P4 (entry point) violations — these are architectural observations,
   not code-level fixes. DO NOT rewrite the fuzzer to use a different API.
3. PRESERVE the original fuzzer's structure, API calls, and overall approach
4. Change the MINIMUM number of lines needed to fix P1/P2 issues
5. Ensure correct cleanup on ALL paths
6. Follow the same coding style as the original
7. Keep the same #include headers (do NOT replace internal headers with public API)

## Common minimal fixes
- Resource leak: add missing free()/close()/unref() calls
- Stale state: add cleanup/reset call between iterations (e.g., regional_free_all())
- Unaligned read: replace `*(uint32_t*)ptr` with `memcpy(&val, ptr, sizeof(val))`
- Missing null check: add `if (!ptr) return 0;`
- Buffer size mismatch: cap the size variable to actual buffer size
- Missing null termination: allocate buf with +1, copy data, add '\\0'
- Wrong API order: swap the two function calls
- Same src/dest: create a separate dest object
- Wrong function on wrong object type: REMOVE the mismatched calls entirely
  (e.g., remove cairo_pdf_surface_* calls when surface is image type, not PDF)

## Original source
Project: {project}
Fuzzer: {fuzzer}

```
{source_code}
```

## Violations found
{violations}

## Output
Output ONLY the complete fixed source code wrapped in ```c or ```cpp code blocks.
Add a brief comment at each fix point explaining the change.
The fix must be as SMALL as possible — ideally just a few lines changed.
"""


TRIAGE_PROMPT = """You are an expert fuzz harness auditor performing a second-pass triage.
A first-pass analysis found P1/P2 violations in this fuzzer. Your job is to determine
which violations are ACTIONABLE (real bugs needing code fixes) vs FALSE_POSITIVE
(theoretical, OOM-dependent, or library-handled issues that don't need fixing).

## FALSE_POSITIVE rules — if ANY of these apply, the violation is NOT actionable:
- Missing NULL check after malloc/calloc/realloc/strdup/new → FALSE_POSITIVE (OOM-only)
- Missing NULL check after ANY library allocator that only fails on OOM
  (e.g., BIO_new, ares_buf_create, apr_pool_create, cairo_create, g_malloc) → FALSE_POSITIVE
- Null dereference BEFORE null check: if the pointer comes from a function that only returns
  NULL on OOM (pool allocator, apr_create*, malloc wrapper), dereferencing it before the
  null check is OOM-dependent and cannot happen in practice → FALSE_POSITIVE
- Resource leak only on OOM error path → FALSE_POSITIVE
- Library function validates parameters internally and returns error/nil (not crash) → FALSE_POSITIVE
  This includes: cairo_image_surface_create_for_data with wrong stride returns nil surface,
  the code checks status → NOT a bug. Passing fuzz-controlled size/stride to such functions
  is correct fuzzer behavior.
- Pool/arena-managed resources freed by pool destruction (apr_pool_terminate) → FALSE_POSITIVE
  This includes ALL Apache APR allocations — pools clean up everything. ALL memory allocated
  from an APR pool (apr_pcalloc, apr_pstrdup, apr_table_make, etc.) is freed by apr_pool_terminate.
  Individually freeing pool-allocated objects is unnecessary.
- Passing fuzz-derived data as API arguments → FALSE_POSITIVE (intended behavior)
- Manual struct init with all critical fields properly set → FALSE_POSITIVE
- Stack-allocated structs with partial initialization: if only the fields actually accessed by the
  target function are set and the struct has no public constructor API, partial init is correct.
  Unset fields that are never read by the code path → FALSE_POSITIVE
- Using fuzz input to select enum values / API options → FALSE_POSITIVE
- Static singleton error objects (e.g., cairo nil surfaces) not freed → FALSE_POSITIVE
- Preprocessor #ifdef creates separate code paths — ONLY analyze violations in the DEFAULT
  (active) path. Violations in inactive #ifdef/#ifndef paths → FALSE_POSITIVE
- Resource IS used before being freed (any function call with it as arg) → NOT create-and-discard
- Uninitialized struct fields that are never read by the code path → FALSE_POSITIVE
- Architecture-inherent issues: if the harness constructs internal objects because no public API
  exists, imperfect initialization is FALSE_POSITIVE — there is no better way to write this code.
  If a "fixed" version would crash or be unable to reach the test target, the original is correct.
- Allocation size that doesn't match sizeof(type) but works in practice → FALSE_POSITIVE
- Const-to-non-const casts required by API signatures → FALSE_POSITIVE
- Using internal/extern functions because the public API cannot produce required objects
  (e.g., extern ap_create_request to make request_rec) → architecture-inherent, FALSE_POSITIVE
- Passing fuzz-controlled values (size, stride, format) to creation functions that validate
  parameters internally: if the code checks return status/error after the call, the harness
  handles invalid parameters correctly. This IS correct fuzzer behavior → FALSE_POSITIVE
- Extracting a property from a resource (get_format, get_size, get_type, etc.) IS using the
  resource. A resource that has get_* called on it before destruction is NOT create-and-discard
  → FALSE_POSITIVE for the "create-and-discard" claim

## ACTIONABLE rules — real bugs that need code fixes:
- Unaligned memory reads (cast uint8_t* to wider pointer)
- Stale state (static/global not reset between fuzzer calls)
- Wrong encoding (integer vs ASCII character)
- memcmp/memcpy confusion
- Buffer size mismatch (telling API buffer is larger than actual)
- Missing null-termination for C string APIs
- Wrong API call order
- Same object as both src and dest
- Wrong function on wrong object type (PDF func on image surface)
- Create-and-discard: resource created → ONLY closed/freed, NO processing calls in between
- Inverted return value checks
- Dangerous memset (0xFE filling pointer fields)
- Real resource leaks (not OOM-dependent, not pool-managed)

## Source code
```
{source_code}
```

## First-pass violations found
{violations}

## Output
For each P1/P2 violation listed above, state: ACTIONABLE or FALSE_POSITIVE with a brief reason.
Then output one line:
```
NEEDS_FIX: YES
```
or
```
NEEDS_FIX: NO
```
Output NEEDS_FIX: YES only if at least one P1/P2 violation is ACTIONABLE.
"""


# ── LLM call ─────────────────────────────────────────────────────────────────


async def call_llm(model: str, prompt: str, max_tokens: int = 4096) -> str:
    """Call LLM and return response text via the shared LLMClient."""
    response = await _llm.create(
        model=model,
        system="",
        messages=[{"role": "user", "content": prompt}],
        max_tokens=max_tokens,
    )
    return response.content


# ── Parse detection output ───────────────────────────────────────────────────


def parse_detection(output: str) -> list[str]:
    """Extract detected principles from LLM output."""
    principles = set()

    match = re.search(r"PRINCIPLES_VIOLATED:\s*(.+)", output, re.IGNORECASE)
    if match:
        text = match.group(1).strip()
        if "NONE" in text.upper():
            return []
        for p in re.findall(r"P[1-4]", text):
            principles.add(p)

    for m in re.finditer(r"VIOLATION:\s*P([1-4])", output, re.IGNORECASE):
        principles.add(f"P{m.group(1)}")

    return sorted(principles)


# ── High-level check function ────────────────────────────────────────────────


@dataclass
class CheckResult:
    project: str
    fuzzer: str
    principles: list[str]
    has_violations: bool
    needs_fix: bool  # True only if actionable P1/P2 confirmed by triage
    detection_output: str
    triage_output: str = ""
    fix_code: str = ""
    fix_output: str = ""


def parse_triage(output: str) -> bool:
    """Parse triage output. Returns True if NEEDS_FIX: YES."""
    match = re.search(r"NEEDS_FIX:\s*(YES|NO)", output, re.IGNORECASE)
    if match:
        return match.group(1).upper() == "YES"
    # Default to YES if parsing fails (conservative)
    return True


async def check_harness(
    source_code: str,
    project: str,
    fuzzer: str,
    model: str = "deepseek/deepseek-chat",
    generate_fix: bool = True,
    skip_triage: bool = False,
    detection_retries: int = 1,
) -> CheckResult:
    """Analyze a fuzzer and optionally generate a fix.

    Pipeline:
    1. Detection: find all potential violations (with optional union retry)
    2. Triage (if P1/P2 found): 5-vote majority to filter false positives
    3. Fix (if actionable): generate minimal fix

    Args:
        detection_retries: Number of detection runs. Results are union-merged
            to handle LLM non-determinism. Default 1 (single run).

    Returns a CheckResult with the verdict:
    - needs_fix=False, has_violations=False → CLEAN
    - needs_fix=False, has_violations=True  → issues found but not actionable
    - needs_fix=True                        → actionable P1/P2 bugs confirmed
    """
    # Step 1: Detection (with optional union retry)
    prompt = DETECTION_PROMPT.format(
        project=project,
        fuzzer=fuzzer,
        source_code=source_code,
    )
    all_principles: set[str] = set()
    detection_output = ""
    for attempt in range(detection_retries):
        output = await call_llm(model, prompt)
        detected = parse_detection(output)
        all_principles |= set(detected)
        if attempt == 0:
            detection_output = output
        else:
            detection_output += "\n\n--- RETRY ---\n" + output
    principles = sorted(all_principles)

    has_violations = len(principles) > 0
    has_p1p2 = bool({"P1", "P2"} & set(principles))

    # Step 2: Triage (if P1/P2 detected) — 5 parallel calls, majority vote
    triage_output = ""
    needs_fix = has_p1p2
    if has_p1p2 and not skip_triage:
        triage_prompt = TRIAGE_PROMPT.format(
            source_code=source_code,
            violations=detection_output,
        )
        import asyncio as _asyncio

        triage_tasks = [call_llm(model, triage_prompt, max_tokens=2048) for _ in range(5)]
        triage_outputs = await _asyncio.gather(*triage_tasks)
        votes = [parse_triage(t) for t in triage_outputs]
        needs_fix = sum(votes) >= 3  # majority vote: 3/5 must say YES
        triage_output = triage_outputs[0]  # keep first for reporting

    # Step 3: Fix (if actionable)
    fix_code = ""
    fix_output = ""
    if needs_fix and generate_fix:
        fix_prompt = FIX_PROMPT.format(
            project=project,
            fuzzer=fuzzer,
            source_code=source_code,
            violations=detection_output,
        )
        fix_output = await call_llm(model, fix_prompt, max_tokens=8192)
        code_match = re.search(r"```(?:c|cpp|cc)?\n(.*?)```", fix_output, re.DOTALL)
        if code_match:
            fix_code = code_match.group(1)

    return CheckResult(
        project=project,
        fuzzer=fuzzer,
        principles=principles,
        has_violations=has_violations,
        needs_fix=needs_fix,
        detection_output=detection_output,
        triage_output=triage_output,
        fix_code=fix_code,
        fix_output=fix_output,
    )
