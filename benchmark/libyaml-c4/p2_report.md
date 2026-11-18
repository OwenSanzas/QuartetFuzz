## target: yaml_parser_scan

### P2.1 Init Sequence
**Claim:** Before calling `yaml_parser_scan`, the caller must (1) zero/allocate a `yaml_parser_t` and call `yaml_parser_initialize(&parser)`, which allocates the internal raw/decoded buffers, token queue and the indents/simple_keys/states/marks/tag_directives stacks; (2) attach exactly one input source via `yaml_parser_set_input_string`, `yaml_parser_set_input_file`, or `yaml_parser_set_input`; (3) optionally call `yaml_parser_set_encoding` before the first scan call if auto-detection is not desired. `yaml_parser_scan` must then be called repeatedly (not interleaved with `yaml_parser_parse`/`yaml_parser_load`) until it returns a `YAML_STREAM_END_TOKEN` token or an error.
**Evidence:**
> include/yaml.h:1397-1399: "An application must not alternate the calls of yaml_parser_scan() with the calls of yaml_parser_parse() or yaml_parser_load(). Doing this will break the parser."
> src/api.c:177-213 `yaml_parser_initialize`: `memset(parser,0,...)` then `BUFFER_INIT(... raw_buffer ...)`, `BUFFER_INIT(... buffer ...)`, `QUEUE_INIT(... tokens ...)`, `STACK_INIT(... indents/simple_keys/states/marks/tag_directives ...)`; on any failure jumps to `error:` which tears everything down and returns 0.
> src/api.c:292-294 `yaml_parser_set_input_string`: `assert(parser); assert(!parser->read_handler); assert(input);` — asserts the source has not already been set (only one input source allowed).
> tests/run-scanner.c:36-38: `assert(yaml_parser_initialize(&parser)); yaml_parser_set_input_file(&parser, file);` then loop calling `yaml_parser_scan`.
> tests/test-nesting.c:24-29: `yaml_parser_initialize(&parser); yaml_parser_set_input_string(...); ... yaml_parser_scan(&parser, &token)`.

### P2.2 Parameter Construction
**Claim:** `yaml_parser_scan(yaml_parser_t *parser, yaml_token_t *token)` takes a non-NULL, previously-initialized `parser` (stack or heap allocated struct, no separate "new" call — `yaml_parser_initialize` fills it in place) and a non-NULL, caller-owned `yaml_token_t *token` which the function fully overwrites (`memset(token, 0, sizeof(yaml_token_t))` at entry) — the token object does not need to be pre-initialized beyond existing as valid memory.
**Evidence:**
> include/yaml.h:1401-1402: `@param[in,out] parser  A parser object.` / `@param[out] token An empty token object.`
> src/scanner.c:751-756 `assert(parser); assert(token); memset(token, 0, sizeof(yaml_token_t));`

### P2.3 Object Lifecycle
**Claim:** Lifecycle is create (`yaml_parser_initialize`) → configure (`yaml_parser_set_input*`, optional `yaml_parser_set_encoding`) → use (repeated `yaml_parser_scan` calls, one `yaml_token_delete` per produced token) → destroy (`yaml_parser_delete`). The parser object itself is reusable only after re-`initialize`; `yaml_parser_delete` zeroes it.
**Evidence:**
> tests/run-scanner.c:36-54: initialize → set_input_file → loop(scan, token_delete) → `yaml_parser_delete(&parser)`.
> src/api.c:219-242 `yaml_parser_delete`: frees raw_buffer/buffer, drains and deletes remaining queued tokens via `yaml_token_delete`, frees indents/simple_keys/states/marks stacks, frees tag_directive handle/prefix pairs, then `memset(parser, 0, sizeof(yaml_parser_t))`.

### P2.4 Return Value Handling
**Claim:** `yaml_parser_scan` returns `1` on success (token filled in `*token`) and `0` on error. Once it has returned the `YAML_STREAM_END_TOKEN` token or set `parser->error`, every subsequent call is a no-op returning `1` with an unmodified/erased token (type `YAML_NO_TOKEN`, value 0) — callers must check `token->type` to detect the terminal condition, not rely on the return code alone for stream end. On error, diagnostic fields `parser->error`, `parser->problem`, `parser->problem_mark`, `parser->context`, `parser->context_mark` are populated for reporting.
**Evidence:**
> src/scanner.c:748-781: `if (parser->stream_end_produced || parser->error) { return 1; }` ... `if (!yaml_parser_fetch_more_tokens(parser)) return 0;` ... `if (token->type == YAML_STREAM_END_TOKEN) parser->stream_end_produced = 1; return 1;`
> src/scanner.c:788-799 `yaml_parser_set_scanner_error`: sets `parser->error = YAML_SCANNER_ERROR; parser->context = ...; parser->context_mark = ...; parser->problem = ...; parser->problem_mark = parser->mark;` and returns 0 — the mechanism by which scan failures surface.
> include/yaml.h:1095-1105 `yaml_parser_t` exposes `error`, `problem`, `problem_mark`, `context`, `context_mark` fields for the caller to inspect after a 0 return.
> tests/run-scanner.c:42-45: `if (!yaml_parser_scan(&parser,&token)) { error=1; break; }` then `done=(token.type==YAML_STREAM_END_TOKEN)`.

### P2.5 Cleanup Sequence
**Claim:** Every token successfully produced by `yaml_parser_scan` (including the final `YAML_STREAM_END_TOKEN`) must be released with `yaml_token_delete(&token)` before reusing the token variable or exiting the loop; this frees type-specific heap payloads (tag_directive handle/prefix, alias/anchor value, tag handle/suffix, scalar value). After the scan loop ends (success or error), call `yaml_parser_delete(&parser)` once — it also drains and frees any tokens still left in the internal queue (defensive, e.g. if the caller stopped early without a final scan call) so calling `yaml_parser_delete` without explicitly freeing already-dequeued tokens does NOT cover those already consumed by the caller — the caller is responsible for those via `yaml_token_delete`.
**Evidence:**
> include/yaml.h:1394-1395: "An application is responsible for freeing any buffers associated with the produced token object using the @c yaml_token_delete function."
> src/api.c:583-617 `yaml_token_delete`: switch on `token->type` frees `tag_directive.handle/prefix`, `alias.value`, `anchor.value`, `tag.handle/suffix`, `scalar.value`; then `memset(token, 0, sizeof(yaml_token_t))`.
> src/api.c:226-228 inside `yaml_parser_delete`: `while (!QUEUE_EMPTY(parser, parser->tokens)) { yaml_token_delete(&DEQUEUE(parser, parser->tokens)); }` — only cleans tokens still queued inside the parser, not ones already dequeued by the application's `yaml_parser_scan` calls.
> tests/run-scanner.c:49,54: `yaml_token_delete(&token);` inside the loop, `yaml_parser_delete(&parser);` after the loop.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase.
**Evidence:**
> src/api.c:177 `yaml_parser_initialize`, :220 `yaml_parser_delete`, :289 `yaml_parser_set_input_string`, :309 `yaml_parser_set_input_file`, :326 `yaml_parser_set_input`, :342 `yaml_parser_set_encoding`, :584 `yaml_token_delete`.
> src/scanner.c:749 `yaml_parser_scan`, :807 `yaml_parser_fetch_more_tokens`, :867 `yaml_parser_fetch_next_token`.
> src/reader.c:51 `yaml_parser_determine_encoding` (static, internal only — not part of public API, called indirectly via buffer/encoding logic), :94 `yaml_parser_update_raw_buffer`, :142 `yaml_parser_update_buffer`.
> include/yaml.h:1322,1332,1347,1361,1373,1384,1408 declare all the public entries.

### P2.7 Co-call Constraints
**Claim:** `yaml_parser_initialize` / `yaml_parser_delete` are a strict create/destroy pair (delete must not be called without a prior successful initialize, and must always be called exactly once to release resources). `yaml_parser_set_input*` may be called only once per parser instance (asserted), i.e. it is "set-once" not a toggle — there is no paired "unset". Each `yaml_parser_scan` producing a non-`YAML_NO_TOKEN` token should be paired with one `yaml_token_delete` call (token-level create/destroy pairing), mirroring the event-level `yaml_parser_parse`/`yaml_event_delete` pairing used by the higher-level parser API.
**Evidence:**
> src/api.c:293 `assert(!parser->read_handler); /* You can set the source only once. */` (same assertion pattern at :312 and :330).
> src/api.c:345 `assert(!parser->encoding); /* Encoding is already set or detected. */` in `yaml_parser_set_encoding` — must be called before any scanning triggers auto-detection, and only once.
> include/yaml.h:1394-1395 token/`yaml_token_delete` pairing requirement (quoted above).
> ossfuzz_view/libyaml-c4/libyaml_parser_fuzzer.c:32-46 shows the analogous higher-level pairing: `yaml_parser_initialize` → `yaml_parser_set_input_string` → loop(`yaml_parser_parse`, `yaml_event_delete`) → `yaml_parser_delete`.

### P2.8 Prerequisite State
**Claim:** The only mandatory prerequisite state before the first `yaml_parser_scan` call is: parser struct successfully initialized (`parser->raw_buffer`/`parser->buffer`/`parser->tokens`/stacks allocated) and `parser->read_handler` set (non-NULL) by one of the `set_input*` functions — `yaml_parser_scan` and its internal callees (`yaml_parser_fetch_more_tokens` → `yaml_parser_fetch_next_token` → `CACHE`/`yaml_parser_update_buffer` → `yaml_parser_update_raw_buffer` which invokes `parser->read_handler`) will otherwise dereference a NULL read handler. No file/socket/network connection state beyond the chosen input mechanism (string buffer, open `FILE*`, or custom handler/data) is required.
**Evidence:**
> src/scanner.c:869-872 `yaml_parser_fetch_next_token`: `if (!CACHE(parser, 1)) return 0;` — `CACHE` macro (scanner.c:495) expands to `yaml_parser_update_buffer` when not enough chars are cached, which calls down to `yaml_parser_update_raw_buffer` (reader.c:94) that calls `parser->read_handler(...)` (reader.c:121).
> src/api.c:296-301 `yaml_parser_set_input_string` sets `parser->read_handler = yaml_string_read_handler;` and the `input.string` triple — this is the state the reader depends on.
> src/api.c:315-318 `yaml_parser_set_input_file` sets `parser->read_handler = yaml_file_read_handler；` and `parser->input.file = file` (file must already be `fopen`'d by caller — tests/run-scanner.c:33-34).

### P2.9 Result Usage
**Claim:** The token filled in by `yaml_parser_scan` is consumed by application code inspecting `token->type` (an enum driving the scan loop's termination and dispatch) and, for value-carrying token types, the type-specific fields in `token->data` (e.g. `token->data.scalar.value`/`length`/`style`, `token->data.tag.handle`/`suffix`, `token->data.anchor.value`, `token->data.alias.value`, `token->data.tag_directive.handle`/`prefix`, `token->data.version_directive.major/minor`). After use, every token must be passed to `yaml_token_delete`. In the library's own higher-level parser (`yaml_parser_parse` in src/parser.c), the scanner's token queue (filled via the same `yaml_parser_fetch_more_tokens`/`yaml_parser_fetch_next_token` machinery that backs `yaml_parser_scan`) is consumed internally by `yaml_parser_state_machine` to build `yaml_event_t` objects — i.e., `yaml_parser_parse` and `yaml_parser_scan` are two different, mutually exclusive consumers of the same underlying token-producing core.
**Evidence:**
> tests/run-scanner.c:42-51: `yaml_parser_scan(&parser,&token)` → `done = (token.type == YAML_STREAM_END_TOKEN)` → `yaml_token_delete(&token)` → `count++`.
> tests/test-nesting.c:29-34: identical pattern, using the scan result's `token.type` only to detect stream end and error (return via function result), demonstrating minimal consumption.
> src/parser.c:49,57 (macros `PEEK_TOKEN`/`SKIP_TOKEN`) use `parser->token_available || yaml_parser_fetch_more_tokens(parser)` and `parser->token_available = 0` — the same internal queue/availability flag that `yaml_parser_scan` manages (src/scanner.c:766,774), confirming scan and parse share but must not interleave on the same parser's token stream.
> include/yaml.h (token struct, around yaml_token_t definition) documents per-type `data` union fields that real code reads after a successful scan (field names confirmed above in `yaml_token_delete`'s switch, src/api.c:588-613).

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting inputs across the API surface are: (a) the byte content/length passed to `yaml_parser_set_input_string` (or produced by the file/custom handler) — this single buffer drives every branch of `yaml_parser_fetch_next_token`'s dispatch on the current character (`%`,`-`,`.`,`[`,`]`,`{`,`}`,`,`,`?`,`:`,`*`,`&`,`!`,`|`,`>`,`"`,`'`, or plain scalar fallback); (b) `parser->encoding`, either left `YAML_ANY_ENCODING` (0) to trigger BOM-based auto-detection in `yaml_parser_determine_encoding`, or pre-set via `yaml_parser_set_encoding(parser, YAML_UTF8_ENCODING|YAML_UTF16LE_ENCODING|YAML_UTF16BE_ENCODING)` to force a decode path, changing how raw bytes are interpreted into `parser->buffer` before any token dispatch; (c) indentation/column state (`mark.column == 0` checks) controlling document start/end marker and directive-line detection; (d) `parser->flow_level` (0 = block context vs >0 = flow context) selecting different token-fetch branches for `,`,`[`,`]`,`{`,`}` and scalar scanning rules; (e) block scalar style, literal `|` vs folded `>`, selecting `yaml_parser_scan_block_scalar`'s folding behaviour; (f) flow scalar quoting, single `'` vs double `"`, selecting different escape-processing inside `yaml_parser_scan_flow_scalar`.
**Evidence:**
> src/reader.c:50-87 `yaml_parser_determine_encoding`: BOM-sniffs `BOM_UTF16LE`/`BOM_UTF16BE`/`BOM_UTF8` and defaults to UTF-8 if none found, only reached when encoding not already forced.
> src/api.c:341-348 `yaml_parser_set_encoding`: `assert(!parser->encoding); parser->encoding = encoding;` — forces the path, bypassing BOM auto-detect (callee `yaml_parser_determine_encoding` is only invoked from reader.c:161 when `!parser->encoding`).
> src/scanner.c:867-872+ `yaml_parser_fetch_next_token` dispatches on `CHECK(parser->buffer, c)` for each punctuation character (full switch visible further in the function body, e.g. handling `%`, `-`, `.`, `[`, `]`, `{`, `}`, `,`, `?`, `:`, `*`, `&`, `!`, `|`, `>`, `"`, `'`) per the Logic Group description and file content around scanner.c:870-1000 (dispatcher body).
> Function names `yaml_parser_scan_block_scalar` (literal vs folded) and `yaml_parser_scan_flow_scalar` (single vs double quote) confirmed present in Logic Group's listed Core functions and referenced forward-declared near scanner.c:741-742 pattern (`yaml_parser_scan_plain_scalar` forward decl at line 742 is adjacent to the other `yaml_parser_scan_*` declarations in the same file).

### P2.11 Input Expectations
**Claim:** The scanner expects a raw byte stream with no mandatory header beyond an optional encoding BOM (`EF BB BF` for UTF-8, `FF FE` for UTF-16LE, `FE FF` for UTF-16BE); absent a BOM, bytes are assumed UTF-8. There is no explicit "termination" byte required — EOF is signalled by the read handler returning `size_read == 0` (string handler: `current == end`; file handler: short/zero `fread`), after which the scanner synthesizes a `YAML_STREAM_END_TOKEN`. Internal buffer/queue sizes are bounded by compile-time constants (`INPUT_RAW_BUFFER_SIZE`, `INPUT_BUFFER_SIZE`, `INITIAL_QUEUE_SIZE`) that auto-grow via `yaml_parser_update_buffer`; the scanner enforces a nesting-depth limit (seen in tests/test-nesting.c, default ~1000) rejecting pathologically deep flow/block nesting with a scanner error rather than unbounded recursion/stack growth.
**Evidence:**
> src/reader.c:41-43 `#define BOM_UTF8 "\xef\xbb\xbf"` / `BOM_UTF16LE "\xff\xfe"` / `BOM_UTF16BE "\xfe\xff"`.
> src/reader.c:82-84: `else { parser->encoding = YAML_UTF8_ENCODING; }` — default when no BOM matches.
> src/api.c:253-257 `yaml_string_read_handler`: `if (parser->input.string.current == parser->input.string.end) { *size_read = 0; return 1; }` — EOF signal for string input.
> src/api.c:280-281 `yaml_file_read_handler`: `*size_read = fread(...); return !ferror(...)` — `size_read==0` at EOF without error.
> src/scanner.c:777-779: `if (token->type == YAML_STREAM_END_TOKEN) parser->stream_end_produced = 1;` — terminal token, not a required input terminator byte.
> tests/test-nesting.c:47-58 and :62-91: 2000 levels of `[` nesting and 2000 levels of block-mapping indentation are asserted to be rejected (`scan_string(...) == 1`, i.e. error) by the scanner's nesting-depth enforcement, while 500 levels (test 3, lines 96-110) succeed — demonstrating a documented, tested nesting bound.

## Other entries: call protocol and combination with the target

### yaml_parser_initialize
- **Needs before it:** Nothing except a valid, writable `yaml_parser_t` (stack/heap struct); no other libyaml call required first.
- **Parameters:** `yaml_parser_t *parser` — out-parameter, fully overwritten (`memset` to 0) then its internal buffers/stacks allocated.
- **Return:** `1` success, `0` on allocation failure (OOM) — caller must check before proceeding; on failure all partially-allocated internals are already freed internally (src/api.c:201-212), so `yaml_parser_delete` must NOT be called again on a failed-`initialize` parser (double free risk since it was already cleaned in the `error:` path and the struct wasn't re-zeroed to a "safe" delete-able state at that point — in fact api.c:201-212 frees but does not memset, so an application calling `yaml_parser_delete` after a failed `initialize` would double-free; real callers in this codebase only ever call `yaml_parser_delete` after a successful `initialize`, e.g. tests/run-scanner.c:36,54).
- **Combination with target:** Always the first call in every real call site found (tests/run-scanner.c:36; tests/test-nesting.c:24; ossfuzz libyaml_parser_fuzzer.c:32).

### yaml_parser_set_input_string
- **Needs before it:** `yaml_parser_initialize` must have succeeded; parser's `read_handler` must still be NULL (not previously set).
- **Parameters:** `const unsigned char *input` (must be non-NULL and remain valid for the parser's lifetime, per doc comment include/yaml.h:1337-1339), `size_t size` (length in bytes — this is the natural way to feed arbitrary/fuzzed byte content and length directly, no copy is made, src/api.c:299-301 just stores pointers).
- **Return:** `void` — no error path; failures (e.g. bad encoding) surface later at `yaml_parser_scan` time.
- **Combination with target:** Called once, right after `initialize` and before any `yaml_parser_scan` call, per tests/test-nesting.c:25-26 and ossfuzz libyaml_parser_fuzzer.c:35 (analogous for `yaml_parser_parse`).

### yaml_parser_set_input_file
- **Needs before it:** `yaml_parser_initialize` succeeded; `read_handler` unset; an already-`fopen`'d `FILE*` open for reading (caller owns closing it).
- **Parameters:** `FILE *file` (non-NULL, asserted at src/api.c:313).
- **Return:** `void`.
- **Combination with target:** Alternative to `set_input_string`; tests/run-scanner.c:33-38 opens the file, asserts it, then calls `yaml_parser_set_input_file` before the `yaml_parser_scan` loop, and closes the file (`fclose`) only after `yaml_parser_delete` (run-scanner.c:54-56) — i.e. the FILE* must outlive the whole scan loop and the parser's own delete call before being closed by the app.

### yaml_parser_set_input
- **Needs before it:** `yaml_parser_initialize` succeeded; `read_handler` unset.
- **Parameters:** `yaml_read_handler_t *handler` (non-NULL function pointer matching signature `int (*)(void *data, unsigned char *buffer, size_t size, size_t *size_read)`), `void *data` (opaque, passed back to handler — may be NULL if the handler doesn't need it).
- **Return:** `void`.
- **Combination with target:** Generic alternative to the two above for custom/fuzzer-driven byte feeding (e.g. a harness could implement a handler that hands out bytes from the fuzz input incrementally rather than via `set_input_string`'s whole-buffer approach) — not exercised in the project's own tests/tools found, but declared and documented at include/yaml.h:1363-1374.

### yaml_parser_set_encoding
- **Needs before it:** `yaml_parser_initialize` succeeded; must be called before encoding is "already set or detected" (i.e. before the first `yaml_parser_scan`/read triggers auto-detection) — asserted `!parser->encoding` (src/api.c:345).
- **Parameters:** `yaml_encoding_t encoding` — one of `YAML_ANY_ENCODING`(0, default)/`YAML_UTF8_ENCODING`/`YAML_UTF16LE_ENCODING`/`YAML_UTF16BE_ENCODING` (enum declared in include/yaml.h near the `yaml_encoding_t` typedef, used at reader.c:66,72,78,83).
- **Return:** `void`.
- **Combination with target:** Optional; if omitted, encoding is auto-detected from BOM inside the first `yaml_parser_update_buffer`/`yaml_parser_determine_encoding` call triggered by the first `yaml_parser_scan`. Not used in tests/run-scanner.c or tests/test-nesting.c (both rely on auto-detection), confirming it is optional configuration.

### yaml_parser_delete
- **Needs before it:** A parser that was successfully `yaml_parser_initialize`d (see caveat above about failed-initialize parsers). Should be the last libyaml call on that parser object.
- **Parameters:** `yaml_parser_t *parser` (non-NULL, asserted src/api.c:222).
- **Return:** `void`.
- **Combination with target:** Called exactly once, after the `yaml_parser_scan` loop ends (whether by STREAM_END or error) — tests/run-scanner.c:54, tests/test-nesting.c:37. Internally also frees any tokens still sitting in the internal queue that the application never dequeued via `yaml_parser_scan` (defensive double-duty, src/api.c:226-228), but does NOT free tokens the application already received and is responsible for via `yaml_token_delete`.

### yaml_token_delete (not in Entries list but required companion, declared include/yaml.h near token API)
- **Needs before it:** A token previously filled by a successful `yaml_parser_scan` call.
- **Parameters:** `yaml_token_t *token`.
- **Return:** `void`.
- **Combination with target:** Must be called once per token returned by `yaml_parser_scan` (src/api.c:584-617), including the terminal `YAML_STREAM_END_TOKEN`, per the documented contract (include/yaml.h:1394-1395) and every real call site found (tests/run-scanner.c:49; tests/test-nesting.c:34).

## Build notes

**Claim:** Fuzzer source files for this project are plain C, compiled individually with the project's `$CC`/`$CFLAGS` against the in-tree `include/` directory (via `-Iinclude`) and `$SRC` (for any helper headers), then linked with `$CXX` (C++ driver, `-std=c++11`) against the OSS-Fuzz engine and the static library `src/.libs/libyaml.a` built by the project's own `./bootstrap && ./configure && make` sequence.

**Evidence:**
> ossfuzz_view/libyaml-c4/build.sh:17-19: `./bootstrap` / `./configure` / `make "-j$(nproc)"` — builds the autotools-based libyaml static library first.
> ossfuzz_view/libyaml-c4/build.sh:21-26: `for fuzzer in $SRC/*_fuzzer.c; do ... $CC $CFLAGS -I $SRC -Iinclude -c $fuzzer -o $fuzzer_basename.o ...` — each `*_fuzzer.c` is compiled as C with `$CC`, header resolved via `-Iinclude` (i.e. `#include "yaml.h"` or `#include <yaml.h>` resolves to `libyaml/include/yaml.h`, confirmed used in ossfuzz_view/libyaml-c4/libyaml_parser_fuzzer.c:15 `#include "yaml.h"`).
> ossfuzz_view/libyaml-c4/build.sh:28-33: `$CXX $CXXFLAGS -std=c++11 $fuzzer_basename.o -o $OUT/$fuzzer_basename $LIB_FUZZING_ENGINE src/.libs/libyaml.a` — final link step pulls in the static archive built in step 1 plus the fuzzing engine; no other external libraries are linked (no `-lyaml`, no system libyaml — only the just-built archive).
> ossfuzz_view/libyaml-c4/libyaml_parser_fuzzer.c:1-49 is an existing, analogous harness (uses `yaml_parser_parse`/`yaml_event_delete` instead of `yaml_parser_scan`/`yaml_token_delete`) showing the exact init→set_input_string→loop→delete pattern that a `yaml_parser_scan`-based harness should mirror at the token level.
> ossfuzz_view/libyaml-c4/libyaml_scanner_fuzzer.c:1-16 is currently a stub ("Gold harness replaced by an empty stub for baseline generation") that only keeps `yaml_parser_scan` linked via an `__asm__` alias trick and does not actually call it — this is the file the downstream harness generator is expected to replace with a real scanner-driving harness.