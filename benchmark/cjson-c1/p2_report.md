## target: cJSON_ParseWithOpts

### P2.1 Init Sequence
**Claim:** No library-wide init call is required before `cJSON_ParseWithOpts`. The only implicit state is the internal `global_hooks` (malloc/free wrappers), which is statically initialized at load time; `cJSON_InitHooks` is optional and only needed if the caller wants custom allocators. No object must be created before calling the target — it is the entry point that creates the root `cJSON` object itself.
**Evidence:**
> cJSON.c:186: `static internal_hooks global_hooks = { internal_malloc, internal_free, internal_realloc };` — statically initialized, no call needed.
> cJSON.c:1166: `item = cJSON_New_Item(&global_hooks);` inside `cJSON_ParseWithLengthOpts` — the target allocates its own root node, caller supplies none.
> cJSON.h:150: `CJSON_PUBLIC(void) cJSON_InitHooks(cJSON_Hooks* hooks);` — optional, only for custom allocators, not required before parse.

### P2.2 Parameter Construction
**Claim:** `value` must be a non-NULL pointer to a byte buffer (does not have to be NUL-terminated semantically for parsing itself, but `cJSON_ParseWithOpts` internally computes `buffer_length = strlen(value) + 1`, so `value` *must* in practice be NUL-terminated or the `strlen` call will over-read). `return_parse_end` is an optional `const char **` (may be NULL) that receives a pointer into `value` on success or failure. `require_null_terminated` is a `cJSON_bool` (0 or 1) flag selecting strict trailing-garbage rejection.
**Evidence:**
> cJSON.c:1131-1144: `cJSON_ParseWithOpts(const char *value, ...) { if (NULL == value) return NULL; buffer_length = strlen(value) + sizeof(""); return cJSON_ParseWithLengthOpts(value, buffer_length, return_parse_end, require_null_terminated); }`
> cJSON.h:156-157: doc comment: "ParseWithOpts allows you to require (and check) that the JSON is null terminated, and to retrieve the pointer to the final byte parsed."
> tests/parse_with_opts.c:31-37: calls with `value=NULL`, `return_parse_end=NULL`, `require_null_terminated=false/true` — all exercised as legal parameter combinations.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: call target (create+parse in one step, no separate "create" call) → use returned `cJSON*` tree via accessor/print APIs → `cJSON_Delete()` the tree exactly once when done. There is no separate "configure" step for the parser itself (options are passed as arguments to the call, not through a mutable context object).
**Evidence:**
> cJSON.c:1192: `return item;` — target returns the fully parsed tree on success.
> cJSON.c:1194-1198 (fail label): `if (item != NULL) { cJSON_Delete(item); }` — on internal failure the partially built tree is freed by the target itself, caller never sees a partial tree.
> tests/parse_with_opts.c:32-34: `item = cJSON_ParseWithOpts("{}", NULL, false); ... cJSON_Delete(item);` — standard create→use→delete pattern.

### P2.4 Return Value Handling
**Claim:** Returns `NULL` on any failure (NULL `value`, zero length, allocation failure, syntax error, or — when `require_null_terminated` is true — trailing non-whitespace/non-NUL bytes after the value). Callers must check for NULL before dereferencing. On failure, `cJSON_GetErrorPtr()` (or `*return_parse_end` if non-NULL) gives the error location; on success, `*return_parse_end` (if requested) points to the first unconsumed byte.
**Evidence:**
> cJSON.c:1172-1176: `if (!parse_value(item, ...)) { goto fail; }` — syntax errors route to `fail`.
> cJSON.c:1194-1223 (fail label): sets `global_error` and `*return_parse_end` before `return NULL;`.
> tests/parse_with_opts.c:57-59: `TEST_ASSERT_NULL(cJSON_ParseWithOpts(json, &parse_end, false)); TEST_ASSERT_EQUAL_PTR(json + strlen(json), parse_end); TEST_ASSERT_EQUAL_PTR(json + strlen(json), cJSON_GetErrorPtr());` — shows the documented error-pointer contract.
> cJSON.c:94-97: `cJSON_GetErrorPtr(void) { return (const char*)(global_error.json + global_error.position); }`.

### P2.5 Cleanup Sequence
**Claim:** On success, caller must call `cJSON_Delete(item)` exactly once to free the whole tree (recursively frees `child`, `next`, `string`, `valuestring`). On failure the target has already freed any partial tree internally — caller must NOT call `cJSON_Delete` on the NULL return. If the input buffer (`value`) was heap-allocated by the caller (e.g. read from a file), it is independent of the cJSON tree and must be freed separately by the caller after parsing (the target does not take ownership of `value`).
**Evidence:**
> cJSON.c:253-266: `cJSON_Delete(cJSON *item) { ... cJSON_Delete(item->child); ... cJSON_Delete(next); hooks.deallocate(item->valuestring); hooks.deallocate(item->string); hooks.deallocate(item); }` (recursive full-tree free).
> cJSON.c:1194-1198: internal `cJSON_Delete(item)` on the `fail:` path — confirms caller must not double-free a NULL result.
> tests/parse_with_opts.c:117 area / fuzzing/afl.c:110-120: `json = read_file(filename); ... item = cJSON_Parse(json + 2); ...` then later `free(json)` is caller's responsibility, separate from `cJSON_Delete(item)`.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase (verified by grep/read in cJSON.c and cJSON.h at the given lines): `cJSON_ParseWithOpts`, `cJSON_ParseWithLengthOpts`, `cJSON_Parse`, `cJSON_ParseWithLength`, `cJSON_Minify`, `cJSON_Print`, `cJSON_PrintUnformatted`, `cJSON_PrintBuffered`, `cJSON_PrintPreallocated`, `cJSON_Delete`, `cJSON_GetErrorPtr`, `cJSON_InitHooks`, `cJSON_malloc`, `cJSON_free`.
**Evidence:**
> cJSON.h:154-166 declares all the Parse*/Print* variants.
> cJSON.c:1131,1147,1227,1232,2924,1307,1312,1317,1348(approx),253,94,3197,3202 — definitions found at each matching line (see individual sections above for exact snippets).

### P2.7 Co-call Constraints
**Claim:** The only mandatory pairing is create/parse (target) ↔ `cJSON_Delete` (destroy) — exactly once each, no "begin/commit" or lock/unlock semantics. `cJSON_Minify` is a standalone in-place transform with no pairing requirement (it does not allocate or need a matching free beyond the buffer the caller already owns). `cJSON_Print`/`cJSON_PrintUnformatted` allocate a new string that must be freed by the caller (with `cJSON_free` or plain `free`, since `global_hooks` defaults to stdlib malloc/free) — independent from freeing the parsed tree.
**Evidence:**
> cJSON.h:152: "Memory Management: the caller is always responsible to free the results from all variants of cJSON_Parse (with cJSON_Delete) and cJSON_Print (with stdlib free, cJSON_Hooks.free_fn, or cJSON_free as appropriate)."
> cJSON.c:2924-2970: `cJSON_Minify(char *json)` operates purely in-place on the caller's buffer, returns void, no companion free.
> cJSON.c:1307-1315: `cJSON_Print`/`cJSON_PrintUnformatted` call `print(item, ..., &global_hooks)` which allocates via `global_hooks.allocate`; result must be freed separately from `item`.

### P2.8 Prerequisite State
**Claim:** No external context, connection, or configuration object must exist before calling the target. The only state consulted is the static `global_hooks` struct (allocator function pointers) and the static `global_error` struct (reset at the top of every parse call), both internal to cJSON.c and not caller-visible except via `cJSON_GetErrorPtr()`.
**Evidence:**
> cJSON.c:1152-1154: `/* reset error position */ global_error.json = NULL; global_error.position = 0;` — executed unconditionally at the start of every `cJSON_ParseWithLengthOpts` call, so no stale state from a previous call leaks in. Confirms parses are independent / reentrant at the single-thread level.
> cJSON.c:186: `global_hooks` default initialization, no explicit call required unless custom allocators are desired via `cJSON_InitHooks`.

### P2.9 Result Usage
**Claim:** The returned `cJSON*` tree is consumed by: (1) type/accessor predicates (`cJSON_IsObject`, `cJSON_IsArray`, `cJSON_IsString`, etc.) and getters (`cJSON_GetObjectItem`, `cJSON_GetArrayItem`, `cJSON_GetStringValue`, `cJSON_GetNumberValue`) to traverse/read it; (2) `cJSON_Print`/`cJSON_PrintUnformatted`/`cJSON_PrintBuffered`/`cJSON_PrintPreallocated` to re-serialize it to text (round-trip testing); (3) `cJSON_Delete` to free it. Real test code uses it for round-trip comparisons: parse then print then compare strings.
**Evidence:**
> tests/parse_examples.c:31-44: `parsed = cJSON_Parse(content); ... return parsed;` then (seen in `do_test`, same file, lines ~46 onward) the tree is printed and compared against an expected output file — classic parse→print round-trip pattern used throughout the test suite.
> cJSON.c:99-107: `cJSON_GetStringValue(const cJSON * const item) { if (!cJSON_IsString(item)) return NULL; return item->valuestring; }` — a typical "read result" consumer of a parsed node.
> cJSON.c:109-117: `cJSON_GetNumberValue` similarly reads `item->valuedouble` from a parsed node.
> tests/parse_with_opts.c:64-70 (`parse_with_opts_should_require_null_if_requested`): parses, asserts non-NULL, then `cJSON_Delete(item);` — delete as the terminal consumer.
> fuzzing/afl.c:110-120: `json = read_file(filename); ... item = cJSON_Parse(json + 2); if (item == NULL) { goto cleanup; } if ((argc...` (printing controlled by `enable_printing` arg) — shows Parse feeding into optional Print.

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting parameters/flags around the target:
- `require_null_terminated` (cJSON_bool): 0 = permissive, trailing bytes after a valid value are not checked, parse stops at first complete value; 1 = after skipping trailing whitespace, requires the next byte to be `'\0'` within the buffer bounds, else fails. This exercises the extra `buffer_skip_whitespace(&buffer)` + NUL check block.
- `return_parse_end` (pointer or NULL): NULL disables the output of the parse-end pointer; non-NULL always receives a value on both success and failure paths (different formula on each, see P2.4).
- `buffer_length` (in `cJSON_ParseWithLengthOpts`, computed internally as `strlen(value)+1` by the target): controls where `can_access_at_index`/bounds checks stop scanning; shorter lengths than the embedded content truncate parsing, longer lengths (if misused) could expose trailing bytes as readable but are bounds-checked against `buffer.length`.
- Inside `parse_value` (core dispatcher), the first non-whitespace byte selects the processing path: `"`→parse_string, `-`/digit→parse_number, `{`→parse_object, `[`→parse_array, `t`/`f`→boolean literal, `n`→null literal; any other byte or truncated buffer → failure.
- `CJSON_NESTING_LIMIT` (compile-time macro, default 1000, cJSON.h:134-138): caps recursion depth in parse_array/parse_object, a compiled-in security boundary.
- `cJSON_Print` vs `cJSON_PrintUnformatted`/`cJSON_PrintBuffered(fmt)`: formatting flag selects whitespace/indentation in the pretty-printer path (`print_value`), exercised after a successful parse.
**Evidence:**
> cJSON.c:1179-1186: `if (require_null_terminated) { buffer_skip_whitespace(&buffer); if ((buffer.offset >= buffer.length) || buffer_at_offset(&buffer)[0] != '\0') { goto fail; } }`
> cJSON.c:1187-1190: `if (return_parse_end) { *return_parse_end = (const char*)buffer_at_offset(&buffer); }`
> cJSON.h:134-138: `#ifndef CJSON_NESTING_LIMIT #define CJSON_NESTING_LIMIT 1000 #endif` — "Limits how deeply nested arrays/objects can be before cJSON rejects to parse them. This is to prevent stack overflows."
> cJSON.c:1317-1336: `cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt)` — `p.format = fmt;` selects formatted vs unformatted printing.
> tests/parse_with_opts.c:62-71 (`parse_with_opts_should_require_null_if_requested`): `cJSON_ParseWithOpts("{}", NULL, true)` succeeds; `cJSON_ParseWithOpts("{} \n", NULL, true)` succeeds (trailing whitespace tolerated); `cJSON_ParseWithOpts("{}x", NULL, true)` fails (trailing garbage rejected) — demonstrates the flag's effect directly.

### P2.11 Input Expectations
**Claim:** `value` must be a readable buffer at least `strlen(value)+1` bytes (since the target computes that length internally via `strlen`), implying `value` must already be NUL-terminated C string — if it is not NUL-terminated, `strlen` inside the target will over-read past the fuzz input buffer. The content may optionally begin with a UTF-8 BOM (`EF BB BF`), which is skipped. Leading/trailing ASCII whitespace (`<= 32`, i.e. space/tab/CR/LF) is skipped around the value. The parser expects exactly one JSON value (object, array, string, number, "true"/"false"/"null" literal); with `require_null_terminated=0` trailing bytes after that one value are ignored (parsing simply stops and `return_parse_end` points there); with `require_null_terminated=1` only trailing whitespace before the terminating NUL is allowed. Nesting depth is capped by `CJSON_NESTING_LIMIT` (default 1000). NULL `value` or zero `buffer_length` are explicitly rejected.
**Evidence:**
> cJSON.c:1141: `buffer_length = strlen(value) + sizeof("");` — the target assumes `value` is NUL-terminated and reads it with `strlen` BEFORE any bounds validation, i.e. the caller-supplied buffer must truly be NUL-terminated within its allocation.
> cJSON.c:1116-1129: `skip_utf8_bom` — checks `can_access_at_index(buffer,4) && strncmp(...,"\xEF\xBB\xBF",3)==0` then advances offset by 3.
> cJSON.c:1098-1105 (`buffer_skip_whitespace`): `while (can_access_at_index(buffer, 0) && (buffer_at_offset(buffer)[0] <= 32)) { buffer->offset++; }`.
> cJSON.c:1156-1159: `if (value == NULL || 0 == buffer_length) { goto fail; }` in `cJSON_ParseWithLengthOpts`.
> cJSON.h:134-138: `CJSON_NESTING_LIMIT` default 1000, documented purpose "prevent stack overflows."

## Other entries in the Logic Group

### cJSON_ParseWithLengthOpts
**Claim:** Lower-level sibling of the target; takes an explicit `buffer_length` instead of deriving it via `strlen`, so it can parse buffers that are not NUL-terminated or where the caller wants to limit/extend the visible length independent of any embedded NUL — ideal for a fuzz harness operating on a raw, non-NUL-terminated `data`/`size` buffer. Same `return_parse_end`/`require_null_terminated` semantics and same create→use→`cJSON_Delete` lifecycle as the target; it is in fact what the target forwards to.
**Evidence:**
> cJSON.c:1147: `CJSON_PUBLIC(cJSON *) cJSON_ParseWithLengthOpts(const char *value, size_t buffer_length, const char **return_parse_end, cJSON_bool require_null_terminated)` — definition.
> cJSON.c:1143: `cJSON_ParseWithOpts` calls it directly, confirming it is the shared core.
> cJSON.c:1156-1159: `if (value == NULL || 0 == buffer_length) goto fail;` — same NULL/zero-length guard as the target.
**Combination with target in one harness:** A harness may call either `cJSON_ParseWithOpts` (NUL-terminated input, derived length) or `cJSON_ParseWithLengthOpts` (explicit length, works on non-NUL-terminated `data`) — not both on the same buffer in the same iteration is required, but calling both with different `require_null_terminated`/length combinations on the same fuzz input is a valid way to broaden coverage of the boundary checks.

### cJSON_Parse
**Claim:** Convenience wrapper equal to `cJSON_ParseWithOpts(value, 0, 0)` — always permissive (`require_null_terminated=0`), never requests `return_parse_end`. Same create→use→`cJSON_Delete` lifecycle; takes a NUL-terminated `const char*`.
**Evidence:**
> cJSON.c:1227-1230: `CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value) { return cJSON_ParseWithOpts(value, 0, 0); }`
**Combination with target in one harness:** Equivalent call shape to the target with fixed flag values; using it alongside the target with `require_null_terminated=1` lets the harness exercise both the strict and permissive termination paths from one input.

### cJSON_ParseWithLength
**Claim:** Convenience wrapper equal to `cJSON_ParseWithLengthOpts(value, buffer_length, 0, 0)` — permissive, no parse-end pointer, but accepts an explicit length like `cJSON_ParseWithLengthOpts`.
**Evidence:**
> cJSON.c:1232-1235: `CJSON_PUBLIC(cJSON *) cJSON_ParseWithLength(const char *value, size_t buffer_length) { return cJSON_ParseWithLengthOpts(value, buffer_length, 0, 0); }`
**Combination with target in one harness:** Natural choice when the raw fuzz buffer (`data`, `size`) should be parsed directly without requiring NUL-termination or `strlen`; result still needs `cJSON_Delete`.

### cJSON_Minify
**Claim:** `void cJSON_Minify(char *json)` — in-place transform over a **mutable, NUL-terminated, writable** buffer; strips whitespace and `//`/`/* */` comments, rewriting the buffer shorter in place and re-NUL-terminating it. It is not a parser and returns nothing; it must be called, if at all, on a private writable copy of the fuzz input BEFORE any of the Parse* functions, never on a `const` or read-only buffer (libFuzzer's `data` is `const uint8_t*`, so a harness must `memcpy` into a heap buffer first). No cleanup is implied for the output since minify modifies the caller's own buffer (no new allocation), but the harness must still free whatever buffer it allocated for the copy.
**Evidence:**
> cJSON.c:2924-2970: `CJSON_PUBLIC(void) cJSON_Minify(char *json) { char *into = json; if (json == NULL) { return; } while (json[0] != '\0') { switch(json[0]) { ... } } *into = '\0'; }` — operates purely in place, requires NUL-terminated mutable input.
> tests/minify_tests.c:37,40,52,66,80,94,149,158: all call sites pass a local writable `char[]`/`char*` buffer, never the output of `cJSON_Parse`.
**Combination with target in one harness:** `cJSON_Minify(buffer)` then `cJSON_Parse(buffer)` (or `ParseWithOpts`) is the documented chaining pattern to strip comments before parsing; requires the harness to own a writable copy of the fuzz bytes.

### cJSON_Print / cJSON_PrintUnformatted
**Claim:** Both take a `const cJSON *item` (non-NULL expected; the pretty-printer walks `item->type`/`child`/`next`) and return a heap-allocated, NUL-terminated `char*` or NULL on allocation/print failure. They are the canonical consumers of a parsed tree in round-trip tests, and their result must be freed with `cJSON_free`/stdlib `free` (not `cJSON_Delete`, which is for the tree, not the string).
**Evidence:**
> cJSON.c:1307-1310: `CJSON_PUBLIC(char *) cJSON_Print(const cJSON *item) { return (char*)print(item, true, &global_hooks); }`
> cJSON.c:1312-1315: `CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item) { return (char*)print(item, false, &global_hooks); }`
> cJSON.h:152: memory-management contract quoted above (free Print results with stdlib free/cJSON_free, free Parse results with cJSON_Delete).
**Combination with target in one harness:** `item = cJSON_ParseWithOpts(...); if (item) { char *out = cJSON_Print(item); free(out); cJSON_Delete(item); }` is the standard round-trip pattern seen in the test suite (tests/parse_examples.c `do_test`).

## Build notes

**Claim:** cJSON is a C project (compiled as C via CMake into `libcjson.a`), but the OSS-Fuzz driver file is compiled with `$CXX` (so the harness source itself is typically written as C++/C and must declare `extern "C"` for `LLVMFuzzerTestOneInput`, and can include `cJSON.h` directly since it has its own `extern "C"` guards for C++ consumers). The library header is found via `-I.` pointing at the build directory (CMake copies/generates headers there) — a harness in `fuzzing/` includes it as `"../cJSON.h"` when run from source tree, or as `<cJSON.h>`/`"cJSON.h"` with the `-I.` include path used by the real OSS-Fuzz build. Linking is against the static archive `libcjson.a` plus `$LIB_FUZZING_ENGINE`.
**Evidence:**
> ossfuzz_view/cjson-c1/build.sh:1-24: `$SRC/cjson/fuzzing/ossfuzz.sh` is invoked, then a separate CMake test build with `-DENABLE_CJSON_TEST=ON`.
> cjson/fuzzing/ossfuzz.sh:6-13: 
  ```
  mkdir build
  cd build
  cmake -DBUILD_SHARED_LIBS=OFF -DENABLE_CJSON_TEST=OFF ..
  make -j$(nproc)
  $CXX $CXXFLAGS $SRC/cjson/fuzzing/cjson_read_fuzzer.c -I. \
      -o $OUT/cjson_read_fuzzer \
      $LIB_FUZZING_ENGINE $SRC/cjson/build/libcjson.a
  ```
  — shows static lib build then `$CXX` compile of the fuzzer source with `-I.` (the `build/` dir, which CMake populates with `cJSON.h`/config), linked against `libcjson.a` and the fuzzing engine.
> ossfuzz_view/cjson-c1/Dockerfile:18-26: `FROM gcr.io/oss-fuzz-base/base-builder` + `git clone ... cjson` — standard OSS-Fuzz C/C++ base image, `cmake` installed explicitly (line 21), confirming CMake-based native build.
> cjson/fuzzing/cjson_read_fuzzer.c: current repo copy is a stub (`/* Gold harness replaced by an empty stub for baseline generation. */`) that just defines an empty `LLVMFuzzerTestOneInput`; the real content (parse+optional minify+print, per the project's upstream fuzzer) is not present verbatim in this checkout, so the downstream harness generator must write it fresh using the API surface documented above — no existing concrete `cJSON_ParseWithOpts` fuzzer body to copy from in this source tree.
> cjson/fuzzing/afl.c:1-120 (non-libFuzzer driver, same project) is the closest available concrete reference for call order: `read_file()` (owns/free's its own buffer) → `cJSON_Parse(json + 2)` → NULL-check → optional `cJSON_Print`-style serialization → cleanup. This is useful evidence of the expected call shape even though it's an AFL (not libFuzzer) driver.