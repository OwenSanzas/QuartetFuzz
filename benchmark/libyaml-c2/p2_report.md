## target: yaml_parser_parse

### P2.1 Init Sequence
**Claim:** Before calling `yaml_parser_parse`, the caller must (1) call `yaml_parser_initialize(&parser)` to zero and allocate the parser's internal buffers/stacks/queues, and (2) install exactly one input source via `yaml_parser_set_input_string`, `yaml_parser_set_input_file`, or `yaml_parser_set_input` (custom handler). `yaml_parser_set_encoding` is optional and, if used, must be called before the first `yaml_parser_parse`/`yaml_parser_scan`/`yaml_parser_load` call (it is only meaningful before any bytes are read). `yaml_set_max_nest_level` is a global (non-per-parser) setting and may be called any time before parsing starts.

**Evidence:**
> src/api.c:177-213 `yaml_parser_initialize` memsets the parser and calls `BUFFER_INIT`/`QUEUE_INIT`/`STACK_INIT` on `raw_buffer`, `buffer`, `tokens`, `indents`, `simple_keys`, `states`, `marks`, `tag_directives`; returns 0 and cleans up partial state on allocation failure.
> src/api.c:292-334 `yaml_parser_set_input_string/_file/_input` each `assert(!parser->read_handler)` — "You can set the source only once," confirming exactly one source call is required/allowed and must precede parsing.
> src/api.c:344-347 `yaml_parser_set_encoding`: `assert(!parser->encoding); /* Encoding is already set or detected. */` — must be called before the reader has autodetected/consumed an encoding, i.e. before the first parse/scan/load call.
> tests/run-parser.c:55-61 canonical use: `yaml_parser_initialize(&parser)` → `yaml_parser_set_input_file(&parser, file)` → loop calling `yaml_parser_parse(&parser, &event)`.
> src/parser.c:67,168-171 `MAX_NESTING_LEVEL` is a process-global `int`, set by `yaml_set_max_nest_level(max)`; not tied to a specific parser instance.

### P2.2 Parameter Construction
**Claim:** `yaml_parser_parse(yaml_parser_t *parser, yaml_event_t *event)` requires both pointers non-NULL; `parser` must have been produced by `yaml_parser_initialize` (not just zero-initialized) and `event` is an output-only struct the function overwrites — caller does not need to pre-populate it, just provide stack/heap storage.

**Evidence:**
> src/parser.c:178-186 `yaml_parser_parse`: `assert(parser); assert(event);` then `memset(event, 0, sizeof(yaml_event_t));` — event contents before the call are irrelevant/overwritten.
> include/yaml.h:1425-1428 doc: "`@param[in,out] parser` A parser object. `@param[out] event` An empty event object."

### P2.3 Object Lifecycle
**Claim:** Lifecycle is create (`yaml_parser_initialize`) → configure (`set_input*`, optional `set_encoding`) → use (repeated `yaml_parser_parse` calls, each followed by `yaml_event_delete`) → destroy (`yaml_parser_delete`). The parser is stateful across calls (internal state machine `parser->state`), so calls must not be interleaved with `yaml_parser_scan` or `yaml_parser_load` on the same parser instance.

**Evidence:**
> include/yaml.h:1421-1423 "An application must not alternate the calls of yaml_parser_parse() with the calls of yaml_parser_scan() or yaml_parser_load(). Doing this will break the parser." (same warning mirrored at yaml.h:1397-1399 for scan, yaml.h:1446-1448 for load).
> tests/run-parser.c:55-83 full lifecycle: initialize → set_input_file → loop{parse, check type, event_delete} → parser_delete → fclose.
> src/parser.c:189-192 state check `if (parser->stream_end_produced || parser->error || parser->state == YAML_PARSE_END_STATE) return 1;` shows the parser retains cross-call state that callers must not reset/bypass.

### P2.4 Return Value Handling
**Claim:** `yaml_parser_parse` returns `1` on success (including the harmless post-stream-end/-error no-op case) and `0` on error. On `0`, the caller must stop calling parse and should not trust `event` for further processing; diagnostic info is available on `parser->error`, `parser->problem`, `parser->problem_mark` (and `parser->context`/`context_mark` for context errors).

**Evidence:**
> src/parser.c:177-197: returns `1` immediately if `stream_end_produced || error || state==YAML_PARSE_END_STATE`; otherwise returns `yaml_parser_state_machine(parser, event)` whose state functions return 0 on syntax errors via `yaml_parser_set_parser_error`/`yaml_parser_set_parser_error_context` (src/parser.c:203-226), which set `parser->error = YAML_PARSER_ERROR` plus `problem`/`problem_mark`.
> tests/run-parser.c:61-69: `if (!yaml_parser_parse(&parser, &event)) { error = 1; ... fprintf(stderr, "Parse error: %s\nLine: %lu Column: %lu\n", parser.problem, parser.problem_mark.line+1, parser.problem_mark.column+1); break; }`
> include/yaml.h:1428 "@returns @c 1 if the function succeeded, @c 0 on error."

### P2.5 Cleanup Sequence
**Claim:** After every successful `yaml_parser_parse` call, the event must be freed with `yaml_event_delete(&event)` before reuse/going out of scope (it owns heap buffers for anchors/tags/scalar values/tag-directives depending on event type). After the parse loop ends (success — `YAML_STREAM_END_EVENT` observed — or failure), the parser itself is freed once with `yaml_parser_delete(&parser)`. If a custom input source owns resources (e.g. a `FILE*`), the caller closes it after `yaml_parser_delete`.

**Evidence:**
> src/api.c:984-1029 `yaml_event_delete` frees `document_start.version_directive` + each tag_directive's `handle`/`prefix` + the tag_directives array (DOCUMENT_START_EVENT), `alias.anchor` (ALIAS_EVENT), `scalar.anchor/tag/value` (SCALAR_EVENT), `sequence_start.anchor/tag`, `mapping_start.anchor/tag`, then memsets the event.
> src/api.c:219-242 `yaml_parser_delete` frees `raw_buffer`,`buffer`, drains+frees `tokens` via `yaml_token_delete`, frees `indents`,`simple_keys`,`states`,`marks` stacks, frees each `tag_directives` entry's `handle`/`prefix`, then memsets parser.
> tests/run-parser.c:74,79: `yaml_event_delete(&event);` inside loop; `yaml_parser_delete(&parser);` once after loop, then `fclose(file)` (file opened by caller, closed by caller — matches yaml.h:1353 "application is responsible for closing the file").

### P2.6 API Existence
**Claim:** All entries listed exist in the current codebase at the given paths.

**Evidence:**
> include/yaml.h:1322-1323 `yaml_parser_initialize`
> include/yaml.h:1346-1348 `yaml_parser_set_input_string`
> include/yaml.h:1360-1361 `yaml_parser_set_input_file`
> include/yaml.h:1372-1374 `yaml_parser_set_input`
> include/yaml.h:1383-1384 `yaml_parser_set_encoding`
> include/yaml.h:1407-1408 `yaml_parser_scan`
> include/yaml.h:1431-1432 `yaml_parser_parse` (target)
> include/yaml.h:1456-1457 `yaml_parser_load`
> include/yaml.h:1470-1471 `yaml_set_max_nest_level`
> src/api.c:985 `yaml_event_delete` (declared include/yaml.h, search for "Destroy an event object")
> src/parser.c:178,233 definitions of `yaml_parser_parse` and `yaml_parser_state_machine`
> src/loader.c:87 `yaml_parser_load` definition
> src/scanner.c:749 `yaml_parser_scan` definition

### P2.7 Co-call Constraints
**Claim:** (a) `yaml_parser_initialize` / `yaml_parser_delete` are a mandatory create/destroy pair per `yaml_parser_t`. (b) Exactly one of `yaml_parser_set_input_string` / `_file` / `_input` may be called per parser (enforced by assert on `read_handler`); none may be called twice or mixed. (c) `yaml_parser_parse`, `yaml_parser_scan`, and `yaml_parser_load` are mutually exclusive per parser instance — pick one driving API and stick to it for the parser's lifetime. (d) Each `yaml_parser_parse`/`yaml_parser_scan` call that succeeds and returns a non-trivial event/token should be paired with `yaml_event_delete`/`yaml_token_delete` before the storage is reused or discarded, to avoid leaking event-owned heap buffers.

**Evidence:**
> src/api.c:293,312,330 `assert(!parser->read_handler); /* You can set the source only once. */` in all three set_input* functions.
> include/yaml.h:1397-1399, 1421-1423, 1446-1448 explicit "must not alternate" warnings for scan/parse/load.
> tests/run-parser.c:61,74 parse+event_delete pairing; tests/run-scanner.c:42,49 scan+token_delete pairing; tests/run-loader.c:42,49 load+document_delete pairing.

### P2.8 Prerequisite State
**Claim:** The only prerequisite state for `yaml_parser_parse` is a parser struct initialized via `yaml_parser_initialize` with an input source attached (`read_handler` non-NULL); no other global/library-wide init is required (there is no `yaml_library_init`). `yaml_set_max_nest_level` affects the process-global `MAX_NESTING_LEVEL` used by the scanner's indent-stack limit check, so if the harness wants deterministic/non-default nesting limits it must call it before parsing (it affects all parsers in the process since it is a plain global, not per-parser).

**Evidence:**
> src/api.c:181 `memset(parser, 0, sizeof(yaml_parser_t));` is the full "construction"; no other global state used.
> src/scanner.c:485 `extern int MAX_NESTING_LEVEL;` and src/scanner.c:1184,1238 `STACK_LIMIT(parser, parser->indents, MAX_NESTING_LEVEL - parser->flow_level)` — the only use site, confirming it only limits indent stack depth, and it's a file-global, not stored inside `yaml_parser_t`.
> src/parser.c:67 `int MAX_NESTING_LEVEL = 1000;` default value.

### P2.9 Result Usage
**Claim:** Every successful `yaml_parser_parse` call fills `event->type` (one of `YAML_STREAM_START_EVENT`, `YAML_STREAM_END_EVENT`, `YAML_DOCUMENT_START_EVENT`, `YAML_DOCUMENT_END_EVENT`, `YAML_ALIAS_EVENT`, `YAML_SCALAR_EVENT`, `YAML_SEQUENCE_START/END_EVENT`, `YAML_MAPPING_START/END_EVENT`) plus a type-specific union (`event->data.scalar.value/length/anchor/tag`, `event->data.alias.anchor`, etc.) and `start_mark`/`end_mark`. Real callers (a) loop checking `event.type == YAML_STREAM_END_EVENT` to terminate, (b) switch on `event.type` to drive further logic (e.g. re-emit via `yaml_emitter_emit` in the deconstructor/reformatter examples, or build a document tree in `yaml_parser_load`), and (c) always call `yaml_event_delete(&event)` once done with the event's buffers.

**Evidence:**
> tests/run-parser.c:72,74 `done = (event.type == YAML_STREAM_END_EVENT); yaml_event_delete(&event);`
> tests/example-deconstructor.c:122-137 `yaml_parser_parse(&parser, &input_event)` result is checked (`input_event.type == YAML_STREAM_END_EVENT`) then the deconstructor builds new output events (`yaml_mapping_start_event_initialize`, etc.) and feeds them to `yaml_emitter_emit(&emitter, &output_event)` — i.e. parse's event is consumed by application logic and by the emitter API, a different module.
> src/loader.c:99-121 `yaml_parser_load` is itself built on top of `yaml_parser_parse`: calls it to get STREAM-START, then DOCUMENT-START/END events, asserts `event.type == YAML_STREAM_START_EVENT` (loader.c:100) and `YAML_STREAM_END_EVENT` short-circuit (loader.c:109-111), then dispatches to `yaml_parser_load_document(parser, &event)` (loader.c:118) which asserts `event->type == YAML_DOCUMENT_START_EVENT` (loader.c:189) and reads `event->data.document_start.*` fields directly (loader.c:192-200) — demonstrating the project's own canonical "result usage" of parse events.
> src/loader.c:217-245 `yaml_parser_load_nodes` switches on `event.type` (ALIAS/SCALAR/SEQUENCE_START/SEQUENCE_END/MAPPING_START/MAPPING_END) dispatching to `yaml_parser_load_alias/_scalar/_sequence/_mapping/_sequence_end/_mapping_end`, each consuming `event.data.*` fields — the fullest in-tree example of consuming yaml_parser_parse's output.

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting inputs across the API surface: (1) which `set_input_*` function is called selects string/FILE*/custom-handler reader code paths; (2) `yaml_parser_set_encoding(parser, encoding)` with `YAML_ANY_ENCODING` (autodetect via BOM) vs `YAML_UTF8_ENCODING`/`YAML_UTF16LE_ENCODING`/`YAML_UTF16BE_ENCODING` forces a decode path in the reader feeding the scanner; (3) `yaml_set_max_nest_level(max)` changes the indent-stack depth bound used by `yaml_parser_fetch_more_tokens`'s scanning (affects whether deeply nested block/flow structures error out); (4) inside the target, `yaml_parser_state_machine`'s `switch (parser->state)` (src/parser.c:233-305+) is the actual grammar dispatcher — the YAML text itself (directives, anchors/aliases, flow `[]/{}` vs block indentation, scalar styles) drives which state-function branch executes, which is the main fuzz-relevant "mode selection" surface.

**Evidence:**
> include/yaml.h:1377-1384 `yaml_parser_set_encoding` doc + signature; `yaml_encoding_t` values declared in yaml.h (YAML_ANY_ENCODING/YAML_UTF8_ENCODING/YAML_UTF16LE_ENCODING/YAML_UTF16BE_ENCODING per the enum referenced by this function).
> src/parser.c:233-305 `yaml_parser_state_machine` switch over `parser->state` enumerators (YAML_PARSE_STREAM_START_STATE ... YAML_PARSE_FLOW_MAPPING_VALUE_STATE) dispatches to ~20 different state functions.
> include/yaml.h:1460-1468 `yaml_set_max_nest_level` doc: "Default: 1000. Each nesting level increases the stack..." with param `max`.
> src/scanner.c:1184,1238 the two call sites where `MAX_NESTING_LEVEL` actually bounds parsing (indents stack).

### P2.11 Input Expectations
**Claim:** The target's "input" is whatever bytes the installed reader produces — raw bytes from a string buffer, a `FILE*`, or a custom `yaml_read_handler_t`; there is no required file framing beyond YAML's own grammar (stream = optional BOM + implicit/explicit documents, ended by EOF from the read handler). Size is bounded only by what the caller passes to `yaml_parser_set_input_string(parser, input, size)` (a `size_t`); the `input` pointer must stay valid for the parser's lifetime (not copied). For `yaml_parser_set_input_file`/`_input`, the reader pulls bytes lazily via `fread`/the custom handler as the scanner needs them (`yaml_file_read_handler`, src/api.c:274-282, `yaml_string_read_handler`, src/api.c:248-268). Encoding is auto-detected from a BOM unless `yaml_parser_set_encoding` forced one. There is no minimum-size requirement; an empty input simply yields STREAM-START immediately followed by STREAM-END.

**Evidence:**
> include/yaml.h:1336-1343 `yaml_parser_set_input_string` doc: "@a input pointer must be valid while the @a parser object exists. The application is responsible for destroying @a input after destroying the @a parser."
> src/api.c:248-268 `yaml_string_read_handler`: when `current == end`, reports `*size_read = 0` (EOF) — defines end-of-input condition for string source.
> src/api.c:274-282 `yaml_file_read_handler`: `*size_read = fread(...); return !ferror(...)` — EOF naturally handled by fread returning 0 without error.
> src/parser.c:3-40 grammar comment block documents the full expected structure: `stream ::= STREAM-START implicit_document? explicit_document* STREAM-END`, etc., which is what the token-driven state machine enforces.

## Other entries: call protocol and combination with the target

### yaml_parser_initialize
**Claim:** Must be called first on a `yaml_parser_t` (any storage, no pre-zeroing needed — it memsets internally). Returns `int` (1 success / 0 on allocation failure of one of its internal buffers/stacks). On failure, no cleanup call is needed/safe-guarded internally (it already undoes partial allocations before returning 0); caller should just abort use of that parser object (do NOT call `yaml_parser_delete` on a failed-initialize parser since fields were already freed and the struct is left as whatever state `BUFFER_DEL` etc. leave it — but it is safe because those macros are idempotent on NULL, matching memset-zero pattern of a never-fully-inited struct). Frees nothing itself (it's a constructor). Paired with `yaml_parser_delete`.

**Evidence:**
> src/api.c:176-213 (quoted above).
> tests/run-parser.c:55 `assert(yaml_parser_initialize(&parser));`

### yaml_parser_set_input_string
**Claim:** Configuration call, used in place of `_file`/custom `_input`. Parameters: `input` (`const unsigned char*`, must be non-NULL and point at `size` valid bytes, typically the fuzzer's `data` buffer), `size` (`size_t`, the fuzz input length). Returns `void`. No separate "undo"/free — the parser does not own or copy `input`; caller must keep `input` alive until after `yaml_parser_delete`. Must be called exactly once, after `yaml_parser_initialize` and before the first `yaml_parser_parse`/`scan`/`load`.

**Evidence:**
> src/api.c:288-302 definition; include/yaml.h:1334-1348 doc.
> ossfuzz_view/libyaml-c2/libyaml_scanner_fuzzer.c:35 and libyaml_loader_fuzzer.c:35 both use `yaml_parser_set_input_string(&parser, data, size)` directly on fuzzer-provided `data`/`size` — the idiomatic pattern a `yaml_parser_parse`-based harness should copy.

### yaml_parser_set_input_file
**Claim:** Alternative source selecting the FILE*-based reader path (`yaml_file_read_handler`). Parameter `file` must be a non-NULL, readable, open `FILE*`; caller opens and later closes it (not owned by parser). Returns `void`. Mutually exclusive with `set_input_string`/`set_input`.

**Evidence:**
> src/api.c:308-319; include/yaml.h:1350-1361 "@a file should be a file object open for reading. The application is responsible for closing the @a file."
> tests/run-parser.c:52,57,81 `fopen` → `yaml_parser_set_input_file` → ... → `fclose(file)` after `yaml_parser_delete`.

### yaml_parser_set_input
**Claim:** Generic/custom source: parameters `handler` (`yaml_read_handler_t*`, signature `int (*)(void *data, unsigned char *buffer, size_t size, size_t *size_read)`) and `data` (opaque context pointer passed back to handler). Returns `void`. Not exercised by any in-tree test/example directly (string/file handlers in api.c are the internal implementations used by the other two setters), but it is the mechanism underlying them. For a fuzz harness, this lets simulation of chunked/short reads; not required if `set_input_string` is used.

**Evidence:**
> src/api.c:325-335 definition and asserts (`!parser->read_handler`, `handler` non-NULL).
> include/yaml.h:1363-1374 doc and `yaml_read_handler_t` prototype earlier in yaml.h (reader handler section).
> Grep of tests/examples shows no direct external caller of `yaml_parser_set_input` besides the two wrapper setters in src/api.c itself — "not found" for a dedicated example beyond api.c's internal use through `yaml_string_read_handler`/`yaml_file_read_handler`.

### yaml_parser_set_encoding
**Claim:** Optional configuration, called at most once per parser, before any parse/scan/load call (enforced by `assert(!parser->encoding)`, where `YAML_ANY_ENCODING` is the zero/default value meaning "not yet set/detected"). Parameter `encoding` is a `yaml_encoding_t` enum. No return value, nothing to free.

**Evidence:**
> src/api.c:341-348 (quoted above); include/yaml.h:1376-1384.

### yaml_parser_scan
**Claim:** Lower-level variant of the target; NOT to be mixed with `yaml_parser_parse`/`yaml_parser_load` on the same parser (P2.7). Same init/config prerequisites as the target (`initialize` + one `set_input_*`). Returns 1/0 like the target. Produces a `yaml_token_t` (not `yaml_event_t`) that must be freed with `yaml_token_delete` (declared in yaml.h, used in tests/run-scanner.c:49) — analogous lifecycle to event_delete but for tokens. A harness exercising the target should NOT also call `yaml_parser_scan` on the same parser instance.

**Evidence:**
> src/scanner.c:748-782 definition (quoted above).
> include/yaml.h:1397-1399 "must not alternate" warning.
> tests/run-scanner.c:42-49 canonical scan+token_delete loop.

### yaml_parser_load
**Claim:** Higher-level consumer/variant built directly on top of `yaml_parser_parse` (calls it internally, see P2.9 evidence at src/loader.c:99,108,118). Same init/config prerequisites. Produces a `yaml_document_t` tree; on success (`1`) with a non-NULL root (checked via `yaml_document_get_root_node(&document)` in tests/run-loader.c:47) caller processes the tree then calls `yaml_document_delete(&document)` (tests/run-loader.c:49) exactly once per successful load, repeating `yaml_parser_load` until it returns a document with no root node (end of stream) or returns `0` (error). Must not be mixed with direct `yaml_parser_parse`/`yaml_parser_scan` calls on the same parser (P2.7) — i.e., a harness picks either the "raw parse" path (target + event_delete) or the "load" path (yaml_parser_load + document_delete), not both on one parser instance.

**Evidence:**
> src/loader.c:86-132 full definition (quoted above): on error path calls `yaml_parser_delete_aliases(parser)` and `yaml_document_delete(document)` before returning 0 — i.e. the function cleans up its own partial document on failure, caller must NOT additionally free a document after a failed `yaml_parser_load` call beyond what's already done (document was already deleted internally at loader.c:128).
> tests/run-loader.c:36-58 and ossfuzz_view/libyaml-c2/libyaml_loader_fuzzer.c:27-51 canonical init → set_input_string/_file → loop{load, check root node, document_delete} → parser_delete.

### yaml_event_delete
**Claim:** Must be called on every `yaml_event_t` produced by a successful `yaml_parser_parse` (or by `yaml_parser_load`'s internal use, and by event-construction APIs on the emitter side) once the event's data is no longer needed, to free type-specific heap buffers; safe/no-op-like for event types with no heap members (default branch does nothing but the function still memsets). Must not be called twice on the same event without an intervening fill (it memsets to 0 at the end, so a second call is harmless because the switch hits default, but relying on that is not an documented contract — safest is "call once per produced event").

**Evidence:**
> src/api.c:984-1029 (quoted above).
> tests/run-parser.c:74 call site right after the type check and before `count++`.

## Build notes
**Claim:** Fuzzers for this project are plain C, compiled with the OSS-Fuzz `$CC`/`$CFLAGS` against the in-source `include/` directory (so `#include "yaml.h"` or `#include <yaml.h>` resolves via `-Iinclude`), then linked with `$CXX`/`$CXXFLAGS` (C++11) against the static library `src/.libs/libyaml.a` built by the project's own autotools (`./bootstrap && ./configure && make`) plus `$LIB_FUZZING_ENGINE`. No extra third-party libraries are required.

**Evidence:**
> ossfuzz_view/libyaml-c2/build.sh:17-33:
> ```
> ./bootstrap
> ./configure
> make "-j$(nproc)"
> for fuzzer in $SRC/*_fuzzer.c; do
>   ...
>   $CC $CFLAGS -I $SRC -Iinclude -c $fuzzer -o $fuzzer_basename.o
>   $CXX $CXXFLAGS -std=c++11 $fuzzer_basename.o -o $OUT/$fuzzer_basename \
>       $LIB_FUZZING_ENGINE src/.libs/libyaml.a
> done
> ```
> ossfuzz_view/libyaml-c2/Dockerfile:17-24: base image `gcr.io/oss-fuzz-base/base-builder`, installs `make autoconf automake libtool`, clones `libyaml`, copies `*_fuzzer.c`/headers into `$SRC`.
> ossfuzz_view/libyaml-c2/libyaml_scanner_fuzzer.c:15 and libyaml_loader_fuzzer.c:15 both `#include "yaml.h"` (quoted include, resolved via `-Iinclude` passed in build.sh) — confirms the header path convention a new `yaml_parser_parse`-based harness should follow.
> The current `libyaml_parser_fuzzer.c` in the OSS-Fuzz view is a stub placeholder ("Gold harness replaced by an empty stub for baseline generation") that only keeps `yaml_parser_parse` linked via `__asm__` aliasing and does not actually call it — confirming this is the file the downstream harness generator is expected to replace with a real implementation following the patterns shown above (cf. libyaml_scanner_fuzzer.c / libyaml_loader_fuzzer.c as structurally analogous, fully-working sibling harnesses in the same directory).