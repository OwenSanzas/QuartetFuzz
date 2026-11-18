## target: plist_from_openstep

### P2.1 Init Sequence
**Claim:** No library-global init call is required. Only a byte buffer and length must exist before calling `plist_from_openstep`; the function itself allocates and returns the output `plist_t` through an out-parameter.
**Evidence:**
> include/plist/plist.h:1094-1101: doxygen comment for `plist_from_openstep` lists only `openstep`, `length`, `plist` — no mention of any prior init call.
> src/oplist.c:954-964: `plist_from_openstep` validates `plist` (non-NULL, sets `*plist=NULL` up front) then validates `plist_ostep`/`length`, builds a local `struct _parse_ctx ctx = { plist_ostep, plist_ostep, plist_ostep+length, 0, 0 };` — no external/global state is touched.
> test/plist_otest.c:68-79: caller only `malloc`s a buffer, `fread`s file content into it, NUL-terminates it, then calls `plist_from_openstep(plist_ostep, size_in, &root_node1);` directly — no prior library init.

### P2.2 Parameter Construction
**Claim:** `openstep` is a caller-owned `const char*` buffer of at least `length` bytes (NUL-termination not required by the parser itself, since it uses `ctx->end = start+length` bounds, though the regression test NUL-terminates for convenience); `length` is a `uint32_t` byte count that must be `> 0`; `plist` must be a non-NULL `plist_t*` out-parameter (its pointee is unconditionally set, to NULL on entry and then either to NULL on error or to the parsed tree).
**Evidence:**
> src/oplist.c:956-962:
> ```
> if (!plist) { return PLIST_ERR_INVALID_ARG; }
> *plist = NULL;
> if (!plist_ostep || (length == 0)) { return PLIST_ERR_INVALID_ARG; }
> ```
> src/oplist.c:964: `struct _parse_ctx ctx = { plist_ostep, plist_ostep, plist_ostep + length, 0 , 0 };` — bounds are `[plist_ostep, plist_ostep+length)`, so parsing never reads past `length` bytes regardless of NUL termination.
> test/plist_otest.c:68-76: allocates `size_in+1` bytes and writes a trailing `\0`, but passes `size_in` (not `size_in+1`) as `length`, confirming the NUL byte is not required by the API contract, just test hygiene.

### P2.3 Object Lifecycle
**Claim:** Create (parse) → use (read/traverse/convert the resulting `plist_t` tree) → destroy with `plist_free()`. No separate "configure" step for the target itself; parsing is one-shot.
**Evidence:**
> src/oplist.c:954-988: single call does validation, parse (`node_from_openstep`), and (conditionally) the "strings data" fallback reparse (`parse_dict_data`), returning a fully built tree or leaving `*plist == NULL` on failure.
> test/plist_otest.c:79-84, 119: `plist_from_openstep(...)` then later `plist_free(root_node1);` at the end of `main` — classic create-then-free lifecycle, no intermediate configuration call.

### P2.4 Return Value Handling
**Claim:** Return type is `plist_err_t` (0 = `PLIST_ERR_SUCCESS`, negative values `PLIST_ERR_INVALID_ARG(-1)`, `PLIST_ERR_FORMAT(-2)`, `PLIST_ERR_PARSE(-3)`, `PLIST_ERR_NO_MEM(-4)`, `PLIST_ERR_IO(-5)`, `PLIST_ERR_CIRCULAR_REF(-6)`, `PLIST_ERR_MAX_NESTING(-7)`, `PLIST_ERR_UNKNOWN(-255)`). Callers should check both the return code and that `*plist != NULL` before using the tree; on any parse error `*plist` is guaranteed NULL.
**Evidence:**
> include/plist/plist.h:141-149: enum `plist_err_t` values as above.
> src/oplist.c:944-951 (`node_from_openstep` err_out path): `if (ctx->err != PLIST_ERR_SUCCESS) { plist_free(subnode); plist_free(*plist); *plist = NULL; return ctx->err; }` — error path frees partial nodes and nulls the out-param.
> src/oplist.c:966-988: top-level `plist_from_openstep` propagates `err` from `node_from_openstep`, and on the "strings data" fallback failure sets `err = PLIST_ERR_PARSE` after freeing the partial `pl`.
> test/plist_otest.c:79-84: real caller checks only `!root_node1` (i.e., relies on `*plist == NULL` on failure) rather than the integer return code — demonstrates the NULL-check pattern is sufficient and commonly used, though checking the return code is also valid/more precise.

### P2.5 Cleanup Sequence
**Claim:** On success, the returned `plist_t` must eventually be released with `plist_free()`. On failure, the function itself has already freed any partial tree and set `*plist = NULL`, so the caller has nothing to free from a failed call.
**Evidence:**
> include/plist/plist.h:302: `PLIST_API void plist_free(plist_t plist);` is the sole destructor for any `plist_t` tree (used for trees from any `plist_from_*`/`plist_new_*` source).
> src/oplist.c:944-951, 973-984: internal error paths call `plist_free(subnode)`, `plist_free(*plist)`, `plist_free(pl)` before returning an error, leaving no dangling allocation for the caller to clean up.
> test/plist_otest.c:119-123: `plist_free(root_node1); plist_free(root_node2); free(plist_bin); free(plist_ostep); free(plist_ostep2);` — `plist_t` trees freed with `plist_free`, raw malloc'd byte buffers (including those returned by `plist_to_openstep`/`plist_to_bin`) freed with plain `free`/`plist_mem_free`.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase.
**Evidence:**
> src/oplist.c:954 `plist_err_t plist_from_openstep(...)` — defined.
> src/plist.c:225 `plist_err_t plist_from_memory(...)` — defined.
> src/oplist.c:492 `plist_err_t plist_to_openstep(...)` — defined.
> src/oplist.c:498 `plist_err_t plist_to_openstep_with_options(...)` — defined.
> src/bplist.c:1360 `plist_err_t plist_to_bin(...)` — defined.
> src/bplist.c:905 `plist_err_t plist_from_bin(...)` — defined.
> include/plist/plist.h:302 `plist_free`, 1488 `plist_mem_free` — declared/exist.

### P2.7 Co-call Constraints
**Claim:** `plist_from_openstep` has no lock/unlock-style paired API; its sole mandatory pairing is "parse result → `plist_free()`". `plist_to_openstep`/`plist_to_openstep_with_options` similarly pair their output buffer with `plist_mem_free()` (or plain `free()`, since the allocator is the C library allocator per test usage).
**Evidence:**
> include/plist/plist.h:973,985,998,1021,1034,1058 doxygen notes: "`@note Use plist_mem_free() to free the allocated memory.`" attached to every `plist_to_*` exporter including `plist_to_openstep`/`plist_to_openstep_with_options`.
> test/plist_otest.c:119-123: pairs each `plist_from_*` tree with `plist_free`, and each allocated byte buffer (`plist_bin`, `plist_ostep`, `plist_ostep2`) with `free()`.

### P2.8 Prerequisite State
**Claim:** No connection/context/global config object is required anywhere in the openstep import/export path; the only prerequisite state is the in-memory input buffer (for the target) or an already-built `plist_t` tree (for `plist_to_openstep*`).
**Evidence:**
> src/oplist.c:954-964: `plist_from_openstep` builds its entire working state (`struct _parse_ctx ctx`) locally on the stack from the function arguments only.
> src/oplist.c:498-539: `plist_to_openstep_with_options` takes a `plist_t plist` (already-constructed tree) plus output-buffer out-params; no external context.

### P2.9 Result Usage
**Claim:** The resulting `plist_t` from `plist_from_openstep` is consumed by (a) further plist API traversal/mutation functions (not shown exhaustively here but through the generic `plist_t` accessor API), and concretely in the project's own regression test by `plist_to_bin()` (serialize to binary), whose output is then round-tripped through `plist_from_bin()`, and the resulting tree is serialized back with `plist_to_openstep()`. `plist_from_memory` internally calls `plist_from_openstep` itself as one of its format-dispatch branches, so its result has the exact same downstream usage as a direct `plist_from_openstep` call.
**Evidence:**
> test/plist_otest.c:79-103:
> ```
> plist_from_openstep(plist_ostep, size_in, &root_node1);
> ...
> plist_to_bin(root_node1, &plist_bin, &size_out);
> ...
> plist_from_bin(plist_bin, size_out, &root_node2);
> ...
> plist_to_openstep(root_node2, &plist_ostep2, &size_out2, 0);
> ```
> src/plist.c:296-297: `res = plist_from_openstep(plist_data, length, plist); fmt = PLIST_FORMAT_OSTEP;` inside `plist_from_memory` — the tree produced is returned to `plist_from_memory`'s own caller through the same `plist_t*` out-param, so any caller of `plist_from_memory` is also a "result user" of `plist_from_openstep`.

### P2.10 Behaviour Controls
**Claim:** `plist_from_openstep` itself has no flags (its only behavior switch is the content of the buffer — e.g., a leading `=` after the first value triggers the whole-buffer "strings data" fallback reparse via `parse_dict_data`). `plist_from_memory` selects among XML/JSON/binary/OpenStep parsers by sniffing the buffer header (no explicit flag). `plist_to_openstep`'s `prettify` int selects pretty vs compact text; `plist_to_openstep_with_options`'s `options` bitmask (`PLIST_OPT_COMPACT`, `PLIST_OPT_COERCE`) selects compact output and/or type-coercion for otherwise-unsupported node types.
**Evidence:**
> src/oplist.c:966-985 ("strings data" fallback):
> ```
> } else if (ctx.pos < ctx.end && *ctx.pos == '=') {
>     /* attempt to parse this as 'strings' data */
>     plist_free(*plist); *plist = NULL;
>     plist_t pl = plist_new_dict();
>     ctx.pos = plist_ostep;
>     parse_dict_data(&ctx, pl);
>     ...
> }
> ```
> src/plist.c:237-298: `plist_is_binary()` check, then `<`/`[`/`(`/`{` sniffing with a `:`-after-quoted-key heuristic to disambiguate JSON-vs-OpenStep `{`, dispatching to `plist_from_bin`/`plist_from_xml`/`plist_from_json`/`plist_from_openstep` accordingly.
> src/oplist.c:492-495: `plist_err_t plist_to_openstep(plist_t plist, char **openstep, uint32_t* length, int prettify) { plist_write_options_t opts = prettify ? PLIST_OPT_NONE : PLIST_OPT_COMPACT; return plist_to_openstep_with_options(...); }`.
> include/plist/plist.h:174,178,1041-1050: `PLIST_OPT_COMPACT` ("Only valid for #PLIST_FORMAT_JSON and #PLIST_FORMAT_OSTEP"), `PLIST_OPT_COERCE` doc for openstep: boolean→0/1, date→ISO8601 string, NULL→"NULL" string, UID→integer, else `PLIST_ERR_FORMAT` without coerce.

### P2.11 Input Expectations
**Claim:** Input is raw OpenStep/NeXT plist text, not necessarily NUL-terminated (parser is bounds-driven by `length`), may be whitespace-only (treated as empty dict), must be `length > 0` and non-NULL. Recursion depth is capped at `PLIST_MAX_NESTING_DEPTH` (checked both in the parser `node_from_openstep` and the writer `node_to_openstep`). Unquoted strings are restricted to a fixed allow-list of characters (`allowed_unquoted_chars[256]`); quoted strings support `\`-escapes including octal (`\0`-`\7` digits) and `\U`-Unicode (1-4 hex digits) decoded to UTF-8; `<...>` data blocks are hex-digit pairs.
**Evidence:**
> src/oplist.c:960-964: `if (!plist_ostep || (length == 0)) { return PLIST_ERR_INVALID_ARG; }` then `ctx.end = plist_ostep + length` — strictly length-bounded, no implicit NUL requirement.
> src/oplist.c:968-970: `if (!*plist) { /* whitespace only file is considered an empty dictionary */ *plist = plist_new_dict(); }`.
> src/oplist.c:667-670 (inside `node_from_openstep`): `if (ctx->depth > PLIST_MAX_NESTING_DEPTH) { ... ctx->err = PLIST_ERR_MAX_NESTING; return ctx->err; }`.
> src/oplist.c:360-363 (inside `node_to_openstep`, mirrored limit on the writer side): `if (depth > PLIST_MAX_NESTING_DEPTH) { ... return PLIST_ERR_MAX_NESTING; }`.
> src/oplist.c:95-98 `static const char allowed_unquoted_chars[256] = { ... }` — allow-list table consulted at line 920 (`if (!allowed_unquoted_chars[(uint8_t)*ctx->pos]) break;`) when scanning unquoted strings.

## Other entries of this feature (besides the target)

### plist_from_memory
**Call protocol:** No init needed. Params: `plist_data` (const char*, non-NULL), `length` (uint32_t, >0), `plist` (plist_t* out, non-NULL), `format` (plist_format_t* out, optional/may be NULL). Internally auto-detects format (binary magic `bplist00`, then `<`/XML, `[`/JSON, `(`/OpenStep, or `{` disambiguated by a `"key":` heuristic for JSON vs OpenStep) and dispatches to `plist_from_bin`/`plist_from_xml`/`plist_from_json`/`plist_from_openstep`, filling `*format` with the chosen `plist_format_t` on success. Returns `plist_err_t` (same enum as target). On failure `*plist` is NULL (set at entry, line src/plist.c:231). Combine with target: calling `plist_from_memory` on OpenStep-looking bytes is an alternate path into the exact same `node_from_openstep`/`parse_dict_data` core as calling `plist_from_openstep` directly (src/plist.c:296-297); result must be freed the same way with `plist_free()`.
**Evidence:** src/plist.c:225-304 (full function, quoted above in P2.9/P2.10).

### plist_to_openstep
**Call protocol:** Prerequisite: an already-built, non-NULL `plist_t plist` (e.g., from `plist_from_openstep`/`plist_from_memory`/`plist_new_dict` etc.). Params: `plist` (in), `plist_openstep` (char** out, library-allocated), `length` (uint32_t* out), `prettify` (int: nonzero = pretty-printed, 0 = compact). Returns `plist_err_t`; `0`/`PLIST_ERR_SUCCESS` on success. On error (e.g. `PLIST_ERR_INVALID_ARG` if `plist`/`plist_openstep`/`length` NULL, or `PLIST_ERR_FORMAT`/`PLIST_ERR_MAX_NESTING` propagated from the writer core) `*openstep`/`*length` are set to NULL/0 (see src/oplist.c:522-526). Caller must free `*plist_openstep` with `plist_mem_free()` (doc'd) or `free()` (as used in test/plist_otest.c:122-123). Does not free or alter the input `plist_t`. Thin wrapper over `plist_to_openstep_with_options`.
**Evidence:** src/oplist.c:492-539; include/plist/plist.h:1023-1036.

### plist_to_openstep_with_options
**Call protocol:** Same prerequisite/params as `plist_to_openstep` but takes a `plist_write_options_t options` bitmask instead of a plain int: `PLIST_OPT_COMPACT` (non-prettified output) and `PLIST_OPT_COERCE` (coerce boolean/date/NULL/UID types that have no native OpenStep text form, otherwise those types cause `PLIST_ERR_FORMAT`). Validates all three pointer params non-NULL (src/oplist.c:503-505). Internally: `node_estimate_size()` → allocate `strbuf_t` via `str_buf_new(size)` → `node_to_openstep()` (the paired "core" writer, which itself enforces `PLIST_MAX_NESTING_DEPTH`) → NUL-terminate and hand ownership of the buffer to caller. Returns `plist_err_t`; same cleanup contract as `plist_to_openstep`.
**Evidence:** src/oplist.c:498-539; include/plist/plist.h:1038-1060.

### plist_to_bin
**Call protocol:** Prerequisite: an already-built `plist_t plist`. Params: `plist` (in), `plist_bin` (char** out, library-allocated), `length` (uint32_t* out). Returns `plist_err_t`. Serializes the generic node tree (dict/array/data/string/etc., same node types `plist_from_openstep` can produce) to Apple binary plist format; used in the project's own regression test immediately after `plist_from_openstep` to further exercise the parsed tree (test/plist_otest.c:87-92). Caller frees `*plist_bin` with `plist_mem_free()`/`free()` (test/plist_otest.c:121).
**Evidence:** src/bplist.c:1360 (signature); test/plist_otest.c:79-92; include/plist/plist.h:977-987.

### plist_from_bin
**Call protocol:** No init needed. Params: `plist_bin` (const char*, non-NULL), `length` (uint32_t, binary-plist-formatted bytes, typically beginning with `bplist00` magic — see `plist_is_binary()` at src/bplist.c around line 217, used by `plist_from_memory`), `plist` (plist_t* out). Returns `plist_err_t`; `*plist` NULL on failure (checked at src/bplist.c:917-920). In the project's own test it consumes the output of `plist_to_bin(plist_from_openstep(...))`, i.e. chained after the target via a bin round-trip, then its result is fed to `plist_to_openstep()` (test/plist_otest.c:95-103). Caller frees result with `plist_free()`.
**Evidence:** src/bplist.c:905-920; test/plist_otest.c:87-103,119-120.

**How these combine with the target in one harness (as evidenced by the project's own regression driver, test/plist_otest.c):**
`plist_from_openstep(buf, len, &root1)` → (if `root1` non-NULL) `plist_to_bin(root1, &bin, &binlen)` → `plist_from_bin(bin, binlen, &root2)` → `plist_to_openstep(root2, &ostep2, &ostep2len, prettify)` → free in reverse: `plist_free(root1); plist_free(root2); plist_mem_free/free(bin); free(ostep2);` (original input buffer freed separately by the harness). `plist_from_memory` is an alternative first-stage entry that reaches the exact same core as the target when its format sniffing decides the buffer is OpenStep (src/plist.c:296); its result follows the same downstream chain.
**Evidence:** test/plist_otest.c:79-123 (full chain, quoted above).

## Build notes
**Claim:** Fuzzers for this project are built as C++ (`-std=c++11`) translation units that `#include <plist/plist.h>` via `-Iinclude/`, linked statically against the whole built library archive `src/.libs/libplist-2.0.a` plus the OSS-Fuzz libFuzzer engine; `plist_from_openstep` is a `PLIST_API`-exported C symbol (declared `extern "C"` under C++ via the header's `#ifdef __cplusplus` wrapping, standard libplist pattern) reachable with no special defines. The repo's own `oplist_fuzzer.cc` currently present is a disabled/stub placeholder, and `bplist_fuzzer.cc` shows the idiomatic minimal pattern (`plist_from_*(data,size,&root); ...; plist_free(root);`).
**Evidence:**
> ${QF_WORK}/ossfuzz_view/libplist-c3/build.sh:21-32:
> ```
> ./autogen.sh --without-cython --enable-debug
> make -j$(nproc) clean
> make -j$(nproc) all
> for fuzzer in bplist_fuzzer xplist_fuzzer jplist_fuzzer oplist_fuzzer; do
>   $CXX $CXXFLAGS -std=c++11 -Iinclude/ \
>       fuzz/$fuzzer.cc -o $OUT/$fuzzer \
>       $LIB_FUZZING_ENGINE src/.libs/libplist-2.0.a
> done
> ```
> fuzz/bplist_fuzzer.cc:22-32: `#include <plist/plist.h>` ... `extern "C" int LLVMFuzzerTestOneInput(...)` with `plist_from_bin(...)` + `plist_free(root_node)` — the idiomatic minimal pattern to model for `oplist_fuzzer.cc`.
> fuzz/oplist_fuzzer.cc:1-16: current file in the source tree is a disabled stub ("Gold harness replaced by an empty stub for baseline generation") that only keeps `plist_from_openstep` linked via an `__asm__` alias trick and does not actually call it — a real harness still needs to be written by the downstream agent.
> build.sh:37: `zip -j $OUT/oplist_fuzzer_seed_corpus.zip test/data/*.ostep` confirms OpenStep-format seed corpus files exist under `test/data/*.ostep` for this fuzzer.