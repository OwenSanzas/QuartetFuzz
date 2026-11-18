## target: wabt::ReadBinaryIr

### P2.1 Init Sequence
**Claim:** No library-global init call is required. Before calling `ReadBinaryIr`, the caller must construct (in any order): a `wabt::Errors` object (empty vector), a `wabt::Module` object (default-constructed, used as out-param), and a `wabt::ReadBinaryOptions` (default-constructed or built from a `wabt::Features` instance). `ReadBinaryOptions.skip_function_bodies` must remain `false` (asserted inside the target).
**Evidence:**
> src/binary-reader-ir.cc:1892-1904:
> ```
> Result ReadBinaryIr(const char* filename, ByteSpan data,
>                      const ReadBinaryOptions& options, Errors* errors,
>                      Module* out_module) {
>   assert(!options.skip_function_bodies);
>   BinaryReaderIR reader(out_module, filename, errors);
>   return ReadBinary(data, &reader, options);
> }
> ```
> src/tools/wasm2wat.cc:105-115: `std::vector<uint8_t> file_data; ReadFile(...); Errors errors; Module module; ReadBinaryOptions options(...); result = ReadBinaryIr(s_infile.c_str(), file_data, options, &errors, &module);` (file_data read before the call; Errors/Module default-constructed immediately before).
> ossfuzz_view/wabt-c1/read_binary_ir_fuzzer.cc:30-44: `Errors errors; Module module; Features features; ... ReadBinaryOptions options(features, nullptr, false, true, false); ... ReadBinaryIr("", text.data(), text.size(), options, &errors, &module)`.

### P2.2 Parameter Construction
**Claim:**
- `filename` (`const char*`): any non-null C string, used only for diagnostics/location names; harnesses pass literal strings like `""` or `"dummy filename"`.
- `data` (`ByteSpan` = `std::span<const uint8_t>`, include/wabt/base-types.h:29): a view over the raw, untrusted fuzz bytes; no ownership, no NUL-termination required, any size (including 0) is accepted — the reader itself validates the minimal 8-byte magic+version header and returns `Result::Error` if too short/malformed.
- `options` (`const ReadBinaryOptions&`, include/wabt/binary-reader.h:34-53): default-constructible; fields `features` (`wabt::Features`), `log_stream` (nullable `Stream*`), `read_debug_names`, `stop_on_first_error`, `fail_on_custom_section_error` (bools), `skip_function_bodies` (bool, **must stay false** — asserted in the target).
- `errors` (`Errors*` = `std::vector<Error>*`, include/wabt/error.h:59): non-null pointer to a (typically empty) vector; the target/underlying reader appends `Error` entries to it, never reads existing entries.
- `out_module` (`Module*`, include/wabt/ir.h:1264): non-null pointer to a default-constructed `Module`; fully populated (or partially populated on error) as an out-parameter.
**Evidence:**
> include/wabt/base-types.h:29: `using ByteSpan = std::span<const uint8_t>;`
> include/wabt/binary-reader.h:34-53: `struct ReadBinaryOptions { ... Features features; Stream* log_stream = nullptr; bool read_debug_names = false; bool stop_on_first_error = true; bool fail_on_custom_section_error = true; bool skip_function_bodies = false; };`
> include/wabt/error.h:59: `using Errors = std::vector<Error>;`
> src/binary-reader-ir.cc:1901: `assert(!options.skip_function_bodies);`

### P2.3 Object Lifecycle
**Claim:** Create → use (no separate "configure" step for `Module`/`Errors`, they are value objects) → destroy via normal C++ scope/RAII. No explicit destroy/close function exists for `Module` or `Errors`; both are plain structs/`std::vector` destroyed automatically when they go out of scope. `BinaryReaderIR` itself is an internal, anonymous-namespace class, constructed and destroyed entirely inside `ReadBinaryIr`; the harness never touches it.
**Evidence:**
> src/binary-reader-ir.cc:1902-1903: `BinaryReaderIR reader(out_module, filename, errors); return ReadBinary(data, &reader, options);` (reader is a stack-local temporary, destroyed on return).
> include/wabt/ir.h:1264 `struct Module { ... }` — no virtual destructor / explicit free API declared; all members are value types/vectors.
> src/tools/wasm2wat.cc:108-109: `Errors errors; Module module;` declared as plain stack locals, no explicit destroy call anywhere in the same function.

### P2.4 Return Value Handling
**Claim:** Target returns `wabt::Result` (`enum class` wrapper, `Result::Ok` / `Result::Error`, `[[nodiscard]]`). Callers check with `Succeeded(result)` / `Failed(result)` or `CHECK_RESULT`. On `Result::Error`, `out_module` may be partially populated and `errors` contains one or more `Error` entries that should be formatted/reported; downstream calls (`ValidateModule`, `GenerateNames`, `WriteWat`) must be skipped.
**Evidence:**
> include/wabt/result.h:22-52: `struct [[nodiscard]] Result { enum Enum { Ok, Error }; ... }; inline bool Succeeded(Result result){return result==Result::Ok;} inline bool Failed(...){...}`
> src/tools/wasm2wat.cc:114-116: `result = ReadBinaryIr(...); if (Succeeded(result)) { ... validate/generate-names/apply-names/write ... }`
> src/tools/wasm2c.cc:135-136: `CHECK_RESULT(ReadBinaryIr(s_infile.c_str(), file_data, options, &errors, &module));` (macro: on Failed, immediately returns `Result::Error` from the caller).
> ossfuzz_view/wabt-c1/read_binary_ir_fuzzer.cc:44: `if (wabt::Succeeded(wabt::ReadBinaryIr(...))) { ValidateModule(...); ... }`

### P2.5 Cleanup Sequence
**Claim:** No manual free/close calls are required for any of `errors`, `module`, or `options`; all are stack-allocated value types cleaned up by normal scope exit (RAII) regardless of success or error path. The only "cleanup-like" step in real callers is formatting/printing accumulated `errors` (read-only consumption), not freeing.
**Evidence:**
> src/tools/wasm2wat.cc:143: `FormatErrorsToFile(errors, Location::Type::Binary);` called unconditionally after the `if (Succeeded(result))` block (handles both success/error paths; not a free, just reporting).
> src/wast-parser.cc:3851-3857: iterates `for (const auto& error : errors)` to build diagnostic messages — no destruction call.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase, verified by direct reads.
**Evidence:**
> include/wabt/binary-reader-ir.h:28-40: both `ReadBinaryIr` overloads declared.
> include/wabt/validator.h:32: `Result ValidateModule(const Module*, Errors*, const ValidateOptions&);`
> include/wabt/generate-names.h:31: `Result GenerateNames(struct Module*, NameOpts opts = NameOpts::None);`
> include/wabt/apply-names.h:41: `Result ApplyNames(struct Module*);`
> include/wabt/wat-writer.h:37: `Result WriteWat(Stream*, const Module*, const WriteWatOptions&);`
> include/wabt/binary-reader.h:518: `Result ReadBinary(ByteSpan data, BinaryReaderDelegate* reader, const ReadBinaryOptions& options);` (internal plumbing called by the target).

### P2.7 Co-call Constraints
**Claim:** `ReadBinaryIr` has no strict paired "begin/end" API of its own (it is a one-shot call that internally opens/closes the `BinaryReader`/`BinaryReaderIR` state machine). The project-level pairing/ordering constraint observed in every real caller is: `ReadBinaryIr` → (optional) `ValidateModule` → (optional) `GenerateNames` → (optional) `ApplyNames` → (optional) `WriteWat`, always gated by `Succeeded()` of the previous step. `GenerateNames` must precede `ApplyNames` if names are auto-generated (otherwise `ApplyNames` is a no-op improvement over existing `$name` bindings parsed from the binary's name section).
**Evidence:**
> src/tools/wasm2wat.cc:114-141 (shown above): strict ordering `ReadBinaryIr` → `ValidateModule` (if `s_validate`) → `GenerateNames` (if `s_generate_names`) → `ApplyNames` → `WriteWat`.
> src/tools/wasm2c.cc:135-141: `CHECK_RESULT(ReadBinaryIr(...)); CHECK_RESULT(ValidateModule(...)); CHECK_RESULT(GenerateNames(&module)); (void)ApplyNames(&module);`
> ossfuzz_view/wabt-c1/read_binary_ir_fuzzer.cc:44-53: `ReadBinaryIr` → `ValidateModule` → `WriteBinaryModule`/`WriteWat` (no `GenerateNames`/`ApplyNames` in this particular fuzzer, showing they are optional, not mandatory, consumers).

### P2.8 Prerequisite State
**Claim:** No external context/connection/global state is required. The only "prerequisite state" is the input byte buffer itself (must contain at least the 8-byte wasm magic+version header to pass `BinaryReader::ReadModule`) and the `ReadBinaryOptions.features` bitset, which gates which opcodes/section encodings are legal (e.g. SIMD, threads, multi-value, exceptions, tail-call, function-references, gc — see `feature.def`). `skip_function_bodies` must be `false`.
**Evidence:**
> src/binary-reader.cc:3254-3257: `Result BinaryReader::ReadModule(...) { uint32_t magic=0; CHECK_RESULT(ReadU32(&magic,"magic")); ERROR_UNLESS(magic==WABT_BINARY_MAGIC, "bad magic value"); ...}`
> src/binary-reader.cc:3259-3267: version/layer check: `ERROR_UNLESS(version == WABT_BINARY_VERSION, "bad wasm file version...")`.
> src/binary-reader-ir.cc:1901: `assert(!options.skip_function_bodies);`
> ossfuzz_view/wabt-c1/read_binary_ir_fuzzer.cc:34-37: feature flags toggled from fuzz input before constructing `ReadBinaryOptions`.

### P2.9 Result Usage
**Claim:** The filled `Module*` (and, on error, the `Errors*` vector) are the two outputs consumed afterwards:
- `ValidateModule(&module, &errors, validate_options)` — semantic/type validation, reads the `Module` fields built by the target, appends more `Error`s to the same `errors` vector.
- `GenerateNames(&module)` — mutates `Module` name bindings in place.
- `ApplyNames(&module)` — mutates `Module` `Var` references in place using names/bindings.
- `WriteWat(&stream, &module, wat_options)` — serializes the `Module` to text, treats it read-only.
- `WriteBinaryModule(&stream, &module, write_binary_options)` — re-serializes to binary (seen in the in-repo fuzzer), read-only on `Module`.
- `FormatErrorsToFile(errors, Location::Type::Binary)` / manual iteration over `errors` — consumes accumulated diagnostics for reporting.
**Evidence:**
> src/tools/wasm2wat.cc:117-140: `ValidateModule(&module, ...)`, `GenerateNames(&module)`, `ApplyNames(&module)`, `WriteWat(&stream, &module, wat_options)` all operate on the same `module` filled by `ReadBinaryIr`.
> src/tools/wasm-validate.cc:82-87: `ReadBinaryIr(...); if (Succeeded) { ValidateModule(&module, &errors, options); } FormatErrorsToFile(errors, ...);`
> ossfuzz_view/wabt-c1/read_binary_ir_fuzzer.cc:44-52: `ValidateModule(&module, ...)` then `WriteBinaryModule(&stream, &module, ...)` and `WriteWat(&stream, &module, ...)`.
> src/wast-parser.cc:3851-3857: iterates `errors` to build human-readable diagnostics tied to `bsm->loc`.

### P2.10 Behaviour Controls
**Claim:**
- `ReadBinaryOptions.features` (type `wabt::Features`, populated per-flag from `feature.def`): enables/disables opcode and section encodings (SIMD, threads, multi-value, exceptions, tail-call, function-references, gc, etc.) — determines which binary encodings the core (`BinaryReader`) accepts vs. rejects as errors.
- `ReadBinaryOptions.read_debug_names`: toggles whether `BinaryReader::ReadNameSection` callbacks run / are honored by `BinaryReaderIR` (affects whether `$name` debug symbols populate `Module` bindings).
- `ReadBinaryOptions.stop_on_first_error`: changes `ReadSections` error-recovery (stop immediately vs. continue parsing after the first bad section/opcode).
- `ReadBinaryOptions.fail_on_custom_section_error`: changes whether malformed custom sections (name/linking/reloc/dylink0) abort the whole read or are skipped/ignored.
- `ReadBinaryOptions.skip_function_bodies`: must be `false` for `ReadBinaryIr` (asserted) — true is only valid for other consumers like objdump that don't build label-closing IR.
- `ReadBinaryOptions.log_stream`: non-null enables verbose logging path through the reader (affects only diagnostics, not parse result).
- Downstream: `ValidateOptions.features` (same `Features` type) must match/be consistent with the features used for reading, or validation may reject constructs the reader accepted. `WriteWatOptions.features/fold_exprs/inline_export/inline_import` select textual-output formatting only.
**Evidence:**
> src/tools/wasm2wat.cc:111-113: `ReadBinaryOptions options(s_features, s_log_stream.get(), s_read_debug_names, kStopOnFirstError, s_fail_on_custom_section_error);`
> ossfuzz_view/wabt-c1/read_binary_ir_fuzzer.cc:34-42: all `WABT_FEATURE` flags toggled from fuzz bytes before constructing options; `read_debug_names=false`, `stop_on_first_error=true`, `fail_on_custom_section_error=false`.
> src/binary-reader-ir.cc:1897-1901 comment: "BinaryReaderIR does not support skipping function bodies ... Skipping the bodies here leaves each function's label unclosed" — explains why `skip_function_bodies` must stay false.
> include/wabt/wat-writer.h:28-35: `WriteWatOptions { Features features; bool fold_exprs; bool inline_export; bool inline_import; };`

### P2.11 Input Expectations
**Claim:** Input `data` must begin with the 4-byte ASCII wasm magic (`\0asm`) followed by a 2-byte version and 2-byte layer field; only `layer == WABT_BINARY_LAYER_MODULE` with `version == WABT_BINARY_VERSION` is accepted (component layer and other versions are rejected with an error, not a crash). Beyond the header, the body is the standard section sequence (type/import/function/table/memory/global/export/start/elem/code/data/tag/datacount/custom), each section is a `(id:u8, size:LEB128 u32, payload)` triple; counts/indices are LEB128-encoded; strings are length-prefixed UTF-8/bytes; no overall size limit is enforced beyond section/function nesting bounds (`kMaxNestingDepth=16384`, `kMaxFunctionLocals=50000`, `kMaxFunctionParams=1000`, `kMaxFunctionResults=1000` in `BinaryReaderIR`). There is no requirement for NUL-termination (it's a `ByteSpan`, i.e. length-delimited), and empty/truncated input simply yields `Result::Error` via `ERROR_UNLESS`/`CHECK_RESULT` macros rather than undefined behavior.
**Evidence:**
> src/binary-reader.cc:3254-3267: magic/version/layer checks quoted above.
> src/binary-reader-ir.cc:94-97: `static constexpr size_t kMaxNestingDepth = 16384; kMaxFunctionLocals = 50000; kMaxFunctionParams = 1000; kMaxFunctionResults = 1000;`
> src/test-binary-reader.cc:161-176: example minimal valid-prefix binary `{0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00, ...}` (magic+version) followed by type/function/custom-name sections, used directly as `ReadBinaryIr("test", data, options, &errors, &module)` input, demonstrating the expected wire format.
> include/wabt/base-types.h:29: `ByteSpan = std::span<const uint8_t>` — length-delimited, no NUL-termination needed.

### Other feature entries (how they combine with the target)

**wabt::ValidateModule** — `Result ValidateModule(const Module*, Errors*, const ValidateOptions&)` (include/wabt/validator.h:32).
- Prerequisite: must be called **after** a successful `ReadBinaryIr` (or any producer of a populated `Module`); takes the `Module*` filled by the target and the **same** `Errors*` vector (appends more errors) plus a `ValidateOptions` (wraps a `Features`, should normally match the features used during read — include/wabt/shared-validator.h:36-41).
- Return: `Result` — check with `Succeeded`/`Failed`/`CHECK_RESULT` as with the target.
- Nothing to free; `ValidateOptions` is a stack value.
- Evidence: src/tools/wasm-validate.cc:82-86 `result = ReadBinaryIr(...); if (Succeeded(result)) { ValidateOptions options(s_features); result = ValidateModule(&module, &errors, options); }`; ossfuzz_view/wabt-c1/read_binary_ir_fuzzer.cc:45-46 `ValidateOptions validate_options(features); if (Succeeded(ValidateModule(&module, &errors, validate_options))) {...}`.

**wabt::GenerateNames** — `Result GenerateNames(struct Module*, NameOpts opts = NameOpts::None)` (include/wabt/generate-names.h:31).
- Prerequisite: operates on an already-populated `Module*` (post `ReadBinaryIr`, typically post-`ValidateModule` success in real tools, though not strictly enforced by the API itself).
- Parameter: `NameOpts` enum (`None` or `AlphaNames`, include/wabt/generate-names.h:26-29) selects naming scheme for auto-generated names.
- Return: `Result`; callers in wasm2wat/wasm2c gate it behind `Succeeded(result)` of the previous step and/or wrap in `CHECK_RESULT`.
- Mutates `Module` in place; nothing to free.
- Evidence: src/tools/wasm2wat.cc:122-123 `if (s_generate_names) { result = GenerateNames(&module); }`; src/tools/wasm2c.cc:138 `CHECK_RESULT(GenerateNames(&module));`.

**wabt::ApplyNames** — `Result ApplyNames(struct Module*)` (include/wabt/apply-names.h:41).
- Prerequisite: run after `ReadBinaryIr` (and typically after `GenerateNames` if auto-names are desired) — rewrites index `Var`s to named `Var`s using bindings already present in `Module` (either parsed from the binary's name section or generated by `GenerateNames`).
- Return: `Result`, but real callers treat failure as non-fatal ("shouldn't fail... just be skipped") and explicitly discard it.
- Evidence: src/tools/wasm2wat.cc:126-130 `Result dummy_result = ApplyNames(&module); WABT_USE(dummy_result);`; src/tools/wasm2c.cc:139-141 same pattern with `(void)ApplyNames(&module);`.

**wabt::WriteWat** — `Result WriteWat(Stream*, const Module*, const WriteWatOptions&)` (include/wabt/wat-writer.h:37).
- Prerequisite: `Module*` filled by `ReadBinaryIr` (and typically validated); needs a `Stream*` (e.g. `FileStream` or `MemoryStream`) already constructed/open; `WriteWatOptions` wraps `Features` plus `fold_exprs`/`inline_export`/`inline_import` formatting flags (include/wabt/wat-writer.h:28-35).
- Return: `Result`; checked the same way.
- Nothing to explicitly free; `Stream` lifetime is managed by its own RAII (e.g. `FileStream` closes the file on destruction).
- Evidence: src/tools/wasm2wat.cc:133-140 `WriteWatOptions wat_options(s_features); ... FileStream stream(...); result = WriteWat(&stream, &module, wat_options);`; ossfuzz_view/wabt-c1/read_binary_ir_fuzzer.cc:51-52 `wabt::WriteWatOptions write_wat_options(features); wabt::WriteWat(&stream, &module, write_wat_options);` using a `wabt::MemoryStream`.

**Combined harness pattern (as already exercised by the project's own fuzzer):** build `Features` from fuzz bytes → `ReadBinaryOptions` → `ReadBinaryIr(filename, data, options, &errors, &module)` → if `Succeeded`, `ValidateModule(&module, &errors, validate_options)` → if `Succeeded`, optionally `GenerateNames(&module)` → `ApplyNames(&module)` (ignore its `Result`) → `WriteWat`/`WriteBinaryModule` into a `Stream`. This exact chain is implemented end-to-end in `ossfuzz_view/wabt-c1/read_binary_ir_fuzzer.cc:29-56`, which is itself compiled as one of the OSS-Fuzz targets for this project.

### Build notes
**Claim:** Fuzzers are plain C++ (compiled with `-std=c++20`) OSS-Fuzz libFuzzer targets. The target's header is reached via the project's installed `include/` directory added with `-Iinclude`, plus generated headers under `-Ibuild/include` (CMake-configured output, e.g. `feature.def`-derived or version headers). The library is built as a static archive `libwabt.a` via CMake and linked directly; no pkg-config/other external libraries beyond the libFuzzer engine.
**Evidence:**
> ossfuzz_view/wabt-c1/build.sh:19-30:
> ```
> mkdir build; cd build; cmake ..; cmake --build . --parallel; cd ..
> for fuzzers in $(find $SRC -maxdepth 1 -name '*_fuzzer.cc' | grep -v wasm_objdump_fuzzer); do
>   fuzz_basename=$(basename -s .cc $fuzzers)
>   $CXX $CXXFLAGS -std=c++20 -I. -Ibuild -Iinclude -Ibuild/include \
>   $fuzzers $LIB_FUZZING_ENGINE ./build/libwabt.a -o $OUT/$fuzz_basename
> done
> ```
> ossfuzz_view/wabt-c1/Dockerfile:17-23: `FROM gcr.io/oss-fuzz-base/base-builder`; clones `wabt` with submodules; copies `*_fuzzer.cc` + `build.sh`/`run_tests.sh` into `$SRC`.
> ossfuzz_view/wabt-c1/read_binary_ir_fuzzer.cc:20-27: includes `"wabt/binary-reader-ir.h"`, `"wabt/binary-reader.h"`, `"wabt/binary-writer.h"`, `"wabt/ir.h"`, `"wabt/option-parser.h"`, `"wabt/stream.h"`, `"wabt/validator.h"`, `"wabt/wat-writer.h"` — all resolved under `include/wabt/...` per the `-Iinclude` flag.