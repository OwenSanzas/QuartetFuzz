## target: yr_rules_scan_mem

### P2.1 Init Sequence
**Claim:** Process-wide `yr_initialize()` must be called once before any compiler/scanner/rules API is used. Then a `YR_RULES*` must be produced via the compiler pipeline (`yr_compiler_create` → `yr_compiler_add_string`/`yr_compiler_add_file`/`yr_compiler_add_fd`/`yr_compiler_add_bytes` → `yr_compiler_get_rules` → `yr_compiler_destroy`) or via `yr_rules_load`/`yr_rules_load_stream` (precompiled rules), before calling `yr_rules_scan_mem`. No per-call scanner setup is needed by the caller because `yr_rules_scan_mem` creates/destroys its own `YR_SCANNER` internally.
**Evidence:**
> libyara/rules.c:196-239 `yr_rules_scan_mem` body: `GOTO_EXIT_ON_ERROR(yr_scanner_create(rules, &scanner)); yr_scanner_set_callback(...); yr_scanner_set_timeout(...); yr_scanner_set_flags(...); result = yr_scanner_scan_mem(scanner, buffer, buffer_size); yr_scanner_destroy(scanner);`
> tests/oss-fuzz/math_fuzzer.cc:50-66 `LLVMFuzzerInitialize`: `yr_initialize(); yr_compiler_create(&compiler); yr_compiler_add_string(compiler, RULES, NULL); yr_compiler_get_rules(compiler, &rules); yr_compiler_destroy(compiler);` — done once, before any call to `yr_rules_scan_mem` in `LLVMFuzzerTestOneInput`.
> tests/oss-fuzz/dex_fuzzer.cc:9-25 identical pattern with `yr_compiler_add_string(compiler, "import \"dex\"", NULL)`.
> tests/test-api.c:271-297 `test_max_match_data`: `yr_initialize(); ... compile_rule(rules_str, &rules); ... yr_rules_scan_mem(...); yr_rules_destroy(rules); yr_finalize();` — shows full init→compile→scan→destroy→finalize lifecycle.

### P2.2 Parameter Construction
**Claim:** `rules` must be a valid, non-NULL `YR_RULES*` obtained from the compiler or `yr_rules_load*` (it is dereferenced immediately inside `yr_scanner_create`, e.g. `rules->ext_vars_table`, `rules->num_strings`). `buffer` must be a readable memory region of at least `buffer_size` bytes (can be `NULL`/size 0, tests do `yr_rules_scan_mem(rules, (uint8_t*)"", 0, 0, count, &counters, 0)`). `flags` is an int bitmask of `SCAN_FLAGS_*` (`FAST_MODE`=1, `PROCESS_MEMORY`=2, `NO_TRYCATCH`=4, `REPORT_RULES_MATCHING`=8, `REPORT_RULES_NOT_MATCHING`=16); 0 is valid (both REPORT_* flags default on, see P2.10). `callback` must be non-NULL — if NULL the underlying `yr_scanner_scan_mem_blocks` returns `ERROR_CALLBACK_REQUIRED` without touching the buffer. `user_data` is an opaque pointer forwarded verbatim to `callback`, may be NULL. `timeout` is an int number of seconds, converted internally to nanoseconds (`timeout * 1000000000ULL`); 0 means no timeout enforcement considerations beyond the stopwatch check.
**Evidence:**
> libyara/include/yara/scan.h:38-42 `#define SCAN_FLAGS_FAST_MODE 1` / `PROCESS_MEMORY 2` / `NO_TRYCATCH 4` / `REPORT_RULES_MATCHING 8` / `REPORT_RULES_NOT_MATCHING 16`.
> libyara/scanner.c:477-481 `if (scanner->callback == NULL) { result = ERROR_CALLBACK_REQUIRED; goto _exit; }` (inside `yr_scanner_scan_mem_blocks`, reached from `yr_rules_scan_mem`).
> libyara/scanner.c:385-389 `yr_scanner_set_timeout`: `scanner->timeout = timeout * 1000000000ULL;` (seconds→ns).
> tests/test-api.c:627 `yr_rules_scan_mem(rules, (uint8_t*) "", 0, 0, count, &counters, 0);` shows a 0-length buffer is an accepted input.
> libyara/scanner.c:254 `new_scanner->rules = rules;` then `rules->num_strings`/`rules->ext_vars_table` dereferenced at scanner.c:263-327 — `rules` must be non-NULL and a valid compiled `YR_RULES`.

### P2.3 Object Lifecycle
**Claim:** Two nested lifecycles exist. (1) Rules lifecycle: `yr_compiler_create` → `yr_compiler_add_*` (one or more) → `yr_compiler_get_rules` (produces `YR_RULES*`, can be reused across many scans) → ... → `yr_rules_destroy(rules)`. The compiler object itself is destroyed right after `yr_compiler_get_rules` (`yr_compiler_destroy`) since rules are now self-contained. (2) Scanner lifecycle, hidden inside `yr_rules_scan_mem`: `yr_scanner_create(rules, &scanner)` → `yr_scanner_set_callback/set_timeout/set_flags` → `yr_scanner_scan_mem(scanner, buffer, buffer_size)` → `yr_scanner_destroy(scanner)`, all internal to the single call; the caller never sees the `YR_SCANNER*`.
**Evidence:**
> libyara/rules.c:217-225 create→configure→scan→destroy sequence inside `yr_rules_scan_mem`.
> tests/oss-fuzz/math_fuzzer.cc:56-65 compiler create→add_string→get_rules→destroy(compiler), rules kept alive in a global for repeated `yr_rules_scan_mem` calls across fuzzer iterations (line 84).
> libyara/rules.c:585-602 `yr_rules_destroy` frees `rules->no_required_strings`, releases `rules->arena`, frees `rules` — must be called once, after all scans are done, to avoid use-after-free in subsequent scans.

### P2.4 Return Value Handling
**Claim:** `yr_rules_scan_mem` returns `ERROR_SUCCESS` (0) on a completed scan (regardless of whether any rule matched — matches are reported via the callback, not the return code), or a non-zero `ERROR_*` code on failure (e.g. `ERROR_CALLBACK_REQUIRED` if no callback set, `ERROR_INSUFFICIENT_MEMORY`, `ERROR_TOO_MANY_MATCHES` if the callback propagates `CALLBACK_ERROR` on `CALLBACK_MSG_TOO_MANY_MATCHES`, `ERROR_SCAN_TIMEOUT`, `ERROR_TOO_SLOW_SCANNING`, `ERROR_COULD_NOT_MAP_FILE` from a trycatch memory fault). Callers must check the return and treat non-zero as scan failure.
**Evidence:**
> libyara/rules.c:215,223,227-238 `int result = ERROR_INTERNAL_FATAL_ERROR; ... GOTO_EXIT_ON_ERROR(yr_scanner_create(...)); ... result = yr_scanner_scan_mem(...); ... _exit: return result;`
> tests/test-api.c:353-372 checks `err != ERROR_TOO_MANY_MATCHES` / `err != ERROR_SUCCESS` depending on callback behaviour, demonstrating that the return code reflects callback-driven errors.
> libyara/scanner.c:735-747 (`yr_scanner_scan_mem`) returns `ERROR_TOO_SLOW_SCANNING` if callback doesn't continue on `CALLBACK_MSG_TOO_SLOW_SCANNING`.
> libyara/include/yara/error.h:71,77 `ERROR_SCAN_TIMEOUT`, `ERROR_TOO_MANY_MATCHES` defined as possible results.

### P2.5 Cleanup Sequence
**Claim:** On both success and error, `yr_rules_scan_mem` always destroys its internal scanner itself (`yr_scanner_destroy(scanner)` at rules.c:225, reached even via the `_exit` label pattern used elsewhere) — no scanner cleanup is needed by the caller for this entry point. The caller's own responsibility is: free/destroy the `YR_RULES*` with `yr_rules_destroy(rules)` after all scans are finished, and call `yr_finalize()` at process end (pairs with `yr_initialize()`). If using `yr_compiler_*`, `yr_compiler_destroy(compiler)` should be called once `yr_compiler_get_rules` has produced the rules (compiler is no longer needed).
**Evidence:**
> libyara/rules.c:217-225 scanner create/destroy fully inside the function body; caller never owns the scanner.
> tests/test-api.c:297-298 `yr_rules_destroy(rules); yr_finalize();` after all scans using that rules object.
> tests/oss-fuzz/math_fuzzer.cc:63 `yr_compiler_destroy(compiler);` right after `yr_compiler_get_rules` — compiler freed before any scan.
> libyara/libyara.c:323-331 `yr_finalize` comment: "yr_finalize shouldn't be called without calling yr_initialize first" — confirms pairing requirement.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase at the cited locations.
**Evidence:**
> libyara/include/yara/rules.h:76-153 declares `yr_rules_scan_mem_blocks`, `yr_rules_scan_mem`, `yr_rules_scan_file`, `yr_rules_scan_fd`, `yr_rules_scan_proc`, `yr_rules_save`, `yr_rules_save_stream`, `yr_rules_load`, `yr_rules_load_stream`, `yr_rules_destroy`, `yr_rules_define_integer_variable`, `yr_rules_define_boolean_variable`, `yr_rules_define_float_variable`, `yr_rules_define_string_variable`, `yr_rules_get_stats`.
> libyara/scanner.c:238,334,376,385,391,406,423,431,448,465,705,762,777,792 define `yr_scanner_create`, `yr_scanner_destroy`, `yr_scanner_set_callback`, `yr_scanner_set_timeout`, `yr_scanner_set_flags`, `yr_scanner_define_integer_variable`, `yr_scanner_define_boolean_variable`, `yr_scanner_define_float_variable`, `yr_scanner_define_string_variable`, `yr_scanner_scan_mem_blocks`, `yr_scanner_scan_mem`, `yr_scanner_scan_file`, `yr_scanner_scan_fd`, `yr_scanner_scan_proc`.
> libyara/include/yara/compiler.h:357-438 declares `yr_compiler_create`, `yr_compiler_destroy`, `yr_compiler_add_file/_fd/_bytes/_string`, `yr_compiler_get_rules`, `yr_compiler_define_*_variable`.
> libyara/include/yara/libyara.h:86,88 declares `yr_initialize`, `yr_finalize`.
> libyara/rules.c:585 defines `yr_rules_destroy`.

### P2.7 Co-call Constraints
**Claim:** Paired lifecycle APIs that must be matched: `yr_initialize()` ↔ `yr_finalize()` (process-global, call once each); `yr_compiler_create()` ↔ `yr_compiler_destroy()` (compiler object, destroy after `yr_compiler_get_rules`); `yr_rules_load`/`yr_compiler_get_rules` (rules creation) ↔ `yr_rules_destroy()` (rules object); inside `yr_rules_scan_mem` itself, `yr_scanner_create()` ↔ `yr_scanner_destroy()` is already paired by the library and not exposed to the caller. `yr_scanner_set_callback`/`set_timeout`/`set_flags` are "configure" calls that must happen after `yr_scanner_create` and before `yr_scanner_scan_mem*` — `yr_rules_scan_mem` does this ordering internally and a caller using the lower-level `yr_scanner_*` API directly must replicate that order.
**Evidence:**
> libyara/rules.c:217-223 order inside the target: create → set_callback → set_timeout → set_flags → scan_mem → destroy.
> libyara/libyara.c:323-331 comment pairing `yr_finalize` with prior `yr_initialize`.
> tests/oss-fuzz/math_fuzzer.cc:54-65 compiler create/destroy pairing, separate from rules lifetime (rules outlive compiler).
> tests/util.c:512-527 shows the equivalent manual sequence `yr_scanner_create` → `set_callback/set_timeout/set_flags` → `yr_scanner_scan_mem_blocks` → `yr_scanner_destroy` when not using `yr_rules_scan_mem` directly.

### P2.8 Prerequisite State
**Claim:** Before `yr_rules_scan_mem` can be safely called: (1) `yr_initialize()` must have run (sets up global tables/locks used by regex engine, modules, thread-local storage for try/catch); (2) `rules` must be a fully compiled, non-corrupt `YR_RULES*` (its `ext_vars_table`, `num_strings`, `num_rules`, `num_namespaces`, `ac_match_table` arrays are read directly in `yr_scanner_create`/`yr_scanner_scan_mem`); (3) if rules reference external variables (`yr_compiler_define_*_variable` at compile time, or `yr_rules_define_*_variable` post-compile), those should be defined with matching types before scanning, otherwise the scanner's internal object table still has default/undefined values which may cause condition evaluation differences but not a crash (`yr_rules_define_integer_variable` at rules.c:44-71 checks type and returns `ERROR_INVALID_EXTERNAL_VARIABLE_TYPE`/`ERROR_INVALID_ARGUMENT` otherwise — it doesn't block scanning if skipped).
**Evidence:**
> libyara/include/yara/globals.h:38 "tables are initialized by yr_initialize."
> libyara/scanner.c:254-327 `yr_scanner_create` dereferences `rules->ext_vars_table`, `rules->num_strings`, `rules->num_rules`, `rules->num_namespaces` to size internal bitmasks/arrays — these must be valid (i.e., `rules` must come from a successful compile/load).
> libyara/scanner.c:735 `scanner->rules->ac_match_table[YR_AC_ROOT_STATE]` dereferenced in `yr_scanner_scan_mem`, requiring a populated Aho-Corasick table from compilation.
> libyara/rules.c:44-71 `yr_rules_define_integer_variable` looks up `rules->ext_vars_table` by identifier; if not found returns `ERROR_INVALID_ARGUMENT` (does not crash scan if simply never called).

### P2.9 Result Usage
**Claim:** `yr_rules_scan_mem` does not return a data object; all results are delivered through the caller-supplied `callback` invoked synchronously during the scan with `message` in `{CALLBACK_MSG_RULE_MATCHING, CALLBACK_MSG_RULE_NOT_MATCHING, CALLBACK_MSG_SCAN_FINISHED, CALLBACK_MSG_IMPORT_MODULE, CALLBACK_MSG_MODULE_IMPORTED, CALLBACK_MSG_TOO_MANY_MATCHES, CALLBACK_MSG_CONSOLE_LOG, CALLBACK_MSG_TOO_SLOW_SCANNING}` and `message_data` typed accordingly (e.g. `YR_RULE*` for RULE_MATCHING/NOT_MATCHING). The callback's return value (`CALLBACK_CONTINUE`/`CALLBACK_ABORT`/`CALLBACK_ERROR`) controls whether scanning continues, aborts cleanly, or causes `yr_rules_scan_mem` to return an error code. Real callers accumulate matches in `user_data` inside the callback (counters, lists) and inspect them after `yr_rules_scan_mem` returns; some also call `yr_rule_tags_foreach`/`yr_rule_metas_foreach`/`yr_rule_strings_foreach`/`yr_string_matches_foreach` macros on the `YR_RULE*`/`YR_STRING*` passed as `message_data` to walk match details.
**Evidence:**
> libyara/include/yara/rules.h:38-49 defines `CALLBACK_MSG_*` and `CALLBACK_CONTINUE/ABORT/ERROR`.
> libyara/include/yara/types.h:747-751 `typedef int (*YR_CALLBACK_FUNC)(YR_SCAN_CONTEXT*, int message, void* message_data, void* user_data);`
> tests/test-api.c:77 `yr_rules_scan_mem(rules, (uint8_t*) buf, strlen(buf), 0, count, &counters, 0);` — `count` callback increments fields in `&counters` (user_data), inspected by the test after the call returns.
> tests/test-api.c:301-320 `propagate_too_many_matches`/`ignore_too_many_matches` show `message == CALLBACK_MSG_TOO_MANY_MATCHES` branching and returning `CALLBACK_ERROR` vs `CALLBACK_CONTINUE`, which changes `yr_rules_scan_mem`'s return value (see P2.4).
> libyara/include/yara/rules.h:63-71 `yr_string_matches_foreach` macro walks `context->matches[string->idx]` — used by real callers to enumerate match offsets/data after a `CALLBACK_MSG_RULE_MATCHING` message.

### P2.10 Behaviour Controls
**Claim:** The `flags` bitmask passed to `yr_rules_scan_mem` (forwarded via `yr_scanner_set_flags`) selects distinct code paths: `SCAN_FLAGS_NO_TRYCATCH` disables the `YR_TRYCATCH` SIGSEGV-guard wrapper around the whole block-scan loop in `yr_scanner_scan_mem_blocks`, so memory faults propagate as crashes instead of being turned into `ERROR_COULD_NOT_MAP_FILE` — maximizes crash surface for fuzzing. `SCAN_FLAGS_PROCESS_MEMORY` switches entry-point computation between `yr_get_entry_point_address` (process-memory layout) and `yr_get_entry_point_offset` (file layout) on first block. `SCAN_FLAGS_FAST_MODE` affects string-match-once short-circuiting inside `yr_scan_verify_match` (core, not directly visible in rules.c/scanner.c excerpts read but referenced in scan flags and functionality description). `SCAN_FLAGS_REPORT_RULES_MATCHING`/`SCAN_FLAGS_REPORT_RULES_NOT_MATCHING` control which `CALLBACK_MSG_RULE_*` messages fire; if neither is set, `yr_scanner_set_flags` forces both on for backward compatibility. `timeout` (seconds, converted to ns) and `callback` return values (`CALLBACK_CONTINUE/ABORT/ERROR`) further gate scan continuation. The compiled rule set itself (which modules are imported: pe/elf/dex/macho/dotnet, which string patterns/conditions exist) is the other major behaviour-selecting input, set at compile time via `yr_compiler_add_string`/`add_file` content, not at scan time.
**Evidence:**
> libyara/scanner.c:522-553 `YR_TRYCATCH(!(scanner->flags & SCAN_FLAGS_NO_TRYCATCH), { ...scan loop... }, { result = ERROR_COULD_NOT_MAP_FILE; });`
> libyara/scanner.c:536-544 `if (scanner->flags & SCAN_FLAGS_PROCESS_MEMORY) scanner->entry_point = yr_get_entry_point_address(...); else scanner->entry_point = yr_get_entry_point_offset(...);`
> libyara/scanner.c:391-404 `yr_scanner_set_flags`: forces both REPORT_RULES_MATCHING and REPORT_RULES_NOT_MATCHING on if neither given.
> libyara/include/yara/scan.h:38-42 flag bit values.
> tests/oss-fuzz/dex_fuzzer.cc:19,43-44 example selecting the `dex` module via `yr_compiler_add_string(compiler, "import \"dex\"", NULL)` at compile time and `SCAN_FLAGS_NO_TRYCATCH` at scan time, directing module-parser execution on the fuzzed buffer.

### P2.11 Input Expectations
**Claim:** `buffer`/`buffer_size` have no required header, magic, or format at the `yr_rules_scan_mem` level itself — the function treats it as an opaque byte array for the Aho-Corasick string matcher; it accepts `buffer_size == 0` (tests pass `""`,0). However, when the compiled rules `import` a module (pe/elf/dex/macho/dotnet), the module's parser (invoked from `yr_execute_code` through `CALLBACK_MSG_IMPORT_MODULE`) does expect that specific binary format and performs its own bounds/format validation on the same buffer — out-of-range/malformed data there is exactly the attack surface the OSS-Fuzz per-module fuzzers target. No NUL-termination is required for `buffer` (it's a `uint8_t*`/`size_t` pair, not a C-string). Internally, `yr_scanner_scan_mem` warns (`CALLBACK_MSG_TOO_SLOW_SCANNING`) when `buffer_size > YR_FILE_SIZE_THRESHOLD` and the rules' AC root state has matches, letting the callback decide (via return value) whether to abort before scanning such large buffers — this is a soft "size limit" enforced via callback decision, not an outright size cap.
**Evidence:**
> libyara/rules.c:198-199 signature `const uint8_t* buffer, size_t buffer_size` — raw byte buffer with explicit length, no implied terminator.
> tests/test-api.c:627 `yr_rules_scan_mem(rules, (uint8_t*) "", 0, 0, count, &counters, 0);` confirms zero-length buffer is accepted.
> libyara/scanner.c:733-747 `if (... buffer_size > YR_FILE_SIZE_THRESHOLD) { ... CALLBACK_MSG_TOO_SLOW_SCANNING ...; if (result != CALLBACK_CONTINUE) return ERROR_TOO_SLOW_SCANNING; }`
> tests/oss-fuzz/dex_fuzzer.cc:19,43-44 — module import (`"import \"dex\""`) means the buffer is expected (by the dex module, not by `yr_rules_scan_mem` itself) to look like dex-format data for deep coverage; `yr_rules_scan_mem` itself does not validate this.

## Other entries of the Logic Group (call protocol and combination with the target)

### yr_rules_scan_mem_blocks
**Claim:** Variant of the target that accepts a caller-supplied `YR_MEMORY_BLOCK_ITERATOR*` instead of a flat buffer, for multi-block/virtual-memory scanning. Requires the same prerequisite state as the target (`yr_initialize()` done, valid `rules`) plus a fully populated `YR_MEMORY_BLOCK_ITERATOR` (`context`, `first`, `next`, optional `file_size`, `last_error = ERROR_SUCCESS` initially) supplied by the caller — `yr_rules_scan_mem` builds exactly such an iterator internally around a single block (see `libyara/scanner.c:718-731`) and this function is its multi-block generalisation. Internally it performs the exact same create→set_callback/set_timeout/set_flags→scan→destroy sequence as the target (libyara/rules.c:172-194), just calling `yr_scanner_scan_mem_blocks` instead of `yr_scanner_scan_mem`. Return value and error handling identical to the target (ERROR_* codes, callback-return-driven errors). A harness would use this only if it wants to simulate multiple discontiguous memory regions (e.g., emulate `SCAN_FLAGS_PROCESS_MEMORY` process scanning); for a single buffer, the target itself is simpler and equivalent.
**Evidence:**
> libyara/rules.c:172-194 full body, mirrors `yr_rules_scan_mem` structure exactly.
> libyara/include/yara/types.h (iterator struct, fields `context/first/next/file_size/last_error`) — same struct the target builds inline at libyara/scanner.c:718-731.

### yr_rules_scan_file / yr_rules_scan_fd
**Claim:** Both are thin wrappers that map a file (by path or fd) into memory with `yr_filemap_map`/`yr_filemap_map_fd`, then call the target `yr_rules_scan_mem` on `mfile.data`/`mfile.size`, and always `yr_filemap_unmap`/`yr_filemap_unmap_fd` afterward. They need the same prerequisites as the target (`yr_initialize`, valid `rules`), plus a readable file path / valid fd. Return value is either the mapping error (`ERROR_COULD_NOT_OPEN_FILE`/`ERROR_COULD_NOT_MAP_FILE`) or whatever the target returns. Not useful for a libFuzzer-style in-memory harness (they require real files/fds) — a harness fuzzing in-memory data should call `yr_rules_scan_mem` directly, as the project's own oss-fuzz fuzzers do (math_fuzzer.cc:84, dex_fuzzer.cc:43, elf_fuzzer.cc:72, macho_fuzzer.cc:76, dotnet_fuzzer.cc:76), not these file-based variants.
**Evidence:**
> libyara/rules.c:241-262 `yr_rules_scan_file`: `yr_filemap_map(filename, &mfile); ... yr_rules_scan_mem(rules, mfile.data, mfile.size, ...); yr_filemap_unmap(&mfile);`
> libyara/rules.c:264-285 `yr_rules_scan_fd`: same pattern with `yr_filemap_map_fd`/`yr_filemap_unmap_fd`.

### yr_scanner_create
**Claim:** Lower-level entry used internally by the target; requires `rules` to be a valid compiled `YR_RULES*` (dereferences `rules->ext_vars_table`, `num_strings`, `num_rules`, `num_namespaces`). Allocates a `YR_SCANNER` (calloc), a hash table (`objects_table`), several `YR_BITMASK` arrays sized from `rules`, and (if the compiled rules have external variables) one `YR_OBJECT` per external variable. Returns `ERROR_SUCCESS` and sets `*scanner`, or `ERROR_INSUFFICIENT_MEMORY` on any allocation failure (in which case it already calls `yr_scanner_destroy` on the partially built scanner internally — caller must not double-free). Must be paired with `yr_scanner_destroy`. A harness using the lower-level API directly (instead of `yr_rules_scan_mem`) would call this once, then `yr_scanner_set_flags`/`set_callback`/`set_timeout` (and optionally `yr_scanner_define_*_variable`) before each `yr_scanner_scan_mem`/`scan_mem_blocks`/`scan_file`/`scan_fd`, reusing the same scanner across multiple scans if desired — a capability the one-shot `yr_rules_scan_mem` does not offer.
**Evidence:**
> libyara/scanner.c:238-332 full definition; cleanup-on-error at lines 250-252, 281-290, 296-300, 311-323.
> libyara/scanner.c:334-374 `yr_scanner_destroy` frees fibers, positions, `objects_table` (with `yr_object_destroy`), profiling info, bitmasks, matches arrays, and the scanner struct itself.
> tests/util.c:512-527 shows direct use: `yr_scanner_create` → `set_callback/set_timeout/set_flags` → `yr_scanner_scan_mem_blocks` → `yr_scanner_destroy`, as an alternative path to `yr_rules_scan_mem`.

### yr_scanner_set_flags
**Claim:** Sets `scanner->flags`; must be called after `yr_scanner_create` and before scanning. If neither `SCAN_FLAGS_REPORT_RULES_MATCHING` nor `SCAN_FLAGS_REPORT_RULES_NOT_MATCHING` is present in the passed flags, both are OR'd in automatically (backward-compat default). No return value to check (`void`). Combines with the target: `yr_rules_scan_mem`'s `flags` parameter is forwarded verbatim to this function (libyara/rules.c:221).
**Evidence:**
> libyara/scanner.c:391-404 full definition including the default-both-flags behavior.
> libyara/rules.c:221 `yr_scanner_set_flags(scanner, flags);` inside the target.

### yr_scanner_scan_mem
**Claim:** The function the target delegates to after building its own single-block iterator. Requires `scanner->callback != NULL` (enforced inside `yr_scanner_scan_mem_blocks`, which it calls) or returns `ERROR_CALLBACK_REQUIRED`. Internally builds a one-block `YR_MEMORY_BLOCK_ITERATOR` wrapping `buffer`/`buffer_size`, checks the `YR_FILE_SIZE_THRESHOLD` "too slow" heuristic, then calls `yr_scanner_scan_mem_blocks`. Return codes identical in nature to the target's (ERROR_SUCCESS / ERROR_* on failure). A harness calling the lower-level scanner API directly would call this instead of the target after doing its own `yr_scanner_create`+configure, which is useful for varying flags/timeout across repeated scans of different buffers with one scanner object, or for calling `yr_scanner_define_*_variable` (scanner-scoped external variables) between scans without recompiling rules.
**Evidence:**
> libyara/scanner.c:705-760 full definition; `yr_rules_scan_mem` is structurally identical but owns scanner lifetime itself (libyara/rules.c:196-239 vs libyara/scanner.c:705-760).

### yr_rules_define_integer_variable / yr_rules_define_string_variable (and sibling _boolean_/_float_)
**Claim:** Must be called on a valid `YR_RULES*` (post-compile) and BEFORE `yr_rules_scan_mem`/`yr_rules_scan_file`/etc., since each call to `yr_rules_scan_mem` creates a fresh scanner from `rules->ext_vars_table` (via `yr_object_from_external_variable`, libyara/scanner.c:305-327) — i.e. the values set on `rules->ext_vars_table` are copied into the new scanner's object table at `yr_scanner_create` time, so defining them before each `yr_rules_scan_mem` call (or once if the value doesn't change across scans) is required to affect that scan's rule-condition evaluation. The identifier must already exist in the compiled rules (declared via `yr_compiler_define_*_variable` before compiling, or implicitly as an `external` in the `.yar` source) and match the declared type, else `ERROR_INVALID_ARGUMENT` / `ERROR_INVALID_EXTERNAL_VARIABLE_TYPE` is returned and no scanner state changes. For strings, `yr_rules_define_string_variable` `yr_strdup`s the value and frees any prior malloc'd string — ownership of the passed `const char*` stays with the caller, the library copies it.
**Evidence:**
> libyara/rules.c:44-71 `yr_rules_define_integer_variable` — looks up identifier in `rules->ext_vars_table`, type-checks, sets `external->value.i`, returns `ERROR_INVALID_EXTERNAL_VARIABLE_TYPE` or `ERROR_INVALID_ARGUMENT` on mismatch/missing identifier.
> libyara/rules.c:102-170 (`_float_`/`_string_` variants) `yr_strdup(value)` and `yr_free(external->value.s)` of any prior malloc'd string, `ERROR_INSUFFICIENT_MEMORY` if `yr_strdup` fails.
> libyara/scanner.c:305-327 `yr_scanner_create` iterates `rules->ext_vars_table`, calling `yr_object_from_external_variable(external, &object)` to materialize each external variable as a `YR_OBJECT` in the new scanner's `objects_table` — confirms the values defined via `yr_rules_define_*` must be set before `yr_scanner_create` (i.e. before `yr_rules_scan_mem`) to take effect for that scan.
> libyara/scanner.c:406-463 scanner-scoped `yr_scanner_define_*_variable` counterparts operate on `scanner->objects_table` directly (post `yr_scanner_create`), an alternative for the lower-level `yr_scanner_*` API path.

## Build notes
**Claim:** Fuzzers for this project are built as C++ (`-std=c++17`) translation units (`.cc` files) found under `tests/oss-fuzz/`, compiled with `$CXX $CXXFLAGS` and linked statically against the already-built `libyara.a` plus `$LIB_FUZZING_ENGINE`. The header is pulled in via `-Ilibyara/include` (umbrella header `<yara.h>`, which transitively includes `yara/rules.h`, `yara/scanner.h`, `yara/compiler.h`, `yara/error.h`); `-I.` is also on the include path (for the top-level `yara.h` under `$SRC/yara` if present). The library is configured with `--enable-macho --enable-debug --enable-dex --enable-dotnet --without-crypto` before building, so the dex/dotnet/macho modules are compiled in (relevant for module-import harnesses) but crypto-dependent features (e.g. Authenticode in `pe`) are disabled.
**Evidence:**
> ossfuzz_view/yara-c5/build.sh:18-19 `./bootstrap.sh; ./configure --enable-macho --enable-debug --enable-dex --enable-dotnet --without-crypto`
> ossfuzz_view/yara-c5/build.sh:25-31 `fuzzers=$(find $SRC/yara/tests/oss-fuzz/ -name "*.cc"); ... $CXX $CXXFLAGS -std=c++17 -I. -Ilibyara/include $f -o $OUT/$fuzzer_name ./.libs/libyara.a $LIB_FUZZING_ENGINE`
> tests/oss-fuzz/dex_fuzzer.cc:1-3 `#include <stddef.h>` `#include <stdint.h>` `#include <yara.h>` — confirms fuzzers include the umbrella `yara.h`, not individual module headers.
> ossfuzz_view/yara-c5/Dockerfile:17-28 `FROM gcr.io/oss-fuzz-base/base-builder`, installs `automake autoconf make libtool pkg-config bison flex`, clones `https://github.com/VirusTotal/yara.git`, copies `build.sh`/`run_tests.sh`.