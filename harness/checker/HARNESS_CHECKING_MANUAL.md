# Fuzz Harness Checking Manual

A comprehensive guide for detecting and fixing violations of the Four Principles (P1-P4) in OSS-Fuzz harnesses.

---

## 1. The Four Principles

### P1: Fuzzer Logic Must Be Correct

The harness code itself must be free of bugs. Common P1 violations:

- **Unaligned memory reads**: Casting `uint8_t*` to `uint32_t*` is undefined behavior on strict-alignment architectures. Fix: use `memcpy()`.
- **Stale state across calls**: Static/global variables not reset between `LLVMFuzzerTestOneInput` invocations. The fuzzer is called thousands of times in one process — all mutable state must be re-initialized each call.
- **Incorrect variable usage**: Using wrong variable names (e.g., always using `x` when `y` should be used on some paths), reducing the effective input space.
- **Memory leaks in harness**: Forgetting to free resources (profiles, transforms, contexts) on error paths. This causes LSAN to report leaks that look like library bugs but are harness bugs.
- **NULL dereference in harness**: Not checking return values from allocation functions before use.
- **Logic errors reducing input space**: Hardcoded constants where fuzz input should drive variation, modular arithmetic that collapses the input space (e.g., `% 401` with only 4 bytes of input = only 401 possible values).

### P2: Follow Target API Protocol

The harness must call the target library's API in the correct sequence, with valid parameters, following the documented contract.

- **Wrong initialization order**: Calling `set_params()` before `init()` when the API requires init first (e.g., OpenSSL EVP: must `EVP_EncryptInit_ex2()` before `EVP_CIPHER_CTX_set_params()`).
- **Missing required setup**: Skipping mandatory initialization steps (e.g., not calling `yaml_emitter_open()` before `yaml_emitter_dump()`).
- **Incomplete API usage**: Creating a resource but never using it in its intended pipeline (e.g., creating a device link profile but never passing it to `cmsCreateTransform()` + `cmsDoTransform()`).
- **Wrong cleanup order**: Freeing resources in wrong order or missing cleanup on error paths.
- **Parameter constraint violations**: Passing parameters outside documented valid ranges.

### P3: Do Not Cross Security Boundaries

The harness must only test through public APIs.

- Using internal headers (`#include "internal.h"`, `#include "*.c"`)
- Calling unexported/private functions directly
- Accessing internal struct fields

**Note**: P3 violations are treated as *generation guidelines* for LLM-based harness generation, not as empirical bugs. They don't produce measurable coverage impact and are better addressed at the generation stage.

### P4: Use Correct Entry Point

The harness must test through realistic, production-relevant entry points.

- Testing internal parsers instead of the public API that wraps them
- Testing test-only functions that are not part of the public interface
- Testing seed generators instead of the consumer functions

**Note**: Like P3, P4 violations are generation guidelines.

---

## 2. Detection Workflow

### Step 1: Locate the Fuzzer Source

Fuzzers live in two places:

1. **Upstream** (`fuzzer_location: upstream`): In the project's own repo under `fuzz/`, `test/fuzz/`, or similar.
2. **OSS-Fuzz** (`fuzzer_location: oss-fuzz`): In `google/oss-fuzz` repo under `projects/<project>/`.

```bash
# For OSS-Fuzz fuzzers:
ls oss-fuzz-repo/projects/<project>/*.c oss-fuzz-repo/projects/<project>/*.cc

# For upstream fuzzers, clone the project and look for fuzz directories:
find <project_repo> -name "*fuzz*" -type f \( -name "*.c" -o -name "*.cc" -o -name "*.cpp" \)
```

### Step 2: Read and Understand the Fuzzer

Before making any judgment, **read the entire fuzzer source code**. Understand:

1. **What API is being tested?** Identify the target library functions called.
2. **What is the intended test scenario?** Read the fuzzer name and any comments.
3. **What is the data flow?** How does fuzz input (`data`, `size`) get consumed?
4. **What is the resource lifecycle?** Track all allocations and their corresponding frees.

### Step 3: Check P1 (Logic Correctness)

Scan for:

- **Unaligned reads**: `*((uint32_t*)data)` or similar casts from `uint8_t*`
- **Static/global state**: Any `static` variables that accumulate state across calls
- **Missing NULL checks**: Return values from `malloc`, `new`, API create functions
- **Resource leaks**: Track every `open/create/alloc` and verify matching `close/free/delete`
- **Input space collapse**: Hardcoded values where fuzz input should drive variation
- **Dead code**: Code paths that can never execute (always-true/false conditions)

### Step 4: Check P2 (API Protocol)

For each API call sequence in the fuzzer:

1. **Read the API documentation** or reference examples in the same project
2. **Verify initialization order**: Is every context/handle properly initialized before use?
3. **Verify parameter validity**: Are parameters within documented ranges?
4. **Verify cleanup completeness**: Is every resource freed on all paths (including error paths)?
5. **Compare with other fuzzers**: Check if other fuzzers in the same project follow a different (correct) pattern
6. **Check for "create then discard"**: If the fuzzer creates a resource and immediately destroys it without exercising it, that's a P2 violation — the API was not used as intended

### Step 5: Cross-reference with Correct Usage

Always look for reference patterns:

- Other fuzzers in the same project that test similar APIs correctly
- Unit tests in the upstream project
- API documentation / header file comments
- Example code in the project's docs

---

## 3. Verification Workflow

### Static Analysis is NOT Enough

Many "looks wrong" patterns have zero practical impact. Every finding must be dynamically verified.

### Building and Running Fuzzers

The project provides automated OSS-Fuzz build tooling via the `infra/ossfuzz/` package
and the `BuildValidator` in `harness/builder.py`. For build commands, workspace
isolation, and Docker concurrency constraints, see
[`infra/BUILD_PARALLELISM.md`](../../infra/BUILD_PARALLELISM.md).

Quick programmatic build via `BuildValidator`:

```python
from harness.builder import BuildValidator

validator = BuildValidator(
    oss_fuzz_dir="/tmp/oss-fuzz",
    project="libyaml",
)
result = await validator.validate(harness_source, fuzzer_name="yaml_scanner_fuzz")
# result.is_valid, result.build_log
```

Or via the benchmark CLI with build validation enabled:

```bash
uv run python -m benchmark.oss_fuzz_harness.run_benchmark \
    --enable-build --oss-fuzz-dir /tmp/oss-fuzz \
    --manifest benchmark/oss_fuzz_harness/data/benchmark_cases.jsonl \
    ...
```

### Coverage Comparison

The gold standard for verification is **coverage comparison**: run the original and
fixed fuzzer for the same duration and compare edge/feature coverage. The project
automates this end-to-end via the coverage evaluator.

For the full swap-and-rebuild pipeline, pinned OSS-Fuzz workspace setup, corpus
hygiene, and output format, see
[`benchmark/oss_fuzz_harness/COVERAGE_EVAL.md`](../../benchmark/oss_fuzz_harness/COVERAGE_EVAL.md).

Quick single-case evaluation:

```bash
uv run python -m benchmark.oss_fuzz_harness.eval_coverage \
    --experiment-dir benchmark/oss_fuzz_harness/runs/my-experiment \
    --case-id libplist/bplist_fuzzer \
    --duration 60 -v
```

### Interpreting Coverage Results

- **>10% edge improvement**: Strong evidence, definitely submit
- **5-10% edge improvement**: Good evidence, likely worth submitting
- **<5% improvement or no improvement**: Weak evidence — reconsider if this is a real issue
- **Coverage decrease**: Your fix may have broken something — investigate

### When Coverage Comparison is Not Applicable

Some bugs are real but don't show coverage improvement:
- **Latent UB** (unaligned reads on x86): Real UB but x86 tolerates it — no coverage impact
- **OOM-dependent NULL checks**: `malloc` rarely returns NULL — the error path is never exercised
- **"Still reachable" memory**: LSAN can't detect pointers that are still on the stack

For these cases, provide **source-level proof** in the issue description instead of coverage data.

---

## 4. Fix Writing Guidelines

### Minimal Fixes Only

- Fix ONLY the identified violation — do not refactor, restyle, or add features
- Preserve the original fuzzer's intent and structure
- Keep the same file name, function signatures, and overall approach

### Common Fix Patterns

**Unaligned read → memcpy**:
```c
// Before (UB):
uint32_t val = *((const uint32_t *)data);

// After (safe):
uint32_t val;
memcpy(&val, data, sizeof(val));
```

**Missing state reset → memset/reinit**:
```c
// Before (stale state):
static struct flow ndpi_flow;

// After (reset each call):
struct ndpi_flow_struct ndpi_flow;
memset(&ndpi_flow, 0, sizeof(ndpi_flow));
// ... at end:
ndpi_free_flow_data(&ndpi_flow);
```

**Wrong API order → swap**:
```c
// Before (dead code):
ctx = EVP_CIPHER_CTX_new();
EVP_CIPHER_CTX_set_params(ctx, params);  // fails: no cipher
EVP_EncryptInit_ex2(ctx, cipher, key, iv, NULL);  // never reached

// After (correct order):
ctx = EVP_CIPHER_CTX_new();
EVP_EncryptInit_ex2(ctx, cipher, key, iv, NULL);
EVP_CIPHER_CTX_set_params(ctx, params);  // now succeeds
```

**Incomplete API usage → add full pipeline**:
```c
// Before (create and discard):
profile = cmsCreateInkLimitingDeviceLink(cs, limit);
cmsCloseProfile(profile);

// After (full pipeline):
profile = cmsCreateInkLimitingDeviceLink(cs, limit);
outProfile = cmsCreate_sRGBProfile();
hTransform = cmsCreateTransform(profile, srcFmt, outProfile, TYPE_BGR_8, 0, 0);
cmsCloseProfile(profile);
cmsCloseProfile(outProfile);
if (hTransform) {
    cmsDoTransform(hTransform, data, output, 1);
    cmsDeleteTransform(hTransform);
}
```

**Input space expansion → use fuzz bytes**:
```c
// Before (hardcoded):
cmsCreateInkLimitingDeviceLink(cmsSigCmykData, limit);

// After (fuzz-driven):
cmsColorSpaceSignature spaces[] = {cmsSigCmykData, cmsSigCmyData, cmsSigRgbData};
cmsColorSpaceSignature cs = spaces[data[0] % 3];
data++; size--;
cmsCreateInkLimitingDeviceLink(cs, limit);
```

### Error Path Cleanup

Every fix must ensure proper cleanup on ALL paths:
```c
if (!resource1) return 0;
resource2 = create();
if (!resource2) { free(resource1); return 0; }
resource3 = create();
if (!resource3) { free(resource2); free(resource1); return 0; }
// ... use resources ...
free(resource3);
free(resource2);
free(resource1);
```

---

## 5. Output Generation

### Issue Report Format (`{fuzzer}_issue.md`)

```markdown
# <project>: <fuzzer_name> — <brief description>

## Summary
One-paragraph description of what's wrong and why it matters.

## Fuzzer Location
- **File**: `<path>` in [repo](url)
- **Fuzzer purpose**: What the fuzzer is supposed to test

## Bug Description
### Violation 1: <P1/P2> — <specific issue>
Code snippet showing the problem, with explanation.

### Violation 2 (if any): ...

## Consequence
What observable effect does this have? (dead code, reduced coverage, false positives, UB)

## Coverage Comparison (N minutes, ASan, fork mode)
| Metric | Original | Fixed | Improvement |
|--------|----------|-------|-------------|
| Edge coverage | X | Y | +Z% |
| Features | X | Y | +Z% |
| Corpus size | X | Y | +Z% |

## Fix
Explanation of what was changed and why.
```

### Diff File (`{fuzzer}.patch`)

Standard unified diff:
```bash
diff -u original.c fixed.c > fuzzer.patch
```

---

## 6. Submission Decision Criteria

### SUBMIT if:
- Coverage improvement > 5% (edges or features)
- Clear P1/P2 violation confirmed by code reading
- Fix is minimal and correct
- No security implications (fix doesn't expose unreported vulns)

### DO NOT SUBMIT if:
- No measurable coverage improvement AND no PoC/ASAN evidence
- The "violation" is actually standard practice (check FP list)
- The fix changes the fuzzer's intent or scope
- Fixing would expose an unreported security vulnerability
- The fuzzer is dead code (not compiled by build.sh)

### Known False Positive Patterns:
- `magic_compile()` failing on random input is expected — not a leak
- `gsapi_exit()` error paths that never trigger in practice
- Static initialization in `FuzzerInitialize()` is standard libFuzzer practice
- `yaml_emitter_dump()` internally auto-opens streams
- Double-feed with unchecked returns in incremental parsing fuzzers is intentional

---

## 7. Submission Workflow

### For OSS-Fuzz fuzzers (`fuzzer_location: oss-fuzz`):
1. Fork `google/oss-fuzz`
2. Create branch: `fix-<project>-<fuzzer>`
3. Replace the fuzzer source with the fixed version
4. Create PR to `google/oss-fuzz`
5. Include coverage comparison data in PR description

### For upstream fuzzers (`fuzzer_location: upstream`):
1. Fork the project repo
2. Create branch: `fix-<fuzzer>`
3. Replace the fuzzer source with the fixed version
4. Create PR to the project repo
5. Include coverage comparison data in PR description

### One fuzzer = one PR. Never combine multiple fuzzer fixes in a single PR.
