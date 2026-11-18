## target: yr_rules_scan_mem

### P2.0 Main path to keep (every input) — read first
**Claim:** Compile once (in `LLVMFuzzerInitialize`, with stdout redirected to /dev/null) one rule that imports `dex`, reads header fields and calls `dex.has_method` / `dex.has_class` in their string and regex forms. Per input: `flags = SCAN_FLAGS_NO_TRYCATCH | bits of one control byte`, then `yr_rules_scan_mem(rules, buf, len, flags, cb, &state, 0)` with the DEX bytes at a fixed offset (0 preferred, control byte taken from the end). The callback always returns `CALLBACK_CONTINUE`, prints the imported module with `yr_object_print_data`, and walks string matches.
**Rule for the harness:** keep every step of this main path on every input, fed with the full fuzz input; Logic-Group entries and the other sections of this report only add calls on top of it (selected by spare input bits), they never replace or dilute a main-path step.
**Evidence:**
> libyara/modules/dex/dex.c:466-486: `dex_get_header` returns NULL unless `data_size >= sizeof(dex_header_t)` (112 bytes, libyara/include/yara/dex.h:15-41) and `memcmp(dex_header->magic, DEX_FILE_MAGIC_03x, 8) == 0` for one of 035..039.
> libyara/modules/dex/dex.c:1529-1553: `module_load` → `foreach_memory_block` → `dex_get_header(block_data, block->size)` → `dex_parse(dex, block->base)`. The whole parser hangs off this check.
> libyara/modules.c:133-139: `result = context->callback(context, CALLBACK_MSG_IMPORT_MODULE, &mi, ...); if (result == CALLBACK_ERROR) { yr_object_destroy(module_structure); return ERROR_CALLBACK_ERROR; }` — returning CALLBACK_ERROR skips the module load.
> libyara/modules.c:165-172: `CALLBACK_MSG_MODULE_IMPORTED` is sent with the filled `module_structure`.
> cli/yara.c:1231-1249: the CLI handles `CALLBACK_MSG_MODULE_IMPORTED` with `yr_object_print_data(object, 0, 1); printf("\n");`; libyara/object.c:1073 `yr_object_print_data` recurses over structures, arrays and dictionaries (1144-1174).
> libyara/modules/dex/dex.c:337-342: `declare_function("has_method", "s"|"ss"|"r"|"rr", ...)`, `declare_function("has_class", "s"|"r", ...)`.
> libyara/rules.c:196-203: `yr_rules_scan_mem(rules, buffer, buffer_size, flags, callback, user_data, timeout)`.


### Summary of target
`YR_API int yr_rules_scan_mem(YR_RULES* rules, const uint8_t* buffer, size_t buffer_size, int flags, YR_CALLBACK_FUNC callback, void* user_data, int timeout)` declared in `libyara/include/yara/rules.h:84-91`, defined in `libyara/rules.c:196-239`. It creates a `YR_SCANNER` via `yr_scanner_create`, wires callback/timeout/flags into it, calls `yr_scanner_scan_mem(scanner, buffer, buffer_size)`, then always destroys the scanner before returning.

```
libyara/rules.c:196-239
YR_API int yr_rules_scan_mem(...)
{
  ...
  GOTO_EXIT_ON_ERROR(yr_scanner_create(rules, &scanner));
  yr_scanner_set_callback(scanner, callback, user_data);
  yr_scanner_set_timeout(scanner, timeout);
  yr_scanner_set_flags(scanner, flags);
  result = yr_scanner_scan_mem(scanner, buffer, buffer_size);
  yr_scanner_destroy(scanner);
_exit:
  return result;
}
```

### P2.1 Init Sequence
**Claim:** Process-wide `yr_initialize()` must be called once before any other libyara API (including building rules and scanning). Then a `YR_RULES*` must exist (built via the compiler pipeline: `yr_compiler_create` -> `yr_compiler_add_string`/`yr_compiler_add_file` -> `yr_compiler_get_rules`, or loaded via `yr_rules_load`). `yr_rules_scan_mem` itself needs no separate "open" call — it internally creates and destroys its own `YR_SCANNER` — so the only prerequisite is a valid, non-NULL `rules` object.
**Evidence:**
> libyara/libyara.c:237-238: "// Should be called by main thread before using any other function from libyara." above `yr_initialize()`
> tests/oss-fuzz/pe_fuzzer.cc:43-58: `LLVMFuzzerInitialize` calls `yr_initialize()`, then `yr_compiler_create`, `yr_compiler_add_string`, `yr_compiler_get_rules`, `yr_compiler_destroy`, storing the resulting `rules` in a global before any scan.
> libyara/rules.c:217: `yr_scanner_create(rules, &scanner)` is the only call inside `yr_rules_scan_mem` that needs `rules`.

### P2.2 Parameter Construction
**Claim:**
- `rules`: non-NULL `YR_RULES*` produced by `yr_compiler_get_rules` or `yr_rules_load`/`yr_rules_load_stream`. Must outlive the scan call.
- `buffer`: pointer to the raw bytes to scan; `NULL` is not checked explicitly in `yr_rules_scan_mem` — it is forwarded straight into `yr_scanner_scan_mem` which builds a single `YR_MEMORY_BLOCK` wrapping it (`block.context = (void*) buffer`), so `buffer` must be a valid readable region of at least `buffer_size` bytes (or `buffer_size==0`, a pattern explicitly supported, see P2.11).
- `buffer_size`: `size_t`, any value including 0 is accepted; large values (>0.2MB, `YR_FILE_SIZE_THRESHOLD`) combined with a root-AC-state match trigger a `CALLBACK_MSG_TOO_SLOW_SCANNING` report (see scanner.c below).
- `flags`: bitmask of `SCAN_FLAGS_FAST_MODE(1)`, `SCAN_FLAGS_PROCESS_MEMORY(2)`, `SCAN_FLAGS_NO_TRYCATCH(4)`, `SCAN_FLAGS_REPORT_RULES_MATCHING(8)`, `SCAN_FLAGS_REPORT_RULES_NOT_MATCHING(16)`. If neither of the last two report flags are set, scanner defaults both on (see `yr_scanner_set_flags`).
- `callback`: must be non-NULL — if NULL, `yr_scanner_scan_mem_blocks` returns `ERROR_CALLBACK_REQUIRED` immediately (checked before any scanning work).
- `user_data`: opaque pointer forwarded verbatim to `callback`; may be NULL.
- `timeout`: int, seconds; converted internally to nanoseconds (`timeout * 1000000000ULL`) and used to bound the AC scan loop; `0` means "respect default/no bound" per existing fuzzer usage patterns (all project fuzzers pass `0`).
**Evidence:**
> libyara/scanner.c:477-481: `if (scanner->callback == NULL) { result = ERROR_CALLBACK_REQUIRED; goto _exit; }`
> libyara/scanner.c:385-389: `yr_scanner_set_timeout` — "// Convert timeout from seconds to nanoseconds." `scanner->timeout = timeout * 1000000000ULL;`
> libyara/include/yara/scan.h:38-42: `SCAN_FLAGS_*` definitions.
> libyara/scanner.c:391-401: `yr_scanner_set_flags` default-both-report-flags logic.
> libyara/scanner.c:735-747: `buffer_size > YR_FILE_SIZE_THRESHOLD` check triggers `CALLBACK_MSG_TOO_SLOW_SCANNING`.
> tests/oss-fuzz/pe_fuzzer.cc:77-78: `yr_rules_scan_mem(rules, data, size, SCAN_FLAGS_NO_TRYCATCH, callback, NULL, 0);` — real caller passing timeout=0, user_data=NULL.

### P2.3 Object Lifecycle
**Claim:** `yr_rules_scan_mem` fully owns the create→configure→use→destroy lifecycle of its internal `YR_SCANNER` object — the caller never sees or manages the `YR_SCANNER` for this entry point. Lifecycle inside the call: `yr_scanner_create` (alloc + init object table from `rules->ext_vars_table`) → `yr_scanner_set_callback`/`set_timeout`/`set_flags` (configure) → `yr_scanner_scan_mem` (use, runs AC engine + `yr_execute_code`) → `yr_scanner_destroy` (always called before return, both on success and on error after `yr_scanner_create` succeeded).
**Evidence:**
> libyara/rules.c:217-225: create → set_callback/set_timeout/set_flags → scan_mem → destroy, in that order.
> libyara/scanner.c:238-332: `yr_scanner_create` allocates bitmasks/match arrays and builds one `YR_OBJECT` per external variable in `rules->ext_vars_table`.
> libyara/scanner.c:334-374: `yr_scanner_destroy` frees fiber pool, exec-position pool, objects_table, profiling_info, all bitmasks/match arrays, and the scanner struct itself — no return value (void), always safe to call once scanner != NULL.

### P2.4 Return Value Handling
**Claim:** Return is an `int` error code; `0` is `ERROR_SUCCESS`. Non-zero values include `ERROR_INSUFFICIENT_MEMORY(1)`, `ERROR_CALLBACK_REQUIRED(53)` (callback==NULL), `ERROR_TOO_SLOW_SCANNING(65)`, `ERROR_CALLBACK_ERROR` (callback returned `CALLBACK_ERROR`), and others bubbled up from `yr_execute_code`/module parsers. Callers in the project log/print the error and abort/skip further processing rather than inspecting match data when non-zero (matches are instead delivered incrementally via the callback regardless of the final return code).
**Evidence:**
> libyara/include/yara/error.h:40,47,100,112: `ERROR_SUCCESS 0`, `ERROR_INSUFFICIENT_MEMORY 1`, `ERROR_CALLBACK_REQUIRED 53`, `ERROR_TOO_SLOW_SCANNING 65`.
> cli/yara.c:1730-1735: `if (result != ERROR_SUCCESS) { ...; print_scanner_error(scanner, result); exit_with_code(EXIT_FAILURE); }` (same pattern used for `yr_scanner_scan_*`, the lower-level sibling of `yr_rules_scan_mem`).
> tests/oss-fuzz/pe_fuzzer.cc:77-80: fuzzer ignores the return code entirely (matches/errors observed only via the callback side-effects), a valid simplified harness pattern already used by the project's own OSS-Fuzz fuzzers.

### P2.5 Cleanup Sequence
**Claim:** On the scan-only path there is nothing the caller must free — `yr_rules_scan_mem` cleans up its internal scanner itself. The caller is responsible for the objects it built before the scan: destroy `YR_COMPILER` (`yr_compiler_destroy`) once `YR_RULES` has been obtained, and eventually destroy `YR_RULES` (`yr_rules_destroy`) when done with all scans, and call `yr_finalize()` once at process exit (paired with `yr_initialize`). Order: scans finish → `yr_rules_destroy(rules)` → `yr_finalize()`. Compiler can be destroyed immediately after `yr_compiler_get_rules` succeeds (rules object is independent of the compiler afterward).
**Evidence:**
> libyara/rules.c:225: `yr_scanner_destroy(scanner);` inside `yr_rules_scan_mem`, unconditionally reached on the success path, and reached via `goto _exit` label is bypassed only if `yr_scanner_create` itself failed (no scanner to destroy).
> tests/oss-fuzz/pe_fuzzer.cc:56: `yr_compiler_destroy(compiler);` immediately after `yr_compiler_get_rules` in `LLVMFuzzerInitialize`, rules object survives independently in the global `rules`.
> cli/yara.c:1754-1758: `if (compiler != NULL) yr_compiler_destroy(compiler); if (rules != NULL) yr_rules_destroy(rules);` final cleanup order in the CLI tool after all scanning finished.
> libyara/rules.c:585-602: `yr_rules_destroy` frees `ext_vars_table` malloc-strings, `no_required_strings`, releases the arena, frees the `rules` struct.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase at the cited locations; none were fabricated.
**Evidence:**
> libyara/include/yara/rules.h:76-151 declares `yr_rules_scan_mem_blocks`, `yr_rules_scan_mem`, `yr_rules_scan_file`, `yr_rules_scan_fd`, `yr_rules_scan_proc`, `yr_rules_save*`, `yr_rules_load*`, `yr_rules_destroy`, `yr_rules_define_*_variable`, `yr_rules_get_stats`.
> libyara/include/yara/scanner.h` and `libyara/scanner.c:238,334,376,385,391,465,705`: `yr_scanner_create`, `yr_scanner_destroy`, `yr_scanner_set_callback`, `yr_scanner_set_timeout`, `yr_scanner_set_flags`, `yr_scanner_scan_mem_blocks`, `yr_scanner_scan_mem`.
> libyara/compiler.c:217,288,658,729: `yr_compiler_create`, `yr_compiler_destroy`, `yr_compiler_add_string`, `yr_compiler_get_rules`.
> libyara/libyara.c:241,323: `yr_initialize`, `yr_finalize`.

### P2.7 Co-call Constraints
**Claim:** Paired lifecycle APIs relevant to the harness:
- `yr_initialize()` / `yr_finalize()` — process-level init/shutdown, reference-counted (`init_count`), must balance 1:1.
- `yr_compiler_create()` / `yr_compiler_destroy()` — compiler object pair; `yr_compiler_destroy` must not be called before `yr_compiler_get_rules` if you still need to call `yr_compiler_add_string` (asserted: "Don't allow calls to yr_compiler_add_string() after yr_compiler_get_rules() has been called").
- `yr_compiler_get_rules()` (produces `YR_RULES*`) / `yr_rules_destroy()` — rules object pair; the rules object is independent of the compiler once obtained and can be used across many `yr_rules_scan_mem` calls.
- Inside `yr_rules_scan_mem` itself: `yr_scanner_create` / `yr_scanner_destroy` (fully internal, not caller-visible for this entry point).
- `yr_rules_define_*_variable(rules, ...)` must be called on `rules` **before** any `yr_rules_scan_mem` call whose condition reads that external variable (the variable is copied into the scanner's object table at `yr_scanner_create` time from `rules->ext_vars_table`), i.e. after each `yr_rules_scan_mem`/`yr_scanner_create` the define must have already happened to take effect for that particular scan — defining variables between scans is fine because each scan creates a fresh scanner from the (now updated) `rules->ext_vars_table`.
**Evidence:**
> libyara/libyara.c:250-253: `init_count++; if (init_count > 1) return ERROR_SUCCESS;` — reference counted init.
> libyara/compiler.c:663-665: `assert(compiler->rules == NULL);` with comment "Don't allow calls to yr_compiler_add_string() after yr_compiler_get_rules() has been called."
> libyara/scanner.c:305-327: `yr_scanner_create` reads `rules->ext_vars_table` and builds objects at scanner-creation time — so defines made via `yr_rules_define_*_variable` before this particular `yr_rules_scan_mem` call are the ones that apply to it.
> libyara/rules.c:44-71: `yr_rules_define_integer_variable` mutates `rules->ext_vars_table` in place (searches by identifier, type-checks, sets `external->value.i`).

### P2.8 Prerequisite State
**Claim:** Before calling `yr_rules_scan_mem` the harness needs: (1) `yr_initialize()` called at least once in process lifetime; (2) a successfully compiled `YR_RULES*` (non-NULL) from `yr_compiler_get_rules` (requires a prior successful `yr_compiler_add_string`/`add_file` with 0 errors — `yr_compiler_get_rules` asserts `compiler->errors == 0`); (3) a valid callback function pointer (required, else `ERROR_CALLBACK_REQUIRED`); (4) any external variables referenced by rule conditions pre-defined via `yr_rules_define_*_variable` if the condition logic depends on them (not required if rules don't declare/use `import`/external vars that affect truthiness; module imports like `pe`, `elf`, `dotnet`, `macho`, `dex`, `math` are resolved internally via `yr_modules_load` during `yr_execute_code`, no separate harness-side init needed beyond having configured `./configure --enable-...` at build time, see Build notes).
**Evidence:**
> libyara/compiler.c:731-733: `yr_compiler_get_rules` — "// Don't allow calls to yr_compiler_get_rules() if a previous call to yr_compiler_add_XXXX failed." `assert(compiler->errors == 0);`
> libyara/scanner.c:477-481: callback-required check.
> tests/oss-fuzz/pe_fuzzer.cc:34-39,74-75: rule source textually embeds `import "pe"`; module resolution (`pe.rva_to_offset`, `pe.sections[...]`) happens purely inside `yr_execute_code`/`yr_modules_load` at scan time, no extra harness setup beyond having the rule compiled with that import string and the module built into libyara.a (build.sh enables `--enable-dex --enable-dotnet --enable-macho`).

### P2.9 Result Usage
**Claim:** `yr_rules_scan_mem` has no direct "returned object" consumed afterward by other APIs — its result is delivered *during* the call through repeated invocations of the caller-supplied `callback(context, message, message_data, user_data)`. Real callers interpret `message` (`CALLBACK_MSG_RULE_MATCHING`, `CALLBACK_MSG_RULE_NOT_MATCHING`, `CALLBACK_MSG_SCAN_FINISHED`, `CALLBACK_MSG_IMPORT_MODULE`, `CALLBACK_MSG_MODULE_IMPORTED`, `CALLBACK_MSG_TOO_MANY_MATCHES`, `CALLBACK_MSG_CONSOLE_LOG`, `CALLBACK_MSG_TOO_SLOW_SCANNING`) and cast `message_data` accordingly (e.g. to `YR_RULE*` for the MATCHING/NOT_MATCHING messages), and return one of `CALLBACK_CONTINUE`/`CALLBACK_ABORT`/`CALLBACK_ERROR` to steer the scan loop. All project OSS-Fuzz fuzzers use a trivial callback returning `CALLBACK_CONTINUE` unconditionally, which is sufficient and valid for a harness (it keeps scanning through all rules/messages without special-casing).
**Evidence:**
> libyara/scanner.c:580-607: rule-matching loop calls `scanner->callback(scanner, message, rule, scanner->user_data)` with `message` = `CALLBACK_MSG_RULE_MATCHING`/`CALLBACK_MSG_RULE_NOT_MATCHING`, and switches on the return value: `CALLBACK_ABORT` -> stop with `ERROR_SUCCESS`, `CALLBACK_ERROR` -> stop with `ERROR_CALLBACK_ERROR`.
> libyara/scanner.c:609-610 (truncated view) and context: final `scanner->callback(scanner, CALLBACK_MSG_SCAN_FINISHED, NULL, scanner->user_data)` call after the rule loop.
> tests/oss-fuzz/pe_fuzzer.cc:62-69: `int callback(YR_SCAN_CONTEXT* context, int message, void* message_data, void* user_data) { return CALLBACK_CONTINUE; }` — minimal valid callback pattern reused verbatim in elf_fuzzer.cc, math_fuzzer.cc and others.
> libyara/include/yara/rules.h:38-49: `CALLBACK_MSG_*` and `CALLBACK_CONTINUE/ABORT/ERROR` constant definitions that the callback must interpret/return.

### P2.10 Behaviour Controls
**Claim:** Parameters/flags that select different processing paths:
- `flags & SCAN_FLAGS_PROCESS_MEMORY` selects entry-point computation mode: `yr_get_entry_point_address` (memory-mapped PE/ELF layout) vs `yr_get_entry_point_offset` (file layout) — libyara/scanner.c:538-543.
- `flags & SCAN_FLAGS_NO_TRYCATCH` disables the `YR_TRYCATCH` SIGSEGV-guard wrapper around the AC scan loop and around `yr_execute_code`, converting internal faults into `ERROR_COULD_NOT_MAP_FILE` only when trycatch is enabled — libyara/scanner.c:522-524,553,570-573. All project OSS-Fuzz fuzzers always pass `SCAN_FLAGS_NO_TRYCATCH` so that libFuzzer/ASan directly observes crashes instead of the trycatch handler suppressing them.
- `flags & SCAN_FLAGS_REPORT_RULES_MATCHING` / `SCAN_FLAGS_REPORT_RULES_NOT_MATCHING` select which of `CALLBACK_MSG_RULE_MATCHING`/`CALLBACK_MSG_RULE_NOT_MATCHING` get reported per rule (default both if neither set) — libyara/scanner.c:391-401,584-592.
- `timeout` bounds scan duration; converted to ns, enforced inside the AC scan loop/condition evaluation (checked via `yr_stopwatch`-based deadline, not shown in full but referenced via `scanner->timeout`).
- Callback return value (`CALLBACK_CONTINUE`/`CALLBACK_ABORT`/`CALLBACK_ERROR`) changes control flow: abort stops scanning with success, error propagates `ERROR_CALLBACK_ERROR`.
- `buffer_size` relative to `YR_FILE_SIZE_THRESHOLD` (0.2MB) combined with whether the AC root state has matches (`rules->ac_match_table[YR_AC_ROOT_STATE] != 0`) triggers the extra `CALLBACK_MSG_TOO_SLOW_SCANNING` pre-check that can abort the whole scan before it starts — libyara/scanner.c:733-747.
- The compiled rule set (what `yr_compiler_add_string` was given) selects which AC transitions / regex paths / module parsers / bytecode opcodes actually execute — e.g. `import "pe"` pulls in the PE module parser during `yr_execute_code`.
**Evidence:** (file:line as cited inline above)

**Additional behaviour controls (previously missing):**
- Callback return value is a behaviour control with a large blast radius: `CALLBACK_ERROR` on `CALLBACK_MSG_IMPORT_MODULE` prevents the dex module from loading at all (modules.c:133-139), `CALLBACK_ABORT` stops the scan. Return `CALLBACK_CONTINUE` on the main path.
- Buffer offset: the dex parser needs the magic at the start of the scanned block (dex.c:466-486). Keep the scanned buffer at a fixed offset (0 preferred); control bytes come from the end of the input.
- `CALLBACK_MSG_MODULE_IMPORTED` + `yr_object_print_data` (cli/yara.c:1231-1249) is the result-usage path that walks the parsed dex object tree; redirect stdout to /dev/null once in `LLVMFuzzerInitialize`.
- Rule conditions with `for i in (0..dex.header.X_size)` iterate up to an attacker-chosen 32-bit count; avoid them.

### P2.11 Input Expectations
**Claim:** `buffer`/`buffer_size` have no required header or magic — it's treated as an arbitrary byte blob fed through the Aho-Corasick automaton byte-by-byte; `buffer_size == 0` is an explicitly supported edge case in some fuzzers (others guard `size == 0` themselves, e.g. math_fuzzer, because module functions like `math.entropy(0, filesize)` may need nonzero size to be meaningful, not because the scan engine itself requires it). No NUL-termination is required (size-bounded, not string-based). There is a soft "too slow" heuristic at ~0.2MB (`YR_FILE_SIZE_THRESHOLD`) when the automaton's root state already carries matches, which can short-circuit the scan via the callback unless it returns `CALLBACK_CONTINUE`. Internally `yr_scanner_scan_mem` wraps the whole buffer as a single `YR_MEMORY_BLOCK` (`block.base = 0`, `block.size = buffer_size`, `block.context = (void*) buffer`) and drives it through the same block-iterator machinery as `yr_rules_scan_mem_blocks`/file/fd variants, so from the engine's point of view a flat buffer is just a 1-block iterator.
**Evidence:**
> libyara/scanner.c:718-731: `yr_scanner_scan_mem` builds `block.size = buffer_size; block.base = 0; block.context = (void*) buffer;` and a 1-element iterator passed to `yr_scanner_scan_mem_blocks`.
> libyara/scanner.c:733-737: `// Detect cases where every byte of input is checked for match and input size is bigger then 0.2 MB` — `buffer_size > YR_FILE_SIZE_THRESHOLD` check.
> tests/oss-fuzz/pe_fuzzer.cc:72-78: no size check before calling `yr_rules_scan_mem(rules, data, size, ...)` — size 0 accepted.
> tests/oss-fuzz/math_fuzzer.cc:81-85: `if (rules == NULL || size == 0) return 0;` — this particular fuzzer additionally guards zero-size itself (module-specific choice, not an engine requirement).

### Other entries (API surface) — call protocol and combination with the target

**`yr_rules_scan_mem_blocks(YR_RULES* rules, YR_MEMORY_BLOCK_ITERATOR* iterator, int flags, YR_CALLBACK_FUNC callback, void* user_data, int timeout)`**
- Exists: libyara/rules.c:172-194; declared libyara/include/yara/rules.h:76-82.
- Needs before it: same `rules` prerequisites as the target, plus caller must build a `YR_MEMORY_BLOCK_ITERATOR` (with `first`/`next`/`file_size`/`last_error` set) — a lower-level alternative input shape to a flat buffer; not needed if using the target directly.
- Internally identical lifecycle to the target: `yr_scanner_create` → `set_callback/set_timeout/set_flags` → `yr_scanner_scan_mem_blocks(scanner, iterator)` → `yr_scanner_destroy`. (libyara/rules.c:183-191)
- Return/cleanup: same conventions as the target (int error code; scanner self-destroyed).
- Combination with target: mutually exclusive alternative entry for the same core; a harness normally picks one. If both are exercised, each call independently creates/destroys its own scanner, so no extra ordering constraint between them beyond both needing the same `rules`.

**`yr_rules_scan_file(YR_RULES* rules, const char* filename, int flags, YR_CALLBACK_FUNC callback, void* user_data, int timeout)`**
- Exists: libyara/rules.c:241-262; declared libyara/include/yara/rules.h:93-99.
- Internally: `yr_filemap_map(filename, &mfile)` then calls `yr_rules_scan_mem(rules, mfile.data, mfile.size, flags, callback, user_data, timeout)`, then `yr_filemap_unmap(&mfile)`. (libyara/rules.c:251-259)
- Needs before it: a real/accessible file path (less convenient for in-memory fuzzing than the target); same `rules`/callback prerequisites.
- Return: propagates `yr_filemap_map`'s error if mapping fails, else the target's return code.
- Cleanup: `yr_filemap_unmap` always called by this wrapper if map succeeded; caller doesn't need to free anything extra.
- Combination: a drop-in file-backed variant of the target; a harness using libFuzzer data would normally write `data` to a temp file to exercise this path, or just call the target directly on the in-memory buffer instead (simpler, and is what pe_fuzzer.cc/elf_fuzzer.cc/math_fuzzer.cc do).

**`yr_scanner_scan_mem(YR_SCANNER* scanner, const uint8_t* buffer, size_t buffer_size)`**
- Exists: libyara/scanner.c:705-760; declared in libyara/include/yara/scanner.h (same header group).
- Needs before it: an already-created `YR_SCANNER*` via `yr_scanner_create(rules, &scanner)`, with `yr_scanner_set_callback` (mandatory, else `ERROR_CALLBACK_REQUIRED`), and optionally `yr_scanner_set_flags`/`yr_scanner_set_timeout` configured beforehand (scanner defaults: flags = report-matching+report-not-matching, timeout = 0).
- Parameters: identical `buffer`/`buffer_size` semantics as the target.
- Return: same int error codes as the target.
- Cleanup: caller must call `yr_scanner_destroy(scanner)` itself (unlike the target, which does this internally) — this is the key lifecycle difference: using this lower-level API directly means the harness owns scanner create/destroy.
- Combination with target: `yr_rules_scan_mem` is literally implemented as create+configure+`yr_scanner_scan_mem`+destroy (libyara/rules.c:217-225), so calling this function directly is only useful if the harness wants to reuse one scanner across multiple scans or set additional state (e.g., multiple `yr_scanner_set_flags` calls) between runs; a harness should not call both the target and this function on the *same* already-destroyed scanner.
**Evidence:** cli/yara.c:1704-1718 — CLI builds the scanner once per thread with `yr_scanner_create`, configures callback/flags/timeout, calls `scan_file` (which wraps `yr_scanner_scan_fd`), and destroys the scanner only after the whole file/proc scan completes (cli/yara.c:1751-1752).

**`yr_compiler_add_string(YR_COMPILER* compiler, const char* rules_string, const char* namespace_)`**
- Exists: libyara/compiler.c:658-680; declared libyara/include/yara/compiler.h.
- Needs before it: `yr_compiler_create(&compiler)` must have succeeded; must not be called after `yr_compiler_get_rules` (asserted `compiler->rules == NULL`); must not be called again after a previous failing add call (asserted `compiler->errors == 0`).
- Parameters: `rules_string` is a YARA rules-source text (attacker/engineer controlled); `namespace_` may be NULL (defaults to `"default"`).
- Return: number of parse errors accumulated (`0` means success); on error, `compiler->errors`/`compiler->last_error` reflect failure and further add/get_rules calls are precluded by the asserts above.
- Combination with target: must run to completion (return 0) before `yr_compiler_get_rules` is called to obtain the `YR_RULES*` consumed by the target.
**Evidence:** libyara/compiler.c:663-679; tests/oss-fuzz/pe_fuzzer.cc:53 `if (yr_compiler_add_string(compiler, RULES, NULL) == 0) yr_compiler_get_rules(compiler, &rules);`

**`yr_compiler_get_rules(YR_COMPILER* compiler, YR_RULES** rules)`**
- Exists: libyara/compiler.c:729-743.
- Needs before it: zero-or-more successful `yr_compiler_add_string`/`add_file` calls, and `compiler->errors == 0` (asserted).
- Return: `ERROR_SUCCESS` on success, with `*rules` set to a valid `YR_RULES*` (compiles on first call via `_yr_compiler_compile_rules`, cached in `compiler->rules` for subsequent calls); `*rules` is set to `NULL` first and only populated on success.
- Combination with target: produces the `rules` object that is the first argument to `yr_rules_scan_mem`/`yr_rules_scan_mem_blocks`/`yr_rules_scan_file`. Caller must check the return code and only proceed to scan if `ERROR_SUCCESS`.
**Evidence:** libyara/compiler.c:735-742; tests/oss-fuzz/pe_fuzzer.cc:50-56.

**`yr_rules_define_integer_variable` / `yr_rules_define_boolean_variable` / `yr_rules_define_float_variable` / `yr_rules_define_string_variable`**
- Exist: libyara/rules.c:44-170; declared libyara/include/yara/rules.h:127-145.
- Needs before them: a valid `YR_RULES*` (post `yr_compiler_get_rules`) whose `ext_vars_table` already contains an entry with the given `identifier` matching the expected `EXTERNAL_VARIABLE_TYPE_*` — the identifier/type pair must have been declared earlier in the YARA rule source (e.g. via an `external`/declared variable in the rule text) or via the compiler-side `yr_compiler_define_*_variable` before compiling; otherwise these return `ERROR_INVALID_ARGUMENT` (unknown identifier) or `ERROR_INVALID_EXTERNAL_VARIABLE_TYPE` (type mismatch).
- Parameters: `identifier` non-NULL string; `value` typed per variant (`int64_t`, `int` boolean 0/1, `double`, `const char*`).
- Return: `ERROR_SUCCESS`, `ERROR_INVALID_ARGUMENT`, `ERROR_INVALID_EXTERNAL_VARIABLE_TYPE`, or `ERROR_INSUFFICIENT_MEMORY` (string variant, malloc'd copy via `yr_strdup`).
- Cleanup: the string variant frees any previous `EXTERNAL_VARIABLE_TYPE_MALLOC_STRING` value before replacing it (libyara/rules.c:151-155); `yr_rules_destroy` frees the final malloc'd string (libyara/rules.c:591-592).
- Combination with target: must be called on `rules` before the `yr_rules_scan_mem` call(s) whose rule conditions read that external variable, because the scanner snapshots `rules->ext_vars_table` into per-scan `YR_OBJECT`s at `yr_scanner_create` time (libyara/scanner.c:305-327). Calling a define function between two scans is valid and affects only subsequent scans.
**Evidence:** libyara/rules.c:44-170 (all four variants); cli/common.c:165-202 (`define_external_variables` calling these before scanning in the CLI tool).

### Build notes
**Claim:** Fuzzers are built as **C++** translation units (`.cc`) that `#include <yara.h>` (the umbrella public header which pulls in `yara/rules.h`, `yara/scanner.h`, etc. via its own include chain), linked statically against `libyara.a` plus the OSS-Fuzz fuzzing engine. No special `-I` beyond `-I. -Ilibyara/include` is needed since the project is `make install`-ed first.
**Evidence:**
> ossfuzz_view/yara-c1/build.sh:18-23: `./bootstrap.sh`; `./configure --enable-macho --enable-debug --enable-dex --enable-dotnet --without-crypto`; `make clean && make -j... all && make install` — builds libyara (and module support for macho/dex/dotnet) before any fuzzer compilation.
> ossfuzz_view/yara-c1/build.sh:25-35: `fuzzers=$(find $SRC/yara/tests/oss-fuzz/ -name "*.cc")`; for each: `$CXX $CXXFLAGS -std=c++17 -I. -Ilibyara/include $f -o $OUT/$fuzzer_name ./.libs/libyara.a $LIB_FUZZING_ENGINE` — confirms C++17, static link against `.libs/libyara.a`, include path `libyara/include` (where `yara/rules.h` lives) plus `.` for the top-level `yara.h` umbrella header.
> tests/oss-fuzz/pe_fuzzer.cc:32: `#include <yara.h>` — the single include used by all project fuzzers; it transitively provides `yr_rules_scan_mem`, `YR_RULES`, `YR_CALLBACK_FUNC`, `CALLBACK_*`, `SCAN_FLAGS_*`.
> ossfuzz_view/yara-c1/build.sh:37-38: seed corpora and `.dict`/`.options` files are copied per-fuzzer-name match from `tests/oss-fuzz`, confirming fuzzer binary names are derived 1:1 from the `.cc` basenames in that directory (e.g. `pe_fuzzer`, `rules_fuzzer`, `math_fuzzer`).