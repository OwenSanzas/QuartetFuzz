## target: simdjson::dom::element::dump_raw_tape

### Overview / definitions found
- Declaration: `include/simdjson/dom/element.h:511` — `inline bool dump_raw_tape(std::ostream &out) const noexcept;` (annotated `@private for debugging. Prints out the root element.`)
- Definition: `include/simdjson/dom/element-inl.h:491-494`:
  ```
  inline bool element::dump_raw_tape(std::ostream &out) const noexcept {
    SIMDJSON_DEVELOPMENT_ASSERT(tape.usable()); // https://github.com/simdjson/simdjson/issues/1914
    return tape.doc->dump_raw_tape(out);
  }
  ```
  It is a thin wrapper around `document::dump_raw_tape`, reached via the `internal::tape_ref` member `tape` (holding `doc` pointer + `json_index`).
- Core walker: `include/simdjson/dom/document-inl.h:69-172` — `document::dump_raw_tape(std::ostream&)`.
- Note: the `tape_ref` the element holds is set to `json_index==1` (the root), via `document::root()` (`include/simdjson/dom/document-inl.h:20-22`: `element document::root() const noexcept { return element(internal::tape_ref(this, 1)); }`). Despite this, `document::dump_raw_tape` itself ignores `json_index` and always starts walking from `tape[0]` (`tape_idx = 0;` at document-inl.h:71) — so calling `elem.dump_raw_tape(out)` on *any* element dumps the *whole* document tape, not just the subtree rooted at `elem`.

### P2.1 Init Sequence
**Claim:** Before calling `element::dump_raw_tape`, you must (1) construct a `dom::parser`, optionally (2) call `parser.number_as_string(true)` if you want BIGINT/'Z' tape entries exercised, then (3) call one of the `parser.parse(...)` overloads successfully and obtain a `dom::element` (typically the root) from its `simdjson_result<element>`. No other global/library init is required.
**Evidence:**
> include/simdjson/dom/parser.h:704-705: `inline void number_as_string(bool enabled) noexcept { _number_as_string = enabled; }` / `inline bool number_as_string() const noexcept { return _number_as_string; }` — must be set before parse to affect that parse.
> include/simdjson/dom/parser-inl.h:136-141 (`parse_into_document`): `implementation->_number_as_string = _number_as_string; _error = implementation->parse(buf, len, provided_doc); if (_error) { return _error; } return provided_doc.root();` — shows `_number_as_string` is copied into the implementation at parse time, and the returned element comes from `document::root()`.
> tools/json2json.cpp:81-89 — real caller: `simdjson::dom::parser parser; simdjson::dom::element doc; auto error = parser.load(filename).get(doc); if (error) {...return EXIT_FAILURE;} if(rawdump) { doc.dump_raw_tape(std::cout); }` — parse (here via `load`, itself calling parse internally) must succeed before dump_raw_tape is invoked.
> tests/dom/big_integer_tests.cpp:34-37: `dom::parser parser; parser.number_as_string(true); dom::element doc; ASSERT_SUCCESS(parser.parse(R"({"val":123456789012345678901})"_padded).get(doc));` — shows `number_as_string(true)` set before `parse()`.

### P2.2 Parameter Construction
**Claim:** `dump_raw_tape` takes a single parameter `std::ostream &out`; any valid, writable `std::ostream` (e.g. `std::ostringstream`, `std::cout`, or a no-op sink like the project's own `NulOStream`) works — no size/range constraints on this parameter itself. No other parameters.
**Evidence:**
> include/simdjson/dom/element.h:511 — signature `bool dump_raw_tape(std::ostream &out) const noexcept;`
> fuzz/NullBuffer.h:17-21 — project's own `NulOStream : public std::ostream` used in other fuzzers (`fuzz_dump.cpp`, `fuzz_print_json.cpp`) as a cheap discard-everything ostream; suitable for `dump_raw_tape` too.
> tools/json2json.cpp:89: `doc.dump_raw_tape(std::cout);` — real usage with `std::cout`.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: construct `dom::parser` → (optional) `parser.number_as_string(true)` → `parser.parse(...)` (fills parser-owned `document doc` and returns `simdjson_result<element>`) → extract `element` via `.get(elem)` or `.value()` → call `elem.dump_raw_tape(out)` while the parser (and its internal `document`) is still alive → parser destructor frees document memory automatically. The `element` returned does NOT own memory; it only refers into the parser's `document` (`tape`, `string_buf`), so the parser/document must outlive the `dump_raw_tape` call.
**Evidence:**
> include/simdjson/dom/parser.h:71-79 (load() doc comment, same contract applies to parse()): "### IMPORTANT: Document Lifetime / The JSON document still lives in the parser ... you *must* use the document before you destroy the parser or call parse() again."
> include/simdjson/internal/tape_ref.h:39-43: `const dom::document *doc; size_t json_index;` — `element`/`tape_ref` is a non-owning reference into the document.
> include/simdjson/dom/document.h:57-63: `std::unique_ptr<uint64_t[]> tape{}; std::unique_ptr<uint8_t[]> string_buf{};` — these are owned by `document`, released automatically on document/parser destruction (no manual free needed).
> include/simdjson/dom/parser.h:59-60: `~parser()=default;` — default destructor is sufficient cleanup.

### P2.4 Return Value Handling
**Claim:** `dump_raw_tape` returns `bool`: `true` if the whole tape was walked to a trailing matching root marker, `false` on any of several "tape looks wrong" conditions (no leading `'r'` tag, numeric payload truncated before its 2nd tape slot, an unexpected `'r'` found mid-tape, or an unrecognized tag byte). A harness should just inspect/log the boolean; `false` is a normal, safe outcome documented in the API (not necessarily a crash), but still indicates the tape was not what the function expected.
**Evidence:**
> include/simdjson/dom/document.h:48-54: doc-comment: "@return false if the tape is likely wrong (e.g., you did not parse a valid JSON)."
> include/simdjson/dom/document-inl.h:77-82: `if (type == 'r') { how_many = ...; } else { // Error: no starting root node? return false; }`
> include/simdjson/dom/document-inl.h:101-106, 107-112, 113-121: `if (tape_idx + 1 >= how_many) { return false; }` guards before reading the second tape slot for `'l'`, `'u'`, `'d'`.
> include/simdjson/dom/document-inl.h:150-152: `case 'r': ... return false;` (unexpected root tag mid-tape).
> include/simdjson/dom/document-inl.h:162-163: `default: return false;` (unknown tag byte).
> include/simdjson/dom/document-inl.h:171: `return true;` on normal completion.

### P2.5 Cleanup Sequence
**Claim:** No explicit cleanup is required for `dump_raw_tape` itself: it only reads from `document::tape`/`string_buf` and writes to the caller-supplied `ostream`. All memory (`tape`, `string_buf`) is owned by `std::unique_ptr` members of `document`, which is owned by `parser`; they are freed automatically when the `parser` (or a `document` obtained via `parser.parse_into_document`) goes out of scope. On error, no partial state needs freeing — `dump_raw_tape` does not allocate.
**Evidence:**
> include/simdjson/dom/document.h:57-63 — `tape`/`string_buf` are `std::unique_ptr`.
> include/simdjson/dom/document-inl.h:69-172 — `document::dump_raw_tape` body contains no `new`/allocation, only reads and `os <<` writes.
> include/simdjson/dom/parser.h:59: `~parser()=default;` default dtor relies on member dtors (document, unique_ptrs) for cleanup.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited locations (checked by direct `Grep`/`Read`).
**Evidence:**
> include/simdjson/dom/element.h:511 — `element::dump_raw_tape` declared.
> include/simdjson/dom/element-inl.h:491 — `element::dump_raw_tape` defined.
> include/simdjson/dom/document.h:54 — `document::dump_raw_tape` declared.
> include/simdjson/dom/document-inl.h:69 — `document::dump_raw_tape` defined.
> include/simdjson/dom/parser.h:696-697 — `parser::dump_raw_tape` declared (marked "Private and deprecated: use `parser.parse(...).doc.dump_raw_tape()` instead").
> include/simdjson/dom/parser-inl.h:34-36 — `parser::dump_raw_tape` defined: `return valid ? doc.dump_raw_tape(os) : false;`
> include/simdjson/dom/parser.h:234-246 — `parser::parse` overloads (`const uint8_t*`, `const char*`, `const std::string&`, `const padded_string&`, `const padded_string_view&`) declared.
> include/simdjson/dom/parser.h:704-705 — `parser::number_as_string(bool)` / `number_as_string() const` declared.
> include/simdjson/dom/document-inl.h:20-22 — `document::root()` defined.
> include/simdjson/internal/jsonformatutils.h:14-21 — `internal::escape_json_string` class exists.
> include/simdjson/internal/tape_ref-inl.h:19-20 — `tape_ref::tape_ref(...)` constructors defined.

### P2.7 Co-call Constraints
**Claim:** There is no explicit lock/unlock pairing. The relevant pairing constraint is: `parser.parse(...)` must be called (and must succeed, i.e. return no error) before `dump_raw_tape` is called on an element/document derived from that same parse; and the parser/document object must not be destroyed or re-used for another `parse()` call before `dump_raw_tape` runs (parse() on the same parser instance overwrites the single internal document). `number_as_string(bool)` (if used) must be set before, not after, the `parse()` call it is meant to affect — it only affects the parse call(s) made after it's set.
**Evidence:**
> include/simdjson/dom/parser.h:12-28 class doc-comment: "The parsed document is overwritten on each parse." / "This is not thread safe: one parser cannot produce two documents at the same time!"
> include/simdjson/dom/parser-inl.h:136: `implementation->_number_as_string = _number_as_string;` executed at the start of `parse_into_document`, i.e. the flag is consumed once per parse call, confirming "set before parse" ordering.
> include/simdjson/dom/parser.h:71-79: doc comment on document lifetime — re-parsing or destroying parser before using document invalidates prior elements.

### P2.8 Prerequisite State
**Claim:** Prerequisite state for a meaningful (non-`false`) `dump_raw_tape` call: a `dom::parser` whose most recent `parse()` call returned success (`SUCCESS`/no error), so that `document::tape[0]` holds a valid `'r'` tape_type entry and `document::string_buf` is populated consistently with any `'"'`/`'Z'` string payload offsets referenced by the tape. Calling `dump_raw_tape` on a parser/element before any successful parse (e.g., default-constructed `document` with zero capacity, null `tape`/`string_buf` unique_ptrs) is undefined/unsafe since `document::dump_raw_tape` immediately dereferences `tape[0]` without a null check.
**Evidence:**
> include/simdjson/dom/document-inl.h:71-73: `uint64_t tape_val = tape[tape_idx]; uint8_t type = uint8_t(tape_val >> 56);` — dereferences `tape.get()[0]` with no null-check of the `std::unique_ptr<uint64_t[]> tape`.
> include/simdjson/dom/document-inl.h:29-38 (`document::allocate`): shows `tape`/`string_buf` start out reset to null / are populated only on successful `allocate()`, which happens as part of `parse()`.
> include/simdjson/dom/element-inl.h:492: `SIMDJSON_DEVELOPMENT_ASSERT(tape.usable());` — a debug-only assert (`internal::tape_ref::usable()` just checks `doc != nullptr`, `include/simdjson/internal/tape_ref-inl.h:26-28`) that does NOT check that `doc->tape` is populated; it is compiled out in release builds (`include/simdjson/common_defs.h:360-362`), so it gives no real protection in a fuzz build with assertions disabled.
> include/simdjson/dom/parser.h:696-697 doc comment on `parser::dump_raw_tape`: "Private and deprecated: use `parser.parse(...).doc.dump_raw_tape()` instead" — confirms dump_raw_tape is meant to be chained directly off a successful parse result.

### P2.9 Result Usage
**Claim:** `dump_raw_tape`'s only "result" is the text written into the caller-supplied `ostream` plus the boolean return; there is no returned object/handle for further API calls. In the real project, the only consumer found is a CLI tool that just prints the stream contents to the user; it is not chained into further simdjson API calls. The companion "normal" dump (`operator<<` on `element`/`document`) is used as an alternative representation of the same tape for human/diff consumption, not as an input to another API.
**Evidence:**
> tools/json2json.cpp:88-92: `if(rawdump) { doc.dump_raw_tape(std::cout); } else { std::cout << doc; }` — the stream (`std::cout`) is simply flushed to terminal; return value of `dump_raw_tape` is not checked here.
> include/simdjson/dom/element-inl.h:497+ : `inline std::ostream& operator<<(std::ostream& out, element_type type) {...}` and (elsewhere in the file) `element::operator<<` — the alternative "pretty" dump path, used by `fuzz_print_json.cpp`/`fuzz_dump.cpp` as shown below, not consuming `dump_raw_tape`'s output.
> fuzz/fuzz_print_json.cpp:9-17: `auto error = parser.parse(Data, Size).get(elem); if (!error) { NulOStream os; os<<elem; }` — shows the parallel non-raw dump path that a harness combining both for differential testing would use (per the feature description's "variants" entry).

### P2.10 Behaviour Controls
**Claim:** The only behaviour-selecting input the target itself has is the content of the already-built tape (determined entirely by the JSON text given to `parse()`); `dump_raw_tape` has no flags. Around it, `parser::number_as_string(bool)` is the one real "mode" flag: `false` (default) makes big integers that overflow 64 bits fail parsing with `BIGINT_ERROR` (so `'Z'` tape entries never appear); `true` makes the parser emit `'Z'` BIGINT tape entries that are stringified in `string_buf`, which is the only way to exercise the `case 'Z':` branch in `document::dump_raw_tape`. JSON content also selects every other switch branch: nesting of `{}`/`[]` (branches `'{', '}', '[', ']'`), numeric literal class (`'l'` int64, `'u'` uint64, `'d'` double), literals `true/false/null` (`'t'/'f'/'n'`), and quoted strings (`'"'`, exercising `internal::escape_json_string`).
**Evidence:**
> include/simdjson/dom/parser.h:704-705 — `number_as_string(bool enabled)`.
> tests/dom/big_integer_tests.cpp:16-21: `default_returns_bigint_error()`: without the flag, `parser.parse(...)` on an overflowing integer literal returns `BIGINT_ERROR` — so no `'Z'` tape entry is ever produced unless the flag is set first.
> src/generic/stage2/tape_builder.h:236: `iter.dom_parser._number_as_string` consulted inside the core tape-building code, confirming it gates BIGINT handling at parse time (the point where 'Z' tape entries, read later by dump_raw_tape, are created).
> include/simdjson/dom/document-inl.h:90-164 — the full `switch(type)` in `document::dump_raw_tape` enumerating `'"', 'l', 'u', 'd', 'n', 't', 'f', '{', '}', '[', ']', 'r', 'Z', default` branches, each reached by a corresponding JSON construct fed through `parse()`.

### P2.11 Input Expectations
**Claim:** `dump_raw_tape` does not take raw JSON directly; its "input" is the binary tape (`document::tape`, a `uint64_t[]`) and side string buffer (`document::string_buf`, a `uint8_t[]`) produced by a prior successful `parser.parse()`/`parse_into_document()` call. It expects: tape slot 0 to have the high byte (`tape_val >> 56`) equal to `'r'` with the low 56 bits giving the index one-past-the-last tape entry (`how_many`); for `'"'`/`'Z'` entries, the low 56 bits ("payload") must be a valid offset into `string_buf` at which a 4-byte little-endian `uint32_t` string length precedes that many bytes of string data; for `'l'/'u'/'d'` entries, there must be one more valid tape slot following (`tape_idx+1 < how_many`) holding the raw int64/uint64/double bits; `'{'/'['/'}'/']'` payloads encode a next/previous tape index in the low 32 bits and a saturated count in bits 32-55. The function performs some bounds checks against `how_many` (for numeric payload truncation) but does NOT validate that string/`'Z'` payload offsets are within `string_buf`'s allocated size, nor that `{`/`}`/`[`/`]` matching indices are in range — those are the exact OOB-read risk areas flagged in the task description. No explicit size limit/termination is checked beyond the `how_many` loop bound read from tape slot 0 itself (so it trusts the tape's self-reported length).
**Evidence:**
> include/simdjson/dom/document-inl.h:72-82 — leading `'r'` tag required, `how_many` extracted from `tape_val & internal::JSON_VALUE_MASK` (`internal::JSON_VALUE_MASK = 0x00FFFFFFFFFFFFFF`, `include/simdjson/internal/tape_ref-inl.h:13`).
> include/simdjson/dom/document-inl.h:91-98: `case '"': ... std::memcpy(&string_length, string_buf.get() + payload, sizeof(uint32_t)); os << internal::escape_json_string(std::string_view(reinterpret_cast<const char*>(string_buf.get() + payload + sizeof(uint32_t)), string_length));` — reads length+bytes from `string_buf` at `payload` with no bounds check against `string_buf`'s allocated size (`allocated_capacity`-derived `string_capacity`, see `document-inl.h:54`).
> include/simdjson/dom/document-inl.h:153-160 — same unchecked pattern for `case 'Z':` (bigint).
> include/simdjson/dom/document-inl.h:101-106/107-112/113-121 — only bound check present (`tape_idx + 1 >= how_many`) is against the tape length, not against `string_buf`.
> include/simdjson/dom/document-inl.h:131-149 — `'{'/'}'/'['/']'` just print `uint32_t(payload)` and `(payload >> 32) & internal::JSON_COUNT_MASK` without validating these indices are within `[0, how_many)`.

### Other Entries of the Logic Group (how each is used alongside the target)

**`simdjson::dom::parser::parse` (all overloads)**
- Needs before it: a constructed `dom::parser` (default-constructed, zero capacity, auto-grows up to `max_capacity`; `include/simdjson/dom/parser.h:30-43`).
- Parameters/flags: overloads for `(const uint8_t* buf, size_t len, bool realloc_if_needed=true)`, `(const char*, size_t, bool)`, `(const std::string&)`, `(const padded_string&)`, `(const padded_string_view&)` — `include/simdjson/dom/parser.h:234-246`. `realloc_if_needed` controls whether the parser copies+pads the buffer internally (needed when the input buffer isn't already `SIMDJSON_PADDING`-padded) — see `include/simdjson/dom/parser-inl.h:178-193`. For a fuzz harness taking raw `(data,size)`, the `(const uint8_t*, size_t)` overload with default `realloc_if_needed=true` is the simplest, safest choice since it does not require the caller to pre-pad the buffer.
- Return: `simdjson_result<element>`; must check/`get()` for error before use (e.g. `auto error = parser.parse(buf,len).get(elem); if (error) return 0;` — pattern used throughout `fuzz/*.cpp`, e.g. `fuzz/fuzz_element.cpp:44-49`, `fuzz/fuzz_dump.cpp:62-67`, `fuzz/fuzz_print_json.cpp:9-13`).
- What to free: nothing explicit — internal buffers (`document::tape`, `string_buf`, and parser's own `loaded_bytes`) are `unique_ptr`-owned and freed by the parser's/document's destructors.
- Combination with target: `parser.parse(...)` must run first and succeed; its resulting `element` (or `parser.doc.root()`/the element obtained from `.get()`) is exactly what `dump_raw_tape` is called on afterward, and the same parser must stay alive for the duration of the `dump_raw_tape` call.

**`simdjson::dom::parser::number_as_string`**
- Needs before it: just a constructed `dom::parser` (can be called any time before the `parse()` call it's meant to affect; it is a pure setter, `include/simdjson/dom/parser.h:704`: `inline void number_as_string(bool enabled) noexcept { _number_as_string = enabled; }`).
- Parameters: `bool enabled`. No return (void) for the setter; the getter `number_as_string() const` returns the current `bool`.
- Return handling: none needed (noexcept setter, cannot fail).
- Combination with target: call `parser.number_as_string(true)` before `parser.parse(...)` to make overflowing integer literals become `'Z'` BIGINT tape entries instead of parse errors, so that a subsequent `dump_raw_tape` call actually exercises the `case 'Z':` branch (`include/simdjson/dom/document-inl.h:153-161`). If left `false` (default), BIGINT inputs make `parse()` fail with `BIGINT_ERROR` and no element/document is produced to dump.

**`simdjson::dom::document::root`**
- Needs before it: a `document` that has been successfully populated by `parse()`/`parse_into_document()` (so `tape`/`string_buf` are non-null and `tape[1]` is well-formed as the first real tape value right after the leading `'r'` marker at `tape[0]`).
- Parameters: none (`element root() const noexcept;`, `include/simdjson/dom/document.h:46`).
- Return: `dom::element` wrapping `internal::tape_ref(this, 1)` — i.e. tape index 1 (the first entry after the root marker) — `include/simdjson/dom/document-inl.h:20-22`. Cannot fail (noexcept, no error path); garbage-in-garbage-out if `document` wasn't actually parsed.
- Combination with target: this is how `parser::parse_into_document` obtains the `element` it returns (`include/simdjson/dom/parser-inl.h:141`: `return provided_doc.root();`), i.e. it is implicitly called by every successful `parse()`. A harness can also call `parser.doc.root()`/`document.root()` explicitly to get a fresh `element` handle to invoke `dump_raw_tape` on (same underlying tape as any other element from that document, since — as noted in P2.3 — `document::dump_raw_tape` ignores `json_index` and always starts at `tape[0]`).

**`simdjson::dom::parser::dump_raw_tape`**
- Needs before it: successful `parser.parse(...)` (sets `parser::valid` true internally) — `include/simdjson/dom/parser-inl.h:34-36`: `inline bool parser::dump_raw_tape(std::ostream &os) const noexcept { return valid ? doc.dump_raw_tape(os) : false; }`.
- Parameters: `std::ostream&` only, same as the target.
- Return: `bool`, `false` immediately (without touching the tape) if the parser's last parse was not valid/successful, otherwise delegates to `document::dump_raw_tape`'s boolean result.
- Note: declared "Private and deprecated: use `parser.parse(...).doc.dump_raw_tape()` instead" (`include/simdjson/dom/parser.h:696`). It exists in the current codebase and is a valid alternative call path to the same core walker, but is explicitly discouraged in favor of calling `dump_raw_tape` on the `document`/`element`.
- Combination with target: can be used in place of `elem.dump_raw_tape(out)` directly on the `parser` object after `parse()`, exercising identical core logic (`document::dump_raw_tape`) but with the extra `valid` guard that `element::dump_raw_tape` lacks (the element path only has a debug-only assert, see P2.8).

**`simdjson::dom::document::dump_raw_tape`**
- This is the actual core walker the target (`element::dump_raw_tape`) and `parser::dump_raw_tape` both forward to. Needs before it: a `document` populated by a successful `parse()`. Parameters: `std::ostream&`. Return: `bool` per P2.4. Documented at `include/simdjson/dom/document.h:48-54` and defined at `include/simdjson/dom/document-inl.h:69-172` (fully analyzed above under the target walkthrough, since it is the target's only callee of substance).
- Combination with target: `element::dump_raw_tape` is a 2-line wrapper (`out << tape.doc->dump_raw_tape(out)` effectively) — calling the target IS calling this one indirectly; a harness does not need to call `document::dump_raw_tape` separately unless it wants to bypass the (debug-only, often compiled-out) `tape.usable()` assert in `element::dump_raw_tape`.

### How a harness combines these entries
1. Construct `simdjson::dom::parser parser;`
2. Optionally: `parser.number_as_string(true);` (to reach the `'Z'` BIGINT branch).
3. `simdjson::dom::element elem; auto error = parser.parse(Data, Size).get(elem);` (fuzz-input bytes `Data`/`Size` feed directly into `parse`); on `error`, return early (mirrors `fuzz/fuzz_element.cpp:44-49`, `fuzz/fuzz_dump.cpp:62-67`).
4. On success, call the target: `elem.dump_raw_tape(out);` with some `std::ostream` (e.g. a `NulOStream` as in `fuzz/NullBuffer.h`, or `std::ostringstream`), and optionally check/log the returned `bool`.
5. Optionally also call `parser.dump_raw_tape(out2)` and/or `elem.operator<<`/`parser.doc.root()` for differential comparison, as hinted by the "variants" in the feature description.
6. No manual cleanup needed; `parser` going out of scope frees `document::tape`/`string_buf` and any internal `loaded_bytes` buffer automatically.

### Build notes
**Claim:** Fuzzers for this project are built as C++ via CMake, each fuzzer is a single `.cpp` file in `fuzz/`, linked against the `simdjson` static library target and an interface target `simdjson-fuzzer` (which supplies `main.cpp` or libFuzzer LDFLAGS), and OSS-Fuzz's `build.sh` simply calls `fuzz/ossfuzz.sh`, which runs the CMake/Ninja build and copies `fuzz/fuzz_*` binaries to `$OUT`. The target's header is reached purely via `#include "simdjson.h"` (the amalgamated/aggregating public header), relying on CMake include paths — no special include path hacking needed in the harness.
**Evidence:**
> ${QF_WORK}/ossfuzz_view/simdjson-c1/build.sh:18: `fuzz/ossfuzz.sh` — OSS-Fuzz build.sh entry point.
> fuzz/ossfuzz.sh:26-40: `cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug -DSIMDJSON_DEVELOPER_MODE=ON -DBUILD_SHARED_LIBS=OFF -DSIMDJSON_ENABLE_FUZZING=On ... -DSIMDJSON_FUZZ_LINKMAIN=Off ... -DSIMDJSON_FUZZ_LDFLAGS=$LIB_FUZZING_ENGINE` then `cmake --build . --target all_fuzzers all_tests` then `cp fuzz/fuzz_* $OUT`.
> fuzz/CMakeLists.txt:37-44: `add_library(simdjson-fuzzer INTERFACE); target_link_libraries(simdjson-fuzzer INTERFACE simdjson); ... target_link_libraries(simdjson-fuzzer INTERFACE ${SIMDJSON_FUZZ_LDFLAGS}); target_link_libraries(simdjson-fuzzer INTERFACE simdjson-internal-flags);` — shows every fuzzer links the `simdjson` library target plus libFuzzer's LDFLAGS via `SIMDJSON_FUZZ_LDFLAGS`.
> fuzz/CMakeLists.txt:50-55: `function(implement_fuzzer name) add_executable(${name} ${name}.cpp) target_link_libraries(${name} PRIVATE simdjson-fuzzer) add_dependencies(all_fuzzers ${name}) ... endfunction()` — one `.cpp` file per fuzzer, no custom include-path flags needed beyond what `simdjson` target already exports.
> fuzz/fuzz_element.cpp:1: `#include "simdjson.h"` — all existing harnesses (including `fuzz_dump.cpp`, `fuzz_print_json.cpp`) include the project's umbrella public header `simdjson.h` (resolved via the `simdjson` CMake target's public include directories, which expose `include/simdjson.h` and `include/simdjson/...`), the same header that (transitively) declares `simdjson::dom::element::dump_raw_tape` at `include/simdjson/dom/element.h:511`.
> fuzz/CMakeLists.txt:57-69: list of already-implemented fuzzers includes `implement_fuzzer(fuzz_dump_raw_tape)` (line 59), confirming the project already defines (at the CMake level) a fuzzer named for this exact target, though the current file content at `fuzz/fuzz_dump_raw_tape.cpp` is a stub (`/* Gold harness replaced by an empty stub for baseline generation. */`) that needs to be (re)written by the downstream Harness Generator.