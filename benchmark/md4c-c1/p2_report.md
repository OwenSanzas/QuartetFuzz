## target: md_html

### Overview
`md_html()` (src/md4c-html.h:59, implemented src/md4c-html.c:628-669) is a thin
convenience wrapper: it builds an `MD_PARSER` vtable wired to the internal HTML
renderer callbacks, optionally strips a UTF-8 BOM, and forwards to the lower-level
`md_parse()` (src/md4c.h:488, implemented src/md4c.c:7294-7336), which is the
actual core driver. Both are public API entries declared in installed headers
(`md4c-html.h`, `md4c.h`). `md_parse()` is never called directly anywhere else in
the project tree (only from inside `md_html()`), so for an HTML-oriented harness
`md_html` is the natural primary target and `md_parse` is reached transitively
through it, or can be exercised directly with a caller-supplied `MD_PARSER`.

### P2.1 Init Sequence
**Claim:** No separate library/global init call is required before `md_html()`.
There is no `md_*_init`/`md_*_create` call anywhere in the codebase; the function
is stateless and self-contained — it builds its own local `MD_HTML` render struct
and local `MD_PARSER` struct on the stack for every call.
**Evidence:**
> src/md4c-html.c:633-646: `MD_HTML render = { process_output, userdata, renderer_flags, 0, {0} }; ... MD_PARSER parser = {0, parser_flags, enter_block_callback, ...};` — both built fresh, no prior init call.
> md2html/md2html.c:178-179: `ret = md_html(buf_in.data, (MD_SIZE)buf_in.size, process_output, (void*)&buf_out, p_flags, r_flags);` is the very first md4c call in that function (after only reading the file into a buffer).
> test/embedded/smoke.cpp:28-29: `md_html(src, (MD_SIZE)strlen(src), process_output, NULL, MD_DIALECT_COMMONMARK, 0);` called directly with no preceding md4c call.

### P2.2 Parameter Construction
**Claim:** Six parameters: `input` (pointer to markdown bytes, need not be
NUL-terminated), `input_size` (unsigned `MD_SIZE`, must equal the exact number of
valid bytes at `input`), `process_output` (non-NULL function pointer, called
possibly many times with non-zero-terminated chunks), `userdata` (opaque pointer
passed through, may be NULL if the callback does not dereference it), `parser_flags`
(bitmask of `MD_FLAG_xxxx` / `MD_DIALECT_xxxx` from md4c.h), `renderer_flags`
(bitmask of `MD_HTML_FLAG_xxxx` from md4c-html.h).
**Evidence:**
> src/md4c-html.h:59-61: signature with all 6 params.
> src/md4c-html.h:48-54: "Params input and input_size specify the Markdown input. Callback process_output() gets called with chunks of HTML output... Param parser_flags are flags from md4c.h propagated to md_parse(). Param render_flags is bitmask of MD_HTML_FLAG_xxxx."
> src/md4c.h:444-446: "Note any strings provided to the callbacks as their arguments ... are generally not zero-terminated. Application has to take the respective size information into account." — same contract applies to `process_output`'s `text`/`size`.
> src/md4c-html.c:660-666: `input_size >= sizeof(bom)` guards a `memcmp` on `input`, so `input`/`input_size` must be a consistent, readable pair (no implicit NUL-termination assumed).
> md2html/md2html.c:137-148,178: input buffer allocated with `membuf_init`/`membuf_grow`, filled via `fread`, and its `size` (byte count actually read) passed as `input_size` — i.e. size must match actually-valid bytes, not a buffer capacity.

### P2.3 Object Lifecycle
**Claim:** `md_html()` has no persistent object/handle lifecycle for the caller:
it is a single call that internally creates, uses, and discards its own `MD_HTML`
render context and `MD_PARSER` vtable on the stack; nothing is returned to be
freed by the caller except whatever the `process_output` callback itself
allocates (e.g., an output buffer that *the caller* owns).
**Evidence:**
> src/md4c-html.c:633: `MD_HTML render = {...};` — local stack struct, not heap-allocated, not returned.
> src/md4c-html.c:668: `return md_parse(input, input_size, &parser, (void*)&render);` — single call in, single int out; no handle/object persists after return.
> md2html/md2html.c:130-131,178-204 (see below): the only lifecycle object is the caller's own `struct membuffer buf_out`, created before the call and freed after (`membuf_fini`), entirely outside md4c's control.

### P2.4 Return Value Handling
**Claim:** Return is `int`: `0` = success; non-zero = failure/abort. `-1`
specifically means `md_parse()` itself failed (e.g., allocation failure or
unsupported ABI version); any other non-zero value is the value returned by one
of the caller's own block/span/text callbacks when it aborts parsing early (not
relevant to `md_html`, since its own internal callbacks always return 0). Callers
must check the return value before using the callback-accumulated output.
**Evidence:**
> src/md4c-html.h:55-57: "Returns -1 on error (if md_parse() fails.) Returns 0 on success."
> src/md4c.h:484-486: "Zero is returned on success. If a runtime error occurs (e.g. a memory fails), -1 is returned. If the processing is aborted due any callback returning non-zero, the return value of the callback is returned."
> src/md4c.c:7301-7305: `if(parser->abi_version != 0) { ... return -1; }` — one concrete failure path inside `md_parse`.
> md2html/md2html.c:182-185: `if(ret != 0) { fprintf(stderr, "Parsing failed.\n"); goto out; }` — real caller checks return value and skips using `buf_out` on failure.

### P2.5 Cleanup Sequence
**Claim:** `md_html()`/`md_parse()` free all of their own internal allocations
before returning (success or failure); the caller only needs to free what *it*
allocated for input and output buffers.
**Evidence:**
> src/md4c.c:7330-7335: `ret = md_process_doc(&ctx); /* Clean-up. */ md_free_ref_defs(&ctx); md_free_footnote_defs(&ctx); free(ctx.buffer); ...` executed unconditionally via the `abort:` fallthrough path, i.e. internal buffers are freed regardless of success/failure.
> md2html/md2html.c:81-85: `membuf_fini()` (`free(buf->data)`) called by the caller on its own `buf_in`/`buf_out` membuffers after the `md_html()` call completes (success or `goto out` on failure) — this is caller-owned, not something md_html returns a handle for.

### P2.6 API Existence
**Claim:** All functions/structs referenced below exist in the current source
tree exactly as named.
**Evidence:**
> src/md4c-html.h:59: `int md_html(...)` — declared.
> src/md4c-html.c:628: `md_html(...)` — defined.
> src/md4c.h:488: `int md_parse(const MD_CHAR* text, MD_SIZE size, const MD_PARSER* parser, void* userdata);` — declared.
> src/md4c.c:7295: `md_parse(...)` — defined.
> src/md4c.h:427-471: `typedef struct MD_PARSER {...} MD_PARSER;` — defined with fields `abi_version, flags, enter_block, leave_block, enter_span, leave_span, text, debug_log, syntax`.
> src/md4c-html.c:636-646: local `MD_PARSER parser = {...}` literal in md_html populates exactly those fields.

### P2.7 Co-call Constraints
**Claim:** No lock/unlock or begin/commit pairing exists for `md_html`/`md_parse`;
each call is fully self-contained (open=call, close=return). The only "pairing"
visible in the codebase is caller-side: allocate input buffer before the call,
free/consume output buffer after.
**Evidence:**
> src/md4c-html.c:628-669: entire function body — one entry, one exit, no persistent state exposed to the caller between calls.
> md2html/md2html.c:137,178,230s (membuf_fini at line 81-85 called near the end of `process_file`) — caller's own buffer lifecycle, independent of md4c's API contract.

### P2.8 Prerequisite State
**Claim:** No external context, global config, or connection must exist before
calling `md_html`. The only "state" is what the caller must construct itself:
a readable input buffer of `input_size` bytes and a `process_output` callback
(and whatever state that callback's `userdata` points to, e.g. an output
membuffer) that is valid for the whole duration of the call, since `process_output`
may be invoked many times synchronously inside `md_html`.
**Evidence:**
> src/md4c-html.h:49-52: "Callback process_output() gets called with chunks of HTML output ... Param userdata is just propagated back to process_output() callback."
> md2html/md2html.c:120-124: `process_output` implementation that appends to `userdata` cast to `struct membuffer*` — that membuffer must be initialized (`membuf_init`) before the `md_html` call (line 152) and remains valid until after the call for subsequent writes.

### P2.9 Result Usage
**Claim:** `md_html`'s "result" is not a returned object but (a) the `int` status
and (b) the side effect of however many `process_output(text, size, userdata)`
calls happened during parsing. Real callers accumulate those chunks into a
caller-owned buffer and then write/print it after checking the int return value.
**Evidence:**
> md2html/md2html.c:120-124: `static void process_output(const MD_CHAR* text, MD_SIZE size, void* userdata) { membuf_append((struct membuffer*) userdata, text, size); }` — accumulates every chunk.
> md2html/md2html.c:178-185: call, then `if(ret != 0) { ...; goto out; }` gate before using `buf_out`.
> md2html/md2html.c (want_fullhtml branch, ~188 onward): once `ret == 0`, `buf_out.data`/`buf_out.size` (filled purely via the callback) are written out as the final HTML document — this is the only consumer of the "result".
> test/embedded/smoke.cpp:18-23,28-35: minimal no-op `process_output`; result (`ret`) checked only to keep the call from being optimized away — demonstrates the int return is the primary externally-checkable result when output isn't needed.

### P2.10 Behaviour Controls
**Claim:** Two independent flag bitmasks select processing paths: `parser_flags`
(core dialect/extension switches consumed by `md_parse`/`md_process_doc` and all
the block/inline analysis functions) and `renderer_flags` (HTML-escaping/entity
and self-closing-tag behaviour consumed only inside md4c-html.c's render_* and
callback functions). `MD_PARSER.abi_version` must be 0 or `md_parse` immediately
fails.
**Evidence:**
> src/md4c.h:422-423: `#define MD_DIALECT_COMMONMARK 0` / `#define MD_DIALECT_GITHUB (MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_ADMONITIONS | MD_FLAG_FOOTNOTES)` — composite presets for `parser_flags`.
> src/md4c-html.h:37-40: `MD_HTML_FLAG_DEBUG 0x0001`, `MD_HTML_FLAG_VERBATIM_ENTITIES 0x0002`, `MD_HTML_FLAG_SKIP_UTF8_BOM 0x0004`, `MD_HTML_FLAG_XHTML 0x0008`.
> src/md4c-html.c:659-666: `if(renderer_flags & MD_HTML_FLAG_SKIP_UTF8_BOM && sizeof(MD_CHAR)==1) {... strips BOM from input before parsing ...}` — directly alters what bytes reach `md_parse`.
> src/md4c-html.c:598-601,624-625: `r->flags & MD_HTML_FLAG_XHTML` selects `<br />\n` vs `<br>\n`; `r->flags & MD_HTML_FLAG_DEBUG` gates `fprintf(stderr, ...)` in `debug_log_callback`.
> src/md4c.c:7301-7305: `if(parser->abi_version != 0) { ...; return -1; }` — abi_version is a hard gate, must stay 0 (md_html always sets it to 0, src/md4c-html.c:637).
> src/md4c.c:7313: `ctx.code_indent_offset = (ctx.parser.flags & MD_FLAG_NOINDENTEDCODEBLOCKS) ? (OFF)(-1) : 4;` — one example of a `parser_flags` bit changing the core analysis path.

### P2.11 Input Expectations
**Claim:** Input is an arbitrary byte buffer (`MD_CHAR` = `char` unless
`MD4C_USE_UTF16`) of exactly `input_size` bytes; it need not be NUL-terminated,
there is no mandatory header/magic, but it is expected to be (ideally) UTF-8 text;
malformed UTF-8 is tolerated by the renderer (replacement-char fallback) rather
than rejected; an optional 3-byte UTF-8 BOM can be stripped via a renderer flag;
there's no documented maximum size other than `MD_SIZE` (`unsigned`, src/md4c.h:47)
range (practically 4GB on 32-bit unsigned); trailing-newline state is detected and
affects output but is not required.
**Evidence:**
> src/md4c.h:44: `typedef char MD_CHAR;` (default); `MD_CHAR` is `WCHAR` only under `MD4C_USE_UTF16` (lines 33-42).
> src/md4c.h:47: `typedef unsigned MD_SIZE;` — size type, no further documented range check before use.
> src/md4c-html.c:660-666: BOM-skip logic only applies `if(sizeof(MD_CHAR)==1)`, confirming the input is treated as a raw byte stream.
> src/md4c-html.c:182-183 (`utf8_replacement_char[] = {0xef,0xbf,0xbd}` in `render_utf8_codepoint`) — used when a codepoint can't be encoded cleanly, evidence that malformed/edge-case code points are substituted rather than causing a hard failure.
> src/md4c.c:7315: `ctx.doc_ends_with_newline = (size > 0 && ISNEWLINE_(text[size-1]));` — reads `text[size-1]` directly, so `input`/`input_size` must be a consistent valid pair (no assumption of extra NUL byte needed, but `size` must not lie about the buffer bounds).

### Build notes
**Claim:** The project's OSS-Fuzz driver is compiled in C (not C++) directly
against the library headers in `src/`, statically linked against the two
prebuilt static libraries `libmd4c.a` and `libmd4c-html.a`; the fuzz entry point
itself is C++-aware because it's finally linked with `$CXX`/`$LIB_FUZZING_ENGINE`.
**Evidence:**
> ossfuzz_view/md4c-c1/build.sh:21-26:
> ```
> mkdir build && cd build
> cmake ../ -DBUILD_SHARED_LIBS=OFF
> make
> $CC $CFLAGS -c ../test/fuzzers/fuzz-mdhtml.c -I../src
> $CXX $CXXFLAGS $LIB_FUZZING_ENGINE fuzz-mdhtml.o -o $OUT/fuzz-mdhtml \
>     ./src/libmd4c-html.a ./src/libmd4c.a
> ```
> i.e. harness source is compiled with `$CC` (C) and `-I../src` (so `#include "md4c-html.h"` / `#include "md4c.h"` resolve directly from `src/`), then the object is linked with `$CXX` + libFuzzer against the static archives built by CMake.
> ossfuzz_view/md4c-c1/Dockerfile:17-19,29-30: base image `gcr.io/oss-fuzz-base/base-builder:ubuntu-24-04`, clones `md4c` into `$SRC/md4c`, sets `WORKDIR $SRC/md4c`, copies `build.sh` — confirms build.sh runs from the repository root (`src/` and `test/fuzzers/` are siblings there).
> src/CMakeLists.txt:10,34,44: `add_library(md4c md4c.c md4c.h)`; `add_library(md4c-html md4c-html.c md4c-html.h entity.c entity.h)`; `target_link_libraries(md4c-html PUBLIC md4c)` — confirms `md4c-html` depends on `md4c`, matching the link order `libmd4c-html.a ./src/libmd4c.a` in build.sh.
> test/fuzzers/fuzz-mdhtml.c:1-16: current in-tree fuzzer is a stub ("Gold harness replaced by an empty stub for baseline generation") that only keeps `md_html` linked via `__asm__("md_html")` and does not actually call it — it is NOT a valid behavioural reference for how to call `md_html`; use md2html/md2html.c:178-179 and test/embedded/smoke.cpp:28-29 instead as the real calling-convention evidence.

## Other Logic-Group entries: md_parse (second entry)

### Call protocol of md_parse (used directly, bypassing md_html's HTML layer)
**Claim:** `md_parse(text, size, parser, userdata)` is the lower-level entry
`md_html` wraps. A harness exercising it directly (to separate parser-core bugs
from HTML-renderer bugs) must build its own `MD_PARSER` struct with
`abi_version = 0` and non-NULL `enter_block/leave_block/enter_span/leave_span/text`
callbacks (as per the struct definition), since `md_parse` does not supply any
defaults and will accept whatever vtable is passed, including one with callbacks
that do nothing (minimal fuzzing driver) or one that mimics md4c-html's own
callback table.
**Evidence:**
> src/md4c.h:427-471: full `MD_PARSER` struct; all five rendering callback members (`enter_block`, `leave_block`, `enter_span`, `leave_span`, `text`) are plain function pointers with no documented "may be NULL" exemption (unlike `debug_log`, explicitly marked "Optional (may be NULL)" at line 459-461, and `syntax`, "Reserved. Set to NULL." at line 468-470) — implies the five rendering callbacks should be valid function pointers.
> src/md4c.c:7301-7305: `if(parser->abi_version != 0) {...; return -1;}` — first thing checked; `abi_version` must be 0.
> src/md4c.c:7307-7328: after the abi check, `md_parse` copies the parser struct (`memcpy(&ctx.parser, parser, sizeof(MD_PARSER))`), sets up `ctx.text`/`ctx.size` from the input params, builds internal hash tables and stacks — entirely self-initializing, no external init call needed before `md_parse` either.
> src/md4c.c:7330: `ret = md_process_doc(&ctx);` — this is the Core function from the Logic Group that does the actual work; callbacks in `parser` get invoked from deep inside this call tree (e.g. `enter_block_callback`/`text_callback` as wired by `md_html`, or custom equivalents a harness would supply).
> src/md4c.c:7332-7336: cleanup (`md_free_ref_defs`, `md_free_footnote_defs`, `free(ctx.buffer)`) and return of `ret` — same return-code contract as documented in md4c.h:484-486 (0 success, -1 internal error, else the first non-zero value returned by one of the caller's own callbacks).
**Return value / parameters:** identical semantics to `md_html`'s underlying call
(see P2.4, P2.10 above) since `md_html` simply forwards to it — `parser_flags`
bitmask goes into `parser->flags` (src/md4c.h:432-434), and `text`/`size` have the
same byte-buffer expectations as `input`/`input_size` in P2.11.
**Nothing to free belonging to the caller** beyond the `MD_PARSER` struct itself
(typically stack-allocated, see src/md4c-html.c:636) — `md_parse` frees all of its
own internal allocations (P2.5 evidence above, src/md4c.c:7332-7335) before
returning.

### How md_parse combines with md_html in one harness
**Claim:** Because `md_parse` is otherwise uncalled in-tree (no test/tool in this
source tree calls it directly — confirmed by grep below), a harness that wants to
reach both entries from the Logic Group would call `md_html()` as the primary
(outer) entry for the HTML-specific rendering/escaping paths, and separately
construct its own `MD_PARSER` (reusing `md_html`'s literal at
src/md4c-html.c:636-646 as the template, or with minimal no-op callbacks) to call
`md_parse()` directly for parser-core-only coverage with the same input bytes and
`parser_flags`; the two calls are independent (no shared state), so both can be
invoked back-to-back on the same or different fuzzer-derived `parser_flags` /
`renderer_flags` / text slices within one `LLVMFuzzerTestOneInput`.
**Evidence:**
> Grep for `md_parse\(` across the whole `md4c` source tree (src/, md2html/, test/) found exactly two occurrences: the declaration (src/md4c.h:488) and its definition (src/md4c.c:7295) plus the one call site inside `md_html` (src/md4c-html.c:668) — i.e. no existing project code calls `md_parse` independently of `md_html`; a harness doing so would be novel but uses only documented public API (md4c.h is an installed/public header per src/CMakeLists.txt `PUBLIC_HEADER` likely applied analogous to md4c-html.h:42).
> src/CMakeLists.txt:10: `add_library(md4c md4c.c md4c.h)` — `md4c.c`/`md4c.h` form their own library target independent of `md4c-html`, confirming `md_parse` is linkable/usable without going through `md_html` at all (only `libmd4c.a` needed, not `libmd4c-html.a`), consistent with build.sh linking both archives anyway (line 26) for a combined harness.