## target: simdjson::dom::parser::parse

### P2.1 Init Sequence
**Claim:** No mandatory global init call. A `simdjson::dom::parser` object must simply be default-constructed (or constructed with a `max_capacity`); no separate "open"/"init" API is required before calling `parse`. Internally `parse()` calls `ensure_capacity()`/`allocate()` lazily on first use if the parser's implementation pointer is null or too small.
**Evidence:**
> include/simdjson/dom/parser-inl.h:23-26: `parser::parser(size_t max_capacity) noexcept : _max_capacity{max_capacity}, loaded_bytes(nullptr) {}` — trivial constructor, no required follow-up call.
> include/simdjson/dom/parser-inl.h:178-180: `parser::parse(buf,len,realloc_if_needed) { return parse_into_document(doc, buf, len, realloc_if_needed); }` calls `parse_into_document` which itself calls `ensure_capacity(provided_doc, len)` at line 117 before touching `buf`.
> include/simdjson/dom/parser-inl.h:271-283 `parser::allocate(capacity,max_depth)`: lazily creates `implementation` via `simdjson::get_active_implementation()->create_dom_parser_implementation(...)` if not yet created — this is exactly what `ensure_capacity` triggers, so the harness does **not** need to call `allocate()` itself unless it wants to preset capacity/depth (see P2.10).
> fuzz/fuzz_atpointer.cpp:24-27: real caller just does `simdjson::dom::parser parser; ... parser.parse(strings[0]);` with no prior init call.

### P2.2 Parameter Construction
**Claim:** `buf` is a raw byte pointer (no required null-termination), `len` is its exact byte length, `realloc_if_needed` (default `true`) selects whether the parser copies `buf` into its own padded buffer or uses `buf` directly. If `realloc_if_needed==false`, caller MUST provide at least `SIMDJSON_PADDING` extra allocated bytes after `buf+len` (uninitialized is fine, but must be addressable/allocated).
**Evidence:**
> singleheader/simdjson.h:6206-6224 (doc comment on `parse`): "The buffer must have at least SIMDJSON_PADDING extra allocated bytes... If realloc_if_needed is true (the default), it is assumed that the buffer does *not* have enough padding, and it is copied into an enlarged temporary buffer before parsing... If you set realloc_if_needed to false... you must provide a buffer with at least SIMDJSON_PADDING extra bytes at the end."
> include/simdjson/dom/parser-inl.h:119-130: when `realloc_if_needed` is true, parser allocates/reuses `loaded_bytes` (`internal::allocate_padded_buffer(len)`) and `memcpy`s `buf` into it, then repoints `buf` at the padded copy.
> include/simdjson/internal/dom_parser_implementation.h:60: "`buf` ... *MUST* be allocated up to len + SIMDJSON_PADDING bytes" (doc on the core `dom_parser_implementation::parse`, which is what the padded path ultimately calls).
> A fuzz harness must NOT pass arbitrary unpadded attacker buffers with `realloc_if_needed=false`; use the default `true` (safe) or go through `padded_string`/`padded_string_view` overloads, or deliberately use `parse_unpadded` (see separate entry) which is the explicitly-supported unpadded path.

### P2.3 Object Lifecycle
**Claim:** Create parser (stack or heap) → optionally `allocate(capacity,max_depth)` → call `parse()` (which internally does ensure_capacity → copy/strip-BOM → `implementation->parse` stage1+stage2) → use returned `element` (and/or the parser's internal `document`) → parser/document destructors are default/no manual free needed. The returned `element`'s lifetime is tied to the parser: must be consumed/discarded before the parser is destroyed or `parse()` is called again.
**Evidence:**
> include/simdjson/dom/parser-inl.h:114-141 `parse_into_document`: ensure_capacity → (optional memcpy) → strip BOM (lines 132-135) → `implementation->parse(buf,len,provided_doc)` → return `provided_doc.root()`.
> singleheader/simdjson.h:6196-6204: "IMPORTANT: Document Lifetime ... you *must* use the document before you destroy the parser or call parse() again."
> include/simdjson/dom/parser.h:60: `~parser()=default;` — RAII, no explicit close/free call.
> include/simdjson/dom/document.h:24: `~document() noexcept = default;` — same for `document`.

### P2.4 Return Value Handling
**Claim:** `parse()` returns `simdjson_result<element>`, a pair of `(element, error_code)`. Callers must check the error before using the element, either via `.get(element)` (returns `error_code`), `.error()`, or (if exceptions enabled) implicit conversion that throws `simdjson_error`. Possible errors include `MEMALLOC`, `CAPACITY`, `DEPTH_ERROR`, `UTF8_ERROR`, `TAPE_ERROR`, and other JSON-syntax errors documented in error.h.
**Evidence:**
> include/simdjson/error.h:100 `error_code error() const noexcept { return _error; }` and lines 168,173,201 (the `.get()`, `.error()`, `.value()` accessors of `simdjson_result<T>`).
> include/simdjson/error.h:21-31: enumerators `CAPACITY`, `MEMALLOC`, `DEPTH_ERROR`, `UTF8_ERROR`.
> fuzz/fuzz_atpointer.cpp:27-33: `auto res = parser.parse(strings[0]); if (res.error()) return 0; simdjson::dom::element root; if (res.get(root)) return 0;` — canonical non-throwing check-then-use pattern.
> fuzz/fuzz_element.cpp:44-49: `auto error = parser.parse(strings[1]).get(elem); if (error) return 0;` — same idiom used before any further element API calls.

### P2.5 Cleanup Sequence
**Claim:** No explicit free/close function exists or is needed for `parser`, `document`, or `element` — all are RAII types (`std::unique_ptr`-managed internals). On error, simply don't use the (unset/garbage) `element`; on success, use the element before the parser goes out of scope or is reused by another `parse()`/`parse_many()` call (which invalidates previous elements).
**Evidence:**
> include/simdjson/dom/parser.h:60: `~parser()=default;`
> include/simdjson/dom/document.h:24: `~document() noexcept = default;`
> include/simdjson/dom/parser.h:667: `std::unique_ptr<internal::dom_parser_implementation> implementation{};` — implementation freed automatically with the parser.
> singleheader/simdjson.h:6202-6204: moving the parser is safe but invalidates prior `element`s — reinforces no manual free, but a definite invalidation rule to respect in the harness (don't keep elements across repeated parse calls).

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase at the given paths (verified via Grep/Read, not fabricated): `parser::parse`, `parser::parse_unpadded`, `parser::parse_into_document`, `parser::parse_into_document_unpadded`, `parser::parse_many` (5 overloads + format overloads), `parser::allocate`, `parser::ensure_capacity` (two overloads), `element::at_pointer`, `dom_parser_implementation::parse/stage1/stage2/stage2_next`.
**Evidence:**
> include/simdjson/dom/parser-inl.h:114 (`parse_into_document`), 144 (`parse_into_document_unpadded`), 178 (`parse`), 195 (`parse_unpadded`), 205-257 (`parse_many` overloads), 271 (`allocate`), 292/297 (`ensure_capacity`).
> include/simdjson/dom/element-inl.h:427 (`element::at_pointer`).
> include/simdjson/internal/dom_parser_implementation.h:64 (`parse`), 80 (`stage1`), 94 (`stage2`), ~101-107 (`stage2_next`, confirmed via Grep at document_stream-inl.h:251,269 and generic/dom_parser_implementation.h:40,42).

### P2.7 Co-call Constraints
**Claim:** No lock/unlock-style pairing. The only "paired" relationship is: (a) `allocate(capacity,max_depth)` is optionally paired with later `parse()` calls that rely on the preset capacity (not a strict pre-requisite, since `parse` self-allocates via `ensure_capacity`); (b) `parse_many`/`load_many` produce a `document_stream` that must be iterated (range-for) to actually run stage2 via `stage2_next` for each sub-document — the stream iterator itself is the "closing" step, there is no separate close call.
**Evidence:**
> include/simdjson/dom/parser-inl.h:117-118 `ensure_capacity(provided_doc, len)` called unconditionally inside `parse_into_document`, independent of whether `allocate()` was called beforehand.
> include/simdjson/dom/document_stream-inl.h:251,269: `parser->implementation->stage2_next(parser->doc)` invoked from the stream's iterator/next() logic — i.e., `parse_many()` alone does not run stage2 for each doc; the for-loop over the returned stream does.
> fuzz/fuzz_ndjson.cpp:13-21: `parser.parse_many(json,batch_size).get(docs); for (auto doc : docs) { ... }` — demonstrates the mandatory iteration-to-consume pattern.

### P2.8 Prerequisite State
**Claim:** Only state needed is a constructed `dom::parser` (and, for `parse_into_document`, a `dom::document` object supplied by the caller). No file descriptors, sockets, or global library state must be set up beyond process-wide SIMD implementation auto-detection (`simdjson::get_active_implementation()`), which is self-initializing and requires no explicit call from the harness.
**Evidence:**
> include/simdjson/dom/parser-inl.h:279: `simdjson::get_active_implementation()->create_dom_parser_implementation(capacity, max_depth, implementation)` — called transparently inside `allocate()`/`ensure_capacity()`, no harness-visible init call required.
> include/simdjson/dom/parser.h:30-60: `parser` class has a default and max_capacity constructor only; no other setup method is mandatory.

### P2.9 Result Usage
**Claim:** The `element` produced by `parse()` is consumed by: (1) `element::at_pointer(json_pointer)` for JSON-pointer lookups, returning another `simdjson_result<element>`; (2) typed getters/checkers (`get_array`, `get_object`, `get_string`, `get_int64`, `get_double`, `get_bool`, `is_*`, templated `get<T>()`/`is<T>()`/`tie()`); (3) iteration via `operator[]`, `begin()/end()` over `dom::object`/`dom::array` obtained from the element; (4) serialization via `operator<<` (stream insertion) or `simdjson::minify`.
**Evidence:**
> fuzz/fuzz_atpointer.cpp:27-45: `auto res = parser.parse(strings[0]); ... root.at_pointer(strings[1]); ... leaf.get_string().get(sv);` — parse → at_pointer → get_string chain.
> fuzz/fuzz_element.cpp:44-93: `parser.parse(strings[1]).get(elem); ... elem.get_array(); elem.get_object(); elem.get_int64(); elem.is<T>(); elem.get<T>(); elem.tie(t,ec);` — many typed consumers of the parsed element.
> tests/dom/unpadded_tests.cpp:24-27,37,43,52-53: `std::ostringstream out; out << e;` (`to_text`) — stream-insertion serialization consumer, used to compare two parses' output.
> singleheader/simdjson.h:25-31 (doc example in simdjson.h top comment, mirrored at include/simdjson.h:25-31): `for (simdjson::dom::object obj : parser.parse(abstract_json)) { for(const auto key_value: obj) {...} }` — range-for iteration directly over the parsed element treated as an object.

### P2.10 Behaviour Controls
**Claim:** Key behaviour-selecting parameters/flags:
- `realloc_if_needed` (bool, default true) on `parse`/`parse_into_document`: true = copy into internally-managed padded buffer (safe for any input, handles non-padded attacker buffers); false = parse in place, requires caller-supplied padding.
- `parse_unpadded` (separate entry point, no realloc flag): forces `_unpadded=true` on the implementation, selecting length-aware/bounded string-unescape, atom and near-end-of-buffer number-parsing code paths in stage2 instead of the padded fast path.
- `allocate(capacity, max_depth)`: presets buffer capacity and max nesting depth (`DEFAULT_MAX_DEPTH=1024`); if a document's nesting exceeds `max_depth`, stage2 returns `DEPTH_ERROR`; if `len > max_capacity`, `ensure_capacity` returns `CAPACITY` without even invoking the implementation.
- `parser::set_max_capacity(size_t)`: hard ceiling (`_max_capacity`) that `ensure_capacity` checks before trying to grow; MINIMAL_DOCUMENT_CAPACITY=32 is the floor.
- `parse_many`'s `stream_format` parameter (`whitespace_delimited` default, `comma_delimited_array`, `comma_delimited`, etc.) and `batch_size` (default `DEFAULT_BATCH_SIZE`, floor `MINIMAL_BATCH_SIZE`) select the concatenated-document framing and streaming granularity, driving `stage1_mode::streaming_partial/streaming_final` in the implementation.
- Leading UTF-8 BOM (`EF BB BF`) is auto-stripped by both `parse_into_document` and `parse_many` before stage1/stage2 ever see the bytes.
**Evidence:**
> include/simdjson/dom/parser-inl.h:119-130 (`realloc_if_needed` branch), 144-165 (`parse_into_document_unpadded` sets `implementation->_unpadded = true;` at line 158, restores to false at 160).
> include/simdjson/dom/parser-inl.h:271-283 (`allocate`), 297-317 (`ensure_capacity` checking `desired_capacity > max_capacity()` → `CAPACITY` at line 309, calling `target_document.allocate`/`allocate(desired_capacity, max_depth())` at 311-312).
> include/simdjson/base.h:44 `constexpr size_t DEFAULT_MAX_DEPTH = 1024;`; include/simdjson/dom/base.h:28 `MINIMAL_DOCUMENT_CAPACITY = 32`.
> include/simdjson/dom/parser-inl.h:205-245 (`parse_many` overloads, `stream_format format`, `comma_delimited_array` handling stripping `[`/`]` at 227-244); 221-226 BOM strip; 132-135 BOM strip in `parse_into_document`.
> include/simdjson/internal/dom_parser_implementation.h:22-27 `enum class stage1_mode { regular, streaming_partial, streaming_final, json_sequence_partial, json_sequence_final, comma_delimited_partial, comma_delimited_final };` and `is_streaming()` helper at 32-37.

### P2.11 Input Expectations
**Claim:** `buf` is treated as raw untrusted bytes (not necessarily NUL-terminated for `uint8_t*`/`char*` overloads — length is explicit via `len`). It need not be valid JSON at all: any syntax/encoding problem surfaces as an `error_code`, never UB in the public contract, provided padding rules are honored. Required: `len`-byte buffer; if `realloc_if_needed==false`, at least `SIMDJSON_PADDING` extra allocated/readable bytes past `buf+len` are mandatorily required (they will be read, uninitialized is OK but must be allocated memory, not past-the-end of a true allocation). Optional leading 3-byte UTF-8 BOM `EF BB BF` is detected and skipped. Internally stage1 requires input to be valid UTF-8 (else `UTF8_ERROR`); stage2 enforces `max_depth` on nesting (else `DEPTH_ERROR`).
**Evidence:**
> include/simdjson/internal/dom_parser_implementation.h:60-62: "`buf` ... *MUST* be allocated up to len + SIMDJSON_PADDING bytes. `len` The length of the json document."
> include/simdjson/dom/parser-inl.h:132-135: `if((len >= 3) && (std::memcmp(buf, "\xEF\xBB\xBF", 3) == 0)) { buf += 3; len -= 3; }` — BOM stripping before parse.
> tests/dom/unpadded_tests.cpp:1-9 (file header comment): "parse_unpadded: parsing JSON in place from a buffer that has NO trailing SIMDJSON_PADDING ... must never read past buf+len" — documents the strict bound for the unpadded variant, contrasted with the normal padded requirement.
> include/simdjson/error.h:24,31: `DEPTH_ERROR`, `UTF8_ERROR` as explicit error codes returned on malformed/over-nested/invalid-UTF8 input rather than crashing.

## Other entries of the Logic Group

### simdjson::dom::parser::parse_unpadded
**Claim:** Public overload set (`(const uint8_t*,len)`, `(const char*,len)`, `(string_view)`) that calls `parse_into_document_unpadded(doc, buf, len)` internally. No realloc flag — always parses in place and never copies. Needs only a constructed `parser` beforehand (same as `parse`). Returns `simdjson_result<element>` with identical error-handling discipline. Forces `implementation->_unpadded = true` for the duration of the call, restored to `false` afterward — so interleaving `parse()` and `parse_unpadded()` on the same parser is safe. Combines with the target by giving the harness a second code path (bounded stage2) over the *same* fuzz bytes without needing to manage padding itself.
**Evidence:**
> include/simdjson/dom/parser-inl.h:195-203 (`parse_unpadded` overloads); 144-165 (`parse_into_document_unpadded`, setting/restoring `_unpadded`).
> tests/dom/unpadded_tests.cpp:41-58: real caller pattern — `dom::parser unpadded_parser; dom::element unpadded_elt; auto unpadded_err = unpadded_parser.parse_unpadded(exact.data(), exact.size()).get(unpadded_elt);` then compares error and serialized output against the padded `parse()` result.

### simdjson::dom::parser::parse_into_document
**Claim:** Public overload (`document& doc, const uint8_t*/char*/std::string&/padded_string&, len, realloc_if_needed=true`) that `parse()` itself calls with the parser's own internal `doc`. Lets the harness supply its own `dom::document` object (constructed by the caller, lifetime managed by the caller) instead of reusing the parser's; `ensure_capacity(provided_doc, len)` grows **both** `provided_doc` and the parser's `implementation` as needed. Returns `simdjson_result<element>` rooted in `provided_doc`. Same realloc/BOM/error semantics as `parse`. Must be followed by use of the returned element before `provided_doc` is reused/destroyed or the parser's `implementation` is reallocated by another `parse_into_document`/`parse` call on a bigger document.
**Evidence:**
> include/simdjson/dom/parser-inl.h:114-142 (definition, `ensure_capacity`, BOM strip, `implementation->parse(buf,len,provided_doc)`, `return provided_doc.root();`).
> tests/dom/basictests.cpp:522,528,535: `element doc_root = parser.parse_into_document(doc, input);` — real caller pattern reusing one externally-declared `document doc` across multiple parses.
> singleheader/simdjson.h:6366-6368 (doc comment): shows repeated `parser.parse_into_document(doc, buf1, len)` calls reusing the same caller-owned `document`.

### simdjson::dom::parser::parse_many
**Claim:** Public overload set (`buf/len` variants of `uint8_t*`, `char*`, `std::string`, `padded_string`, `padded_string_view`, each with `batch_size=DEFAULT_BATCH_SIZE` and optional `stream_format format=whitespace_delimited`). Treats input as a concatenation of JSON documents; returns `simdjson_result<document_stream>`. Needs only a constructed `parser` first (same prerequisite as `parse`). The returned `document_stream` **must be iterated** (range-for) — each iteration step internally invokes `implementation->stage2_next(parser->doc)`, i.e. stage2 work is deferred until consumption, unlike `parse()` which eagerly completes both stages. `batch_size` is clamped up to `MINIMAL_BATCH_SIZE` if smaller. `stream_format::comma_delimited_array` additionally strips whitespace and a leading `[`/trailing `]` before delegating to `comma_delimited` framing; malformed brackets yield `TAPE_ERROR` immediately (before any stage1/stage2 call). Combines with the target by exercising the streaming stage1_mode branches (`streaming_partial`/`streaming_final`) not reachable from a single `parse()` call, on the same parser type.
**Evidence:**
> include/simdjson/dom/parser-inl.h:205-257 (all `parse_many` overloads, BOM strip at 223-226, `comma_delimited_array` handling at 227-244, `return document_stream(*this, buf, len, batch_size, format);` at 245).
> include/simdjson/dom/document_stream-inl.h:251,269 `stage2_next` calls driven by stream iteration (confirmed via Grep; not re-quoted in full here but file/line verified).
> fuzz/fuzz_ndjson.cpp:9-32: real caller — `parser.parse_many(json,batch_size).get(docs); for (auto doc : docs) { total_count1++; bool_count1 += doc.is_bool(); }` then repeats the whole parse_many+iterate cycle again on the same parser to check idempotency.

### simdjson::dom::parser::allocate
**Claim:** `error_code allocate(size_t capacity, size_t max_depth=DEFAULT_MAX_DEPTH)` — configuration call, optional before `parse()`/`parse_many()` (both self-invoke it via `ensure_capacity` if not pre-called or too small). Presets the parser's internal `dom_parser_implementation` capacity and max nesting depth. Must be called on an already-constructed `parser`; no other prerequisite. Returns `error_code` (`SUCCESS` or allocation/capacity failure) — harness should check it if called explicitly. Does not need an explicit paired "deallocate": freed with the parser's destructor. Using `allocate()` with a small `capacity`/`max_depth` before calling `parse()` on larger/deeper fuzz input is how the harness can deterministically trigger `CAPACITY`/`DEPTH_ERROR` paths.
**Evidence:**
> include/simdjson/dom/parser-inl.h:270-283 (`allocate` definition, lazy `create_dom_parser_implementation` if `implementation` is null, else `implementation->allocate(capacity, max_depth)`).
> tests/dom/readme_examples.cpp:441: `auto error = parser.allocate(1000*1000); // This allocates enough capacity to handle documents <= 1MB` — real caller pattern, checking the returned `error_code`.
> include/simdjson/dom/parser-inl.h:307-316 (`ensure_capacity` calling `allocate(desired_capacity, max_depth())` automatically when capacity is insufficient — so explicit `allocate()` in the harness is optional but gives deterministic control of CAPACITY/DEPTH_ERROR thresholds).

### simdjson::dom::element::at_pointer
**Claim:** Consumer of the target's result, not a separate lifecycle entry — takes `std::string_view json_pointer` (RFC 6901 JSON Pointer syntax) and an already-parsed `element` (`*this`), dispatching to `object::at_pointer`/`array::at_pointer` or returning the element itself (empty pointer) / `NO_SUCH_FIELD` / `INVALID_JSON_POINTER`. Requires a successfully-parsed, still-live `element` (prerequisite: `parse()`/`parse_into_document()` succeeded and the owning parser/document has not been destroyed or re-parsed). Returns `simdjson_result<element>` — same check-then-use discipline as `parse()`. No separate cleanup.
**Evidence:**
> include/simdjson/dom/element-inl.h:427-446 (full definition: `switch(tape.tape_ref_type())`, `START_OBJECT`→`object(tape).at_pointer(...)`, `START_ARRAY`→`array(tape).at_pointer(...)`, default branch validates pointer well-formedness else `NO_SUCH_FIELD`/`INVALID_JSON_POINTER`, empty pointer returns a copy of `*this`).
> include/simdjson/dom/element-inl.h:428: `SIMDJSON_DEVELOPMENT_ASSERT(tape.usable());` — development-mode assertion that the element's tape is from a still-valid parse (referencing simdjson issue #1914), reinforcing that `at_pointer` must be called on a live, successfully-parsed element.
> fuzz/fuzz_atpointer.cpp:35-41: `auto maybe_leaf = root.at_pointer(strings[1]); if (maybe_leaf.error()) return 0; simdjson::dom::element leaf; if (maybe_leaf.get(leaf)) return 0;` — canonical combined usage: `parse()` → `at_pointer()` → typed getter, all error-checked.

## Build notes
**Claim:** Fuzz harnesses for simdjson are C++ (compiled as part of the normal CMake build, linked against the `simdjson` static library), including the top-level public header `simdjson.h` found via the `include/` directory (not the raw `src/` internals, and not necessarily the amalgamated `singleheader/simdjson.h`, though that header is equivalent/public too). OSS-Fuzz's `build.sh` simply delegates to `fuzz/ossfuzz.sh`, which configures CMake with `-DSIMDJSON_ENABLE_FUZZING=On` and builds target `all_fuzzers`, then copies `fuzz/fuzz_*` binaries to `$OUT`.
**Evidence:**
> ${QF_WORK}/ossfuzz_view/simdjson-c3/build.sh:18: `fuzz/ossfuzz.sh`.
> src/simdjson-c3/simdjson/fuzz/ossfuzz.sh:26-40: `cmake .. -GNinja ... -DSIMDJSON_ENABLE_FUZZING=On ... -DSIMDJSON_FUZZ_LDFLAGS=$LIB_FUZZING_ENGINE`; `cmake --build . --target all_fuzzers all_tests`; `cp fuzz/fuzz_* $OUT`.
> src/simdjson-c3/simdjson/fuzz/CMakeLists.txt:37-44: `add_library(simdjson-fuzzer INTERFACE); target_link_libraries(simdjson-fuzzer INTERFACE simdjson); ... target_link_libraries(simdjson-fuzzer INTERFACE ${SIMDJSON_FUZZ_LDFLAGS});` — each `fuzz_*.cpp` (e.g. `implement_fuzzer(fuzz_atpointer)` etc. at lines 57-69) is an `add_executable` linked against `simdjson-fuzzer` (hence the `simdjson` lib target).
> src/simdjson-c3/simdjson/CMakeLists.txt:285-289: `simdjson_add_props(target_include_directories PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>" PRIVATE "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>")` — public include path is `include/`, so a harness does `#include "simdjson.h"` resolving to `include/simdjson.h`.
> src/simdjson-c3/simdjson/fuzz/fuzz_atpointer.cpp:1-2: `#include "FuzzUtils.h"` then `#include "simdjson.h"` — real harness include pattern confirming `simdjson.h` (not a deep path) is the public entry header.