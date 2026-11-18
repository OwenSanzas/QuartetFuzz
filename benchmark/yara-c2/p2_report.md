## target: yr_rules_scan_mem

### P2.1 Init Sequence
**Claim:** The correct order is: `yr_initialize()` (process-wide, once) → build a `YR_RULES*` via the compiler pipeline (`yr_compiler_create` → `yr_compiler_add_string`/`yr_compiler_add_file`/`yr_compiler_add_fd` → `yr_compiler_get_rules`) → optionally `yr_rules_define_*_variable` to set external variables → `yr_rules_scan_mem(rules, buffer, buffer_size, flags, callback, user_data, timeout)`. No direct scanner-level init is needed by the caller because `yr_rules_scan_mem` itself creates and destroys the `YR_SCANNER` internally.
**Evidence:**
> tests/oss-fuzz/pe_fuzzer.cc:47-58: `yr_initialize()` then `yr_compiler_create`, `yr_compiler_add_string`, `yr_compiler_get_rules`, `yr_compiler_destroy` all done once in `LLVMFuzzerInitialize`, before `yr_rules_scan_mem` is called repeatedly in `LLVMFuzzerTestOneInput` (line 77).
> libyara/rules.c:196-239 (`yr_rules_scan_mem`): internally does `GOTO_EXIT_ON_ERROR(yr_scanner_create(rules, &scanner));` then `yr_scanner_set_callback/set_timeout/set_flags`, then `yr_scanner_scan_mem(scanner, buffer, buffer_size)`, then `yr_scanner_destroy(scanner)` — the scanner is fully owned/managed inside the call, caller doesn't need to create one.
> tests/test-api.c:271-297 (`test_max_match_data`): `yr_initialize(); ... compile_rule(rules_str,&rules) ...; yr_rules_scan_mem(rules, ...); yr_rules_destroy(rules); yr_finalize();` shows canonical full-process order.

### P2.2 Parameter Construction
**Claim:** `rules` must be a non-NULL `YR_RULES*` produced by `yr_compiler_get_rules` (or `yr_rules_load`/`yr_rules_load_stream`); `buffer` is any pointer to `buffer_size` readable bytes (may be empty, `buffer_size==0` is valid); `flags` is a bitwise-OR of the `SCAN_FLAGS_*` constants (0 is valid, meaning no special behaviour); `callback` must be a function matching `YR_CALLBACK_FUNC` (non-NULL expected — it's invoked for every message); `user_data` is an opaque pointer passed through to the callback, may be NULL; `timeout` is an int number of seconds (0 = no timeout).
**Evidence:**
> libyara/include/yara/rules.h:84-91: signature `yr_rules_scan_mem(YR_RULES* rules, const uint8_t* buffer, size_t buffer_size, int flags, YR_CALLBACK_FUNC callback, void* user_data, int timeout)`.
> libyara/include/yara/scan.h:38-42: `SCAN_FLAGS_FAST_MODE=1`, `SCAN_FLAGS_PROCESS_MEMORY=2`, `SCAN_FLAGS_NO_TRYCATCH=4`, `SCAN_FLAGS_REPORT_RULES_MATCHING=8`, `SCAN_FLAGS_REPORT_RULES_NOT_MATCHING=16`.
> tests/test-api.c:627: `yr_rules_scan_mem(rules, (uint8_t*) "", 0, 0, count, &counters, 0);` shows `buffer_size=0`, `flags=0`, `timeout=0` are all accepted.
> libyara/rules.c:205-212 (`YR_DEBUG_FPRINTF`) logs `buffer`, `buffer_size`, `timeout` confirming these are the raw pass-through params.

### P2.3 Object Lifecycle
**Claim:** The target's own lifecycle for the scanner object is fully internal: create → set_callback/set_timeout/set_flags → scan → destroy, all inside one call to `yr_rules_scan_mem`. The lifecycle the harness must manage is at the `YR_RULES`/`YR_COMPILER` level: create compiler → add rule source → get rules (destroys/frees compiler-owned intermediate state only on `yr_compiler_destroy`) → scan (repeatable, many times, same `rules`) → destroy rules.
**Evidence:**
> libyara/rules.c:217-225: `yr_scanner_create` → `yr_scanner_set_callback/set_timeout/set_flags` → `yr_scanner_scan_mem` → `yr_scanner_destroy`, all within `yr_rules_scan_mem`.
> tests/oss-fuzz/pe_fuzzer.cc:41,53-58,77: a single global `rules` object compiled once in `LLVMFuzzerInitialize` is reused across many `yr_rules_scan_mem` calls in `LLVMFuzzerTestOneInput` — confirms `YR_RULES*` is reusable/stateless with respect to repeated scans.
> libyara/include/yara/rules.h:125: `YR_API int yr_rules_destroy(YR_RULES* rules);` is the terminal lifecycle call, used once rules are no longer needed.

### P2.4 Return Value Handling
**Claim:** Return is an `int` error code; `ERROR_SUCCESS` (0) means normal completion (regardless of whether any rule matched — match/no-match is reported via the callback, not the return value). Non-zero codes indicate failure/abnormal termination, notably `ERROR_SCAN_TIMEOUT` (26) if the timeout elapses, `ERROR_TOO_MANY_MATCHES` (30) if the callback for `CALLBACK_MSG_TOO_MANY_MATCHES` doesn't return `CALLBACK_CONTINUE`, and `ERROR_CALLBACK_ERROR` (28) if the callback returns `CALLBACK_ERROR`. `ERROR_INSUFFICIENT_MEMORY` (1) can also occur from internal allocation failures (e.g. in `yr_scanner_create`).
**Evidence:**
> libyara/error.h:40,47,71,75,77: `#define ERROR_SUCCESS 0`, `#define ERROR_INSUFFICIENT_MEMORY 1`, `#define ERROR_SCAN_TIMEOUT 26`, `#define ERROR_CALLBACK_ERROR 28`, `#define ERROR_TOO_MANY_MATCHES 30`.
> tests/test-api.c:353-372: `int err = yr_rules_scan_mem(...propagate_too_many_matches...); if (err != ERROR_TOO_MANY_MATCHES) { ...exit(EXIT_FAILURE); }` — demonstrates expected non-success return on callback signalling too-many-matches.
> tests/test-api.c:291-295: `if (err != ERROR_SUCCESS) { fprintf(stderr, ...); exit(EXIT_FAILURE); }` shows normal success check pattern.
> libyara/rules.c:217 `GOTO_EXIT_ON_ERROR(yr_scanner_create(rules, &scanner));` — if scanner creation fails, `result` keeps `ERROR_INTERNAL_FATAL_ERROR` default or propagated error and jumps to `_exit` without ever scanning.

### P2.5 Cleanup Sequence
**Claim:** Per-scan cleanup is automatic: `yr_rules_scan_mem` always calls `yr_scanner_destroy(scanner)` after `yr_scanner_scan_mem` returns (success or error), so the caller does not manage a scanner object. At the application level the caller must eventually call `yr_rules_destroy(rules)` (once, after all scans done) and, if a `YR_COMPILER` was used only to build rules, `yr_compiler_destroy(compiler)` right after `yr_compiler_get_rules` succeeds (compiler and rules are independent after extraction). Finally `yr_finalize()` once at process exit.
**Evidence:**
> libyara/rules.c:223-225: `result = yr_scanner_scan_mem(scanner, buffer, buffer_size); yr_scanner_destroy(scanner);` — destroy always runs after scan (not gated on result).
> tests/oss-fuzz/pe_fuzzer.cc:56: `yr_compiler_destroy(compiler);` called right after `yr_compiler_get_rules(compiler, &rules);` in `LLVMFuzzerInitialize`, while `rules` remains alive and used later.
> tests/oss-fuzz/rules_fuzzer.cc:64-68: `if (yr_compiler_get_rules(compiler, &rules) == ERROR_SUCCESS) yr_rules_destroy(rules); yr_compiler_destroy(compiler);` — shows `yr_rules_destroy` paired with successful `yr_compiler_get_rules`, and `yr_compiler_destroy` always called regardless.
> tests/test-api.c:297-298: `yr_rules_destroy(rules); yr_finalize();` final cleanup order.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase at the cited locations.
**Evidence:**
> libyara/include/yara/rules.h:84-91 `yr_rules_scan_mem`; :76-82 `yr_rules_scan_mem_blocks`; :93-99 `yr_rules_scan_file`; :101-107 `yr_rules_scan_fd`; :125 `yr_rules_destroy`; :127-145 `yr_rules_define_{integer,boolean,float,string}_variable`.
> libyara/include/yara/compiler.h:357 `yr_compiler_create`; :359 `yr_compiler_destroy`; :388 `yr_compiler_add_file`; :400 `yr_compiler_add_bytes`; :406 `yr_compiler_add_string`; :438 `yr_compiler_get_rules`.
> libyara/include/yara/scanner.h:39 `yr_scanner_create`; :41 `yr_scanner_destroy`; :43 `yr_scanner_set_callback`; :48 `yr_scanner_set_timeout`; :50 `yr_scanner_set_flags`; :76 `yr_scanner_scan_mem`.
> libyara/include/yara/libyara.h:86 `yr_initialize`; :88 `yr_finalize`.
> libyara/rules.c:196,241,264 confirm definitions of `yr_rules_scan_mem`, `yr_rules_scan_file`, `yr_rules_scan_fd`.

### P2.7 Co-call Constraints
**Claim:** Paired lifecycle APIs: `yr_initialize()`/`yr_finalize()` (process-wide, once each); `yr_compiler_create()`/`yr_compiler_destroy()` (compiler object); `yr_compiler_get_rules()` success pairs with eventual `yr_rules_destroy()`; inside `yr_rules_scan_mem` itself, `yr_scanner_create()` is unconditionally paired with `yr_scanner_destroy()` (RAII-like, handled internally — the harness never calls these directly when using `yr_rules_scan_mem`). `yr_rules_define_*_variable` must be called on a successfully-built `rules` object and before scanning if the condition depends on that external variable's value.
**Evidence:**
> libyara/rules.c:217,225: `yr_scanner_create(...)` / `yr_scanner_destroy(scanner)` pairing inside `yr_rules_scan_mem`.
> tests/oss-fuzz/pe_fuzzer.cc:47,56: `yr_initialize()` at init; (no matching `yr_finalize()` in this particular fuzzer — acceptable since OSS-Fuzz process exits, but canonical pairing shown in tests/test-api.c:271,298 `yr_initialize()`/`yr_finalize()`).
> libyara/rules.c:54-71 (`yr_rules_define_integer_variable`, read earlier) walks `rules->ext_vars_table` and requires the identifier to already exist (declared via `external` in the compiled rule source, e.g. a rule using `external_var` in its condition) — i.e. the variable must have been declared in the YARA rule text compiled into `rules` before `yr_rules_define_*_variable` can find/set it (returns `ERROR_INVALID_ARGUMENT` otherwise).

### P2.8 Prerequisite State
**Claim:** Before calling `yr_rules_scan_mem`, the process must have called `yr_initialize()` at least once (sets up global state—modules, regex engine tables). The `rules` argument must be a fully compiled, non-NULL `YR_RULES*` (from `yr_compiler_get_rules` or `yr_rules_load*`); a `NULL` or partially-initialized `rules` is not a protocol the API supports. If the rule source declares external variables, they should be defined (`yr_rules_define_*_variable` or `yr_compiler_define_*_variable` before compiling) prior to scanning for their values to affect condition evaluation.
**Evidence:**
> libyara/include/yara/libyara.h:86 `YR_API int yr_initialize(void);` — global init entry point.
> tests/oss-fuzz/pe_fuzzer.cc:47-58: `yr_initialize()` called before any compiler/rules/scan activity.
> libyara/rules.c:61 `external = rules->ext_vars_table;` in `yr_rules_define_integer_variable` dereferences `rules` directly — no NULL check, so `rules` must be valid.
> libyara/rules.c:217: `yr_scanner_create(rules, &scanner)` also dereferences `rules` inside, consistent with requiring a valid compiled `YR_RULES*`.

### P2.9 Result Usage
**Claim:** `yr_rules_scan_mem` does not return a data object to the caller directly — all "results" (per-rule match/no-match events, console logs, too-many-matches, scan-finished) are delivered synchronously via the `YR_CALLBACK_FUNC callback` during the scan, with `message` one of `CALLBACK_MSG_RULE_MATCHING` (1), `CALLBACK_MSG_RULE_NOT_MATCHING` (2), `CALLBACK_MSG_SCAN_FINISHED` (3), `CALLBACK_MSG_IMPORT_MODULE` (4), `CALLBACK_MSG_MODULE_IMPORTED` (5), `CALLBACK_MSG_TOO_MANY_MATCHES` (6), `CALLBACK_MSG_CONSOLE_LOG` (7), `CALLBACK_MSG_TOO_SLOW_SCANNING` (8), and `message_data` cast to `YR_RULE*` for matching/not-matching messages. The callback's return value (`CALLBACK_CONTINUE`/`CALLBACK_ABORT`/`CALLBACK_ERROR`) controls whether scanning continues, and feeds back into `yr_rules_scan_mem`'s own return code (e.g. `CALLBACK_ERROR` on `TOO_MANY_MATCHES` yields `ERROR_TOO_MANY_MATCHES`). Separately, the integer return code of `yr_rules_scan_mem` itself is consumed by the caller for success/failure branching (see P2.4).
**Evidence:**
> libyara/include/yara/rules.h:38-49: `CALLBACK_MSG_*` and `CALLBACK_CONTINUE/ABORT/ERROR` definitions.
> tests/oss-fuzz/pe_fuzzer.cc:62-69: `int callback(YR_SCAN_CONTEXT* context, int message, void* message_data, void* user_data) { return CALLBACK_CONTINUE; }` passed into `yr_rules_scan_mem` at line 77-78.
> tests/test-api.c:301-320: `propagate_too_many_matches`/`ignore_too_many_matches` callbacks branch on `message == CALLBACK_MSG_TOO_MANY_MATCHES` and return `CALLBACK_ERROR` or `CALLBACK_CONTINUE`, directly shaping `yr_rules_scan_mem`'s return value checked at line 362 and 383.
> tests/test-api.c:77: `yr_rules_scan_mem(rules, (uint8_t*) buf, strlen(buf), 0, count, &counters, 0);` — `count` callback presumably increments `counters` user_data struct per match (standard "count matching rules" pattern used across tests/util.c and test-api.c).

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting inputs are: (a) the `flags` bitmask to `yr_rules_scan_mem` — `SCAN_FLAGS_FAST_MODE` changes AC-match verification strategy (stops at first match per string rather than finding all occurrences), `SCAN_FLAGS_PROCESS_MEMORY` changes how `fullword`/alignment or padding may be interpreted for in-memory (non-file) scanning, `SCAN_FLAGS_NO_TRYCATCH` disables the internal try/catch wrapper around native-code execution (exception handling path), `SCAN_FLAGS_REPORT_RULES_MATCHING`/`SCAN_FLAGS_REPORT_RULES_NOT_MATCHING` control whether `CALLBACK_MSG_RULE_NOT_MATCHING` is also emitted; (b) `timeout` controls whether `_yr_scanner_scan_mem_block`'s periodic `yr_stopwatch_elapsed_ns` check can abort the scan with `ERROR_SCAN_TIMEOUT`; (c) the compiled rule source (string modifiers nocase/wide/ascii/fullword/xor/base64/base64wide, literal vs regexp vs hex-string, and rule conditions) selects which verification function (`_yr_scan_verify_literal_match` vs `_yr_scan_verify_re_match`) and which bytecode branches in `yr_execute_code` run for a given buffer.
**Evidence:**
> libyara/include/yara/scan.h:38-42: flag bit values.
> tests/oss-fuzz/pe_fuzzer.cc:78: `SCAN_FLAGS_NO_TRYCATCH` passed as `flags`.
> libyara/scanner.c:76-83: `if (i % 4096 == 0 && scanner->timeout > 0) { if (yr_stopwatch_elapsed_ns(&scanner->stopwatch) > scanner->timeout) { result = ERROR_SCAN_TIMEOUT; goto _exit; } }` — timeout directly gates scan abort inside the per-byte scan loop.
> libyara/scanner.c:62-63,100: `rules->ac_transition_table`/`rules->ac_match_table` and `if (match_table[state] != 0)` — the compiled rule patterns (via `yr_compiler_add_string`) determine which states trigger match verification, i.e. rule text chooses the code path.

### P2.11 Input Expectations
**Claim:** `buffer` is treated as an opaque flat byte array of exactly `buffer_size` bytes; there is no required header, magic number, or NUL-termination — the function walks `i` from 0 to `block->size` (== `buffer_size`) byte-by-byte through the Aho-Corasick transition table. `buffer_size` of 0 is explicitly supported (tests pass `""`,0). Very large buffers are limited only by `size_t` and available memory; `YR_MAX_STRING_MATCHES` governs how many times a single string pattern may match before `CALLBACK_MSG_TOO_MANY_MATCHES` fires. No alignment or encoding requirements on `buffer` itself — any byte value 0x00-0xFF is valid input and is exactly what the state machine indexes on.
**Evidence:**
> libyara/scanner.c:68,74: `size_t i = 0; ... while (i < block->size)` — raw byte-indexed loop, no format checks.
> tests/test-api.c:627: `yr_rules_scan_mem(rules, (uint8_t*) "", 0, 0, count, &counters, 0);` confirms zero-length buffer is valid.
> tests/test-api.c:343,351,356: `uint8_t* buffer = (uint8_t*) malloc(2 * YR_MAX_STRING_MATCHES); ... memset(buffer, 'a', ...); yr_rules_scan_mem(rules, buffer, 2 * YR_MAX_STRING_MATCHES, ...)` shows arbitrary repeated-byte content and large size are valid inputs that exercise `CALLBACK_MSG_TOO_MANY_MATCHES`.
> libyara/rules.c:198: `const uint8_t* buffer` — typed as raw bytes, no string/NUL-termination semantics implied by the signature itself (contrast with `yr_rules_scan_file` which takes a `const char* filename`, a C string).

## Other Logic-Group entries: call protocol and combination with the target

### yr_rules_scan_mem_blocks
**Claim:** Variant entry that scans via a caller-supplied `YR_MEMORY_BLOCK_ITERATOR*` instead of a flat buffer, reaching the same core (`yr_scanner_scan_mem_blocks` → `_yr_scanner_scan_mem_block`). Requires the harness to build and populate a `YR_MEMORY_BLOCK_ITERATOR` (first/next/file_size callback function pointers plus a context) — this is nontrivial extra plumbing not needed for `yr_rules_scan_mem`. Same `rules`/`flags`/`callback`/`user_data`/`timeout` parameter semantics and same internal create/set/scan/destroy scanner lifecycle as the target.
**Evidence:**
> libyara/include/yara/rules.h:76-82: signature `yr_rules_scan_mem_blocks(YR_RULES* rules, YR_MEMORY_BLOCK_ITERATOR* iterator, int flags, YR_CALLBACK_FUNC callback, void* user_data, int timeout)`.
> libyara/rules.c:172-194: identical internal pattern — `yr_scanner_create` → `set_callback/set_timeout/set_flags` → `yr_scanner_scan_mem_blocks(scanner, iterator)` → `yr_scanner_destroy(scanner)`.
> tests/test-iterators.c:151: `int result = yr_rules_scan_mem(` — note: a grep hit on `yr_rules_scan_mem` not `_blocks`; a targeted search for an iterator example should inspect `tests/test-iterators.c` fully to build a correct `YR_MEMORY_BLOCK_ITERATOR`, since this file is dedicated to iterator testing.
**Combination:** Not required to call the target itself (it's an alternative, not a prerequisite); a harness could pick either `yr_rules_scan_mem` (buffer) or `yr_rules_scan_mem_blocks` (iterator) for a given fuzz run, but mixing both in series on the same `rules` object is legal since each call is self-contained (creates/destroys its own scanner).

### yr_rules_scan_file
**Claim:** Convenience wrapper: maps a file with `yr_filemap_map`, then calls `yr_rules_scan_mem(rules, mfile.data, mfile.size, flags, callback, user_data, timeout)` internally, then `yr_filemap_unmap(&mfile)`. Returns whatever `yr_filemap_map` or `yr_rules_scan_mem` returned. Not useful for a buffer-based fuzz harness since it requires a real filename/filesystem path rather than consuming fuzz bytes directly — confirms the harness should feed `yr_rules_scan_mem` the buffer directly rather than writing to a temp file and reimplementing this.
**Evidence:**
> libyara/rules.c:241-262: full body, as quoted above (`yr_filemap_map` → `yr_rules_scan_mem` → `yr_filemap_unmap`).
**Combination:** Do not call in the harness; it is a thin wrapper reaching the same core, with added file I/O overhead/complexity not needed when a buffer is already available.

### yr_rules_define_integer_variable / yr_rules_define_string_variable (and sibling boolean/float variants)
**Claim:** Configuration calls on an already-compiled `YR_RULES*`, used to set values of external variables declared in the YARA rule source (the rule source must `import`/reference an external identifier, e.g. via `yr_compiler_define_integer_variable` at compile time or simply referenced in the condition — the table `rules->ext_vars_table` is populated at compile time from the rule source's external declarations). Must be called after `yr_compiler_get_rules` and before `yr_rules_scan_mem` for the new value to affect the scan (condition bytecode reads `external->value.i`/`.s`/`.f` at scan/evaluation time, not at variable-set time). Returns `ERROR_SUCCESS` on success, `ERROR_INVALID_ARGUMENT` if `identifier` is NULL or not found, `ERROR_INVALID_EXTERNAL_VARIABLE_TYPE` if the identifier exists but with a different declared type, `ERROR_INSUFFICIENT_MEMORY` for `yr_rules_define_string_variable` if `yr_strdup` fails.
**Evidence:**
> libyara/rules.c:44-71 (`yr_rules_define_integer_variable`), :73-100 (`yr_rules_define_boolean_variable`), :102-170 (`yr_rules_define_float_variable`/`yr_rules_define_string_variable`): all walk `rules->ext_vars_table` by `strcmp(external->identifier, identifier)`, type-check against `external->type`, and return `ERROR_INVALID_ARGUMENT` if not found.
> libyara/rules.c:151-163 (`yr_rules_define_string_variable` body, read above at lines 151-163): frees previous `MALLOC_STRING` value with `yr_free`, then `yr_strdup(value)`, returns `ERROR_INSUFFICIENT_MEMORY` on allocation failure.
**Combination:** Harness should call these (if the compiled rule declares matching external variables) after `yr_compiler_get_rules` succeeds and before each `yr_rules_scan_mem` call; the values selected by the fuzz input choose which rule-condition branches in `yr_execute_code` are taken.

### yr_compiler_add_string
**Claim:** Compiles a NUL-terminated YARA rule-source C string into the `YR_COMPILER`'s in-progress arena; returns the number of compile errors (0 = success, per usage pattern `if (yr_compiler_add_string(...) == 0)`), not an `ERROR_*` code. Must be called after `yr_compiler_create` and before `yr_compiler_get_rules`. `namespace_` may be `NULL` (uses default namespace).
**Evidence:**
> libyara/include/yara/compiler.h:406-409: signature `yr_compiler_add_string(YR_COMPILER* compiler, const char* rules_string, const char* namespace_)`.
> tests/oss-fuzz/rules_fuzzer.cc:62-66: `if (yr_compiler_add_string(compiler, (const char*) buffer, NULL) == 0) { if (yr_compiler_get_rules(compiler, &rules) == ERROR_SUCCESS) yr_rules_destroy(rules); }` — the `== 0` check (error count) confirms return semantics, and the ordering `add_string` before `get_rules`.
> tests/oss-fuzz/pe_fuzzer.cc:53-54: `if (yr_compiler_add_string(compiler, RULES, NULL) == 0) yr_compiler_get_rules(compiler, &rules);`
**Combination:** Produces the `YR_RULES*` consumed by the target; the rule-source string itself (which can be derived from or be the fuzz input, or a fixed/dictionary-driven string) determines the AC automaton and bytecode that `yr_rules_scan_mem` exercises on the separately-supplied scan buffer.

### yr_compiler_get_rules
**Claim:** Finalizes compilation, producing a `YR_RULES**` out-parameter. Must be called after one or more successful `yr_compiler_add_string`/`add_file`/`add_fd`/`add_bytes` calls. Returns `ERROR_SUCCESS` on success; the resulting `YR_RULES*` is independent of the compiler afterward (compiler can be destroyed immediately) and is the only kind of object `yr_rules_scan_mem` accepts as its first argument.
**Evidence:**
> libyara/include/yara/compiler.h:438: `YR_API int yr_compiler_get_rules(YR_COMPILER* compiler, YR_RULES** rules);`
> tests/oss-fuzz/rules_fuzzer.cc:64-65: `if (yr_compiler_get_rules(compiler, &rules) == ERROR_SUCCESS) yr_rules_destroy(rules);` — error-code check pattern (`ERROR_SUCCESS`), distinct from `add_string`'s error-count return.
> tests/oss-fuzz/pe_fuzzer.cc:56: `yr_compiler_destroy(compiler);` runs immediately after `yr_compiler_get_rules`, and the resulting global `rules` (line 41) remains valid and is used later in `LLVMFuzzerTestOneInput` (line 77) — confirms `rules` lifetime is independent of `compiler`.
**Combination:** This is the direct producer of the `rules` object consumed as the target's first parameter; it must succeed before `yr_rules_scan_mem` can be called at all.

## Build notes

**Claim:** Fuzzers for yara are C++ (`.cc`) files compiled with `$CXX $CXXFLAGS -std=c++17`, with include path `-I. -Ilibyara/include` (so `#include <yara.h>` or `#include <yara/rules.h>` etc. resolve), statically linked against `./.libs/libyara.a` plus `$LIB_FUZZING_ENGINE`. The library itself is built beforehand via `./bootstrap.sh; ./configure --enable-macho --enable-debug --enable-dex --enable-dotnet --without-crypto; make; make install`. A harness for `yr_rules_scan_mem` should be placed as a `.cc` file under `tests/oss-fuzz/` (or wherever the harness-generation pipeline places new fuzzer sources matching the `find $SRC/yara/tests/oss-fuzz/ -name "*.cc"` glob) and use `#include <yara.h>` (the umbrella header) as existing fuzzers do.
**Evidence:**
> ossfuzz_view/yara-c2/build.sh:18-23: `./bootstrap.sh`; `./configure --enable-macho --enable-debug --enable-dex --enable-dotnet --without-crypto`; `make clean`; `make -j$(nproc) all`; `make install`.
> ossfuzz_view/yara-c2/build.sh:25-31: `fuzzers=$(find $SRC/yara/tests/oss-fuzz/ -name "*.cc") ... $CXX $CXXFLAGS -std=c++17 -I. -Ilibyara/include $f -o $OUT/$fuzzer_name ./.libs/libyara.a $LIB_FUZZING_ENGINE`.
> tests/oss-fuzz/pe_fuzzer.cc:32: `#include <yara/rules.h>` (and :33-42 other `yara/*.h` headers) — shows headers are included via the `libyara/include` path, either the umbrella `<yara.h>` (as in tests/oss-fuzz/rules_fuzzer.cc:33) or individual `<yara/xxx.h>` headers.
> ossfuzz_view/yara-c2/Dockerfile:28-30: `git clone --depth 1 https://github.com/VirusTotal/yara.git; COPY run_tests.sh build.sh $SRC/; WORKDIR yara` — confirms project root is `$SRC/yara`, matching the `-I.` /`-Ilibyara/include` include paths used relative to that root.