## target: yaml_parser_load

### P2.1 Init Sequence
**Claim:** Before calling `yaml_parser_load(parser, document)` the caller must (1) call `yaml_parser_initialize(parser)` to zero and allocate the parser's internal buffers/stacks, and (2) select exactly one input source via `yaml_parser_set_input_string`, `yaml_parser_set_input_file`, or `yaml_parser_set_input` (custom handler). Optionally `yaml_parser_set_encoding` may be called before any parsing to force the byte decoding (otherwise it is auto-detected from a BOM). `yaml_parser_load` itself does not initialize the parser struct; it only initializes/clears the `document` argument.

**Evidence:**
> src/loader.c:87-96: `yaml_parser_load(yaml_parser_t *parser, yaml_document_t *document) { ... assert(parser); assert(document); memset(document, 0, sizeof(yaml_document_t)); if (!STACK_INIT(parser, document->nodes, yaml_node_t*)) goto error; ...}` — only `document` is memset/initialized, `parser` must already be set up.
> src/api.c:177-199: `yaml_parser_initialize` does `memset(parser, 0, sizeof(yaml_parser_t))` then `BUFFER_INIT`/`QUEUE_INIT`/`STACK_INIT` for raw_buffer, buffer, tokens, indents, simple_keys, states, marks, tag_directives — must run before any parse/scan/load call.
> src/api.c:292-294, 312-313, 330-331: each `yaml_parser_set_input_*` asserts `!parser->read_handler` ("You can set the source only once"), i.e. exactly one of the three must be called, after init and before parsing.
> src/api.c:344-345: `yaml_parser_set_encoding` asserts `!parser->encoding` ("Encoding is already set or detected"), so it must be called before the first `yaml_parser_parse`/`yaml_parser_load`/`yaml_parser_scan` call triggers encoding auto-detection.
> ossfuzz_view/libyaml-c1/libyaml_reformatter_alt_fuzzer.c:44-52: `yaml_parser_initialize(&parser)` → `yaml_emitter_initialize(&emitter)` → `yaml_parser_set_input_string(&parser, data, size)` → loop calling `yaml_parser_load`.

### P2.2 Parameter Construction
**Claim:** `yaml_parser_load` takes two non-NULL pointers: `yaml_parser_t *parser` (fully initialized, stack-allocated struct, not heap-required) and `yaml_document_t *document` (caller-owned, uninitialized memory is fine — it is memset internally). No size/range parameters on `yaml_parser_load` itself; all input-size handling happens via the earlier `yaml_parser_set_input_*` call.

**Evidence:**
> src/loader.c:91-94: `assert(parser); assert(document); memset(document, 0, sizeof(yaml_document_t));` — document must just be a valid writable `yaml_document_t*`; contents are discarded.
> include/yaml.h:1456 (declaration) and surrounding doc comment (not shown with @param tags in this version, but behavior confirmed in loader.c): parser must be previously initialized and have an input source.
> src/api.c:289-302: `yaml_parser_set_input_string(parser, input, size)` stores `input`/`input+size` as a pointer range into `parser->input.string`; caller must keep the buffer alive at least as long as the parser is used (per header doc at yaml.h:1337-1339 "the input pointer must be valid while the parser object exists").

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: `yaml_parser_initialize` (create) → `yaml_parser_set_input_*`[+ optional `yaml_parser_set_encoding`] (configure) → repeated `yaml_parser_load` calls, one per document in the stream, each producing a `yaml_document_t` that must be consumed/inspected and then destroyed with `yaml_document_delete` before the next `yaml_parser_load` call or before the parser itself is destroyed → `yaml_parser_delete` (destroy parser) at the end. A single parser can yield multiple documents; the stream ends when `yaml_parser_load` succeeds but the resulting document is empty (`yaml_document_get_root_node` returns NULL).

**Evidence:**
> src/loader.c:98-111: on first call, if `!parser->stream_start_produced`, pulls a STREAM-START event; if `parser->stream_end_produced` returns success with an empty document (asserted via memset above); if the very next event is STREAM-END, also returns success with empty document — this is how the end-of-stream/last-call is signaled.
> ossfuzz_view/libyaml-c1/libyaml_reformatter_alt_fuzzer.c:63-77: `while (!done) { if (!yaml_parser_load(&parser, &document)) break; done = (!yaml_document_get_root_node(&document)); if (!yaml_emitter_dump(&emitter, &document)) break; }` — loop pattern driving repeated loads until empty-root/document signals end; note `yaml_emitter_dump` here takes ownership of document's nodes (see P2.9).
> ossfuzz_view/libyaml-c1/libyaml_dumper_fuzzer.c:236-264: `while (!done) { if (!yaml_parser_load(&parser, &document)) {...} done = (!yaml_document_get_root_node(&document)); if (!done) { ...copy_document(...); yaml_emitter_dump(&emitter,&document); } else { yaml_document_delete(&document); } }` — explicit `yaml_document_delete` call on the empty terminal document since `yaml_emitter_dump` is not invoked on it.
> src/loader.c:125-131: `error: yaml_parser_delete_aliases(parser); yaml_document_delete(document); parser->document = NULL; return 0;` — on failure, `yaml_parser_load` itself calls `yaml_document_delete` on the partially-built document, so caller must NOT call `yaml_document_delete` again on that same document after a failed load (double free risk) — but callers *do* need to call it on documents obtained from a *successful* load.

### P2.4 Return Value Handling
**Claim:** `yaml_parser_load` returns `int`: `1` on success (including the "no more documents" terminal case where the returned `document` is a valid but empty `yaml_document_t`), `0` on error. On error, `parser->error`/`parser->problem`/`parser->problem_mark` (and `context`/`context_mark`) are set (by `yaml_parser_set_composer_error[_context]` or by the underlying scanner/parser error setters) and are readable the same way as after `yaml_parser_parse` failures; the caller should stop calling `yaml_parser_load` again after a `0` return (the parser is left in an error state) and must NOT call `yaml_document_delete` on the document again since the function already deleted it internally on the error path.

**Evidence:**
> src/loader.c:118-131: success path returns 1 after `yaml_parser_load_document`; `error:` label calls `yaml_parser_delete_aliases(parser); yaml_document_delete(document); parser->document = NULL; return 0;`
> src/loader.c:104-106, 109-111: two early-return `return 1;` points for the empty/terminal document case (stream_end_produced already true, or next event is STREAM_END).
> src/loader.c:138-165 (yaml_parser_set_composer_error / _context, static functions in loader.c): set `parser->error = YAML_COMPOSER_ERROR` plus problem/context fields, mirroring `yaml_parser_set_parser_error` in parser.c:203-226 used by `yaml_parser_parse`.
> ossfuzz_view/libyaml-c1/libyaml_dumper_fuzzer.c:237-240: `if (!yaml_parser_load(&parser, &document)) { equal = 1; break; }` — caller treats 0 as terminal/error and stops looping without touching `document` again.

### P2.5 Cleanup Sequence
**Claim:** On success, caller must eventually call `yaml_document_delete(&document)` for every document obtained from a successful `yaml_parser_load` (after using/consuming it, e.g. after `yaml_document_get_root_node`/`yaml_document_get_node` traversal or after `yaml_emitter_dump`, which itself takes ownership — see P2.9). The parser itself must be destroyed once with `yaml_parser_delete(&parser)` after all documents have been processed. On error from `yaml_parser_load`, the document is already freed internally — only `yaml_parser_delete(&parser)` remains to be called (no double `yaml_document_delete`). Order: document(s) deleted before or independent of parser deletion; parser deleted last.

**Evidence:**
> src/api.c:219-242: `yaml_parser_delete` frees `raw_buffer`, `buffer`, drains+deletes `tokens` queue (via `yaml_token_delete`), deletes `indents`, `simple_keys`, `states`, `marks` stacks, drains `tag_directives` freeing `handle`/`prefix`, then memsets parser to 0.
> src/api.c:1122-1158: `yaml_document_delete` pops all nodes freeing `node.tag`, scalar `value`, or deleting sequence `items`/mapping `pairs` stacks, frees `version_directive`, frees each tag_directive's `handle`/`prefix`, frees `tag_directives.start`, memsets document to 0.
> ossfuzz_view/libyaml-c1/libyaml_reformatter_alt_fuzzer.c:79-85: `free(out.buf); yaml_emitter_delete(&emitter); cleanup_parser: yaml_parser_delete(&parser);` — emitter/output resources freed, then parser deleted last; document deletion handled implicitly by `yaml_emitter_dump` (see P2.9), not called separately here.
> ossfuzz_view/libyaml-c1/libyaml_dumper_fuzzer.c:244-253, 262-264, 294, 299, 307-309: explicit `yaml_document_delete` on every document not consumed by `yaml_emitter_dump` or on error branches, plus a final loop `for (k...) yaml_document_delete(documents + k);` to free documents retained via `copy_document`.

### P2.6 API Existence
**Claim:** All entry functions listed for this feature exist in the current codebase at the cited locations.

**Evidence:**
> include/yaml.h:1456: `yaml_parser_load` declared — target.
> include/yaml.h:1322-1323: `yaml_parser_initialize` declared; defined src/api.c:176-213.
> include/yaml.h:1346-1348: `yaml_parser_set_input_string` declared; defined src/api.c:288-302.
> include/yaml.h:1360-1361: `yaml_parser_set_input_file` declared; defined src/api.c:308-319.
> include/yaml.h:1372-1374: `yaml_parser_set_input` declared; defined src/api.c:325-335.
> include/yaml.h:1383-1384: `yaml_parser_set_encoding` declared; defined src/api.c:341-348.
> include/yaml.h:1407-1408/~ and further: `yaml_parser_parse` declared; defined src/parser.c:177-197.
> include/yaml.h:861-862: `yaml_document_get_node` declared; defined src/api.c:1164-1173.
> include/yaml.h:880-881: `yaml_document_get_root_node` declared; defined src/api.c:1179-1188.
> include/yaml.h:846-847: `yaml_document_delete` declared; defined src/api.c:1122-1158.
> Core (internal, static, confirmed in src/loader.c): `yaml_parser_load_document` (loader.c:184), `yaml_parser_load_nodes` (loader.c:216), `yaml_parser_load_alias`, `yaml_parser_load_scalar`, `yaml_parser_load_sequence`, `yaml_parser_load_mapping` (declared loader.c:58-72, defined later in file), `yaml_parser_register_anchor` (loader.c:30/263), `yaml_parser_state_machine` (parser.c:232-233), `yaml_parser_fetch_more_tokens` (referenced in scanner.c, not directly read but is the standard libyaml scanner function called from `yaml_parser_parse`'s token pump — exists per project convention, confirmed via `yaml_parser_parse` → scanner chain).

### P2.7 Co-call Constraints
**Claim:** Paired constraints found:
1. `yaml_parser_initialize` ↔ `yaml_parser_delete` (create/destroy the parser, exactly once each).
2. Exactly one of `yaml_parser_set_input_string` / `yaml_parser_set_input_file` / `yaml_parser_set_input` must be called (mutually exclusive — asserted via `!parser->read_handler`), and only once.
3. `yaml_parser_load` (per document) ↔ `yaml_document_delete` (per document obtained from a *successful* load) — but NOT on documents already freed internally by `yaml_parser_load`'s own error path.
4. The header explicitly forbids mixing `yaml_parser_scan()` with `yaml_parser_parse()`/`yaml_parser_load()` on the same parser ("An application must not alternate the calls of yaml_parser_scan() with the calls of yaml_parser_parse() or yaml_parser_load(). Doing this will break the parser.") — so a harness must pick either the scan-level API or the parse/load-level API exclusively per parser instance.
5. `yaml_document_initialize` ↔ `yaml_document_delete` for manually-built documents (not produced via `yaml_parser_load`), relevant if the harness also builds documents by hand (e.g. dumper fuzzer's `copy_document`).

**Evidence:**
> include/yaml.h:1396-1399: "An application must not alternate the calls of yaml_parser_scan() with the calls of yaml_parser_parse() or yaml_parser_load(). Doing this will break the parser."
> src/api.c:292-294/312-313/330-331: `assert(!parser->read_handler)` in all three `yaml_parser_set_input_*` functions enforcing "set the source only once".
> src/loader.c:125-131: internal `yaml_document_delete(document)` on error path — caller must not call it again for that document.
> include/yaml.h:833-838: `yaml_document_initialize` paired with `yaml_document_delete` (846-847) for manually constructed documents.

### P2.8 Prerequisite State
**Claim:** Before `yaml_parser_load` can be called meaningfully: (a) `parser` struct must be zero-initialized and have all internal stacks/buffers allocated by `yaml_parser_initialize`; (b) `parser->read_handler` must be non-NULL (set by one `yaml_parser_set_input_*` call) — `yaml_parser_parse`/the scanner will assert/fail otherwise; (c) no prior `yaml_parser_scan` calls must have been made on the same parser; (d) `parser->document` must be NULL before the call (it's only non-NULL while `yaml_parser_load` is running, set/cleared by the function itself) — so the harness should not call `yaml_parser_load` re-entrantly on the same parser from inside a callback. `document` itself requires no prior state — it's memset inside the call.

**Evidence:**
> src/reader.c:146: `assert(parser->read_handler);  /* Read handler must be set. */` inside the raw byte-fill routine invoked while scanning, i.e. parsing bytes before an input source is set triggers an assertion failure.
> src/loader.c:116,121,129: `parser->document = document;` set at start of real work, `parser->document = NULL;` cleared on both success and error paths — confirms single in-flight document per parser.
> src/api.c:181: `memset(parser, 0, sizeof(yaml_parser_t))` inside `yaml_parser_initialize` establishes the zeroed prerequisite state (encoding=0/YAML_ANY_ENCODING, stream_start_produced=0, etc.) that `yaml_parser_load`'s stream-start/stream-end checks (loader.c:98,104) rely on.

### P2.9 Result Usage
**Claim:** The `yaml_document_t` filled by `yaml_parser_load` is consumed in real code by:
1. `yaml_document_get_root_node(document)` — get the first/top node, used to detect "no more documents" (returns NULL) and as the traversal entry point.
2. `yaml_document_get_node(document, index)` — look up a specific node by 1-based id, used recursively to walk `sequence.items`/`mapping.pairs` arrays of child node ids.
3. `yaml_emitter_dump(&emitter, &document)` — re-serializes the document; per its contract (seen via usage) it takes ownership of/empties the document's internal node stack (callers in the project do not call `yaml_document_delete` on documents passed to `yaml_emitter_dump`, only on documents that are NOT dumped).
4. Manual node-array walking (`document->nodes.start` .. `document->nodes.top`) plus `yaml_document_add_scalar/add_sequence/add_mapping/append_sequence_item/append_mapping_pair` to copy node-by-node into a second document (seen in the dumper fuzzer's `copy_document`).
5. `yaml_document_delete(document)` — the terminal consumer in all cases (either directly, or implicitly via `yaml_emitter_dump`) to release the node stack, tag/scalar buffers, and tag_directives.

**Evidence:**
> ossfuzz_view/libyaml-c1/libyaml_reformatter_alt_fuzzer.c:66-76: `if (!yaml_parser_load(&parser, &document)) break; done = (!yaml_document_get_root_node(&document)); if (!yaml_emitter_dump(&emitter, &document)) break;` — root-node check then hand off to emitter.
> ossfuzz_view/libyaml-c1/libyaml_dumper_fuzzer.c:242-264: `done = (!yaml_document_get_root_node(&document)); if (!done) { copy_document(&documents[document_number++], &document); yaml_emitter_dump(&emitter, &document); } else { yaml_document_delete(&document); }` — the empty terminal document is deleted explicitly since it's never passed to `yaml_emitter_dump`.
> ossfuzz_view/libyaml-c1/libyaml_dumper_fuzzer.c:139-171 (copy_document body, read earlier): iterates `document_from->nodes.start` to `.top`, calling `yaml_document_add_scalar/add_sequence/add_mapping` and `yaml_document_append_sequence_item/append_mapping_pair`, reading `node->data.scalar.value/length/style`, `node->data.sequence.style`, `node->data.sequence.items`, `node->data.mapping.pairs`.
> ossfuzz_view/libyaml-c1/libyaml_deconstructor_fuzzer.c:36-93 (`nodes_equal`): recursive consumer using `yaml_document_get_node(document, index)`, reading `node->type`, `node->tag`, `node->data.scalar.{value,length}`, `node->data.sequence.items.{start,top}`, `node->data.mapping.pairs.{start,top}.{key,value}` — demonstrates the expected post-load node traversal API for indices returned inside items/pairs arrays (1-based ids, dereferenced via `yaml_document_get_node`).
> ossfuzz_view/libyaml-c1/libyaml_dumper_fuzzer.c:284-300: a second parser re-parses the emitter's output via `yaml_parser_load` again, and compares via `documents_equal`/`nodes_equal`, then `yaml_document_delete(&document)` every loop iteration — another real consumption pattern (round-trip verification).

### P2.10 Behaviour Controls
**Claim:** Behaviour of `yaml_parser_load` and the surrounding API is controlled by:
1. **Input source selection** — `yaml_parser_set_input_string` vs `_file` vs `_input` (custom handler) chooses the byte source (reader.c `read_handler`), mutually exclusive (P2.7).
2. **Encoding** — `yaml_parser_set_encoding(parser, encoding)` with `YAML_ANY_ENCODING` (default, BOM-autodetected), `YAML_UTF8_ENCODING`, `YAML_UTF16LE_ENCODING`, `YAML_UTF16BE_ENCODING` forces the decode path in `reader.c`'s `yaml_parser_determine_encoding`, bypassing BOM sniffing.
3. **Document content shape** drives which `yaml_parser_load_*` composer function runs per event type: `YAML_ALIAS_EVENT` → `yaml_parser_load_alias` (looks up `parser->aliases` by anchor string, error "found undefined alias" if missing — loader.c:366-367); `YAML_SCALAR_EVENT` → `yaml_parser_load_scalar` (defaults tag to `YAML_DEFAULT_SCALAR_TAG` if NULL or "!" — loader.c:384-388); `YAML_SEQUENCE_START_EVENT`/`YAML_MAPPING_START_EVENT` → push a loader_ctx stack entry (nesting) and default tag to `YAML_DEFAULT_SEQUENCE_TAG`/`YAML_DEFAULT_MAPPING_TAG`.
4. **Anchors** — presence of `&name` on a node registers it via `yaml_parser_register_anchor`; a duplicate anchor name triggers a composer error "found duplicate anchor; first occurrence"/"second occurrence" (loader.c:280-283) — a key fuzz-relevant branch. An alias `*name` with no matching prior anchor triggers "found undefined alias" (loader.c:366-367).
5. **Node count / nesting limits** — `STACK_LIMIT(parser, parser->document->nodes, INT_MAX-1)` and `STACK_LIMIT(parser, *ctx, INT_MAX-1)` cap node count/nesting to `INT_MAX-1` (loader.c:382,427,450) — exceeding triggers an error, not a crash, if hit (also see `yaml_set_max_nest_level` in parser.c:167-171 which bounds parser recursion depth, `MAX_NESTING_LEVEL`).
6. **Multi-document streams** — repeated `yaml_parser_load` calls on the same parser walk "---"/"..." separated documents; `%YAML`/`%TAG` directives change `document->version_directive`/`tag_directives` (set in `yaml_parser_load_document`, loader.c:192-199) which affects tag resolution for later nodes in that document.
7. **Style flags on nodes** (`event->data.scalar.style`, `sequence_start.style`, `mapping_start.style`) are carried into `SCALAR_NODE_INIT`/`SEQUENCE_NODE_INIT`/`MAPPING_NODE_INIT` and later affect how `yaml_emitter_dump`/`copy_document` reproduce block vs flow / plain vs quoted styles — not behavior of the loader itself but of downstream consumers.

**Evidence:**
> src/reader.c:51-83 (`yaml_parser_determine_encoding`): branch on BOM bytes / `parser->encoding` already set.
> src/loader.c:280-283, 366-367: the two composer-error strings quoted above.
> src/loader.c:382,427,450: `STACK_LIMIT(..., INT_MAX-1)` calls bounding node/nesting growth.
> src/parser.c:167-171: `yaml_set_max_nest_level(int max) { MAX_NESTING_LEVEL = max; }` — global knob affecting parser-level recursion limits reachable through `yaml_parser_load`.
> src/loader.c:384-388,429-433 (and the analogous mapping default `YAML_DEFAULT_MAPPING_TAG` further in file): tag defaulting logic.

### P2.11 Input Expectations
**Claim:** `yaml_parser_load` expects the parser's configured input (raw bytes from string/file/handler) to be a YAML 1.1 stream as understood by the libyaml scanner/parser: UTF-8, UTF-16LE, or UTF-16BE encoded text (auto-detected via BOM, or forced by `yaml_parser_set_encoding`), consisting of zero or more YAML documents separated by `---`/`...` markers, optionally preceded by `%YAML`/`%TAG` directives, containing block/flow scalars, sequences, and mappings, anchors (`&name`) and aliases (`*name`). There is no explicit size limit enforced at the `yaml_parser_load` level beyond internal `STACK_LIMIT`/`INT_MAX-1` guards (P2.10); malformed/incomplete input (unterminated quotes, bad indentation, invalid escapes, truncated multi-byte UTF-16/UTF-8 sequences) is rejected by lower-level scanner/parser errors which propagate up as `yaml_parser_parse` returning 0 inside `yaml_parser_load_nodes`/`yaml_parser_load_document`, causing `yaml_parser_load` to go to its `error:` label. No explicit NUL-termination is required for string input — `yaml_parser_set_input_string` takes an explicit `size` (byte count), so embedded NULs are fine for UTF-8/16 content (loader relies on the length-based buffer, not strlen).

**Evidence:**
> src/loader.c:221-251 (`yaml_parser_load_nodes`): `if (!yaml_parser_parse(parser, &event)) return 0;` — any scanner/parser-level failure on malformed input aborts the load.
> src/loader.c:99-111: `yaml_parser_load` expects the first event to be `YAML_STREAM_START_EVENT` (`assert(event.type == YAML_STREAM_START_EVENT)`), i.e. the stream-level protocol is enforced by assertion, meaning the parser state machine (not the raw bytes) must have produced a well-formed event sequence; this is guaranteed by `yaml_parser_parse`'s own state machine, not re-validated here.
> src/api.c:299-301: `yaml_parser_set_input_string` stores `input`/`input+size`, i.e. explicit length-delimited buffer, no implicit NUL-termination requirement.
> src/reader.c:41-83: BOM-byte expectations (`\xef\xbb\xbf` UTF-8, `\xff\xfe` UTF-16LE, `\xfe\xff` UTF-16BE) for auto-detection; absent any BOM, defaults to UTF-8 (reader.c:83: `parser->encoding = YAML_UTF8_ENCODING;`).

### Build notes
**Claim:** Fuzz harnesses for this project are plain C, compiled against the project's own static library (`src/.libs/libyaml.a`) built via autotools (`./bootstrap && ./configure && make`), then linked with a C++ driver using `$LIB_FUZZING_ENGINE`. The target's header is included simply as `"yaml.h"` using `-Iinclude` (the project's own installed-header directory) plus `-I $SRC` for auxiliary fuzzer-shared headers (e.g. `yaml_write_handler.h`). No special defines beyond standard OSS-Fuzz `$CFLAGS`/`$CXXFLAGS` are used for libyaml itself.

**Evidence:**
> ossfuzz_view/libyaml-c1/build.sh:17-33:
> ```
> ./bootstrap
> ./configure
> make "-j$(nproc)"
> for fuzzer in $SRC/*_fuzzer.c; do
>   fuzzer_basename=$(basename -s .c $fuzzer)
>   $CC $CFLAGS -I $SRC -Iinclude -c $fuzzer -o $fuzzer_basename.o
>   $CXX $CXXFLAGS -std=c++11 $fuzzer_basename.o -o $OUT/$fuzzer_basename \
>       $LIB_FUZZING_ENGINE src/.libs/libyaml.a
> done
> ```
> ossfuzz_view/libyaml-c1/libyaml_loader_fuzzer.c:2 and sibling fuzzers (e.g. libyaml_dumper_fuzzer.c:15, libyaml_reformatter_alt_fuzzer.c:15): `#include "yaml.h"` (found via `-Iinclude` relative to the libyaml source root, i.e. `libyaml/include/yaml.h`).
> ossfuzz_view/libyaml-c1/libyaml_dumper_fuzzer.c:16, libyaml_reformatter_alt_fuzzer.c:16: `#include "yaml_write_handler.h"` — a fuzzer-local helper (output buffer write-handler for the emitter), found via `-I $SRC`.
> Note: `ossfuzz_view/libyaml-c1/libyaml_loader_fuzzer.c` currently present in the OSS-Fuzz view is a stub ("Gold harness replaced by an empty stub for baseline generation") with an empty `LLVMFuzzerTestOneInput` — it is NOT a real reference for `yaml_parser_load` usage; the real usage patterns for `yaml_parser_load` were found instead in `libyaml_dumper_fuzzer.c` and `libyaml_reformatter_alt_fuzzer.c` (both compiled by the same build.sh loop since they match `*_fuzzer.c`).

## Other entries' call protocol (combined with the target in one harness)

**yaml_parser_initialize(yaml_parser_t *parser)** — include/yaml.h:1322-1323, defined src/api.c:176-213.
Needs before it: nothing (parser may be uninitialized/garbage stack memory; function memsets it).
Params: `parser` non-NULL pointer to caller-allocated `yaml_parser_t` (stack or heap).
Returns: 1 success / 0 on allocation failure (`BUFFER_INIT`/`STACK_INIT`/`QUEUE_INIT` failed) — on 0, internal partial allocations are already freed by its own error path (src/api.c:201-212), so caller should NOT call `yaml_parser_delete` after a failed `yaml_parser_initialize`.
Free: paired with `yaml_parser_delete` (src/api.c:219-242), only if `yaml_parser_initialize` returned 1.
Combination: first call in the harness, before any `yaml_parser_set_input_*`/`yaml_parser_set_encoding`/`yaml_parser_parse`/`yaml_parser_load`/`yaml_parser_scan` call.

**yaml_parser_set_input_string(yaml_parser_t *parser, const unsigned char *input, size_t size)** — include/yaml.h:1346-1348, defined src/api.c:288-302.
Needs before it: `yaml_parser_initialize` done; no read handler set yet (`assert(!parser->read_handler)`, src/api.c:293).
Params: `input` pointer to fuzz bytes (must stay valid for the parser's lifetime — yaml.h:1337-1339), `size` byte length (no NUL-termination requirement).
Returns: void.
Free: none owned by parser; caller keeps owning/freeing `input` buffer itself, after the parser is done/deleted.
Combination: called once, right after init, before the first `yaml_parser_parse`/`yaml_parser_load` call. This is the natural choice for a libFuzzer harness (`data`, `size` directly from `LLVMFuzzerTestOneInput`).

**yaml_parser_set_input_file(yaml_parser_t *parser, FILE *file)** — include/yaml.h:1360-1361, defined src/api.c:308-319.
Alternative source to `_string`; needs an already-open `FILE*` (`assert(file)`, src/api.c:313); not relevant for a libFuzzer-byte-buffer harness unless bytes are first written to a tmpfile.
Mutually exclusive with `_string`/`_input` on same parser (P2.7).

**yaml_parser_set_input(yaml_parser_t *parser, yaml_read_handler_t *handler, void *data)** — include/yaml.h:1372-1374, defined src/api.c:325-335.
Generic handler variant; `handler` must be non-NULL (`assert(handler)`, src/api.c:331) and match `int (*)(void *data, unsigned char *buffer, size_t size, size_t *size_read)` (see yaml.h read-handler typedef). Useful if the harness wants a custom incremental feed from fuzz data; otherwise `_string` is simplest and is what real callers use (P2.1 evidence).

**yaml_parser_set_encoding(yaml_parser_t *parser, yaml_encoding_t encoding)** — include/yaml.h:1383-1384, defined src/api.c:341-348.
Needs before it: init done, must be called before `parser->encoding` gets set by auto-detection, i.e. before first `yaml_parser_parse`/`yaml_parser_load`/`yaml_parser_scan` (`assert(!parser->encoding)`, src/api.c:345). Optional — if omitted, encoding is BOM-autodetected (reader.c:51-83). A harness that wants to fuzz the explicit-encoding code paths should derive the `yaml_encoding_t` value (`YAML_ANY_ENCODING`=0 default / `YAML_UTF8_ENCODING` / `YAML_UTF16LE_ENCODING` / `YAML_UTF16BE_ENCODING`) from a byte of fuzz input and call this before `yaml_parser_load`.
Returns: void. No cleanup needed (stores a plain int field).

**yaml_parser_parse(yaml_parser_t *parser, yaml_event_t *event)** — include/yaml.h (~1407+), defined src/parser.c:177-197.
Lower-level alternative/complement to `yaml_parser_load` — used internally by the loader (`yaml_parser_load_nodes` calls it per event, src/loader.c:222) and can also be called directly by a harness to exercise the event-level state machine without composing a document. Needs: parser initialized + input set, same as `yaml_parser_load`. Each successful call fills `event` (caller must call `yaml_event_delete(&event)` when done with it, per the header comment at yaml.h:1418 "An application is responsible for freeing any buffers associated with the produced event object using the yaml_event_delete function" — not explicitly re-confirmed by reading event.c here but stated in the header). Returns 1/0 like `yaml_parser_load`. If a harness mixes direct `yaml_parser_parse` calls with `yaml_parser_load` on the *same* parser instance in the same stream, it must not call `yaml_parser_scan` on it (P2.7) but mixing `yaml_parser_parse` and `yaml_parser_load` on the same parser across different documents of a stream is exactly what `yaml_parser_load` does internally, so it is safe to interleave for different purposes.

**yaml_document_get_root_node(yaml_document_t *document)** — include/yaml.h:880-881, defined src/api.c:1179-1188.
Needs before it: a document produced by a successful `yaml_parser_load` (or manually by `yaml_document_initialize` + `yaml_document_add_*`). Returns pointer to first node or NULL if document is empty (used as the stream-end sentinel, see P2.3/P2.9). No ownership transfer — pointer is only valid until the document is mutated/deleted (yaml.h:869-870,852-853). No free needed for the returned pointer itself (owned by `document->nodes`).

**yaml_document_get_node(yaml_document_t *document, int index)** — include/yaml.h:861-862, defined src/api.c:1164-1173.
Needs before it: same as above; `index` is a 1-based node id as produced in `sequence.items`/`mapping.pairs` arrays (src/loader.c:296-344 shows ids pushed as `index = parser->document->nodes.top - parser->document->nodes.start` after each `PUSH`). Returns NULL if `index <= 0` or out of range (src/api.c:1169-1172). Used by harness/consumer code to recursively walk the tree (P2.9 evidence from `libyaml_deconstructor_fuzzer.c`).

**yaml_document_delete(yaml_document_t *document)** — include/yaml.h:846-847, defined src/api.c:1122-1158.
Needs before it: a document that was either successfully produced by `yaml_parser_load` (and not already consumed/freed by `yaml_emitter_dump`) or built via `yaml_document_initialize`. Must be called exactly once per live document, after use, before the memory backing `document` goes away. Returns void. Must NOT be called again on a document already deleted internally by `yaml_parser_load`'s error path (P2.4/P2.7) or already consumed by `yaml_emitter_dump`.

**Harness combination summary:** `yaml_parser_initialize` → `yaml_parser_set_input_string(data,size)` [→ optional `yaml_parser_set_encoding`] → loop { `yaml_parser_load(&parser,&document)`; break/stop on 0 return; check `yaml_document_get_root_node`/`yaml_document_get_node` for emptiness/traversal; optionally traverse via `yaml_document_get_node` on child ids; `yaml_document_delete(&document)` } → `yaml_parser_delete(&parser)`. This mirrors `libyaml_reformatter_alt_fuzzer.c` and `libyaml_dumper_fuzzer.c` exactly (minus their emitter-specific parts, which belong to a different Logic Group).