## target: simdjson::minify<T>

### P2.0 Main path to keep (every input) — read first
**Claim:** Per input: split with `FuzzData::splitIntoStrings()` (JSON doc / JSON pointer / JSONPath) → `dom::parser::parse(doc)` → `minify(doc)` → minify the array or object and up to 16 children / field values → `doc.at_pointer(ptr)` and `doc.at_path(path)` → minify the reached element → re-parse the minified text with a second parser and minify again (plus the `simdjson_result<T>` overload under exceptions). Mode selectors that send most inputs to other paths, and `parse_many`, are not part of the harness (`parse_many` hangs on small batch sizes).
**Rule for the harness:** keep every step of this main path on every input, fed with the full fuzz input; Logic-Group entries and the other sections of this report only add calls on top of it (selected by spare input bits), they never replace or dilute a main-path step.
**Evidence:**
> include/simdjson/dom/serialization.h:274 `template <class T> std::string minify(T x) { return to_string(x); }`; 277 `minify(simdjson_result<T> x)`; 303/311 `prettify`.
> include/simdjson/dom/element-inl.h:317 `element::get_array()`, 326 `element::get_object()`, 427 `element::at_pointer(std::string_view)`, 461 `element::at_path(std::string_view)`; include/simdjson/jsonpathutil.h:58 `json_path_to_pointer_conversion` (used by at_path, element-inl.h:134).
> tests/dom/pointercheck.cpp:95-97 `error = doc.at_pointer(json_pointer).get(answer); ... std::string str_answer = simdjson::minify(answer);` and tests/dom/json_path_tests.cpp:105-110 `error = doc.at_path(json_path).get(answer); ... simdjson::minify(answer);` — the project's own tests minify navigated sub-elements.
> tests/minify_tests.cpp:50-62: `load_minify` parses, `minify(doc)`, parses the result again and checks that minifying it again gives the same string.
> fuzz/FuzzUtils.h:26-42 (`split` on "\n~~\n") and 117-126 (`splitIntoStrings`).
> include/simdjson/dom/document_stream-inl.h:192-202: `error = run_stage1(*parser, batch_start); while(error == EMPTY) { batch_start = next_batch_start(); if (batch_start >= len) return; error = run_stage1(...); }` and 254-255 — the loop in which all 67 timeouts of the previous LG harness were stuck (with batch_size 0..1000 and non-default stream_format).


### Overview / template instantiations
`simdjson::minify<T>` (singleheader/simdjson.h:8312, == src tree `include/simdjson/dom/serialization.h` merged into the singleheader) is a thin wrapper: `template <class T> std::string minify(T x) { return to_string(x); }`. It is typically instantiated with `T = simdjson::dom::element` (and, under `SIMDJSON_EXCEPTIONS`, `T = simdjson_result<dom::element>`/`simdjson_result<object>`/`simdjson_result<array>` via the overload at singleheader/simdjson.h:8315). `to_string<T>` (singleheader/simdjson.h:8285) builds an `internal::string_builder<mini_formatter> sb`, calls `sb.append(x)`, and returns a `std::string` copy of `sb.str()`.

### P2.1 Init Sequence
**Claim:** Before `minify(x)` can be called meaningfully, a `simdjson::dom::parser` must have parsed JSON bytes into a `dom::element`/`dom::object`/`dom::array` (or `simdjson_result<...>` of those) — this builds the tape that `minify` walks. No other library-level global init is required (the parser self-initializes lazily via `allocate()`/`parse()`); `simdjson::dom::parser parser;` default-constructs with zero capacity and allocates automatically on `parse()`.
**Evidence:**
> simdjson/singleheader/simdjson.h:6076: `simdjson_inline explicit parser(size_t max_capacity = SIMDJSON_MAXSIZE_BYTES) noexcept;` — construct with zero initial capacity
> simdjson/singleheader/simdjson.h:6267: `simdjson_result<element> parse(const uint8_t *buf, size_t len, bool realloc_if_needed = true) & noexcept;`
> simdjson/tests/minify_tests.cpp:52-59: `simdjson::dom::parser parser; ... parser.load(filename).get(doc); ... auto serial1 = simdjson::minify(doc);`
> simdjson/singleheader/simdjson.h:8278-8293: `to_string`/`minify` comment example: `dom::parser parser; element doc = parser.parse(...); cout << minify(doc) << endl;`

### P2.2 Parameter Construction
**Claim:** `minify<T>` takes its argument **by value** (`T x`), where `T` is a lightweight handle type (`dom::element`, `dom::object`, `dom::array`, or a `simdjson_result<>` of one of those) that internally just wraps a `tape_ref` (pointer+index into the parser's still-alive tape/string buffers). The caller must not destroy or re-use (`parse()` again on) the parser before calling `minify`, since the element only references the parser's internal tape storage, not an owned copy.
**Evidence:**
> simdjson/singleheader/simdjson.h:6104-6112 (`parser::load` doc comment): "the JSON document still lives in the parser ... you *must* use the document before you destroy the parser or call parse() again."
> simdjson/singleheader/simdjson.h:5682-5701: `class tape_ref { ... tape_ref_type(); matching_brace_index(); get_string_view(); ... }` — element/object/array are thin tape_ref wrappers.
> simdjson/singleheader/simdjson.h:8315-8320: `simdjson_result<T>` overload unwraps via `x.error()`/`x.value()` before delegating to `to_string(x.value())`, i.e. error must be checked/propagated first (throws `simdjson_error` if `SIMDJSON_EXCEPTIONS`).

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: construct `dom::parser` → `parse`/`parse_into_document`/`load`/`parse_many` (produces `element`/`document_stream`) → pass the resulting element (or each `document_stream` element) to `minify`/`to_string`/`prettify` → use the returned `std::string` → `string_builder` and parser clean up automatically (no explicit destroy call needed; `string_builder` is a local stack object created fresh inside `to_string`/`minify`/`prettify` each call, RAII-cleaned on return).
**Evidence:**
> simdjson/singleheader/simdjson.h:8289-8292 (`to_string`): `internal::string_builder<> sb; sb.append(x); std::string_view answer = sb.str(); return std::string(answer.data(), answer.size());` — builder is local, string copy decouples result from builder's internal buffer.
> simdjson/singleheader/simdjson.h:6093: `~parser()=default;` — RAII parser destructor; must outlive the `element` used by `minify`.
> simdjson/tests/dom/document_stream_tests.cpp:513-530 (`issue1133`): `document_stream docs = parser.parse_many(json); for (element doc : docs) { ... minify(doc) ...}` then checks `docs.truncated_bytes()`.

### P2.4 Return Value Handling
**Claim:** `minify<T>` (non-`simdjson_result` overload) returns a plain `std::string`; it has no error return itself — any parse error must already have been checked before calling it. The `simdjson_result<T>` overload (guarded by `SIMDJSON_EXCEPTIONS`) checks `x.error()` and `throw`s `simdjson_error` on error, otherwise forwards to `to_string(x.value())`. Without exceptions enabled, callers must manually check the `simdjson_result<element>` error via `.get(doc)` or `if (error) {...}` before calling `minify(doc)`.
**Evidence:**
> simdjson/singleheader/simdjson.h:8315-8320: `if (x.error()) { throw simdjson_error(x.error()); } return to_string(x.value());`
> simdjson/fuzz/fuzz_minify.cpp: current repo copy is a stub (see Build notes) but historical pattern (seen in tests) is: `auto error = parser.parse(...).get(doc); if (error) { ...; return/continue; } minify(doc);` — simdjson/tests/minify_tests.cpp:33-38.
> simdjson/singleheader/simdjson.h:6260-6265: `parse()`'s own doc comment lists MEMALLOC/CAPACITY/"other json errors" as its own error outcomes that must be checked first.

### P2.5 Cleanup Sequence
**Claim:** No explicit free/cleanup calls are needed for `minify`'s own objects: `string_builder` is stack-allocated and destroyed automatically when `to_string`/`minify`/`prettify` returns; the returned `std::string` is a normal RAII object. The only lifetime constraint is that the source `dom::parser` (and thus the tape the `element` references) must still be alive and not reused via another `parse()`/`parse_many()` call while `minify` executes.
**Evidence:**
> simdjson/singleheader/simdjson.h:8201-8226 (`class string_builder`): no explicit destructor/free method is exposed; `formatter format{};` is a plain member, `str()` just returns a view into the builder's internal buffer.
> simdjson/singleheader/simdjson.h:6104-6112: explicit warning that destroying/reparsing the parser invalidates the element before minify/to_string reads it.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase (verified in `simdjson/singleheader/simdjson.h`, which is generated from `simdjson/include/simdjson/...`):
- `simdjson::minify<T>` — singleheader/simdjson.h:8312
- `simdjson::to_string<T>` — singleheader/simdjson.h:8285
- `simdjson::prettify<T>` — singleheader/simdjson.h:8341
- `simdjson::dom::parser::parse` (several overloads) — singleheader/simdjson.h:6267-6283
- `simdjson::dom::parser::parse_into_document` — singleheader/simdjson.h:6387-6400
- `simdjson::dom::parser::parse_many` — singleheader/simdjson.h:6575-6611
- `simdjson::dom::parser::load` — singleheader/simdjson.h:4367 (`padded_string::load`) and parser's own `load` member (singleheader/simdjson.h:6096-6099 doc, declared nearby)
- `simdjson::minify(const char*, size_t, char*, size_t&)` (raw-buffer minify) — singleheader/simdjson.h:4632, dispatches to `implementation::minify` (pure virtual, singleheader/simdjson.h:4106; per-backend overrides at lines 14200, 14238, 14278, 14318, 14359, 14398, 14433, 14468, 14506)
- `internal::string_builder<formatter>::append(element/array/object/key_value_pair)` — singleheader/simdjson.h:8201-8226 declared, defined singleheader/simdjson.h:11415 onward
- `internal::tape_ref::tape_ref_type()/matching_brace_index()/get_string_view()` — singleheader/simdjson.h:5682-5701, defined 8806/8812/8843
**Evidence:** line numbers above, all found via grep in singleheader/simdjson.h which is checked in at simdjson/singleheader/simdjson.h.

### P2.7 Co-call Constraints
**Claim:** `parse_many`/`document_stream` is a begin/iterate pattern: `parser.parse_many(buf[,batch_size[,format]]).get(stream)` must succeed before iterating `for (auto doc : stream)`, and each `doc` from the stream is independently fed to `minify`/`to_string` inside the loop body — no explicit "close" call, the stream's destructor/iterator exhaustion ends it. `dom::parser::parse`/`load` have no explicit begin/end pairing beyond construct-parser → parse → use-result-before-next-parse-or-destroy.
**Evidence:**
> simdjson/tests/dom/document_stream_tests.cpp:513-524: `document_stream docs = parser.parse_many(json); for (element doc : docs) { ... minify(doc) ...}` then `docs.truncated_bytes()` checked after the loop (line 525).
> simdjson/fuzz/fuzz_ndjson.cpp:13-29: `parser.parse_many(json,batch_size).get(docs)` guarded by `if (...) { return 0; }` before the `for (auto doc : docs)` loop; parser is reused for a second `parse_many` call afterward (a new call invalidates results from the prior one).

### P2.8 Prerequisite State
**Claim:** A live, non-reused `dom::parser` holding the tape for the element(s) being minified is the only prerequisite state. For `parse_many`, the `batch_size` parameter (must be large enough, `>= dom::MINIMAL_BATCH_SIZE`, see parse_many docs) and `stream_format` (ndjson vs json) select how the parser splits the input into multiple documents before each is handed individually to `minify`. No network/filesystem/global state is required except when using `parser.load(path)`.
**Evidence:**
> simdjson/singleheader/simdjson.h:6575-6611: `parse_many(..., size_t batch_size = dom::DEFAULT_BATCH_SIZE)` and `parse_many(..., size_t batch_size, stream_format format)` overloads.
> simdjson/fuzz/fuzz_ndjson.cpp:11-15: `const auto batch_size = static_cast<size_t>(fd.getInt<0,1000>()); ... parser.parse_many(json,batch_size).get(docs)`.

### P2.9 Result Usage
**Claim:** The `std::string` returned by `minify`/`to_string`/`prettify` is consumed by: (a) round-trip re-parsing — fed back into `parser.parse(serial)` to verify idempotence; (b) direct string comparison against an expected minified literal; (c) printed to a stream. `prettify`'s result is used identically (round-tripped through `parser.parse`) but is never itself re-minified in the same call (prettify output still needs a separate parse before re-prettify/minify).
**Evidence:**
> simdjson/tests/minify_tests.cpp:54-59 (`load_minify`): `auto serial1 = simdjson::minify(doc); error = parser.parse(serial1).get(doc); ... auto serial2 = simdjson::minify(doc); bool match = (serial1 == serial2);`
> simdjson/tests/dom/readme_examples.cpp:35-38 (`to_string` round-trip, same pattern): `auto serial1 = simdjson::to_string(doc); error = parser.parse(serial1).get(doc); auto serial2 = simdjson::to_string(doc);`
> simdjson/tests/prettify_tests.cpp:36-39: `auto serial1 = simdjson::prettify(doc); error = parser.parse(serial1).get(doc); auto serial2 = simdjson::prettify(doc);`
> simdjson/tests/dom/document_stream_tests.cpp:480-484/497-506: `std::string answer = simdjson::minify(this_document); if (answer != expected) {...}` — direct string comparison against expected minified JSON literal.

### P2.10 Behaviour Controls
**Claim:** Inside the tape walk (`string_builder<formatter>::append`), the `tape_ref_type()` switch selects the serialization path per tape entry: `START_ARRAY`/`START_OBJECT` (with a `MAX_DEPTH=16` stack-local `is_object[]` array — beyond depth 16 the walker **recurses** by calling `append(dom::array(iter))`/`append(dom::object(iter))` instead of continuing the iterative loop), `STRING` (quoted/escaped via `format.string`), `BIGINT` (printed raw, unquoted, via `format.chars`), `INT64`/`UINT64`/`DOUBLE` (via `format.number`, consuming an extra tape slot), empty `[]`/`{}` short-circuit (no comma/newline branch entered). The `formatter` template parameter (`mini_formatter` default vs `pretty_formatter`) controls whether `print_newline`/`print_indents`/`print_space` emit whitespace (prettify) or no-ops (minify); this is the key difference between `minify`/`to_string` and `prettify`. For `parse_many`, `batch_size` and `stream_format` (ndjson vs json) change how many separate `element`s are produced for minify to walk.
**Evidence:**
> simdjson/singleheader/simdjson.h:11415-11519 (`string_builder<serializer>::append`): full switch over `tape_type::{START_ARRAY, START_OBJECT, STRING, BIGINT, INT64, UINT64, ...}`, `MAX_DEPTH = 16` at line 11419, recursive calls at 11448/11476 when `depth >= MAX_DEPTH`.
> simdjson/singleheader/simdjson.h:11400-11409: `pretty_formatter::print_indents`/`print_space` emit spaces; (by contrast `mini_formatter`'s versions, used by `minify`/`to_string`, are no-ops — confirmed by doc comment singleheader/simdjson.h:8286-8288: "Currently minify and to_string are identical but ... may differ").
> simdjson/singleheader/simdjson.h:8201 vs 8342: `string_builder<formatter = mini_formatter>` default used by `to_string`; `string_builder<internal::pretty_formatter>` explicitly instantiated by `prettify` (line 8342).
> simdjson/singleheader/simdjson.h:6607-6611: `parse_many(..., size_t batch_size, stream_format format)` overload — `stream_format` selects ndjson vs json splitting.

**Additional behaviour controls (previously missing):**
- Navigation is a behaviour control: whether the root is an array or object (get_array/get_object) and the JSON pointer / JSONPath taken from the input (at_pointer/at_path) choose which sub-element type minify walks; keep them on every input.
- Number parsing: the parser is fed the document directly (no prefix bytes), so float/big-integer literals reach fast_float / numberparsing, then minify prints them through to_chars.
- Library hang: `parse_many` / `document_stream` with batch_size < input size (and with `stream_format::json_sequence`) can loop forever in document_stream-inl.h:192-202; do not use it in this harness.

### P2.11 Input Expectations
**Claim:** `minify<T>`'s "input" is not raw bytes but an already-validated tape produced by `dom::parser::parse*`. It expects the tape to be well-formed per simdjson's own tape invariants (matched `START_ARRAY`/`END_ARRAY`, `START_OBJECT`/`END_OBJECT` pairs reachable via `matching_brace_index()`, string tape entries whose `get_string_view()` decode is valid UTF-8/byte range within the parser's owned buffer, numeric entries occupying the documented 2 tape slots for INT64/UINT64/DOUBLE). Because the tape is only built by the parser after successful validation, `minify` itself performs no additional format/size/termination checks on its input — those checks happened during `parser.parse()`/`parse_many()` (structural/UTF-8 validation, max depth `DEFAULT_MAX_DEPTH=1024`/`SIMDJSON_MAX_DEPTH` enforced during parsing, not during minify). The separate raw-buffer `simdjson::minify(const char*, len, dst, dst_len)` function, by contrast, explicitly documents that it does **not** validate its input ("does not parse or validate... merely return an error in simple cases") and requires `dst` to be allocated to at least `len` bytes.
**Evidence:**
> simdjson/singleheader/simdjson.h:6256-6257 (`parse` doc): "`buf` ... Must have at least len + SIMDJSON_PADDING allocated bytes, unless realloc_if_needed is true."
> simdjson/singleheader/simdjson.h:3400/3417: `SIMDJSON_MAX_DEPTH = SIMDJSON_MAXSIZE_BYTES/2;` and `DEFAULT_MAX_DEPTH = 1024;` — depth limits enforced at parse time.
> simdjson/singleheader/simdjson.h:11419/11447/11475: separate, smaller `MAX_DEPTH = 16` inside the minify/to_string/prettify *serializer* itself, governing its own iterative-vs-recursive tape-walk switch (independent of the parser's max_depth).
> simdjson/singleheader/simdjson.h:4620-4630 (raw `minify` doc): "does not parse or validate the input... dst *MUST* be allocated up to len bytes."

### Other Logic-Group Entries — call protocol and combination with the target

**`simdjson::to_string<T>`** (singleheader/simdjson.h:8285): Same prerequisites/lifecycle as `minify` (needs a parsed `dom::element`/`object`/`array`). Parameter: `T x` by value. Returns `std::string`. Uses default `string_builder<mini_formatter>` — functionally identical output to `minify` today (per its own doc comment, line 8286-8288, they may diverge in the future). No separate cleanup. In a combined harness: can be called on the *same* parsed element as `minify` to differential-test ("to_string(doc) == minify(doc)" expected to currently match), as done in simdjson/tests/dom/readme_examples.cpp:35-38 pattern mirrored by simdjson/tests/minify_tests.cpp:35-38.

**`simdjson::prettify<T>`** (singleheader/simdjson.h:8341): Same prerequisites as `minify`/`to_string`. Returns indented `std::string` via `string_builder<pretty_formatter>`. Exercises `print_newline`/`print_indents`/`print_space` branches not hit by `minify`. In a combined harness: call on the same parsed `element` right after `minify`/`to_string` to exercise the pretty-formatter's extra branches with the same tape (as in simdjson/tests/prettify_tests.cpp:36-39, which round-trips prettify output back through `parser.parse`).

**`simdjson::dom::parser::parse`** (singleheader/simdjson.h:6267 family): The producer the target depends on. Needs: a `dom::parser parser;` instance (zero-capacity by default, auto-allocates up to `max_capacity` from the constructor, singleheader/simdjson.h:6070-6076). Parameters: `buf,len[,realloc_if_needed]` or `std::string`/`padded_string`/`padded_string_view` overloads — raw `const uint8_t*`/`const char*` overloads require `len + SIMDJSON_PADDING` allocated bytes unless `realloc_if_needed=true` (default). Returns `simdjson_result<element>`; caller must check `.error()` or use `.get(doc)` and check the returned `error_code` before passing `doc` to `minify`. Combination: this is the direct upstream of the target — `parser.parse(Data,Size).get(doc)` then (if no error) `minify(doc)`, as in simdjson/fuzz/fuzz_dump.cpp:63-67 pattern (dump instead of minify, but identical parse-then-use shape) and simdjson/tests/minify_tests.cpp:52-58.

**`simdjson::dom::parser::parse_many`** (singleheader/simdjson.h:6575-6611): Needs a `dom::parser`. Parameters: `buf[,len],batch_size=dom::DEFAULT_BATCH_SIZE[,stream_format]`. `batch_size` must be large enough to hold the largest single document plus padding (too-small values yield `CAPACITY` errors, see parse_many doc in source headers under `include/simdjson/dom/parser.h`, mirrored into the singleheader). Returns `simdjson_result<document_stream>`; check via `.get(stream)`. Combination: iterate `for (auto doc : stream)` and call `minify(doc)` (or `to_string`/`prettify`) once per document, exactly as in simdjson/tests/dom/document_stream_tests.cpp:513-524 and simdjson/fuzz/fuzz_ndjson.cpp — this is the way to exercise minify across many distinct tapes from one fuzz input. After the loop, `stream.truncated_bytes()` can be checked for leftover unparsed bytes (optional diagnostic, not required for minify itself).

**`simdjson::minify(const char*, size_t, char*, size_t&)`** (singleheader/simdjson.h:4632): Does **not** depend on `dom::parser` at all — separate, lower-level entry point dispatching to `simdjson::get_active_implementation()->minify(...)` (pure virtual at singleheader/simdjson.h:4106, overridden per backend at lines 14200+). Parameters: `buf,len` (input JSON bytes, no padding requirement documented), `dst` (**caller-allocated buffer of at least `len` bytes**, per doc comment singleheader/simdjson.h:4628), `dst_len` (output-only, bytes written). Returns `error_code` (not `simdjson_result`): caller must check non-`SUCCESS` and treat the `dst`/`dst_len` content as undefined on error. No allocation/init beyond allocating `dst`. Used standalone in simdjson/fuzz/fuzz_minifyimpl.cpp:19-60 and simdjson/tools/minify.cpp:74-83 (loads a `padded_string`, allocates an equally-sized `padded_string copy`, calls `get_active_implementation()->minify(...)`). Combination: this is a *variant* path, not meant to be chained with the DOM-based `minify<T>` in the same call sequence; a combined harness would run both independently on the same input bytes as a differential check (DOM minify output vs raw minify output) rather than feeding one's result into the other.

## Build notes

**Claim:** Fuzzers for this project are C++ (compiled with clang++), linked against the `simdjson` CMake target (which in turn exposes `simdjson.h`/`simdjson.cpp` — the amalgamated singleheader, or the modular `include/simdjson/...` + `src/*.cpp` depending on build mode), via CMake's `fuzz/CMakeLists.txt` `implement_fuzzer()` helper. Each fuzzer source simply does `#include "simdjson.h"` (found via the `simdjson` target's public include directories) and defines `extern "C" int LLVMFuzzerTestOneInput(...)`. The entry function `simdjson::minify<T>` is declared/defined entirely in this header (no separate .cpp needed for the template), while `dom::parser::parse` and friends are declared in the header and implemented in the linked `simdjson` library target.

**Evidence:**
> simdjson/fuzz/CMakeLists.txt:37-44: `add_library(simdjson-fuzzer INTERFACE); target_link_libraries(simdjson-fuzzer INTERFACE simdjson); ... target_link_libraries(simdjson-fuzzer INTERFACE simdjson-internal-flags);`
> simdjson/fuzz/CMakeLists.txt:50-55 (`implement_fuzzer`): `add_executable(${name} ${name}.cpp); target_link_libraries(${name} PRIVATE simdjson-fuzzer);` — each `fuzz_*.cpp` is its own translation unit linked against the shared `simdjson` lib.
> simdjson/fuzz/CMakeLists.txt:62: `implement_fuzzer(fuzz_minify)          # minify *with* parsing` and `implement_fuzzer(fuzz_minifyimpl)      # minify *without* parsing, plus compare implementations` — both already registered as distinct fuzz targets for the two `minify` variants described above.
> simdjson/fuzz/fuzz_minifyimpl.cpp:12,17: `#include "simdjson.h"` and `#include "supported_implementations.h"` — only the aggregated header is included, resolved via the `simdjson` target's include dirs, no extra include path needed by the fuzzer file itself.
> ${QF_WORK}/src/simdjson-c2/build.sh:18: `fuzz/ossfuzz.sh` — the OSS-Fuzz `build.sh` for this project simply delegates to the in-repo `fuzz/ossfuzz.sh`.
> simdjson/fuzz/ossfuzz.sh:26-40: `cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug -DSIMDJSON_DEVELOPER_MODE=ON -DBUILD_SHARED_LIBS=OFF -DSIMDJSON_ENABLE_FUZZING=On ... -DSIMDJSON_FUZZ_LDFLAGS=$LIB_FUZZING_ENGINE` then `cmake --build . --target all_fuzzers all_tests` then `cp fuzz/fuzz_* $OUT`.
> simdjson/fuzz/build_like_ossfuzz.sh:14-18: local emulation sets `CC=clang`, `CXX=clang++`, `CXXFLAGS="-fsanitize=fuzzer-no-link,address,undefined -O1 -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION"`, `LIB_FUZZING_ENGINE="-fsanitize=fuzzer"` — confirms C++/clang++, ASan+UBSan+libFuzzer build.
> Note: the project's in-repo `fuzz/fuzz_minify.cpp` at ${QF_WORK}/src/simdjson-c2/simdjson/fuzz/fuzz_minify.cpp is currently a trivial stub (empty `LLVMFuzzerTestOneInput` placeholder, "Gold harness replaced by an empty stub for baseline generation") — it must be reconstructed by the downstream Harness Generator using the `dom::parser::parse`/`parse_many` + `minify`/`to_string`/`prettify` protocol documented above (see e.g. the equivalent pattern in simdjson/tests/minify_tests.cpp:50-69 and simdjson/fuzz/fuzz_ndjson.cpp for the parse_many variant).