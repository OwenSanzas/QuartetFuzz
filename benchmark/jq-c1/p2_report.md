## target: jq_next

### P2.1 Init Sequence
**Claim:** Before calling `jq_next`, the caller must: (1) create state with `jq_init()`, (2) compile a program into it with `jq_compile()`/`jq_compile_args()` (must succeed, return nonzero), (3) initialize execution state with `jq_start(jq, input, flags)`. `jq_next` asserts a valid PC exists (`stack_restore`), which is only set up by `jq_start`.
**Evidence:**
> src/execute.c:343-344: `uint16_t* pc = stack_restore(jq); assert(pc);` — requires `jq_start` to have called `stack_save` first.
> src/execute.c:1116-1129 `jq_start`: calls `jq_reset(jq)`, pushes a top frame, `stack_push(jq, input)`, `stack_save(jq, jq->bc->code, ...)`, sets `jq->initial_execution = 1`. Without this, `pc` from `stack_restore` is NULL and the assert fails.
> tests/jq_fuzz_execute.cpp:27-34: `jq_compile(jq, prog)` must return truthy before `jq_start`/`jq_next` are called at all.
> src/jq_test.c:307-320 (`test_jq_start_resets_state`): `jq_compile` → `jq_start` → loop `jq_next` until invalid — canonical order.

### P2.2 Parameter Construction
**Claim:** `jq_next` takes a single `jq_state*` previously returned by `jq_init` (non-NULL) and already run through `jq_compile`/`jq_start`. No other parameters.
**Evidence:**
> src/jq.h:30: `jv jq_next(jq_state *);`
> src/execute.c:340: `jv jq_next(jq_state *jq) { jv_nomem_handler(jq->nomem_handler, ...); ... }` — dereferences `jq` unconditionally, so `jq` must be a valid, non-NULL, initialized state (no NULL-check).

### P2.3 Object Lifecycle
**Claim:** Lifecycle is `jq_init()` → `jq_compile[_args]()` (repeatable/recompilable) → `jq_start()` → loop `jq_next()` until `jv_invalid()` → optionally repeat `jq_start()`+`jq_next()` loop (state is reusable) → `jq_teardown(&jq)`.
**Evidence:**
> src/execute.c:1054-1092 `jq_init`: allocates and zero/default-initializes `jq_state` (stk, curr_frame=0, fork_top=0, stk_top=0, error=jv_null(), halted=0, exit_code/error_message=invalid, attrs=jv_object()).
> src/execute.c:1131-1143 `jq_teardown`: `jq_reset(old_jq)`, `bytecode_free(old_jq->bc)`, `jv_free(old_jq->attrs)`, `jv_mem_free(old_jq)`; sets caller's pointer to NULL.
> src/jq_test.c:464-515 `run_jq_exhaust_and_reuse_tests`: shows `jq_start`+drain-`jq_next` loop repeated multiple times on the same `jq_state` without re-compiling, then a single `jq_teardown`.
> src/jq_test.c:401-460ish `run_jq_recompile_tests`: shows `jq_compile`/`jq_compile_args` can be called again on the same `jq` (old bytecode freed internally, src/execute.c:1239-1242).

### P2.4 Return Value Handling
**Claim:** `jq_next` returns a valid `jv` for each produced output; returns `jv_invalid()` when the program's output stream is exhausted (ordinary end) or an uncaught error occurred, in which case `jv_invalid_has_msg()`/`jv_invalid_get_msg()` on the returned (invalid) jv distinguishes a real error from plain exhaustion; separately `jq_halted()` must be checked for `halt`/`halt_error`.
**Evidence:**
> src/execute.c:352-356: `if (jq->halted) { ... return jv_invalid(); }` — halted path returns invalid with no message; caller must use `jq_halted`/`jq_get_exit_code`/`jq_get_error_message` to learn why.
> src/execute.c:805-814 (`BACKTRACK`): `if (!pc) { if (!jv_is_valid(jq->error)) { jv error = jq->error; jq->error = jv_null(); return error; } return jv_invalid(); }` — an uncaught error is returned as an invalid jv carrying a message; plain end-of-stream returns a plain `jv_invalid()`.
> src/execute.c:979-993 (`RET`): on top-level return (`retaddr == 0`) returns `value` (a valid jv) as one produced output, after saving a backtrack point so a subsequent `jq_next` call can resume.
> src/main.c:179: `while (jv_is_valid(result = jq_next(jq))) { ... }` then src/main.c:210-232 checks `jq_halted`, then else-branch checks `jv_invalid_has_msg(jv_copy(result))` for a genuine uncaught exception vs. plain exhaustion.

### P2.5 Cleanup Sequence
**Claim:** Each value returned by `jq_next` (valid or carrying an error message) must be `jv_free()`d by the caller. After the drain loop, if `jq_halted()`, free `jq_get_exit_code()`/`jq_get_error_message()` results too. Finally `jq_teardown(&jq)` frees everything owned by the state (bytecode, attrs, stacks); no separate free of bytecode or input is needed/possible since `jq_start`/`jq_compile_args` take ownership of what they are given.
**Evidence:**
> src/jq_test.c:316-319: `jv result = jq_next(jq); ... jv_free(result);` in every iteration.
> src/main.c:212-219, 220-232: `jv_free(exit_code)`, `jv_free(error_message)` after use.
> tests/jq_fuzz_execute.cpp:34-35: `jv actual = jq_next(jq); jv_free(actual);`
> src/execute.c:1131-1143 `jq_teardown`: frees `jq->bc` via `bytecode_free`, frees `jq->attrs`, frees the `jq_state` struct itself (stack memory is released inside `jq_reset`/`stack_reset`, called from `jq_teardown` via `jq_reset(old_jq)`).

### P2.6 API Existence
**Claim:** All entries listed (jq_next, jq_init, jq_compile_args, jq_compile, jq_start, jq_halted, jq_get_exit_code, jq_get_error_message, jq_teardown) exist in the current codebase, declared in src/jq.h and defined in src/execute.c.
**Evidence:**
> src/jq.h:20,26,27,29,30,31,34,35,36: declarations of jq_init, jq_compile, jq_compile_args, jq_start, jq_next, jq_teardown, jq_halted, jq_get_exit_code, jq_get_error_message.
> src/execute.c definitions at lines: jq_reset:303 (internal), jq_next:340, jq_init:1054, jq_start:1116, jq_teardown:1131, jq_compile_args:1232, jq_compile:1260, jq_halt:1325, jq_halted:1334, jq_get_exit_code:1340, jq_get_error_message:1345.
> Core helper functions named in the Logic Group also exist: frame_current (src/execute.c:77), frame_push/frame_pop (grep confirms callers in RET/CALL_JQ handling), make_closure (src/execute.c:107), path_append (used at src/execute.c:791,798), path_intact (referenced near src/execute.c:253-260 logic, named `path_intact`-style check), set_error (src/execute.c:332), stack_restore (src/execute.c:343, 806), stack_save (src/execute.c:820,825,991,1126).

### P2.7 Co-call Constraints
**Claim:** `jq_init`/`jq_teardown` are a mandatory create/destroy pair (exactly one teardown per init). `jq_compile`/`jq_compile_args` and `jq_start` are not strictly paired 1:1 — one compile can precede many `jq_start`+drain cycles, and compile can be called again on the same state to replace the program. `jq_start` and the `jq_next` drain loop are paired: every `jq_start` call should be followed by calling `jq_next` until it returns invalid before the next `jq_start` (jq_start calls `jq_reset` which discards any undrained backtrack state). `jq_halt` (invoked from within a running program, not called directly by the harness) pairs with `jq_halted`+`jq_get_exit_code`+`jq_get_error_message` read after the drain loop ends.
**Evidence:**
> src/execute.c:1131-1135: `jq_teardown` takes `jq_state**`, nulls it after freeing — designed to be called exactly once per `jq_init`.
> src/execute.c:1116-1118 `jq_start`: first line is `jq_reset(jq)`, which clears stacks/path/error/halted/exit_code/error_message (src/execute.c:303-324) — so calling `jq_start` again discards in-flight backtracking state from a prior unfinished `jq_next` loop.
> src/jq_test.c:464-515: demonstrates repeated `jq_start`+drain cycles on one compiled program without recompiling.
> src/execute.c:1325-1332 `jq_halt`: `assert(!jq->halted);` — can only be called once per run before being reset by the next `jq_start`/`jq_reset`.

### P2.8 Prerequisite State
**Claim:** `jq_next` requires: a non-NULL `jq_state` from `jq_init`; `jq->bc` (bytecode) set by a successful `jq_compile`/`jq_compile_args`; and `jq->initial_execution`/stack state set by `jq_start` (so that `stack_restore` yields a non-NULL `pc`). Optionally `jq_set_nomem_handler`, `jq_set_error_cb`, `jq_set_attr`/`jq_set_attrs` (e.g. `JQ_ORIGIN`, `JQ_LIBRARY_PATH` used by builtins like `require`/module loading) may be set before `jq_compile`.
**Evidence:**
> src/execute.c:343-344: unconditional `stack_restore`+`assert(pc)` — undefined behavior / assertion failure if `jq_start` was skipped.
> src/execute.c:1232-1257 `jq_compile_args`: on failure (`nerrors != 0` or bytecode compile failure) `jq->bc` stays NULL (or is freed, src/execute.c:1239-1242) and `jq_compile_args` returns `jq->bc != NULL` (0 on failure) — harness must check this return and not call `jq_start`/`jq_next` if it's 0 (the real `jq_start` would then push input onto `jq->bc->code` with `jq->bc == NULL`, a crash).
> tests/jq_fuzz_execute.cpp:27: `if (jq_compile(jq, prog_payload.c_str())) { ... }` — real-world harness gates `jq_start`/`jq_next` on compile success.
> src/jq_test.c:108: `jq_set_attr(jq, jv_string("JQ_LIBRARY_PATH"), lib_dirs);` set before compiling/running, used by module-loading builtins.

### P2.9 Result Usage
**Claim:** The `jv` returned by `jq_next` is consumed by: printing/dumping functions (`jv_dump`, `jv_dump_string`, `priv_fwrite` + `jv_string_value`/`jv_string_length_bytes` for raw-string output), equality checks (`jv_equal`) in tests, and diagnostic helpers (`jv_invalid_has_msg`, `jv_invalid_get_msg`) to detect uncaught errors; it is always eventually `jv_free()`d.
**Evidence:**
> src/main.c:180-201: branches on `jv_get_kind(result)`; for strings with RAW_OUTPUT calls `jv_string_value`/`jv_string_length_bytes`/`priv_fwrite`; otherwise `jv_dump(result, dumpopts)` (which itself frees `result`, per jv.c convention) or `jv_free(result)` after raw-write branch.
> src/main.c:233-235: `jv_invalid_has_msg(jv_copy(result))` then `jv_invalid_get_msg(jv_copy(result))` to extract an uncaught-exception message when not halted.
> src/jq_test.c:208: `jv_equal(jv_copy(expected), jv_copy(actual))` compares `jq_next`'s result to an expected parsed value.
> tests/jq_fuzz_execute.cpp:34-35: minimal consumption — just `jv_free(actual)`.

### P2.10 Behaviour Controls
**Claim:** (a) the compiled program text (and args to `jq_compile_args`) selects which opcodes/builtins run, including ones that call `jq_halt`; (b) `jq_start`'s `flags` parameter (`JQ_DEBUG_TRACE` / `JQ_DEBUG_TRACE_DETAIL` / `JQ_DEBUG_TRACE_ALL`, see src/jq.h:11-15) toggles `jq->debug_trace_enabled`, which makes `jq_next` print per-opcode stack traces (src/execute.c:360-391) — purely diagnostic, doesn't change results; (c) the JV_KIND of the input value controls which type-check branch (`EACH`/`EACH_OPT`, iteration, range) succeeds vs. raises `"Cannot iterate over ..."`-style errors (src/execute.c:771-779); (d) `jq_compile_args`'s `args` jv must be `JV_KIND_ARRAY` or `JV_KIND_OBJECT` (asserted) and is converted via `args2obj` (src/execute.c:1218-1230) to drive `$name` variable bindings seen by the compiled program.
**Evidence:**
> src/jq.h:11-15: `JQ_DEBUG_TRACE = 1, JQ_DEBUG_TRACE_DETAIL = 2, JQ_DEBUG_TRACE_ALL = ...`
> src/execute.c:1127: `jq->debug_trace_enabled = flags & JQ_DEBUG_TRACE_ALL;`
> src/execute.c:1234: `assert(jv_get_kind(args) == JV_KIND_ARRAY || jv_get_kind(args) == JV_KIND_OBJECT);` in `jq_compile_args` — passing any other kind aborts the process.
> src/execute.c:770-779: EACH/EACH_OPT dispatch depends on container kind; EACH on non-iterable sets `set_error(jq, ...)` with message `"Cannot iterate over %s (%s)"`.

### P2.11 Input Expectations
**Claim:** `jq_next`'s "input" is not a raw buffer but already-structured state: a compiled `struct bytecode*` (`jq->bc`, produced by `jq_compile`/`jq_compile_args` from a NUL-terminated C string program) and a `jv` value tree pushed by `jq_start` (built by the caller, typically via `jv_parse`/`jv_parse_sized` from untrusted JSON text, but any `jv` tree is accepted — no implicit validation beyond the normal `jv` invariants). There is no size limit enforced by `jq_next` itself; depth/complexity limits come from the stack machine's own stack (`struct stack`) which grows dynamically. `jq_next` asserts the VM is in a consistent, started state (`assert(pc)`, `assert(jv_get_kind(jq->error) == JV_KIND_NULL)` at entry, src/execute.c:344,350) — it is not designed to tolerate being called on a never-started or already-torn-down state.
**Evidence:**
> src/execute.c:343-350: `pc = stack_restore(jq); assert(pc); ... assert(jv_get_kind(jq->error) == JV_KIND_NULL);` — precondition checks at top of the function.
> src/execute.c:1116,1125: `void jq_start(jq_state *jq, jv input, int flags) { ... stack_push(jq, input); ... }` — `input` ownership transfers to the VM; caller must not use/free it afterward (confirmed by jq_test.c:473,485,... passing `jv_parse(...)` directly with no corresponding `jv_free`).
> tests/jq_fuzz_execute.cpp:29-37: shows input built from fuzzer-controlled bytes via `jv_parse`, with explicit `jv_is_valid(input)` check and manual `jv_free(input)` only on the invalid-parse branch (comment: "Only free if input is invalid as otherwise jq_teardown frees it").

## Other entries' call protocol (combine with jq_next in one harness)

### jq_init
**Claim:** No prerequisites (global jq library state is in the struct it allocates). Returns `jq_state*` or NULL on allocation failure (must be checked before any other call). Must eventually be paired with exactly one `jq_teardown`.
**Evidence:** src/execute.c:1054-1058: `jq = jv_mem_alloc_unguarded(sizeof(*jq)); if (jq == NULL) return NULL;`
tests/jq_fuzz_execute.cpp:22-24: `jq = jq_init(); if (jq != NULL) { ... }` — real harness checks for NULL.

### jq_compile / jq_compile_args
**Claim:** Called after `jq_init`, before `jq_start`. `jq_compile(jq, str)` is sugar for `jq_compile_args(jq, str, jv_object())` (src/execute.c:1260-1262). `str` must be a NUL-terminated C string (program text); for `jq_compile_args`, `args` must be `JV_KIND_ARRAY` or `JV_KIND_OBJECT` (asserted) and is consumed (ownership transferred, freed internally via `args2obj`/error paths, src/execute.c:1249,1251,1226-1228). Returns nonzero (`jq->bc != NULL`) on success, 0 on failure; on failure the harness must NOT call `jq_start`/`jq_next`. Can be called repeatedly on the same `jq_state` to recompile (old bytecode is freed first, src/execute.c:1239-1242) — this resets state via `jq_reset(jq)` (src/execute.c:1238), discarding any unfinished `jq_next` loop.
**Evidence:** src/execute.c:1232-1262; tests/jq_fuzz_execute.cpp:27: `if (jq_compile(jq, prog_payload.c_str())) { ... }`.

### jq_start
**Claim:** Called after a successful compile, before each drain loop of `jq_next`. Parameters: `jq` (non-NULL, compiled), `input` (a `jv`, ownership transferred to jq — caller must not free or reuse it), `flags` (bitmask of `JQ_DEBUG_TRACE`/`JQ_DEBUG_TRACE_DETAIL`, or 0). No return value. Internally resets all VM state (`jq_reset`) and pushes a fresh top-level frame plus `input` as the initial stack value.
**Evidence:** src/execute.c:1116-1129; src/jq_test.c:313,325 (`jq_start(jq, parsed_input, 0)`), tests/jq_fuzz_execute.cpp:31 (`jq_start(jq, input, 0)`).

### jq_halted / jq_get_exit_code / jq_get_error_message
**Claim:** Called AFTER the `jq_next` drain loop has returned `jv_invalid()` (i.e. after the program finished or stopped), never before. `jq_halted` returns nonzero if the running jq program invoked `halt`/`halt_error` (via `jq_halt`, which is called by builtins, not directly by the harness). If halted, `jq_get_exit_code` and `jq_get_error_message` return `jv`s (copies — caller must `jv_free` them) describing the requested exit status / message; both may be `jv_invalid()`/`JV_KIND_NULL` if not set. These are read-only query functions with no side effects and can be called in any order relative to each other, but are only meaningful once the `jq_next` loop has ended.
**Evidence:** src/execute.c:1325-1347 (`jq_halt`, `jq_halted`, `jq_get_exit_code` returns `jv_copy(jq->exit_code)`, `jq_get_error_message` returns `jv_copy(jq->error_message)`); src/main.c:179,210-232 (checked only after the `while (jv_is_valid(result = jq_next(jq)))` loop ends).

### jq_teardown
**Claim:** Called exactly once, last, regardless of success/failure anywhere earlier (even if `jq_init` partially succeeded but compile failed, or compile succeeded but loop wasn't drained). Takes `jq_state**`; safely handles `*jq == NULL` (no-op). Frees bytecode, attrs, internal stack memory, and the state struct itself; sets `*jq = NULL`.
**Evidence:** src/execute.c:1131-1143; tests/jq_fuzz_execute.cpp:45: `jq_teardown(&jq);` called unconditionally at the end regardless of which branch was taken above.

### Combination in one harness (as evidenced by tests/jq_fuzz_execute.cpp)
**Claim:** The project's own fuzz harness strings these entries together exactly as: `jq_init()` → check non-NULL → (`jq_set_attr` optional) → `jq_compile(jq, prog)` → check truthy → `jv_parse(json_text)` for the input → check `jv_is_valid` → `jq_start(jq, input, flags)` → `jq_next(jq)` (commonly in a loop until invalid) → `jv_free` each result → (optionally `jq_halted`/`jq_get_exit_code`/`jq_get_error_message` after loop) → `jq_teardown(&jq)`.
**Evidence:** tests/jq_fuzz_execute.cpp:16-48 (full source quoted above); src/jq_test.c:301-329, 464-515 (equivalent sequence used in the project's own regression tests, including multi-iteration reuse).

## Build notes
**Claim:** The existing `jq_fuzz_execute` fuzzer (the closest analog to a `jq_next`-focused harness) is compiled as C++ (`.cpp`), linking against the static library `libjq.a` and `libonig.a`, with `-I./src` giving direct access to `jq.h`/`jv.h` (no separate installed-header path is used — headers are consumed straight from the source tree).
**Evidence:**
> ossfuzz build.sh:55-58:
> ```
> $CXX $CXXFLAGS $LIB_FUZZING_ENGINE ./tests/jq_fuzz_execute.cpp \
>     -I./src -I./vendor/oniguruma/src \
>     ./.libs/libjq.a ./vendor/oniguruma/src/.libs/libonig.a\
>     -o $OUT/jq_fuzz_execute -I./src
> ```
> ossfuzz build.sh:21-23: project is built with `autoreconf -fi`, `./configure --with-oniguruma=builtin`, `make -j$(nproc)` before any fuzzer is compiled, producing `./.libs/libjq.a`.
> tests/jq_fuzz_execute.cpp:4-6: `#include "jq.h"`, `#include "jv.h"`, `#include "oniguruma.h"` — resolved via the `-I./src -I./vendor/oniguruma/src` include paths.