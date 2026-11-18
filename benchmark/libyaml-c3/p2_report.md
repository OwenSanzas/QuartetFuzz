## target: yaml_emitter_dump

### P2.1 Init Sequence
**Claim:** Before calling `yaml_emitter_dump`, the caller must (1) `yaml_emitter_initialize(&emitter)`, (2) configure an output sink with `yaml_emitter_set_output_string` / `yaml_emitter_set_output_file` / `yaml_emitter_set_output` (exactly one, only once), (3) optionally set formatting options (`yaml_emitter_set_canonical`, `yaml_emitter_set_unicode`, `yaml_emitter_set_width`, `yaml_emitter_set_indent`, `yaml_emitter_set_break`, `yaml_emitter_set_encoding`), and (4) have a valid `yaml_document_t` produced either by `yaml_document_initialize` + `yaml_document_add_*`/`yaml_document_append_*` calls, or by `yaml_parser_load`. `yaml_emitter_open` may optionally be called first, but `yaml_emitter_dump` calls it itself if not already opened.
**Evidence:**
> include/yaml.h:1941: "This function should be used before yaml_emitter_dump() is called." (re yaml_emitter_open)
> src/dumper.c:122-124: `if (!emitter->opened) { if (!yaml_emitter_open(emitter)) goto error; }`
> src/writer.c:37: `assert(emitter->write_handler); /* Write handler must be set. */` — write handler is required by the writer invoked from yaml_emitter_emit, which yaml_emitter_dump calls.
> src/api.c:461,481,499: `assert(!emitter->write_handler);` in all three `yaml_emitter_set_output*` functions — only one output sink may be set, and only once.
> tests/run-dumper.c:249-259: `yaml_emitter_initialize(&emitter); ... yaml_emitter_set_canonical/unicode ...; yaml_emitter_set_output_string(...); yaml_emitter_open(&emitter);` then loop calling `yaml_emitter_dump`.
> ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:222-234: real fuzzer does `yaml_parser_initialize`, `yaml_parser_set_input_string`, `yaml_emitter_initialize`, `yaml_emitter_set_canonical/unicode`, `yaml_emitter_set_output` (custom handler), `yaml_emitter_open`, before entering the load/dump loop.

### P2.2 Parameter Construction
**Claim:** `yaml_emitter_dump(yaml_emitter_t *emitter, yaml_document_t *document)` requires a non-NULL, initialized `emitter` (via `yaml_emitter_initialize`, stack-allocated struct is fine — it is zeroed internally) and a non-NULL `document` that is either zero-initialized and then populated via `yaml_document_initialize`/`yaml_document_add_*`, or filled by `yaml_parser_load`. No other parameters.
**Evidence:**
> src/dumper.c:117-118: `assert(emitter); /* Non-NULL emitter object is required. */` `assert(document); /* Non-NULL emitter object is expected. */`
> include/yaml.h:1979 signature; both parameters are pointers, no size/flags params.
> src/api.c:1036-1041 `yaml_document_initialize` signature shows how document is built (version_directive, tag_directives range, start/end implicit flags).

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: create emitter → configure output/options → (optional explicit) `yaml_emitter_open` → build/obtain document → `yaml_emitter_dump` (repeatable, once per document, in a loop for multi-document streams) → `yaml_emitter_close` → `yaml_emitter_delete`. The `document` object passed to `yaml_emitter_dump` is consumed/destroyed by the call itself (success or error) — the caller must NOT call `yaml_document_delete` on it afterward, and must not reuse it.
**Evidence:**
> include/yaml.h:1967-1970: "...the document object may be generated using yaml_parser_load()... The emitter takes the responsibility for the document object and destroys its content after it is emitted. The document object is destroyed even if the function fails."
> src/dumper.c:151,157: both the success path and the `error:` path call `yaml_emitter_delete_document_and_anchors(emitter)`, which (dumper.c:166-200) frees all node tags/scalar values/sequence+mapping stacks and `STACK_DEL`s `document->nodes`, then sets `emitter->document = NULL`.
> ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:236-265: loop calls `yaml_parser_load(&parser,&document)` then (after making an independent copy via `copy_document`) calls `yaml_emitter_dump(&emitter,&document)` exactly once per document and never calls `yaml_document_delete` on that same `document` again afterward — the copies (`documents[]`) are the ones explicitly deleted later with `yaml_document_delete` (fuzzer lines 307-309).
> tests/run-dumper.c:283-284: after the dump loop, `yaml_emitter_close(&emitter); yaml_emitter_delete(&emitter);` finalize/free the emitter.

### P2.4 Return Value Handling
**Claim:** `yaml_emitter_dump` returns 1 on success, 0 on failure (e.g. allocation failure for `emitter->anchors`, or any underlying `yaml_emitter_emit`/`yaml_emitter_open`/`yaml_emitter_close` failure, or `yaml_emitter_dump_node` failure). Callers must check the return value; on both outcomes the document has already been destroyed (see P2.3), so error handling should not also free `document`.
**Evidence:**
> src/dumper.c:136: `if (!emitter->anchors) goto error;` (malloc failure)
> src/dumper.c:143,146,149: `if (!yaml_emitter_emit(...)) goto error;` / `if (!yaml_emitter_dump_node(...)) goto error;`
> src/dumper.c:155-159: `error: yaml_emitter_delete_document_and_anchors(emitter); return 0;`
> ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:255-259: `if (!(yaml_emitter_dump(&emitter, &document) || (yaml_emitter_flush(&emitter) && 0))) { equal = true; break; }` — checks the return value and treats failure as terminating the dump loop (not a fatal fuzzer error).

### P2.5 Cleanup Sequence
**Claim:** On success: emitter must still be finalized with `yaml_emitter_close` (STREAM-END event) then `yaml_emitter_delete`. The `document` passed to `yaml_emitter_dump` must NOT be separately deleted (it is already consumed). Any *other* documents the harness built but did not dump (e.g., copies for comparison) must be freed individually with `yaml_document_delete`. On error: same — `yaml_emitter_dump`'s own error path already deletes the document; the harness should still eventually call `yaml_emitter_close`/`yaml_emitter_delete` on the emitter (or just `yaml_emitter_delete` if never opened) to release emitter buffers/stacks.
**Evidence:**
> src/dumper.c:151/157: document deletion happens unconditionally inside yaml_emitter_dump, both paths.
> src/api.c:391-410ish `yaml_emitter_delete` frees emitter->buffer, raw_buffer, states stack, events queue, indents stack, tag_directives stack (see src/api.c:392 onward).
> ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:267-269: `yaml_parser_delete(&parser); yaml_emitter_close(&emitter); yaml_emitter_delete(&emitter);` done once after the whole multi-document dump loop completes.
> ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:307-309: `for (k...) yaml_document_delete(documents + k);` — only the *copied* documents (never passed to yaml_emitter_dump) are explicitly deleted; `free(out.buf)` releases the custom write-handler's buffer.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase.
**Evidence:**
> include/yaml.h:1803 yaml_emitter_initialize; :1812 yaml_emitter_delete; :1830 yaml_emitter_set_output_string; :1844 yaml_emitter_set_output_file; :1856 yaml_emitter_set_output; :1868 yaml_emitter_set_encoding; :1878 yaml_emitter_set_canonical; :1888 yaml_emitter_set_indent; :1898 yaml_emitter_set_width; :1908 yaml_emitter_set_unicode; :1918 yaml_emitter_set_break; :1935 yaml_emitter_emit; :1948 yaml_emitter_open; :1961 yaml_emitter_close; :1978 yaml_emitter_dump; :1990 yaml_emitter_flush.
> src/api.c:1036 yaml_document_initialize; :1122 yaml_document_delete; :1164 yaml_document_get_node; :1179 yaml_document_get_root_node; :1194 yaml_document_add_scalar; :1244 yaml_document_add_sequence; :1289 yaml_document_add_mapping; :1334 yaml_document_append_sequence_item; :1362 yaml_document_append_mapping_pair.
> src/loader.c:87 yaml_parser_load.
> src/dumper.c:61 yaml_emitter_open def; :85 yaml_emitter_close def; :111 yaml_emitter_dump def; :166 static yaml_emitter_delete_document_and_anchors; :206 static yaml_emitter_anchor_node; :263 static yaml_emitter_dump_node; :300 static yaml_emitter_dump_alias; :315 static yaml_emitter_dump_scalar; :338 static yaml_emitter_dump_sequence (and yaml_emitter_dump_mapping follows at ~368, truncated read but referenced at dumper.c:369 "static int yaml_e...").

### P2.7 Co-call Constraints
**Claim:** Paired lifecycle APIs: `yaml_emitter_initialize` / `yaml_emitter_delete` (must call delete exactly once, after all use); `yaml_emitter_open` / `yaml_emitter_close` (open emits STREAM-START, guarded by `assert(!emitter->opened)`; close emits STREAM-END, guarded by `assert(emitter->opened)` and is idempotent via `if (emitter->closed) return 1;`); `yaml_document_initialize` / `yaml_document_delete` for documents NOT passed to `yaml_emitter_dump` (documents that ARE passed to dump must not also get `yaml_document_delete`, see P2.3); `yaml_parser_initialize` / `yaml_parser_delete` for the loader-side parser used by `yaml_parser_load`. Exactly one `yaml_emitter_set_output*` may be called (enforced by assert).
**Evidence:**
> src/dumper.c:67-68: `assert(emitter); assert(!emitter->opened); /* Emitter should not be opened yet. */` in yaml_emitter_open.
> src/dumper.c:91-94: `assert(emitter->opened); ... if (emitter->closed) return 1;` in yaml_emitter_close — safe to call close multiple times but only after an open.
> src/api.c:461/481/499: `assert(!emitter->write_handler);` — set-output functions are mutually exclusive / single-use.
> ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:222-234, 267-269: shows paired init/delete for both parser and emitter in one harness.

### P2.8 Prerequisite State
**Claim:** Before `yaml_emitter_dump`: emitter must have a write handler installed (directly or via set_output_string/file), because the first thing dump does is call `yaml_emitter_open` (if not already opened) which calls `yaml_emitter_emit`, which eventually reaches the writer that asserts a write handler is set. The `document` must have gone through `yaml_document_initialize` (which sets up the internal `nodes` stack, even if empty) — passing an uninitialized/garbage `yaml_document_t` is undefined behavior since `yaml_emitter_dump` immediately dereferences `document->nodes` via `STACK_EMPTY`.
**Evidence:**
> src/writer.c:37: `assert(emitter->write_handler); /* Write handler must be set. */`
> src/dumper.c:126: `if (STACK_EMPTY(emitter, document->nodes)) { ... }` — dereferences document->nodes immediately, requiring it to have been stack-initialized (e.g. by yaml_document_initialize at src/api.c:1064 `STACK_INIT(&context, nodes, yaml_node_t*)`, or by yaml_parser_load at src/loader.c:94-95 `memset(document,0,...); STACK_INIT(parser, document->nodes, yaml_node_t*)`).

### P2.9 Result Usage
**Claim:** `yaml_emitter_dump` does not return a value object usable by other APIs — its "result" is the serialized bytes written through the configured write handler (into a caller-supplied buffer via `yaml_emitter_set_output_string`, a `FILE*` via `yaml_emitter_set_output_file`, or a custom handler via `yaml_emitter_set_output`). Real callers read that buffer/file afterward, often round-tripping it back through `yaml_parser_set_input_string`/`yaml_parser_load` to validate equivalence, or flush pending bytes with `yaml_emitter_flush`.
**Evidence:**
> tests/run-dumper.c:258: `yaml_emitter_set_output_string(&emitter, buffer, BUFFER_SIZE, &written);` then after the dump loop and `yaml_emitter_close`, lines 289-303: `yaml_parser_set_input_string(&parser, buffer, written); while(!done){ yaml_parser_load(&parser,&document); ... compare_documents(...); }` — the dumped buffer is fed back into the parser/loader and compared to the originals.
> ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:232-233: `yaml_output_buffer_t out = {NULL,0,1000}; yaml_emitter_set_output(&emitter, yaml_write_handler, &out);` custom handler accumulates output in `out.buf`/`out.size`.
> ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:282-299: `yaml_parser_set_input_string(&parser, out.buf, out.size); ... yaml_parser_load(&parser,&document); ... documents_equal(documents+count, &document)` — the emitted bytes (`out.buf`) are re-parsed and compared node-by-node to the pre-dump copies.
> src/dumper.c:149/143 (via yaml_emitter_emit → state machine → writer) ultimately call `emitter->write_handler(...)` (src/writer.c:53,128) which is the real sink of yaml_emitter_dump's output.

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting parameters/flags affecting the dump path: (a) emitter-level options set before dump — `yaml_emitter_set_canonical` (canonical vs block/flow style), `yaml_emitter_set_unicode` (escape vs pass-through non-ASCII), `yaml_emitter_set_width`, `yaml_emitter_set_indent`, `yaml_emitter_set_break`, `yaml_emitter_set_encoding`; (b) document-level per-node fields set via `yaml_document_add_scalar`/`add_sequence`/`add_mapping` — `tag` (NULL → default tag constant, else custom string compared against `YAML_DEFAULT_SCALAR_TAG`/`YAML_DEFAULT_SEQUENCE_TAG`/`YAML_DEFAULT_MAPPING_TAG` to decide `plain_implicit`/`implicit` flags), scalar `style` (`yaml_scalar_style_t`: PLAIN/SINGLE_QUOTED/DOUBLE_QUOTED/LITERAL/FOLDED/ANY), sequence/mapping `style` (BLOCK/FLOW/ANY); (c) document graph shape — repeated node indices passed to `yaml_document_append_sequence_item`/`append_mapping_pair` raise a node's `references` count to 2, which makes `yaml_emitter_anchor_node` assign an anchor id, switching `yaml_emitter_dump_node` onto the alias-emission branch (`yaml_emitter_dump_alias`) for subsequent visits of that node.
**Evidence:**
> src/dumper.c:322-325: `int plain_implicit = (strcmp((char*)node->tag, YAML_DEFAULT_SCALAR_TAG)==0); int quoted_implicit = (strcmp(...)==0);` — tag string directly drives implicit flags fed to SCALAR_EVENT_INIT.
> src/dumper.c:345: `int implicit = (strcmp((char*)node->tag, YAML_DEFAULT_SEQUENCE_TAG)==0);` in yaml_emitter_dump_sequence.
> src/dumper.c:213-237: `yaml_emitter_anchor_node` — `references++`; on first visit (`==1`) recurses into children; on second visit (`==2`) assigns `anchor = ++last_anchor_id`.
> src/dumper.c:275-277: `if (emitter->anchors[index-1].serialized) { return yaml_emitter_dump_alias(emitter, anchor); }` — a node visited again after being serialized is emitted as an alias instead of being redumped.
> include/yaml.h:1878-1909: doc comments for set_canonical/set_unicode describing the behaviour switch.
> ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:217-218,229-230: `bool is_canonical = data[0]&1; bool is_unicode = data[1]&1; ... yaml_emitter_set_canonical(&emitter,is_canonical); yaml_emitter_set_unicode(&emitter,is_unicode);` — fuzz input directly drives these behaviour flags.

### P2.11 Input Expectations
**Claim:** `yaml_emitter_dump` expects: `document->nodes` to be a valid (possibly empty) stack of `yaml_node_t`, the root node (if any) at index 1 (`document->nodes.start+0`); each node's `tag` to be a NUL-terminated, valid-UTF-8 C string (enforced at construction time by `yaml_check_utf8` in `yaml_document_add_scalar/add_sequence/add_mapping`, not re-checked by the dumper itself); scalar node `data.scalar.value` to be a buffer of `data.scalar.length` bytes plus a NUL terminator (the dumper itself does not re-validate UTF-8 or NUL-termination — it trusts the document as built, see below); sequence/mapping child indices (`items`/`pairs`) to be valid 1-based indices within `document->nodes` (asserted only at *construction* time by `yaml_document_append_sequence_item/append_mapping_pair`, not re-checked during dump). No explicit max depth/size limit is enforced by the emitter dump path itself — `yaml_emitter_anchor_node` and `yaml_emitter_dump_node` recurse directly over the document graph with no cycle check beyond the `references`-based anchor/alias switch, and `compare_nodes`/`nodes_equal` helpers in the test/fuzzer use an explicit `level++ > 1000` recursion guard, implying depth is otherwise unbounded on the library side.
**Evidence:**
> src/api.c:1214,1222: `if (!yaml_check_utf8(tag,...)) goto error;` / `if (!yaml_check_utf8(value,length)) goto error;` inside `yaml_document_add_scalar` — tag/value validated once, at node-construction time, not inside the dumper.
> src/api.c:1223-1226: `value_copy = YAML_MALLOC(length+1); memcpy(value_copy,value,length); value_copy[length]='\0';` — scalar value is copied with explicit NUL terminator appended.
> src/api.c:1342-1349 (yaml_document_append_sequence_item) and :1372-1381 (yaml_document_append_mapping_pair): `assert(sequence>0 && document->nodes.start+sequence <= document->nodes.top); assert(... type==YAML_SEQUENCE_NODE); assert(item>0 && ...)` — index validity is an assert-enforced precondition at append time, not inside yaml_emitter_dump.
> src/dumper.c:145-146: `yaml_emitter_anchor_node(emitter, 1); if (!yaml_emitter_dump_node(emitter, 1)) goto error;` — dump always starts from node index 1 (the first/root node pushed).
> src/dumper.c:216-232 / 281-293: unconditional recursion into sequence items / mapping pairs / scalar-vs-sequence-vs-mapping dispatch with no explicit depth bound in library code.
> ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:34-35,85 (run-dumper.c): `if (level++ > 1000) return !equal;` — only the fuzzer/test harness imposes a depth cap, for its own comparison helper, not the library.

## Other entries of the Logic Group

### yaml_document_initialize
**Claim:** Creates/zero-populates a `yaml_document_t`. Params: `version_directive` (NULL or pointer to `{major,minor}`, copied), `tag_directives_start/end` (NULL/NULL or a valid range, each `{handle,prefix}` copied and UTF-8 checked), `start_implicit`/`end_implicit` (0/1 flags echoed into DOCUMENT-START/END events by `yaml_emitter_dump`). Returns 1/0. On success, must eventually be consumed by `yaml_emitter_dump` (which frees it) OR explicitly freed with `yaml_document_delete` if never dumped. Must be called (or `yaml_parser_load` used) before any `yaml_document_add_*`/`append_*` call on that document.
**Evidence:**
> src/api.c:1036-1041 signature; :1059-1062 asserts non-NULL document and valid tag-directive range (`(start&&end)||(start==end)`).
> src/api.c:1081-1089: tag_directives validated via `yaml_check_utf8` and duplicated with `yaml_strdup`.
> tests/run-dumper.c:21-25 and ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:142-147: `copy_document` calls `yaml_document_initialize(document_to, document_from->version_directive, document_from->tag_directives.start, document_from->tag_directives.end, document_from->start_implicit, document_from->end_implicit)` before adding nodes.

### yaml_document_add_scalar
**Claim:** Appends a SCALAR node. Params: `tag` (NULL→`YAML_DEFAULT_SCALAR_TAG`, else UTF-8-checked C string, copied), `value` (non-NULL byte buffer, asserted), `length` (if <0, computed via `strlen`, else explicit byte count — allows embedded NULs when length>=0), `style` (`yaml_scalar_style_t`: PLAIN/SINGLE_QUOTED/DOUBLE_QUOTED/LITERAL/FOLDED/ANY — selects emitted quoting style in `yaml_emitter_dump_scalar`). Returns the new node's 1-based index (>0) on success, 0 on failure (bad UTF-8 or OOM). Must be called after `yaml_document_initialize` on the same document. Does not need separate freeing — node memory is owned by the document and released by `yaml_document_delete` or by `yaml_emitter_dump`'s internal cleanup.
**Evidence:**
> src/api.c:1194-1238 (full body, see earlier reads): `assert(document); assert(value);`; `if (!tag) tag = YAML_DEFAULT_SCALAR_TAG;`; `if (!yaml_check_utf8(tag,...)) goto error;`; `if (length<0) length=strlen(...);`; `if (!yaml_check_utf8(value,length)) goto error;`; returns `document->nodes.top - document->nodes.start` (index) or 0 on error.
> src/dumper.c:327-329: `SCALAR_EVENT_INIT(event, anchor, node->tag, node->data.scalar.value, node->data.scalar.length, plain_implicit, quoted_implicit, node->data.scalar.style, mark, mark);` — consumes tag/value/length/style exactly as stored.

### yaml_document_add_sequence / yaml_document_add_mapping
**Claim:** Append a SEQUENCE or MAPPING node (empty `items`/`pairs` stack). Params: `tag` (NULL→default sequence/mapping tag, else UTF-8-checked custom string), `style` (`yaml_sequence_style_t`/`yaml_mapping_style_t`: BLOCK/FLOW/ANY — selects collection layout in `yaml_emitter_dump_sequence`/`dump_mapping`). Returns new node's 1-based index, or 0 on error. Children are attached afterward via `yaml_document_append_sequence_item`/`append_mapping_pair`.
**Evidence:**
> src/api.c:1244-1283 (add_sequence): `if (!tag) tag = YAML_DEFAULT_SEQUENCE_TAG;`, `STACK_INIT(&context,items,...)`, returns index.
> src/api.c:1289-1328 (add_mapping): analogous with `YAML_DEFAULT_MAPPING_TAG` and `pairs` stack.
> src/dumper.c:345,349-350 and (mapping, truncated read) analogous `implicit` strcmp against the matching default tag constant, feeding `SEQUENCE_START_EVENT_INIT`/`MAPPING_START_EVENT_INIT`.

### yaml_document_append_sequence_item / yaml_document_append_mapping_pair
**Claim:** Link existing node indices as children. `append_sequence_item(document, sequence, item)`: `sequence` must be a valid 1-based index of a node already of type `YAML_SEQUENCE_NODE`; `item` must be a valid 1-based index of any existing node (can repeat an index already used elsewhere, which is how shared/aliased nodes are created). `append_mapping_pair(document, mapping, key, value)`: `mapping` must index a `YAML_MAPPING_NODE`; `key`/`value` any valid existing node indices. All validity is `assert`-checked (so malformed/out-of-range indices are undefined behavior / abort in debug builds — a harness constructing documents by hand must keep indices in range `[1, nodes.top-nodes.start]`). Both return 1/0 (0 only on allocation failure of the underlying item/pair stack push).
**Evidence:**
> src/api.c:1334-1356 (append_sequence_item): asserts shown above; `PUSH(&context, document->nodes.start[sequence-1].data.sequence.items, item)`.
> src/api.c:1362-1391 (append_mapping_pair): analogous asserts and `pair.key=key; pair.value=value; PUSH(...)`.
> src/dumper.c:217-221 / 223-228 (yaml_emitter_anchor_node) and :353-356/ (dump_sequence loop) / dump_mapping loop: these indices are exactly what gets walked and what feeds the reference-counting anchor logic — repeating an index across two `append_*` calls is what pushes `references` to 2 and forces an anchor+alias pair in the dumped output.

### yaml_parser_load
**Claim:** Alternative/realistic document source: parses the next YAML document from an already-initialized `yaml_parser_t` (which must have its input set via `yaml_parser_set_input_string`/`yaml_parser_set_input_file`/`yaml_parser_set_input`) into a caller-supplied, NOT-yet-initialized `yaml_document_t` (the function itself does `memset(document,0,...)` then `STACK_INIT`s it — so the caller must NOT call `yaml_document_initialize` first). Returns 1 on success (including the "no more documents" / STREAM-END case, where `yaml_document_get_root_node` returns NULL) and 0 on parse/composer error (in which case it has already called `yaml_document_delete(document)` internally). Must be called in a loop until `yaml_document_get_root_node` returns NULL (end of stream) or load fails; each successfully produced document with a root node is a candidate to pass into `yaml_emitter_dump` (directly, or via a `copy_document` duplication if the original must be retained for later comparison, since dump consumes it).
**Evidence:**
> src/loader.c:87-132 full body: `memset(document,0,sizeof(yaml_document_t)); STACK_INIT(parser,document->nodes,...)`; on error path `yaml_document_delete(document); parser->document=NULL; return 0;`.
> src/loader.c:94: confirms it does its own init, contradicting re-use of `yaml_document_initialize` on the same object beforehand.
> tests/run-dumper.c:263-278 and ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:236-265: both loop `while(!done){ yaml_parser_load(&parser,&document); done = !yaml_document_get_root_node(&document); if(!done){ copy_document(...); yaml_emitter_dump(&emitter,&document); } else { yaml_document_delete(&document); } }` — the no-root-node (end) branch is the one place the raw `document` from load is explicitly deleted (not dumped), confirming the dump call itself must consume any document that does have a root.

## Build notes
**Claim:** Fuzzers are C (compiled with `$CC`/`$CFLAGS`, not C++, though linked with `$CXX`), placed in `$SRC/*_fuzzer.c`, compiled with `-I $SRC -Iinclude` (so `#include "yaml.h"` resolves to `libyaml/include/yaml.h`), and linked against the static library `src/.libs/libyaml.a` plus `$LIB_FUZZING_ENGINE`. The project is built first via autotools (`./bootstrap; ./configure; make`). An existing reference fuzzer for exactly this target (`libyaml_dumper_fuzzer.c`) already exists in the OSS-Fuzz source directory and demonstrates the correct full lifecycle (parser → document → emitter dump → round-trip verification).
**Evidence:**
> ossfuzz_view/libyaml-c3/build.sh:17-33: `./bootstrap; ./configure; make "-j$(nproc)"` then for each `$SRC/*_fuzzer.c`: `$CC $CFLAGS -I $SRC -Iinclude -c $fuzzer -o $fuzzer_basename.o` and `$CXX $CXXFLAGS -std=c++11 $fuzzer_basename.o -o $OUT/$fuzzer_basename $LIB_FUZZING_ENGINE src/.libs/libyaml.a`.
> ossfuzz_view/libyaml-c3/libyaml_dumper_fuzzer.c:15-22: includes `"yaml.h"` and `"yaml_write_handler.h"` (the latter also in `$SRC`, picked up via `-I $SRC`), plus standard C headers (`stdbool.h`, `stdint.h`, etc.) — confirms the fuzzer is written in C using the installed public header `yaml.h`, not any internal header.
> ossfuzz_view/libyaml-c3/build.sh:35-36: seed corpus and `.options` file (`libyaml_fuzzer.options`) copied per fuzzer binary, and `yaml.dict` copied to `$OUT` for all fuzzers — these are shared auxiliary files, same pattern should apply to a dumper-target harness.