## target: yr_rules_scan_mem

### P2.1 Init Sequence
**Claim:** Before `yr_rules_scan_mem` can be called the process must call `yr_initialize()` once (module/global init), build or load a `YR_RULES*` via the compiler (`yr_compiler_create` → `yr_compiler_add_string`/`yr_compiler_add_file` → `yr_compiler_get_rules` → `yr_compiler_destroy`) or via `yr_rules_load`/`yr_rules_load_stream`, and optionally call `yr_rules_define_*_variable` on that `YR_RULES*` before scanning. No explicit scanner-creation call is needed by the caller of `yr_rules_scan_mem` itself — the function creates and destroys its own `YR_SCANNER` internally.
**Evidence:**
> tests/oss-fuzz/pe_fuzzer.cc:47-58: `yr_initialize()` then `yr_compiler_create(&compiler)`, `yr_compiler_add_string(compiler, RULES, NULL)`, `yr_compiler_get_rules(compiler, &rules)`, `yr_compiler_destroy(compiler)` all run in `LLVMFuzzerInitialize` before any call to `yr_rules_scan_mem`.
> libyara/rules.c:196-239 (`yr_rules_scan_mem` body): the function itself calls `yr_scanner_create(rules, &scanner)` first thing (`GOTO_EXIT_ON_ERROR(yr_scanner_create(rules, &scanner));`), so the caller does not create the scanner.
> tests/test-api.c:62-64 / tests/util.c: pattern is `yr_initialize(); compile_rule(...,&rules); yr_rules_scan_mem(rules,...); yr_rules_destroy(rules); yr_finalize();` (see tests/test-api.c:45 area, and `test_disabled_rules` in tests/test-api.c:62-88).

### P2.2 Parameter Construction
**Claim:** `rules` must be a non-NULL, fully-built `YR_RULES*` (from compiler or `yr_rules_load*`). `buffer`/`buffer_size` describe the attacker-controlled memory region to scan (buffer may be any pointer, including NULL if buffer_size is 0, since scanning degrades gracefully — see test-api.c:627 scanning `""`,0). `flags` is a bitmask of `SCAN_FLAGS_*` from scanner.h/rules.h. `callback` must be a valid `YR_CALLBACK_FUNC` (signature `int(YR_SCAN_CONTEXT*, int message, void* message_data, void* user_data)`); it is required — the library does not check callback != NULL, so passing it is mandatory practice; `user_data` is any pointer forwarded verbatim to callback. `timeout` is an int number of seconds (0 = no bound observed in tests, e.g. tests/util.c:508 passes 0).
**Evidence:**
> rules.h:84 (declaration, as supplied): `int yr_rules_scan_mem(YR_RULES* rules, const uint8_t* buffer, size_t buffer_size, int flags, YR_CALLBACK_FUNC callback, void* user_data, int timeout)`
> libyara/include/yara/rules.h:38-49: `CALLBACK_MSG_*` and `CALLBACK_CONTINUE/ABORT/ERROR` constants used for callback contract.
> tests/oss-fuzz/pe_fuzzer.cc:77-78: `yr_rules_scan_mem(rules, data, size, SCAN_FLAGS_NO_TRYCATCH, callback, NULL, 0);` — flags=SCAN_FLAGS_NO_TRYCATCH, user_data=NULL, timeout=0.
> tests/test-api.c:627: `yr_rules_scan_mem(rules, (uint8_t*) "", 0, 0, count, &counters, 0);` shows buffer_size=0 is accepted.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is Create-rules (compiler or load) → (optionally) define external variables on rules → Scan (one or many calls to `yr_rules_scan_mem`, each internally creating/destroying its own `YR_SCANNER`) → Destroy-rules (`yr_rules_destroy`). `YR_RULES*` may be reused across many scan calls; each `yr_rules_scan_mem` call is self-contained (create scanner, scan, destroy scanner) so no scanner object crosses calls unless the caller uses the lower-level `yr_scanner_*` API directly instead.
**Evidence:**
> libyara/rules.c:196-239: inside `yr_rules_scan_mem`, `yr_scanner_create` → `yr_scanner_set_callback/set_timeout/set_flags` → `yr_scanner_scan_mem` → `yr_scanner_destroy`, all in one call.
> tests/test-api.c (lines around 282, 353, 374, 450, 471, 526, 547): the *same* `rules` object is scanned multiple times with different inputs/flags before a single final `yr_rules_destroy(rules)`, confirming rules objects are scan-reusable.
> tests/util.c:505-540 `matches_blob()`: shows the alternative path using `yr_scanner_create`/`yr_scanner_set_*`/`yr_scanner_scan_mem_blocks`/`yr_scanner_destroy` manually, equivalent to what `yr_rules_scan_mem` does internally.

### P2.4 Return Value Handling
**Claim:** `yr_rules_scan_mem` returns an `int` error code; `ERROR_SUCCESS` (0) means the scan ran to completion (independent of whether any rule matched — matches/non-matches are reported via the callback, not the return value). Non-zero values are from `libyara/include/yara/error.h` (e.g. `ERROR_INSUFFICIENT_MEMORY`, `ERROR_SCAN_TIMEOUT`, `ERROR_TOO_MANY_MATCHES`, `ERROR_CALLBACK_ERROR` when callback returns `CALLBACK_ERROR`). Callers must check it and treat nonzero as failure.
**Evidence:**
> libyara/rules.c:215-238: `int result = ERROR_INTERNAL_FATAL_ERROR; GOTO_EXIT_ON_ERROR(yr_scanner_create(...)); ... result = yr_scanner_scan_mem(...); ... _exit: return result;` — explicit propagation of the scanner's result code.
> libyara/scanner.c:80: `result = ERROR_SCAN_TIMEOUT;` set inside the scan loop on timeout.
> tests/util.c:530-538: `if (scan_result != ERROR_SUCCESS) { fprintf(...); exit(EXIT_FAILURE); }` — canonical check pattern used by the project's own test helper.
> tests/test-api.c:282-297 etc.: `int err = yr_rules_scan_mem(...); assert_true_expr(err == ERROR_SUCCESS)`-style checks are used throughout (grep hits at those line numbers).

### P2.5 Cleanup Sequence
**Claim:** On both success and failure of `yr_rules_scan_mem`, no cleanup is owed by the caller for the (internal) scanner — it is destroyed by the function itself before returning, including on early `GOTO_EXIT_ON_ERROR` failure paths. The caller is responsible only for eventually calling `yr_rules_destroy(rules)` once scanning is completely done with that rules object, and `yr_finalize()` at process end. If the caller instead used `yr_rules_define_string_variable`, the strdup'd value is owned by the `ext_vars_table` and freed by `yr_rules_destroy`.
**Evidence:**
> libyara/rules.c:217-225: `GOTO_EXIT_ON_ERROR(yr_scanner_create(rules, &scanner)); ... yr_scanner_destroy(scanner); _exit: return result;` — scanner_create failure jumps to `_exit` *before* scanner exists, so no destroy needed there; all post-creation paths reach `yr_scanner_destroy`.
> libyara/rules.c:585-602 (`yr_rules_destroy`): frees `ext_vars_table` malloc'd strings, `rules->no_required_strings`, releases the arena (`yr_arena_release`), and frees the `YR_RULES` struct itself — this is the single cleanup call the harness must issue once per `YR_RULES*`.
> libyara/rules.c:157-163 (`yr_rules_define_string_variable`): `external->value.s = yr_strdup(value);` — ownership transferred to rules, freed later by `yr_rules_destroy`.

### P2.6 API Existence
**Claim:** All APIs referenced below exist verbatim in the current codebase (found by Grep/Read, not fabricated).
**Evidence:**
> libyara/rules.c:44 `yr_rules_define_integer_variable`; :73 `yr_rules_define_boolean_variable`; :102 `yr_rules_define_float_variable`; :131 `yr_rules_define_string_variable`; :172 `yr_rules_scan_mem_blocks`; :196 `yr_rules_scan_mem`; :241 `yr_rules_scan_file`; :264 `yr_rules_scan_fd`; :287 `yr_rules_scan_proc`; :442 `yr_rules_load_stream`; :460 `yr_rules_load`; :479 `yr_rules_save_stream`; :484 `yr_rules_save`; :585 `yr_rules_destroy`.
> libyara/include/yara/rules.h:76-150 mirrors the same declarations as the public header for these entries.
> libyara/scanner.c:238 `yr_scanner_create`; :334 `yr_scanner_destroy`; :406 `yr_scanner_define_integer_variable`; :423 `yr_scanner_define_boolean_variable`; :431 `yr_scanner_define_float_variable`; :448 `yr_scanner_define_string_variable`; :465 `yr_scanner_scan_mem_blocks`; :705 `yr_scanner_scan_mem`.
> libyara/include/yara/scanner.h:39-96 declares `yr_scanner_set_callback`, `yr_scanner_set_timeout`, `yr_scanner_set_flags`, etc.

### P2.7 Co-call Constraints
**Claim:** (compiler create/destroy) `yr_compiler_create` must be paired with `yr_compiler_destroy`; (rules create/destroy) every successfully obtained `YR_RULES*` (via `yr_compiler_get_rules` or `yr_rules_load*`) must be paired with exactly one `yr_rules_destroy`; `yr_initialize()`/`yr_finalize()` form a global pair bracketing all library use. `yr_rules_scan_mem` itself internally pairs `yr_scanner_create`/`yr_scanner_destroy` — a harness calling `yr_rules_scan_mem` does not need to manage a scanner pair itself (but if it drops to the lower-level API it must).
**Evidence:**
> tests/oss-fuzz/pe_fuzzer.cc:50-56: `yr_compiler_create(&compiler)` ... `yr_compiler_destroy(compiler)` within `LLVMFuzzerInitialize`.
> tests/oss-fuzz/rules_fuzzer.cc:56-68: `yr_compiler_create` → (on success) `yr_compiler_get_rules`/`yr_rules_destroy` → `yr_compiler_destroy` always called, showing destroy-on-all-paths discipline.
> libyara/rules.c:217-225: internal `yr_scanner_create`/`yr_scanner_destroy` pairing inside `yr_rules_scan_mem`.
> tests/util.c:514-527: manual `yr_scanner_create`/`yr_scanner_destroy` pairing when using `yr_scanner_scan_mem_blocks` directly instead of `yr_rules_scan_mem`.

### P2.8 Prerequisite State
**Claim:** A fully compiled, internally consistent `YR_RULES*` (with its `rules_table`, `ac_transition_table`, `ext_vars_table`, `code_start`, etc. populated by `yr_compiler_get_rules` or `yr_rules_load_stream`/`yr_arena_load_stream`) must exist before scanning; this is the prerequisite "context". `yr_initialize()` must have been called to set up global module/string-heap state used by modules (pe/elf/dotnet) referenced from rule conditions. If the rule set uses external variables (`identifier` declared at compile time via `yr_compiler_define_*_variable`), `yr_rules_define_*_variable` can only set values for identifiers that already exist in `rules->ext_vars_table`, i.e. the variable must have been declared to the compiler before `yr_compiler_get_rules` was called — otherwise `yr_rules_define_*_variable` returns `ERROR_INVALID_ARGUMENT`.
**Evidence:**
> libyara/rules.c:44-71 (`yr_rules_define_integer_variable`): walks `rules->ext_vars_table` looking for a pre-existing identifier; `return ERROR_INVALID_ARGUMENT;` if not found (line 70) — proves the variable must pre-exist from compile time.
> libyara/rules.c:405-415 (`yr_rules_from_arena`): `new_rules->ext_vars_table = yr_arena_get_ptr(arena, YR_EXTERNAL_VARIABLES_TABLE, 0);` — table is populated from the arena built by the compiler/loader, confirming it is fixed at compile/load time.
> libyara/scanner.c:238-327 (`yr_scanner_create`): iterates `rules->ext_vars_table` to instantiate one `YR_OBJECT` per external variable — so `rules` must already have this table set before any scan.

### P2.9 Result Usage
**Claim:** `yr_rules_scan_mem` does not return match data directly; results are delivered to the caller's `callback` during the scan, invoked with `message` in `{CALLBACK_MSG_RULE_MATCHING, CALLBACK_MSG_RULE_NOT_MATCHING, CALLBACK_MSG_SCAN_FINISHED, CALLBACK_MSG_IMPORT_MODULE, CALLBACK_MSG_MODULE_IMPORTED, CALLBACK_MSG_TOO_MANY_MATCHES, CALLBACK_MSG_CONSOLE_LOG, CALLBACK_MSG_TOO_SLOW_SCANNING}` and `message_data` typed accordingly (e.g. `YR_RULE*` for RULE_MATCHING/NOT_MATCHING). Inside the callback, real callers walk the rule's strings/matches with `yr_rule_strings_foreach`/`yr_string_matches_foreach` macros against the `YR_SCAN_CONTEXT*` passed in, reading `match->data`/`match->data_length` (pointers into the original scanned buffer). The callback must return one of `CALLBACK_CONTINUE`, `CALLBACK_ABORT`, `CALLBACK_ERROR` to control subsequent scanning. After scanning, callers call `yr_rules_destroy(rules)` once no more scans are needed; they do not need to free anything derived from the scan itself (no caller-owned result object is returned by `yr_rules_scan_mem`).
**Evidence:**
> tests/util.c:565-596 (`capture_matches` callback): `if (message == CALLBACK_MSG_RULE_MATCHING) { YR_RULE* rule = (YR_RULE*) message_data; yr_rule_strings_foreach(rule, string) { yr_string_matches_foreach(context, string, match) { ... match->data_length ... match->data ... } } }` — canonical consumption of scan results via the callback, not the return value.
> libyara/include/yara/rules.h:38-71: defines `CALLBACK_MSG_*`, `CALLBACK_CONTINUE/ABORT/ERROR`, and the `yr_rule_strings_foreach`/`yr_string_matches_foreach`/`yr_rules_foreach` macros used on the callback's `message_data`/`context`.
> tests/oss-fuzz/pe_fuzzer.cc:62-69: minimal callback `{ return CALLBACK_CONTINUE; }` ignoring message_data — shows the minimum valid callback contract for a harness that doesn't need to inspect matches.
> tests/test-api.c:77: `yr_rules_scan_mem(rules, (uint8_t*) buf, strlen(buf), 0, count, &counters, 0);` where `count` is a callback incrementing a `struct COUNTERS` passed as `user_data` — a second common result-consumption pattern (counting messages rather than walking matches).

### P2.10 Behaviour Controls
**Claim:** `flags` passed to `yr_rules_scan_mem` (and internally forwarded via `yr_scanner_set_flags`) selects processing paths: `SCAN_FLAGS_FAST_MODE` stops looking for more matches of a string once one match is confirmed; `SCAN_FLAGS_PROCESS_MEMORY` changes entry-point resolution (`yr_get_entry_point_address` vs `yr_get_entry_point_offset`); `SCAN_FLAGS_NO_TRYCATCH` disables the setjmp/trycatch wrapper around the AC scan loop (`yr_scanner_scan_mem_blocks` uses `!(scanner->flags & SCAN_FLAGS_NO_TRYCATCH)` to decide whether to wrap); `SCAN_FLAGS_REPORT_RULES_MATCHING`/`SCAN_FLAGS_REPORT_RULES_NOT_MATCHING` (on by default from `yr_scanner_create`) gate which `CALLBACK_MSG_RULE_*` events fire. `timeout` bounds the scan loop and causes `ERROR_SCAN_TIMEOUT`. External variables (`yr_rules_define_*_variable`) change which condition branches the VM (`yr_execute_code`) takes, which can enable/disable whole code paths (module imports, string comparisons) without changing the buffer.
**Evidence:**
> libyara/scanner.c:259-261 (`yr_scanner_create`): default flags are `SCAN_FLAGS_REPORT_RULES_MATCHING | SCAN_FLAGS_REPORT_RULES_NOT_MATCHING` unless overridden by `yr_scanner_set_flags`.
> libyara/scanner.c:523,571: `!(scanner->flags & SCAN_FLAGS_NO_TRYCATCH)` gates the trycatch wrapper around the scan-block loop (two call sites, one per block-processing branch).
> tests/oss-fuzz/pe_fuzzer.cc:77-78: harness passes `SCAN_FLAGS_NO_TRYCATCH` directly to `yr_rules_scan_mem`.
> tests/util.c:498: `int flags = SCAN_FLAGS_NO_TRYCATCH;` used identically by the project's own scan helper `matches_blob`.
> cli/common.c:165-202 (`define_external_variables`): shows `yr_rules_define_float_variable/integer/boolean/string_variable` all used to set external-variable values consulted by rule conditions, demonstrating the "behaviour control via external variables" path.

### P2.11 Input Expectations
**Claim:** The `buffer`/`buffer_size` pair is treated as an opaque byte blob with no mandatory header or terminator — it is scanned byte-by-byte through the compiled Aho-Corasick automaton; `buffer_size==0` is accepted (test-api.c:627 scans `""`,0). There is no requirement that `buffer` be NUL-terminated or any particular format; format-specific parsing (PE/ELF/dotnet/dex headers) only happens if the compiled rule set `import`s the corresponding module, in which case that module's own parser imposes format expectations on the same bytes (separate from `yr_rules_scan_mem` itself). Internally `yr_rules_scan_mem` wraps the whole buffer into a single `YR_MEMORY_BLOCK` and delegates to `yr_scanner_scan_mem_blocks`/`yr_scanner_scan_mem`, meaning extremely large `buffer_size` values are limited only by `size_t`/memory, not by an explicit library-imposed cap seen in this function.
**Evidence:**
> libyara/scanner.c:705- (`yr_scanner_scan_mem`): builds a single-block iterator over `(buffer, buffer_size)` and calls `yr_scanner_scan_mem_blocks` (line 749: `result = yr_scanner_scan_mem_blocks(scanner, &iterator);`).
> tests/test-api.c:627: `yr_rules_scan_mem(rules, (uint8_t*) "", 0, 0, count, &counters, 0);` — zero-length buffer accepted without special-casing by the caller.
> rules.h:84 declaration itself: `const uint8_t* buffer, size_t buffer_size` — generic byte buffer, no format tag.

## Other entries in the Logic Group

### yr_rules_scan_mem_blocks
**Claim:** Same call protocol as `yr_rules_scan_mem` but the caller supplies a `YR_MEMORY_BLOCK_ITERATOR*` instead of a flat buffer, enabling multi-block/sparse scanning. Needs the same prerequisite `YR_RULES*` and `yr_initialize()`. Internally identical create/set/scan/destroy scanner sequence. Returns the same `int` error codes. No extra cleanup beyond what the caller owns for the iterator itself (iterator lifetime/ownership is the caller's responsibility; the function does not free it).
**Evidence:**
> libyara/rules.c:172-194: `YR_API int yr_rules_scan_mem_blocks(YR_RULES* rules, YR_MEMORY_BLOCK_ITERATOR* iterator, int flags, YR_CALLBACK_FUNC callback, void* user_data, int timeout) { ... FAIL_ON_ERROR(yr_scanner_create(rules, &scanner)); yr_scanner_set_callback(...); yr_scanner_set_timeout(...); yr_scanner_set_flags(...); result = yr_scanner_scan_mem_blocks(scanner, iterator); yr_scanner_destroy(scanner); return result; }`
> tests/util.c:512-527: manual use pattern building a `YR_MEMORY_BLOCK_ITERATOR`/`YR_TEST_ITERATOR_CTX` via `init_test_iterator(&iterator, &iterator_ctx, blob, blob_size);` then `yr_scanner_scan_mem_blocks(scanner, &iterator)` (lower-level equivalent exercised when `matches_blob_uses_default_iterator` is false).
**Combination with target:** In a single harness, use either `yr_rules_scan_mem` (simple contiguous buffer) OR `yr_rules_scan_mem_blocks` (to exercise multi-block paths) — not normally both in the same call, but both can be exercised from the same compiled `YR_RULES*` in sequence since rules are scan-reusable (P2.3).

### yr_rules_scan_file / yr_rules_scan_fd
**Claim:** Both are thin wrappers that memory-map a file/fd via `yr_filemap_map`/`yr_filemap_map_fd` and then call `yr_rules_scan_mem` with the mapped `data`/`size`, unmapping afterward (`yr_filemap_unmap`/`yr_filemap_unmap_fd`) regardless of the scan's own cleanup. They need the same `YR_RULES*` prerequisite as the target; return value is the mapping error or the propagated `yr_rules_scan_mem` result. These belong to the same feature as alternate input sources but are not used in the in-memory-buffer-driven fuzz harness unless the harness itself maps fuzzer data to a temp file first (not typical; `yr_rules_scan_mem` is the more direct entry for a `LLVMFuzzerTestOneInput(data,size)` harness).
**Evidence:**
> libyara/rules.c:241-262 (`yr_rules_scan_file`): `int result = yr_filemap_map(filename, &mfile); if (result == ERROR_SUCCESS) { result = yr_rules_scan_mem(rules, mfile.data, mfile.size, flags, callback, user_data, timeout); yr_filemap_unmap(&mfile); } return result;`
> libyara/rules.c:264-285 (`yr_rules_scan_fd`): identical pattern using `yr_filemap_map_fd`/`yr_filemap_unmap_fd`.
**Combination with target:** Not needed in the same harness as `yr_rules_scan_mem` — they are alternative entry points over the identical core; a harness driven by `LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)` should call `yr_rules_scan_mem` directly on `data`/`size` rather than round-tripping through a file.

### yr_rules_define_integer_variable / yr_rules_define_boolean_variable / yr_rules_define_float_variable / yr_rules_define_string_variable
**Claim:** Configuration entries, not scan entries. Each requires: (1) a `YR_RULES*` already built by the compiler/loader, and (2) the named external variable to have been declared to the *compiler* (via `yr_compiler_define_*_variable`, in `libyara/compiler.c:808-870`) before `yr_compiler_get_rules` produced this `rules` object — otherwise they return `ERROR_INVALID_ARGUMENT`. Calling with a mismatched type (e.g. defining a string where the variable was declared integer) returns `ERROR_INVALID_EXTERNAL_VARIABLE_TYPE`. They must be called *before* `yr_rules_scan_mem`/`yr_rules_scan_mem_blocks` for the new value to affect the upcoming scan (since `yr_scanner_create`, invoked inside the scan entries, snapshots `rules->ext_vars_table` into per-scan `YR_OBJECT`s at scan start — libyara/scanner.c:305-327). `yr_rules_define_string_variable`'s value is strdup'd into `external->value.s` (freed later by `yr_rules_destroy`, not by the caller).
**Evidence:**
> libyara/rules.c:44-170: all four functions, each returning `ERROR_INVALID_ARGUMENT` if `identifier` not found in `rules->ext_vars_table`, and `ERROR_INVALID_EXTERNAL_VARIABLE_TYPE` on type mismatch (e.g. lines 60-61, 89-90, 118-119, 147-149).
> libyara/scanner.c:305-327 (`yr_scanner_create`): snapshots `rules->ext_vars_table` into scanner-local `YR_OBJECT`s at the moment the scanner (and thus the scan) is created — proving define_* calls must precede the scan call that should see them.
> cli/common.c:139-206 (`define_external_variables`): real-world caller calling `yr_rules_define_float_variable`/`yr_rules_define_integer_variable`/`yr_rules_define_boolean_variable`/`yr_rules_define_string_variable` on a `rules` object obtained earlier, before scanning begins (the CLI scans after this setup step).
**Combination with target:** In one harness: build `YR_COMPILER`, declare externals with `yr_compiler_define_*_variable`, compile rule source referencing those externals in conditions, `yr_compiler_get_rules` → `rules`, optionally call `yr_rules_define_*_variable(rules, id, fuzz_derived_value)` to vary condition outcomes, then call `yr_rules_scan_mem(rules, data, size, flags, callback, user_data, timeout)` repeatedly; finally `yr_rules_destroy(rules)`.

## Build notes
**Claim:** OSS-Fuzz fuzzers for this project are C++ (`.cc`), compiled individually against the static `libyara.a` built by the project's own autotools build (`./bootstrap.sh && ./configure ... && make && make install`), with `yara.h`/`yara/*.h` reachable via `-I. -Ilibyara/include` (no system install path used), and linked with `$LIB_FUZZING_ENGINE`.
**Evidence:**
> ossfuzz_view/yara-c4/build.sh:18-23: `./bootstrap.sh`; `./configure --enable-macho --enable-debug --enable-dex --enable-dotnet --without-crypto`; `make clean`; `make -j$(nproc) all`; `make install`.
> ossfuzz_view/yara-c4/build.sh:25-35: `fuzzers=$(find $SRC/yara/tests/oss-fuzz/ -name "*.cc")`; for each: `$CXX $CXXFLAGS -std=c++17 -I. -Ilibyara/include $f -o $OUT/$fuzzer_name ./.libs/libyara.a $LIB_FUZZING_ENGINE` — C++17, include paths `.` and `libyara/include`, static-linked `./.libs/libyara.a`.
> tests/oss-fuzz/pe_fuzzer.cc:32: `#include <yara.h>` — the single umbrella header a fuzzer includes; `yara.h` in turn includes `yara/scanner.h` (which forward-declares/aliases `YR_SCANNER`) but notably the target's own header `yara/rules.h` is pulled in transitively via `yara/exec.h` included by other headers in the chain (libyara/include/yara/exec.h:34 `#include <yara/rules.h>`), and directly usable through `-Ilibyara/include` since `yara.h` lives at `libyara/include/yara.h`.
> ossfuzz_view/yara-c4/Dockerfile:28-30: `git clone ... yara.git`; `COPY run_tests.sh build.sh $SRC/`; `WORKDIR yara` — confirms build.sh runs from the yara source root, matching the relative `-I.`/`-Ilibyara/include` paths used.