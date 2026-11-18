## target: flatbuffers::Parser::ParseJson

### P2.1 Init Sequence
**Claim:** Before calling `ParseJson`, a `Parser` object must exist and have a root type (`root_struct_def_`) set. This is normally achieved by calling `Parser::Parse()` on an `.fbs` schema (sets `root_struct_def_` as a side effect of parsing the `root_type` schema statement or the last-declared table), or by calling `Parser::Deserialize()` on a serialized `reflection::Schema` binary (also sets `root_struct_def_` from `schema->root_table()`). `Parser::SetRootType()` may optionally be called afterward to override the root. If no root type is set, `ParseJson`'s internal `DoParseJson` returns an error instead of parsing.
**Evidence:**
> include/flatbuffers/idl.h:1058: `bool ParseJson(const char* json, const char* json_filename = nullptr);`
> src/idl_parser.cpp:3944: `if (!root_struct_def_) return Error("no root type set to parse json with");` (inside `DoParseJson`, called from `ParseJson`)
> src/idl_parser.cpp:4490-4492 (`Parser::Deserialize(const reflection::Schema*)`): `if (schema->root_table() == *it) { root_struct_def_ = struct_def; }`
> tests/json_test.cpp:29,60,109-110: `parser.Parse(schemafile.c_str(), include_directories)` then `parser.ParseJson(jsonfile.c_str())`
> tests/json_test.cpp:137-140: `parser.Deserialize(...)` then `parser.ParseJson("{name:\"monster\"}")`
> tests/fuzzer/flatbuffers_monster_fuzzer.cc:101-104 (`LLVMFuzzerInitialize`): `parser_.Deserialize(reinterpret_cast<const uint8_t*>(schemafile.c_str()), schemafile.size());` done once, before any `ParseJson` call in `LLVMFuzzerTestOneInput`.

### P2.2 Parameter Construction
**Claim:** `ParseJson` takes `const char* json` (must be a NUL-terminated C string — the parser does not take an explicit length) and an optional `const char* json_filename` (used only to populate `file_being_parsed_` for error messages; may be `nullptr`). No allocation is required by the caller beyond producing a NUL-terminated buffer.
**Evidence:**
> include/flatbuffers/idl.h:1058: `bool ParseJson(const char* json, const char* json_filename = nullptr);`
> src/idl_parser.cpp:3615-3625 (`StartParseFile`): `source_ = source; ResetState(source_); ... SkipByteOrderMark(); NEXT(); if (Is(kTokenEof)) return Error("input file is empty");` — tokenizer walks `source_` until an embedded `kTokenEof`/NUL, confirming NUL-termination requirement, not a length parameter.
> tests/fuzzer/flatbuffers_monster_fuzzer.cc:31-32,45: `auto input = std::string(original.c_str()); // until '\0'` then `parser_.ParseJson(input_json.c_str())` — real caller explicitly truncates fuzz bytes at first embedded NUL before calling, because `ParseJson` has no length parameter.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: construct `Parser` (optionally with `IDLOptions`) → set `opts` fields as desired → load schema (`Parse` or `Deserialize`) to populate `root_struct_def_` → optionally `SetRootType` → call `ParseJson` (may be called multiple times on the *same* Parser only if `builder_` is cleared between calls, which `ParseJson` itself does via `builder_.Clear()`) → read results from `parser.builder_` → optionally round-trip through `GenText`/`GenerateText` → destroy `Parser` (RAII, non-copyable, move-only).
**Evidence:**
> src/idl_parser.cpp:3601-3609: `bool Parser::ParseJson(...) { ... builder_.Clear(); const auto done = !StartParseFile(...).Check() && !DoParseJson().Check(); ... return done; }` — builder is cleared at the start of every `ParseJson` call, so the same `Parser`/`builder_` can be reused across multiple JSON inputs sequentially (as the monster fuzzer does with `do_test` called twice per iteration, tests/fuzzer/flatbuffers_monster_fuzzer.cc:71-88,127-140).
> include/flatbuffers/idl.h:1032-1036: `Parser(const Parser&) = delete; Parser& operator=(const Parser&) = delete; Parser(Parser&&) = default;` — Parser is non-copyable, move-only; must be used by reference/pointer or moved.
> include/flatbuffers/idl.h:1038-1042 (`~Parser`): destructor frees `namespaces_` entries; no explicit "close"/"destroy" API needed beyond normal C++ destruction.

### P2.4 Return Value Handling
**Claim:** `ParseJson` returns `bool`: `true` on success (a complete binary buffer is in `builder_`), `false` on any lexing/parsing/type error. On failure, `parser.error_` (a `std::string` member) holds a human-readable message. Callers must check the boolean and should not read `builder_`'s output if `false`.
**Evidence:**
> src/idl_parser.cpp:3601-3609: returns `done` which is `false` whenever `StartParseFile` or `DoParseJson` fails (`.Check()` on a `CheckedError`).
> tests/fuzzer/flatbuffers_monster_fuzzer.cc:77-86: `if (parser_.ParseJson(input_json.c_str())) { ...verify... } else if (check_parser) { TEST_OUTPUT_LINE("parser failed..."); TEST_EQ_STR("", parser_.error_.c_str()); TEST_ASSERT(false); }`
> tests/json_test.cpp:140-146: `TEST_EQ(parser.ParseJson("{name:\"monster\"}"), true); TEST_EQ(parser.ParseJson(""), false); TEST_EQ(parser.ParseJson("{name: 1}"), false); ...` — multiple negative cases expected to return `false`.

### P2.5 Cleanup Sequence
**Claim:** No manual cleanup of `ParseJson`'s output is required: the resulting binary lives in `parser.builder_` (a `FlatBufferBuilder` member of `Parser`) and is destroyed/reset automatically when the `Parser` is destroyed or when `builder_.Clear()` runs on the next `ParseJson`/`Parse` call. If the caller needs the buffer to outlive the parser, it must copy the bytes (`builder_.GetBufferPointer()`, `builder_.GetSize()`) before destroying/reusing the `Parser`. Schema structures (`structs_`, `enums_`, `namespaces_`) are owned by `Parser` and freed in `~Parser()`.
**Evidence:**
> include/flatbuffers/idl.h:1038-1042: `~Parser() { for (...namespaces_...) delete *it; }` — automatic cleanup on destruction.
> tests/fuzzer/flatbuffers_monster_fuzzer.cc:50-53 (sibling parser_fuzzer, analogous `Parser::Parse` usage): `const uint8_t* buf = parser.builder_.GetBufferPointer(); flatbuffers::Verifier verifier(buf, parser.builder_.GetSize());` — buffer pointer/size read directly from `builder_` without separate free; lifetime tied to `parser`/`builder_`.

### P2.6 API Existence
**Claim:** All functions mentioned below exist in the current codebase at the cited locations.
**Evidence:**
> include/flatbuffers/idl.h:1058: `ParseJson`
> include/flatbuffers/idl.h:1055-1056: `Parse`
> include/flatbuffers/idl.h:1064: `SetRootType`
> include/flatbuffers/idl.h:1079: `Deserialize(const uint8_t*, size_t)`
> include/flatbuffers/idl.h:1083: `Deserialize(const reflection::Schema*)`
> include/flatbuffers/idl.h:1295-1296: `GenText`
> include/flatbuffers/idl.h:1286-1287: `GenerateText` (deprecated wrapper for `GenText`, src/idl_gen_text.cpp:413-416)
> src/idl_parser.cpp:3575 (`Parser::ParseFlexBuffer`) — present; note it is declared as `bool ParseFlexBuffer(const char* source, const char* source_filename, flexbuffers::Builder* builder);` (not found at idl.h:1058 area; verified separately, see P2.10).
> src/idl_parser.cpp:3940 `DoParseJson`, 3627 `ParseRoot`/ (core) — internal/private `CheckedError`-returning helpers used by the above, not separately public.

### P2.7 Co-call Constraints
**Claim:** `Parse`/`Deserialize` must precede `ParseJson` (schema-before-JSON pairing); there is no explicit "begin/commit" pair for `ParseJson` itself — `builder_.Clear()` is done implicitly at entry, and `builder_.Finish()`/`FinishSizePrefixed()` is done implicitly at the end of a successful parse (inside `DoParseJson`), so the caller does not call `Finish` separately. `GenText` is the paired "reader" of `ParseJson`'s output and asserts `root_struct_def_` is non-null (i.e., `SetRootType`/`Parse`/`Deserialize` must have run) — calling `GenText` without a root type set is a documented precondition violation (assertion fires).
**Evidence:**
> src/idl_parser.cpp:3940-3959 (`DoParseJson`): calls `ParseTable(...)` then `builder_.FinishSizePrefixed(...)` or `builder_.Finish(...)` depending on `opts.size_prefixed` — Finish is internal to the call, not caller-managed.
> src/idl_gen_text.cpp:421: `FLATBUFFERS_ASSERT(parser.root_struct_def_);  // call SetRootType()` inside `GenText`.
> src/idl_parser.cpp:3604: `builder_.Clear();` at top of `ParseJson` — implicit "begin" matching the implicit "commit" (`Finish`) at the end of `DoParseJson`.

### P2.8 Prerequisite State
**Claim:** Required state before `ParseJson`: (1) `root_struct_def_ != nullptr` (from `Parse`/`Deserialize`/`SetRootType`); (2) `opts` (IDLOptions) fields configured as desired (`strict_json`, `skip_unexpected_fields_in_json`, `allow_non_utf8`, `use_flexbuffers`, `size_prefixed`, `require_json_eof`, etc.) — these must be set on `parser.opts` *before* calling `ParseJson` since they are read during parsing, not passed as arguments; (3) the JSON string itself must be NUL-terminated.
**Evidence:**
> src/idl_parser.cpp:3944: `if (!root_struct_def_) return Error("no root type set to parse json with");`
> tests/fuzzer/flatbuffers_monster_fuzzer.cc:73-77 (`do_test`): `parser_.opts = opts;` set immediately before `parser_.ParseJson(input_json.c_str())`.
> src/idl_parser.cpp:3950-3958: `if (opts.size_prefixed) { builder_.FinishSizePrefixed(...) } else { builder_.Finish(...) }` and :3960-3964 `if (opts.require_json_eof) { EXPECT(kTokenEof); }` — both read `opts` at parse time, confirming options must be set beforehand.

### P2.9 Result Usage
**Claim:** After a successful `ParseJson`, callers use `parser.builder_.GetBufferPointer()` / `parser.builder_.GetSize()` to obtain the binary FlatBuffer. Typical consumers: (a) `flatbuffers::Verifier` + generated `VerifyXBuffer()` to validate the buffer against the schema; (b) `flatbuffers::GetRoot<T>()`/`GetSizePrefixedRoot<T>()` to obtain a typed root table pointer for inspection; (c) `flatbuffers::GenText`/`GenerateText` to re-serialize the binary back to JSON text for round-trip/differential testing; (d) `Parser::BytesConsumed()` to see how many source JSON bytes were used.
**Evidence:**
> tests/fuzzer/flatbuffers_monster_fuzzer.cc:77-81: `if (parser_.ParseJson(input_json.c_str())) { flatbuffers::Verifier verifier(parser_.builder_.GetBufferPointer(), parser_.builder_.GetSize()); TEST_EQ(true, MyGame::Example::VerifyMonsterBuffer(verifier)); TEST_NULL(GenText(parser_, parser_.builder_.GetBufferPointer(), &jsongen)); }`
> tests/fuzzer/flatbuffers_parser_fuzzer.cc:48-60 (sibling example via `Parse`, analogous pattern for `ParseJson`): `parser.Serialize(); const uint8_t* buf = parser.builder_.GetBufferPointer(); flatbuffers::Verifier verifier(buf, parser.builder_.GetSize()); ... auto root = flatbuffers::GetRoot<flatbuffers::Table>(buf); ... flatbuffers::GenText(parser, buf, &json_output);`
> tests/json_test.cpp:164 and surrounding (`parser.ParseJson(jsonStr)` then verification/GenText calls in the same file) — same result-usage idiom repeated across the JSON test suite.
> include/flatbuffers/idl.h:1061: `std::ptrdiff_t BytesConsumed() const;` — "Returns the number of characters were consumed when parsing a JSON string," usable after `ParseJson`.

### P2.10 Behaviour Controls
**Claim:** Behaviour of `ParseJson` and its API surface is controlled by `IDLOptions` fields set on `parser.opts` before the call, and by which schema/root type is active:
- `opts.strict_json` — requires quoted field names in JSON (checked in `ParseTableDelimiters`/field-name parsing).
- `opts.skip_unexpected_fields_in_json` — tolerate unknown JSON fields instead of erroring.
- `opts.allow_non_utf8` / `natural_utf8` — control string byte-validation strictness.
- `opts.require_json_eof` — after the top-level `{...}`, require EOF (else trailing content is an error).
- `opts.size_prefixed` — selects `builder_.FinishSizePrefixed` vs `builder_.Finish`, changing the binary buffer layout.
- `opts.use_flexbuffers` — diverts `Parse`/`ParseFlexBuffer` to the schema-less `flexbuffers::Builder` path instead of `ParseTable` (see `Parser::Parse`, src/idl_parser.cpp:3592-3596); `ParseJson` itself always goes through `DoParseJson`/`ParseTable` (schema-typed) and does not itself check `use_flexbuffers`.
- `opts.output_default_scalars_in_json` — affects `GenText`'s output (not `ParseJson` itself), used by callers to make round-trip JSON comparisons stable.
- `opts.proto_mode` — affects `Parse` (schema parsing dialect), indirectly determining the schema/types `ParseJson` walks.
- Which `root_struct_def_` is active (`SetRootType`) determines which `StructDef`/`FieldDef` tree `ParseTable`/`ParseAnyValue` walk, exercising different scalar/vector/union/struct branches.
**Evidence:**
> tests/fuzzer/flatbuffers_monster_fuzzer.cc:16-18,36-40,121-125: `flags_strict_json`, `flags_skip_unexpected_fields_in_json`, `flags_allow_non_utf8` bits decoded from fuzz input byte 0 and assigned to `opts.strict_json`, `opts.skip_unexpected_fields_in_json`, `opts.allow_non_utf8` before `ParseJson`.
> src/idl_parser.cpp:3950-3958: `if (opts.size_prefixed) { builder_.FinishSizePrefixed(...) } else { builder_.Finish(...) }`.
> src/idl_parser.cpp:3960-3964: `if (opts.require_json_eof) { EXPECT(kTokenEof); }`.
> src/idl_parser.cpp:3592-3596 (`Parser::Parse`): `if (opts.use_flexbuffers) { r = ParseFlexBuffer(...) } else { r = !ParseRoot(...).Check(); }`.
> tests/fuzzer/flatbuffers_monster_fuzzer.cc:131-137: `if (jsongen_1 != jsongen_2 && !opts.output_default_scalars_in_json) { opts.output_default_scalars_in_json = true; continue; }` — demonstrates GenText output depends on this flag.
> src/idl_parser.cpp:3010-3016 (`SetRootType`): changes `root_struct_def_` directly.

### P2.11 Input Expectations
**Claim:** `ParseJson`'s `json` argument must be a NUL-terminated C string (tokenizer scans until `kTokenEof`, which is driven off a NUL byte / end of buffer, not an explicit length). An empty input (`""`) is rejected with `"input file is empty"`. The top-level JSON value must (normally, non-flexbuffer path) be an object (`{`) matching the active `root_struct_def_`; `DoParseJson` explicitly checks `token_ != '{'` and calls `EXPECT('{')` otherwise. Only one top-level JSON object per `ParseJson`/builder lifetime is allowed before `builder_.Clear()` resets it — `builder_.GetSize()` non-zero triggers `"cannot have more than one json object in a file"`. Nested structure (vectors, unions, nested tables, enums, hex/float literal forms, `null`, union type+value ordering) is grammar handled deeper in `ParseTable`/`ParseAnyValue`/`ParseVectorDelimiters` (not directly in `ParseJson`), and a parse-depth counter (`parse_depth_counter_`, compile-time limited by `FLATBUFFERS_MAX_PARSING_DEPTH`, set to 24 in the OSS-Fuzz CMake build) guards against stack overflow from deeply nested JSON.
**Evidence:**
> src/idl_parser.cpp:3623: `if (Is(kTokenEof)) return Error("input file is empty");` (inside `StartParseFile`, called by `ParseJson`).
> src/idl_parser.cpp:3941-3947 (`DoParseJson`): `if (token_ != '{') { EXPECT('{'); } else { if (!root_struct_def_) return Error(...); if (builder_.GetSize()) { return Error("cannot have more than one json object in a file"); } ... }`
> tests/json_test.cpp:140-146: negative-path examples (`""`, `"{name: 1}"`, `"{name:+1}"`, `"{name:-f}"`, etc.) all expected `false`, demonstrating strict grammar checks reachable from `ParseJson`.
> tests/fuzzer/CMakeLists.txt:11-15: `if(NOT DEFINED FLATBUFFERS_MAX_PARSING_DEPTH) set(FLATBUFFERS_MAX_PARSING_DEPTH 24) ... # Force checking of RecursionError in the test` — confirms a depth guard is deliberately tightened for fuzzing.

## Other entries of the Logic Group

### flatbuffers::Parser::Parse (schema source)
**Claim:** Parses an `.fbs` schema (or `.proto` if `opts.proto_mode`) string, populating `structs_`, `enums_`, `root_struct_def_` (via a `root_type` schema statement or `SetRootType` calls made during parsing), and `IDLOptions`-dependent grammar tables. Must be called before `ParseJson` unless `Deserialize` is used instead. Parameters: `const char* _source` (NUL-terminated schema text), `const char** include_paths` (nullptr-terminated array, or `nullptr` to use CWD), `const char* source_filename` (used for include-resolution/error messages). Returns `bool`; `false` means schema errors (see `parser.error_`). If `opts.use_flexbuffers` is set, `Parse` instead calls `ParseFlexBuffer` into `flex_builder_`, bypassing schema-typed `root_struct_def_` entirely — in that mode `ParseJson`'s "root type" precondition is irrelevant (`Parser::ParseFlexBuffer`/`ParseFlexBufferValue` is the parse path, not `ParseJson`).
**Evidence:**
> include/flatbuffers/idl.h:1044-1056: doc comment "Parse the string containing either schema or JSON data..."; signature `bool Parse(const char* _source, const char** include_paths = nullptr, const char* source_filename = nullptr);`
> src/idl_parser.cpp:3586-3599: `bool Parser::Parse(...) { if (opts.use_flexbuffers) { r = ParseFlexBuffer(...) } else { r = !ParseRoot(...).Check(); } ... return r; }`
> tests/json_test.cpp:29,60,109: `parser.Parse(schemafile.c_str(), include_directories)` before `ParseJson`.
> Combination with target: a fuzzer typically calls `Parse(schema_text)` once (fixed, valid schema, e.g. from a corpus seed or embedded string) or once per input, then calls `ParseJson(fuzzed_json)` repeatedly on the resulting `root_struct_def_`.

### flatbuffers::Parser::Deserialize (schema source, binary variant)
**Claim:** Loads a serialized `reflection::Schema` buffer (`const uint8_t* buf, size_t size`, as produced by `Parser::Serialize()` or `flatc -b --schema`) directly into the Parser's symbol tables, setting `root_struct_def_` from `schema->root_table()`, without lexing `.fbs` text. Returns `bool`; `false` if the buffer fails `reflection::VerifySchemaBuffer`/`VerifySizePrefixedSchemaBuffer` or lacks the correct identifier, or if later `StructDef::Deserialize` steps fail. Typically called once at harness/fuzzer initialization (not per-input) to give a fixed, valid, structurally rich schema, then `ParseJson` is called repeatedly for each fuzz input against that same Parser.
**Evidence:**
> include/flatbuffers/idl.h:1079: `bool Deserialize(const uint8_t* buf, const size_t size);`
> src/idl_parser.cpp:4423-4442: `bool Parser::Deserialize(const uint8_t* buf, const size_t size) { flatbuffers::Verifier verifier(...); if (!reflection::SchemaBufferHasIdentifier(buf)) { if (!flatbuffers::BufferHasIdentifier(...)) return false; else size_prefixed = true; } ... if (!verify_fn(verifier)) { return false; } ... return Deserialize(schema); }`
> src/idl_parser.cpp:4490-4492: `if (schema->root_table() == *it) { root_struct_def_ = struct_def; }`
> tests/fuzzer/flatbuffers_monster_fuzzer.cc:97-105 (`LLVMFuzzerInitialize`): `parser_.Deserialize(reinterpret_cast<const uint8_t*>(schemafile.c_str()), schemafile.size());` called once per fuzzer process, outside `LLVMFuzzerTestOneInput`, exactly the pattern a harness for `ParseJson` should reuse.
> tests/json_test.cpp:137-140,151: `parser.Deserialize(reinterpret_cast<const uint8_t*>(schemafile.c_str()), schemafile.size())` then `parser.ParseJson(...)`.

### flatbuffers::Parser::SetRootType (config)
**Claim:** `bool SetRootType(const char* name)` looks up a previously-declared struct/table by (possibly namespace-qualified) `name` in `structs_` and assigns it to `root_struct_def_`, returning `true` iff found. Must be called after a schema is loaded (`Parse`/`Deserialize`) that already declared the named type; used to override the schema's own `root_type` statement or to select a different root for a differently-typed `ParseJson` call. Changes which `StructDef`/`FieldDef` tree subsequent `ParseJson` calls walk.
**Evidence:**
> include/flatbuffers/idl.h:1064: `// Set the root type. May override the one set in the schema.\n  bool SetRootType(const char* name);`
> src/idl_parser.cpp:3010-3016: `bool Parser::SetRootType(const char* name) { root_struct_def_ = LookupStruct(name); if (!root_struct_def_) root_struct_def_ = LookupStruct(current_namespace_->GetFullyQualifiedName(name)); return root_struct_def_ != nullptr; }`
> src/flatc.cpp:1009: `if (!parser->SetRootType(opts.root_type.c_str())) ...` — real caller (flatc CLI) checking the bool return and erroring out if the named type doesn't exist.
> src/idl_parser.cpp:3896: `if (!SetRootType(root_type.c_str()))` — used internally when parsing a schema's own `root_type` declaration.

### flatbuffers::GenText / GenerateText (result consumer)
**Claim:** `const char* GenText(const Parser& parser, const void* flatbuffer, std::string* _text)` serializes a binary FlatBuffer (typically `parser.builder_.GetBufferPointer()`, the output of a prior `ParseJson`) back to JSON text using the live `parser.root_struct_def_`/schema metadata. Returns `nullptr` on success (text written to `*_text`), or a non-null C-string error message (e.g., non-UTF-8 content) on failure. **Precondition** (asserted, not gracefully handled): `parser.root_struct_def_` must be non-null, i.e. `SetRootType`/`Parse`/`Deserialize` must already have been done — calling it without a root type triggers `FLATBUFFERS_ASSERT`. `GenerateText` is a deprecated alias with identical behaviour. Commonly used as a differential self-consistency check after `ParseJson` succeeds (parse JSON→binary, then binary→JSON, compare/re-parse).
**Evidence:**
> include/flatbuffers/idl.h:1286-1287,1295-1296: declarations of `GenerateText` and `GenText`.
> src/idl_gen_text.cpp:419-425: `const char* GenText(const Parser& parser, const void* flatbuffer, std::string* _text) { FLATBUFFERS_ASSERT(parser.root_struct_def_);  // call SetRootType() auto root = parser.opts.size_prefixed ? GetSizePrefixedRoot<Table>(flatbuffer) : GetRoot<Table>(flatbuffer); return GenerateTextImpl(parser, root, *parser.root_struct_def_, _text); }`
> src/idl_gen_text.cpp:413-416: `const char* GenerateText(...) { return GenText(parser, flatbuffer, _text); }` (deprecated alias).
> tests/fuzzer/flatbuffers_monster_fuzzer.cc:77-81: `if (parser_.ParseJson(input_json.c_str())) { ... TEST_NULL(GenText(parser_, parser_.builder_.GetBufferPointer(), &jsongen)); }` and lines 127-140 using the resulting `jsongen_1`/`jsongen_2` strings as a second `ParseJson` input for round-trip comparison — this is the project's own idiom combining `ParseJson` + `GenText` in a fuzz harness.

### flatbuffers::Parser::ParseFlexBuffer (variant, not driven by target directly)
**Claim:** `bool ParseFlexBuffer(const char* source, const char* source_filename, flexbuffers::Builder* builder)` is a schema-less alternative entry reached from `Parser::Parse` (not from `Parser::ParseJson`) when `opts.use_flexbuffers` is set. It calls `StartParseFile` then `ParseFlexBufferValue(builder)` and `builder->Finish()`. Because `ParseJson`'s own implementation (`DoParseJson`) unconditionally calls `ParseTable(*root_struct_def_, ...)` and does not check `opts.use_flexbuffers`, this entry is **not** reached through `ParseJson`; it is reached only through `Parser::Parse`. A harness that wants to exercise it must call `parser.Parse(flex_json_text)` with `opts.use_flexbuffers = true`, separate from (or instead of) calling `ParseJson`.
**Evidence:**
> src/idl_parser.cpp:3575-3584: `bool Parser::ParseFlexBuffer(const char* source, const char* source_filename, flexbuffers::Builder* builder) { ... auto ok = !StartParseFile(...).Check() && !ParseFlexBufferValue(builder).Check(); if (ok) builder->Finish(); ... return ok; }`
> src/idl_parser.cpp:3592-3596 (`Parser::Parse`): `if (opts.use_flexbuffers) { r = ParseFlexBuffer(source, source_filename, &flex_builder_); } else { r = !ParseRoot(...).Check(); }` — confirms `ParseFlexBuffer` is reached via `Parse`, not `ParseJson`.
> src/idl_parser.cpp:3601-3609 (`Parser::ParseJson`): no reference to `opts.use_flexbuffers` or `ParseFlexBuffer`/`flex_builder_`; always calls `DoParseJson` → `ParseTable(*root_struct_def_, ...)`.

## Build notes
**Claim:** Fuzz targets for this feature are built as C++ (C++17) via CMake, driven by OSS-Fuzz's `build.sh`, which configures `tests/fuzzer` with `-DOSS_FUZZ:BOOL=ON` and `make`s all fuzzer executables (including `monster_fuzzer`, built from `flatbuffers_monster_fuzzer.cc`, which is the project's own `ParseJson`-driving fuzzer). The target's header `flatbuffers/idl.h` is included via `-I${FLATBUFFERS_DIR}/include`; `flatbuffers/idl.h` and the matching `src/idl_parser.cpp`/`src/idl_gen_text.cpp`/`src/reflection.cpp`/`src/util.cpp` sources are compiled directly into each fuzzer binary (no separate pre-built libflatbuffers linked); the fuzzing engine is linked via `$ENV{LIB_FUZZING_ENGINE}`. The `monster_fuzzer` additionally needs a pre-generated `monster_test.bfbs` reflection-schema binary copied next to the executable (loaded at `LLVMFuzzerInitialize` time via `argv[0]`-relative path) for `Deserialize`.
**Evidence:**
> ossfuzz_view/flatbuffers-c1/build.sh:18-27: `cd $SRC/flatbuffers; mkdir build; cd build; cmake -DOSS_FUZZ:BOOL=ON -G "Unix Makefiles" ../tests/fuzzer; make; cp ../tests/fuzzer/*.dict $OUT/; cp *.bfbs $OUT/; cp *_fuzzer $OUT/`
> ossfuzz_view/flatbuffers-c1/Dockerfile:17-19: `FROM gcr.io/oss-fuzz-base/base-builder` ... `RUN git clone https://github.com/google/flatbuffers`
> tests/fuzzer/CMakeLists.txt:30-32: `add_compile_options(-std=c++17 ...)`
> tests/fuzzer/CMakeLists.txt:65-68,80-82: OSS_FUZZ-conditional use of `${CXX}`/`${CXXFLAGS}` and `$ENV{LIB_FUZZING_ENGINE}` for compile/link.
> tests/fuzzer/CMakeLists.txt:181-184: `include_directories(${FLATBUFFERS_DIR}/include); include_directories(${FLATBUFFERS_DIR}/tests); include_directories(${FLATBUFFERS_DIR}/src); include_directories(${FLATBUFFERS_DIR}/grpc);`
> tests/fuzzer/CMakeLists.txt:216-217,224-227: `add_executable(monster_fuzzer flatbuffers_monster_fuzzer.cc); target_link_libraries(monster_fuzzer PRIVATE flatbuffers_fuzzed);` plus a PRE_BUILD copy of `monster_test.bfbs` next to the binary.
> tests/fuzzer/flatbuffers_monster_fuzzer.cc:10,25: `#include "flatbuffers/idl.h"` and `LoadBinarySchema` reading `exe_path_.parent_path() / "monster_test.bfbs"`.