## target: janet_parser_consume

### P2.0 Main path to keep (every input) — read first
**Claim:** `janet_init` → `janet_core_env(NULL)` → `janet_parser_init` → (inside `janet_try`) per byte: break on PARSE_ERROR/DEAD, `janet_parser_consume`, drain `janet_parser_has_more`/`janet_parser_produce[_wrapped]` and for each form `janet_to_string`, `janet_compile`, and on success `janet_thunk` → `janet_fiber(f,64,0,NULL)` → `fiber->env = env` → `janet_continue` (stringify the error value on failure) → after the loop `janet_parser_eof` + drain + `janet_parser_error` → `janet_restore` → `janet_parser_deinit` → `janet_deinit`. This is the janet_dobytes protocol; it stops at the first parse error.
**Rule for the harness:** keep every step of this main path on every input, fed with the full fuzz input; Logic-Group entries and the other sections of this report only add calls on top of it (selected by spare input bits), they never replace or dilute a main-path step.
**Evidence:**
> src/core/run.c:42-60: `janet_parser_init(parser); ... while (janet_parser_has_more(parser)) { Janet form = janet_parser_produce(parser); JanetCompileResult cres = janet_compile(form, env, where); if (cres.status == JANET_COMPILE_OK) { JanetFunction *f = janet_thunk(cres.funcdef); fiber = janet_fiber(f, 64, 0, NULL); fiber->env = env; JanetSignal status = janet_continue(fiber, janet_wrap_nil(), &ret); ...`
> src/core/run.c:90-104: `switch (janet_parser_status(parser)) { case JANET_PARSE_DEAD: done = 1; ... case JANET_PARSE_ERROR: ... janet_parser_error(parser) ... done = 1;` — the library stops at the first parse error.
> src/core/run.c:107-110: `case JANET_PARSE_ROOT: case JANET_PARSE_PENDING: if (index >= len) { janet_parser_eof(parser); } else { ...consume...`
> src/mainclient/shell.c:480-490: produced forms are stringified with `janet_to_string`.
> test/fuzzers/fuzz_compile.c:26-30: `while (janet_parser_has_more(&parser)) { Janet form = janet_parser_produce(&parser); JanetCompileResult res = janet_compile(form, env, janet_cstring("fuzz")); ...`

### P2.1 Init Sequence
**Claim:** `janet_init()` (global VM/GC init) must be called once before any parser is used, because the core consumer functions allocate GC objects (tuples, strings) via `janet_gcalloc`/`janet_tuple_begin`, which require `janet_vm` state. Then `janet_parser_init(&parser)` must be called to zero-initialize the `JanetParser` struct and push the initial root state before the first call to `janet_parser_consume`.
**Evidence:**
> src/include/janet.h:1635-1641 (doc example): "JanetParser parser; janet_parser_init(&parser); for (...) { janet_parser_consume(&parser, source_code[i]); ... }"
> src/core/parse.c:784-802: `janet_parser_init` sets `args=NULL, states=NULL, buf=NULL, argcount=0, ..., statecount=0, statecap=0, error=NULL, lookback=-1, line=1, column=0, pending=0, flag=0;` then `pushstate(parser, root, PFLAG_CONTAINER);` — this is what sets up `states[0]` with consumer `root`.
> src/core/parse.c:697-715 `janet_parser_consume` dereferences `parser->states[parser->statecount-1]` unconditionally — if `janet_parser_init` was not called, `states`/`statecount` are garbage/0, causing an out-of-bounds read/UB.
> test/fuzzers/fuzz_compile.c:12,18: `janet_init(); ... janet_parser_init(&parser);` called before the consume loop.
> test/fuzzers/fuzz_dostring.c (gold stub, no real call) — not informative for ordering.
> src/core/run.c:41-42: `janet_dobytes` allocates the parser as a GC abstract object (`janet_abstract(&janet_parser_type, ...)`) and calls `janet_parser_init(parser)` immediately after, confirming init always directly follows allocation and precedes any consume.

### P2.2 Parameter Construction
**Claim:** `parser` must be a pointer to a `JanetParser` that has been initialized by `janet_parser_init` (stack, heap, or GC-abstract-allocated are all legal per the header comment) and not yet `janet_parser_deinit`'d. `c` is a raw `uint8_t`; any byte value 0-255 is legal input, no special sentinel value needed (EOF is signaled separately via `janet_parser_eof`, not by a reserved byte).
**Evidence:**
> src/include/janet.h:1659: "Construct/destruct a parser. Parsers can be allocated on the stack or the heap."
> src/include/janet.h:1664: `JANET_API void janet_parser_consume(JanetParser *parser, uint8_t c);` — second parameter type is `uint8_t`, full byte range valid.
> src/core/parse.c:1006: `janet_parser_consume(p, 0xFF & i);` (from `cfun_parse_insert`'s space-insertion trick) shows arbitrary byte values are passed directly.
> src/core/parse.c:697-715: no range check performed on `c` itself besides dispatch through `is_whitespace`/`janet_is_symbol_char`/switch statements in the consumer functions — any byte is accepted and dispatched.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is create (stack/heap/GC-abstract alloc) → `janet_parser_init` → repeated `janet_parser_consume` (optionally `janet_parser_eof` once at end) interleaved with `janet_parser_status`/`janet_parser_produce`/`janet_parser_error`/`janet_parser_flush` → `janet_parser_deinit` (if not GC-managed) when parsing is done.
**Evidence:**
> src/include/janet.h:1635-1654 full documented lifecycle example ending in `janet_parser_deinit(&parser);`.
> src/core/parse.c:804-808: `janet_parser_deinit` frees `parser->args`, `parser->buf`, `parser->states` via `janet_free` — must be called exactly once per init'd parser that was not GC-tracked, or the three buffers leak.
> src/core/run.c:41-43: when the parser is GC-abstract allocated (`janet_abstract`) and `janet_gcroot`ed, `janet_parser_deinit` is NOT called explicitly in `janet_dobytes` — instead a `parsermark`/finalizer path (GC) owns cleanup (see `janet_parser_type` definition at src/core/parse.c:884). This is the alternate (GC-managed) lifecycle vs. the stack-allocated + explicit-deinit lifecycle used in test/fuzzers/fuzz_compile.c:17,35.
> test/fuzzers/fuzz_compile.c:17-35: `janet_parser_init(&parser); ... (loop consuming) ... janet_parser_deinit(&parser);` — stack-allocated, explicit init/deinit pairing.

### P2.4 Return Value Handling
**Claim:** `janet_parser_consume` returns `void`; callers must instead check `janet_parser_status(parser)` after (each) call to detect `JANET_PARSE_ERROR`/`JANET_PARSE_DEAD` before feeding more bytes, since feeding bytes to a dead/errored parser panics.
**Evidence:**
> src/include/janet.h:1664 + comment line 1663: "Feed bytes into the parser. Check the parser state after every byte to handle errors." (no return value)
> src/core/parse.c:690-693: `janet_parser_checkdead` — `if (parser->flag) janet_panic("parser is dead, cannot consume"); if (parser->error) janet_panic("parser has unchecked error, cannot consume");` called at the very top of `janet_parser_consume` (line 699) — so calling consume again after an unhandled error/death triggers a process-terminating panic (longjmp or abort depending on whether `janet_try` context exists).
> test/fuzzers/fuzz_compile.c:23-24: `if (janet_parser_status(&parser) == JANET_PARSE_ERROR) break; janet_parser_consume(&parser, data[i]);` — real caller explicitly gates further `consume` calls on status to avoid the panic.
> src/core/run.c:90-113: `janet_dobytes` switches on `janet_parser_status(parser)`, only calling `janet_parser_consume`/`janet_parser_eof` in the `JANET_PARSE_ROOT`/`JANET_PARSE_PENDING` cases, handling `JANET_PARSE_ERROR` and `JANET_PARSE_DEAD` separately without further consuming.

### P2.5 Cleanup Sequence
**Claim:** On both success and error paths, if the parser was manually allocated (stack/heap, not GC-tracked), call `janet_parser_deinit(parser)` exactly once at the end to free `args`, `buf`, `states`. No other explicit cleanup functions exist for the parser (no separate "close" or "free consumed buffer" calls). If a `janet_try`/`janet_restore` guard (setjmp-based) was used around the consume loop (as in fuzz_compile.c) it must be `janet_restore`d before/around the `janet_parser_deinit` call.
**Evidence:**
> src/core/parse.c:804-808: `void janet_parser_deinit(JanetParser *parser) { janet_free(parser->args); janet_free(parser->buf); janet_free(parser->states); }`
> test/fuzzers/fuzz_compile.c:33-38: `janet_restore(&tstate); janet_parser_deinit(&parser); janet_deinit();` — deinit the parser after restoring from any panic triggered during consume/compile, then global `janet_deinit()`.
> src/mainclient/shell.c:494-497: `parsing_done: janet_parser_deinit(&p); gbl_historyi = 0; fclose(history_file);` — deinit called on both normal completion and the early `goto parsing_done` error path.

### P2.6 API Existence
**Claim:** All functions named in the Logic Group's "Entries" list exist verbatim in this codebase; `janet_parser_clone` also exists but is not in the entry list (not required by the harness).
**Evidence:**
> src/include/janet.h:1660-1688: declarations for `janet_parser_init`, `janet_parser_deinit`, `janet_parser_consume`, `janet_parser_status`, `janet_parser_produce`, `janet_parser_produce_wrapped`, `janet_parser_error`, `janet_parser_flush`, `janet_parser_eof`, `janet_parser_has_more` all present.
> src/core/parse.c:697 `void janet_parser_consume(...)`, :717 `janet_parser_eof`, :730 `janet_parser_status`, :737 `janet_parser_flush`, :744 `janet_parser_error`, :756 `janet_parser_produce`, :770 `janet_parser_produce_wrapped`, :784 `janet_parser_init`, :804 `janet_parser_deinit`, :855 `janet_parser_has_more` — all defined.
> src/core/parse.c:810 `void janet_parser_clone(const JanetParser *src, JanetParser *dest)` exists but is outside the Logic Group's entry list; not needed by harness.

### P2.7 Co-call Constraints
**Claim:** (a) `janet_parser_init`/`janet_parser_deinit` are a strict create/destroy pair (deinit must not be called without a prior init, and should be called exactly once per init). (b) `janet_parser_error` internally calls `janet_parser_flush`, so a harness need not call both — calling `janet_parser_flush` again afterward is a harmless no-op since state is already reset. (c) `janet_parser_has_more`/`janet_parser_produce` form a check-then-use pair: `produce` must only be called while `has_more` is non-zero, otherwise it returns `janet_wrap_nil()` silently (not an error, but a wasted/no-op call). (d) `janet_parser_consume`/`janet_parser_eof` share the same internal "deadness"/error guard (`janet_parser_checkdead`), so neither should be called again after the parser enters `JANET_PARSE_DEAD` (post-`janet_parser_eof`) or `JANET_PARSE_ERROR` (until flushed/cleared).
**Evidence:**
> src/core/parse.c:744-754 `janet_parser_error`: "`janet_parser_flush(parser); return e;`" inside the body — confirms error-retrieval auto-flushes.
> src/core/parse.c:756-759 `janet_parser_produce`: `if (parser->pending == 0) return janet_wrap_nil();` — safe no-op if misused without checking `has_more` first, but yields a bogus nil value mixed into the output stream if not gated.
> src/core/parse.c:690-693 `janet_parser_checkdead` is called at the top of both `janet_parser_consume` (line 699) and `janet_parser_eof` (line 718) — both panic if `parser->flag` (dead) or `parser->error` is set.
> src/core/parse.c:727: `janet_parser_eof` sets `parser->flag |= JANET_PARSER_DEAD;` at the end — after this, any further `janet_parser_consume`/`janet_parser_eof` call panics via `janet_parser_checkdead`.
> src/core/parse.c:804-808 `janet_parser_deinit` has no idempotency guard (no NULL check before `janet_free`, but `janet_free` on NULL is safe via libc `free`) — double-deinit of the same unmodified parser is technically safe since freed pointers aren't re-nulled, so a *second* deinit on the same struct instance would double-free; must only be called once.

### P2.8 Prerequisite State
**Claim:** Beyond `janet_init()`/`janet_parser_init`, no additional global or connection state is required specifically for the parser (it does not need a `JanetTable *env`, fiber, or compiler state — those are only needed by consumers of parsed forms like `janet_compile`). However, real-world callers typically wrap the whole consume/produce loop in a `janet_try`/`janet_restore` (setjmp-based) guard because GC allocation failures or other panics raised deep in the Janet runtime (e.g., `JANET_OUT_OF_MEMORY`, or an "unexpected character"-type internal panic path) could otherwise abort the process.
**Evidence:**
> src/core/parse.c:697-715: `janet_parser_consume` only touches fields of `*parser` plus calling into GC-allocating helpers (`push_arg`→potentially `janet_tuple_begin`/`janet_gcalloc` inside `popstate`), so it needs the global VM (`janet_init`) but nothing else.
> test/fuzzers/fuzz_compile.c:21-33: `JanetTryState tstate; if (janet_try(&tstate) == JANET_SIGNAL_OK) { ... janet_parser_consume ... } janet_restore(&tstate);` wraps the whole consume loop.
> src/core/parse.c:118-120 `JANET_OUT_OF_MEMORY` macro calls `exit(1)` directly (not a janet panic/longjmp) on stack-growth OOM inside `push_buf`/`push_arg`/`_pushstate` — a harness cannot "catch" this via `janet_try`; it is a hard process-exit, not relevant to fuzzer crash classification but worth noting it is not recoverable.

### P2.9 Result Usage
**Claim:** The target itself returns nothing; its effect (new value(s) available) is observed by other entries. `janet_parser_has_more` gates `janet_parser_produce`/`janet_parser_produce_wrapped`, whose returned `Janet` values are fed into `janet_compile` (compiler) or printed/inspected (REPL/shell). `janet_parser_status` is checked after `janet_parser_consume`/`janet_parser_eof` to branch to `janet_parser_error` (on `JANET_PARSE_ERROR`) or continue consuming (on `JANET_PARSE_ROOT`/`JANET_PARSE_PENDING`) or stop (on `JANET_PARSE_DEAD`).
**Evidence:**
> test/fuzzers/fuzz_compile.c:26-30: `while (janet_parser_has_more(&parser)) { Janet form = janet_parser_produce(&parser); JanetCompileResult res = janet_compile(form, env, janet_cstring("fuzz")); (void) res; }` — direct result-consumption pattern.
> src/core/run.c:49-51: `while (janet_parser_has_more(parser)) { Janet form = janet_parser_produce(parser); JanetCompileResult cres = janet_compile(form, env, where); ... }` — identical pattern in the "real" library API `janet_dobytes`.
> src/mainclient/shell.c:480-490: `while (janet_parser_has_more(&p) && gbl_history_count < JANET_HISTORY_MAX) { if (janet_parser_status(&p)==JANET_PARSE_ERROR) { ...janet_parser_error(&p)...; goto parsing_done; } Janet x = janet_parser_produce(&p); const char *cstr = (const char*) janet_to_string(x); ... }` — produce's result converted via `janet_to_string` and stored.
> src/core/parse.c:949-986 (`cfun_parse_insert`, the `(parser/insert ...)` C function): inspects `p->states[p->statecount-1]` (parser internal state, not the return of consume, but built by the same core) to manually inject values — shows the stack-of-states structure built by `janet_parser_consume` is also directly manipulated by other API functions.

### P2.10 Behaviour Controls
**Claim:** `janet_parser_consume` has no explicit flags parameter — all branching is driven purely by the byte value `c` and by which `consumer` function is active on top of `parser->states`. Key byte-driven branches inside `root` (the initial/outer consumer): whitespace bytes return without pushing state; reader-macro bytes `' , ; ~ |` push a `PFLAG_READERMAC` state; `"` pushes `stringchar`/`PFLAG_STRING`; `#` pushes `comment`; `@` pushes `atsign`; `` ` `` pushes `longstring`; `( [ {` push nested `root` states with `PFLAG_PARENS`/`SQRBRACKETS`/`CURLYBRACKETS`; `) ] }` pop state via `popstate`, erroring via `delim_error` on mismatch/empty stack; any other non-symbol-char byte sets `p->error = "unexpected character"`. Other entries have their own mode-selecting parameters: `janet_parser_produce` vs `janet_parser_produce_wrapped` select whether the top-level tuple wrapper (source-mapping) is stripped or kept.
**Evidence:**
> src/core/parse.c:617-687 (`root` function): full switch over `c` showing all branch targets described above.
> src/core/parse.c:640-641: `case '@': pushstate(p, atsign, PFLAG_ATSYM); return 1;` — triggers `atsign` consumer (lines ~560-613 per earlier read) which itself branches on the next byte to decide `@[`, `@(`, `@{` vs. falling back to a token starting with a literal `@`.
> src/core/parse.c:756-767 vs 770-781: `janet_parser_produce` unwraps `janet_unwrap_tuple(parser->args[0])[0]` (strips the source-map wrapper tuple) while `janet_parser_produce_wrapped` returns `parser->args[0]` as-is (keeps the wrapper) — the two "modes" for retrieving the same underlying parsed form.
> src/core/parse.c:650-674: inside `)]}` handling, `state->flags & PFLAG_ATSYM` selects `close_array`/`close_table` vs `close_tuple`/`close_struct`, and `c == ']'` passes `JANET_TUPLE_FLAG_BRACKETCTOR` to `close_tuple` — showing bracket-type-dependent value construction controlled purely by the byte stream.

**Note on error recovery:** `janet_parser_error`/`janet_parser_flush` followed by more `janet_parser_consume` calls is legal but is not what janet_dobytes does (it stops, run.c:90-104); use it only on a small share of inputs as an addition, because each recovered form is compiled and run and per-input cost is already high.

### P2.11 Input Expectations
**Claim:** The target treats its input as a raw, unterminated byte stream (no length/header/framing is implied by `janet_parser_consume` itself — framing/length is the caller's responsibility, e.g. `size` in a fuzz harness loop). High bytes (>=0x80) are accepted as symbol characters but are later checked for well-formed UTF-8 by `janet_valid_utf8`/`janet_is_symbol_char` when a token is finalized; malformed sequences do not panic `consume` itself, they set `parser->error`, which must be checked via `janet_parser_status`. There is no explicit "maximum nesting depth" or token-size limit enforced in `janet_parser_consume` besides dynamic realloc growth of `states`/`buf`/`args` (bounded only by `janet_realloc` failure → `JANET_OUT_OF_MEMORY` → `exit(1)`). `\r`/`\n` bytes update line/column tracking (`parser->line`, `parser->column`) but are not otherwise "special terminators" to the state machine (CRLF handled via `lookback`).
**Evidence:**
> src/core/parse.c:697-715: no length parameter — purely one byte at a time, caller controls stream length.
> src/core/parse.c:700-709: `if (c=='\r') { line++; column=0; } else if (c=='\n') { column=0; if(lookback!='\r') line++; } else { column++; }` — CR/LF bookkeeping, not framing.
> src/core/parse.c:55-57,62-93: `janet_is_symbol_char`/`janet_valid_utf8` — UTF-8 validation happens elsewhere (e.g., when finalizing a `tokenchar`/symbol, not inline per-byte in `consume`), confirming multi-byte UTF-8 sequences are buffered first.
> src/core/parse.c:110-126 (`DEF_PARSER_STACK` macro): dynamic growth (`newcap = 2*newcount`) for `buf`, `args`, `states` — the only size limit is memory/`JANET_OUT_OF_MEMORY` (line 419 in janet.h), i.e. `exit(1)`, not a bounded/panicking path catchable by `janet_try`.
> src/core/parse.c:717-728 (`janet_parser_eof`): internally feeds a synthetic `'\n'` and then checks `parser->statecount > 1` to flag "unexpected end of source" via `delim_error` — this is how callers signal "no more input" rather than any reserved terminator byte.

## Build notes
**Claim:** Fuzz harnesses for this project are plain C (compiled with `$CC`, C99), linked against the static library `build/libjanet.a` by the C++ driver (`$CXX`) together with `$LIB_FUZZING_ENGINE`. The target's declaration is reached via `-Isrc/include` (`#include <janet.h>`), and `-DJANET_BOOTSTRAP` plus `-Isrc/conf` are required compile flags.
**Evidence:**
> ${QF_WORK}/ossfuzz_view/janet-c1/build.sh:17-30:
> `export CFLAGS="$CFLAGS -fPIC"` ; `make` ; then for each fuzzer e.g.:
> `$CC -D_XOPEN_SOURCE=600 $CFLAGS -DJANET_BOOTSTRAP -Isrc/include -Isrc/conf -std=c99 -fPIC -o fuzz_compile.o -c ./test/fuzzers/fuzz_compile.c`
> `$CXX $CXXFLAGS $LIB_FUZZING_ENGINE fuzz_compile.o build/libjanet.a -o $OUT/fuzz_compile`
> test/fuzzers/fuzz_compile.c:2: `#include <janet.h>` — picked up from `-Isrc/include` (the file at src/include/janet.h matches the declaration location given in the task, confirming the same header is what's installed/used).
> ${QF_WORK}/ossfuzz_view/janet-c1/Dockerfile:17-22: base image `gcr.io/oss-fuzz-base/base-builder`, clones `janet` repo, `WORKDIR $SRC/janet`, copies `build.sh` — confirms the project is built via its own `make`-produced `build/libjanet.a` static archive, not a system package.
> Existing fuzzers in this build (`fuzz_dostring`, `fuzz_unmarshal`, `fuzz_dobytes`, `fuzz_compile`) are the authoritative examples of how a harness is wired against `libjanet.a`; `fuzz_compile.c` is the only one directly calling `janet_parser_consume` (test/fuzzers/fuzz_compile.c:25), the rest call higher-level `janet_dostring`/`janet_dobytes`/`janet_unmarshal` which internally reach the parser (`janet_dobytes` → `janet_parser_consume`, src/core/run.c:111, per `find_path`/grep evidence above). Note: `test/fuzzers/fuzz_dostring.c` in this checkout is a "gold stub" that only keeps the symbol linked via `__asm__` aliasing (lines 10-11) and does not actually call it — this is a baseline/placeholder, not evidence of real usage; use `fuzz_compile.c` and `src/core/run.c`/`src/mainclient/shell.c` as the real usage evidence instead.