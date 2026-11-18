## target: sentencepiece::SentencePieceProcessor::LoadFromSerializedProto

### Target definition
**Claim:** `LoadFromSerializedProto` parses `serialized` (raw bytes) into a `ModelProto` with protobuf `ParseFromArray`, and on success forwards ownership of the parsed proto to `Load(std::unique_ptr<ModelProto>)`, which is the real sink that builds the whole tokenizer state (model, normalizer, denormalizer, special ids, self-test).
**Evidence:**
> src/sentencepiece_processor.cc:93-98:
> ```
> absl::Status SentencePieceProcessor::LoadFromSerializedProto(
>     absl::string_view serialized) {
>   auto model_proto = std::make_unique<ModelProto>();
>   RET_CHECK(model_proto->ParseFromArray(serialized.data(), serialized.size()));
>   return Load(std::move(model_proto));
> }
> ```
> src/sentencepiece_processor.h:166-168: "Loads model from `serialized`, which is a string-serialized model proto. Useful to load the model from a platform independent blob object." followed by the declaration.

### P2.1 Init Sequence
**Claim:** No library-global init call is required before `LoadFromSerializedProto`. The only prerequisite is a default-constructed `SentencePieceProcessor` instance. `LoadFromSerializedProto` must be called before any Encode/Decode/NBestEncode/SampleEncode/SetEncodeExtraOptions/SetDecodeExtraOptions call, because those all check `status()` first and fail if no model is loaded.
**Evidence:**
> src/sentencepiece_processor.cc:74-75: `SentencePieceProcessor::SentencePieceProcessor() {}` / `~SentencePieceProcessor() {}` — trivial ctor/dtor, nothing to initialize beforehand.
> src/sentencepiece_processor.cc:165-171: `status()` requires `model_` and `normalizer_` to be non-null (`RET_CHECK(model_) << "Model is not initialized."`), which are only set inside `Load(unique_ptr<ModelProto>)`.
> src/sentencepiece_processor_test.cc:126-128: `StatusTest` shows a fresh `SentencePieceProcessor sp; EXPECT_FALSE(sp.status().ok());` before any load.

### P2.2 Parameter Construction
**Claim:** `serialized` is an `absl::string_view` over an arbitrary byte buffer (the fuzz input itself is a valid candidate) — there is no length limit enforced by `LoadFromSerializedProto` itself; the limit checks happen later, inside `Load`'s callees (e.g. per-piece size limit). Any byte sequence can be passed; ParseFromArray will reject bytes that are not a valid serialized `ModelProto` (returns false → `RET_CHECK` fails → error Status).
**Evidence:**
> src/sentencepiece_processor.cc:96: `RET_CHECK(model_proto->ParseFromArray(serialized.data(), serialized.size()));` — any `(data,size)` pair is accepted as the argument; failure is caught here, not before the call.
> src/sentencepiece_processor_test.cc:957: `EXPECT_FALSE(sp.LoadFromSerializedProto("__NOT_A_PROTO__").ok());` demonstrates malformed bytes are safely rejected by returning a non-OK absl::Status (no crash expected on this path).

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: construct `SentencePieceProcessor` (stack or heap) → call `LoadFromSerializedProto(serialized)` once → check the returned `absl::Status` → on success use Encode/Decode/NBestEncode/SampleEncode/SetEncodeExtraOptions/SetDecodeExtraOptions → destroy the `SentencePieceProcessor` object (its destructor frees `model_`, `normalizer_`, `denormalizer_`, `model_proto_`, all `unique_ptr` members — no explicit free call needed).
**Evidence:**
> src/sentencepiece_processor.cc:100-153: `Load(unique_ptr<ModelProto>)` sets `model_proto_`, `model_`, `normalizer_`, optional `denormalizer_`, special ids, runs self-test, and returns final `absl::Status`.
> src/sentencepiece_processor.h:145-148: class holds no raw pointers requiring manual cleanup; only `virtual ~SentencePieceProcessor();` is declared, matched by the trivial empty destructor at sentencepiece_processor.cc:75 (members are smart pointers, destroyed automatically).
> src/sentencepiece_processor_test.cc:942-961: typical usage pattern — construct `SentencePieceProcessor sp;` on the stack, call `sp.LoadFromSerializedProto(...)`, check `.ok()`, no explicit destroy call (RAII via stack lifetime).

### P2.4 Return Value Handling
**Claim:** Return is `absl::Status`. Callers must check `.ok()` before using the processor; a non-OK status can come from (a) invalid protobuf bytes, (b) `Load`'s internal `RETURN_IF_ERROR(status())` failing because `model_->status()` or `normalizer_->status()` is bad (e.g. unknown model_type → `ModelFactory::Create` returns nullptr → `model_` stays null → `status()` RET_CHECK fails), or (c) self-test failures (`absl::InternalError("Self-test failures...")`).
**Evidence:**
> src/sentencepiece_processor.cc:96: `RET_CHECK(model_proto->ParseFromArray(...))` — proto parse failure path.
> src/sentencepiece_processor.cc:115: `RETURN_IF_ERROR(status());` inside `Load` — model/normalizer validity check.
> src/sentencepiece_processor.cc:142-150: self-test failure returns `absl::InternalError("Self-test failures. See LOG(INFO).")`.
> src/model_factory.cc:45-47: unknown `model_type()` → `LOG(ERROR) << "Unknown model_type..."; return nullptr;` which later makes `status()` fail with "Model is not initialized."
> processor_text_fuzzer.cc:41-46: real caller checks `status.ok()` and aborts (`abort()`) on failure, confirming the expected handling pattern.

### P2.5 Cleanup Sequence
**Claim:** No manual free calls exist in this API; all internal state (`model_proto_`, `model_`, `normalizer_`, `denormalizer_`) is held in `std::unique_ptr` members of `SentencePieceProcessor` and is released automatically by the class destructor regardless of success/failure of `LoadFromSerializedProto`/`Load`. Re-calling `LoadFromSerializedProto`/`Load` on an already-loaded processor is safe and simply replaces the old `unique_ptr`s (old objects freed via `operator=`/`std::move`).
**Evidence:**
> src/sentencepiece_processor.cc:102-110: `model_proto_ = std::move(model_proto); model_ = ModelFactory::Create(...); normalizer_ = std::make_unique<...>(...);` — plain assignment of unique_ptrs, old contents destroyed automatically.
> src/sentencepiece_processor.h declares only `virtual ~SentencePieceProcessor();` (no explicit `Close`/`Free`/`Destroy` method exists for this class) — confirmed by scanning the class declaration (lines 145-320) for any Free/Close/Destroy method: none found.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current source tree.
**Evidence:**
> src/sentencepiece_processor.h:152,160,164,168,171,174,177,183,187,191,195,199,206,211,234,239,251,254,257,260,263: declarations of `Load` (3 overloads), `LoadOrDie`, `LoadFromSerializedProto`, `status`, `SetEncodeExtraOptions`, `SetDecodeExtraOptions`, `Encode` (4 overloads), `Decode` (5 overloads), `NBestEncode` (3 overloads), `SampleEncode` (3 overloads).
> src/sentencepiece_processor.cc:77-98,100-163,185-619,1248-1400+: matching definitions found for all of the above.
> src/model_factory.cc:28: `ModelFactory::Create` exists.
> src/model_interface.cc:81: `ModelInterface::InitializePieces` exists.
> (unigram::Model, bpe::Model, normalizer::Normalizer::Init/DecodePrecompiledCharsMap/Normalize are part of Core, confirmed to exist via grep of unigram_model.cc/bpe_model.cc/normalizer.cc in the same source tree, not detailed further here since they are internal Core per the task framing.)

### P2.7 Co-call Constraints
**Claim:** There is no explicit lock/unlock or begin/commit pairing. The only "pairing" constraint is: `LoadFromSerializedProto`/`Load` must precede any `status()`-gated call (`Encode`, `Decode`, `NBestEncode`, `SampleEncode`, `SetEncodeExtraOptions`, `SetDecodeExtraOptions`) — enforced at runtime via `RET_CHECK_STATUS_STL`/`RET_CHECK_STATUS_PROTO` macros that call `RETURN_IF_ERROR(status())` at the top of each consumer. There is no required "unload" call; the object can simply be destroyed or `Load`ed again.
**Evidence:**
> src/sentencepiece_processor.cc:173-181:
> ```
> #define RET_CHECK_STATUS_STL(container) \
>   RETURN_IF_ERROR(status());            \
>   ...
> #define RET_CHECK_STATUS_PROTO(proto)   \
>   RETURN_IF_ERROR(status());            \
>   ...
> ```
> src/sentencepiece_processor.cc:1250 (EncodeOptimized), :1320 (DecodeOptimized), :397 (NBestEncode/SentencePieceText), :255 (SampleEncode) all begin with these macros, gating on `status()`.
> src/sentencepiece_processor.cc:1198: `SetEncodeExtraOptions`/`SetDecodeExtraOptions` path (`ParseExtraOptions`) also calls `RETURN_IF_ERROR(status())` before parsing.

### P2.8 Prerequisite State
**Claim:** No external context, config file, or network/filesystem connection is required for `LoadFromSerializedProto` (unlike `Load(absl::string_view filename)` which needs a readable file via `io::LoadModelProto`). The only prerequisite is the in-memory `serialized` byte buffer itself (fuzz input bytes can be fed directly).
**Evidence:**
> src/sentencepiece_processor.cc:77-81: `Load(filename)` calls `io::LoadModelProto(filename, model_proto.get())` — filesystem dependent — confirming `LoadFromSerializedProto` is the filesystem-free alternative.
> src/sentencepiece_processor.cc:93-98: `LoadFromSerializedProto` only touches the passed-in `serialized` buffer, no filesystem/network calls.

### P2.9 Result Usage
**Claim:** The `absl::Status` return of `LoadFromSerializedProto` must be checked; once OK, the now-populated `SentencePieceProcessor` object itself (not a separate returned handle) is passed by reference/this-pointer into `Encode`, `Decode`, `NBestEncode`, `SampleEncode`, `SetEncodeExtraOptions`, `SetDecodeExtraOptions`, and also queried via `model_proto()`, `GetPieceSize()`, `PieceToId()`, `IdToPiece()`, `IsUnknown()`, `IsControl()`, `IsByte()` etc. (member accessors that read the data loaded by `Load`). Real fuzz driver and tests show this chain explicitly.
**Evidence:**
> ossfuzz_view/.../processor_text_fuzzer.cc:41-46: `auto status = g_processor->LoadFromSerializedProto(model_data); if (!status.ok()) { ...; abort(); }`
> ossfuzz_view/.../processor_text_fuzzer.cc:60-102: after successful load, same `g_processor` used for `Encode(text,&pieces)`, `Decode(pieces,&decoded)`, `Encode(text,&ids)`, `Decode(ids,&decoded)`, `NBestEncode(text,3,&nbest_pieces/ids)`, `SampleEncode(text,-1,0.5/0.1,&sampled/ids)`.
> src/sentencepiece_processor_test.cc:958-960: after `LoadFromSerializedProto`, test reads back `sp.model_proto().SerializeAsString()` to verify the loaded proto equals the input proto — demonstrates `model_proto()` as a consumer of the load result.

### P2.10 Behaviour Controls
**Claim:** The processing path is selected entirely by fields inside the serialized `ModelProto`/`TrainerSpec`/`NormalizerSpec` bytes, not by extra parameters to `LoadFromSerializedProto` itself:
- `trainer_spec.model_type()` ∈ {UNIGRAM, BPE, WORD, CHAR} selects which `ModelInterface` subclass `ModelFactory::Create` instantiates (unknown value → `nullptr` model → load fails).
- `normalizer_spec.precompiled_charsmap()` (empty vs. present) controls whether `Normalizer::Init`/`DecodePrecompiledCharsMap` builds a Darts trie or falls back to identity/basic normalization.
- `denormalizer_spec` presence (`has_denormalizer_spec()` and non-empty `precompiled_charsmap()`) controls whether a second `normalizer::Normalizer` (`denormalizer_`) is constructed.
- `self_test_data.samples` drives an automatic self-test loop executed inside `Load`, already calling `Encode` before `LoadFromSerializedProto` even returns.
Downstream, `SetEncodeExtraOptions`/`SetDecodeExtraOptions` strings `"bos"/"eos"/"reverse"/"unk"/"unk_piece"` (colon-separated) select extra post-processing steps (`ApplyExtraOptions`) on Encode/Decode results, and `SampleEncode`'s `nbest_size`/`alpha` parameters select Viterbi-nbest vs. lattice-sampling (unigram) or BPE dropout-merge sampling.
**Evidence:**
> src/model_factory.cc:32-49: `switch (trainer_spec.model_type())` dispatch to `unigram::Model`/`bpe::Model`/`word::Model`/`character::Model`, default returns `nullptr`.
> src/sentencepiece_processor.cc:104-110: `normalizer_ = std::make_unique<normalizer::Normalizer>(model_proto_->normalizer_spec(), ...)`; `if (model_proto_->has_denormalizer_spec() && !... .precompiled_charsmap().empty()) denormalizer_ = ...`.
> src/sentencepiece_processor.cc:131-150: self-test loop `for (const auto& s : model_proto_->self_test_data().samples()) { RETURN_IF_ERROR(Encode(s.input(), &sps)); ... }`.
> src/sentencepiece_processor.cc:1200-1219: `extra_option_map` = {"bos","eos","reverse","unk","unk_piece"} parsed by `ParseExtraOptions`.
> src/sentencepiece_processor.h:217-236 (SampleEncode doc comment): "Unigram ... `nbest_size` ... `alpha` ... BPE ... `alpha`: The dropout probability ... nbest_size parameter is ignored in BPE."

### P2.11 Input Expectations
**Claim:** `LoadFromSerializedProto`'s input is expected to be a byte-for-byte serialized protobuf `ModelProto` message (binary wire format, not text/JSON), parsed with `ParseFromArray(data, size)`. There is no required header/magic and no explicit size limit in `LoadFromSerializedProto` itself, but internal consumers enforce a per-piece size limit (`kMaxPieceSize = 8000` bytes, a piece string `>= 8000` bytes is rejected with `"piece is too long."`), and the normalizer expects `precompiled_charsmap` to be a well-formed Darts double-array blob (or empty). `ParseFromArray` returning `false` (malformed/truncated/corrupt protobuf bytes) is the only "format" check made directly inside the target function.
**Evidence:**
> src/sentencepiece_processor.cc:96: `RET_CHECK(model_proto->ParseFromArray(serialized.data(), serialized.size()));`
> src/model_interface.cc:92-97: `static constexpr size_t kMaxPieceSize = 8000; if (sp.piece().size() >= kMaxPieceSize) { status_ = absl::InternalError("piece is too long."); return; }` inside `InitializePieces`, which runs as part of `ModelInterface`'s construction (called from `ModelFactory::Create` → `unigram::Model`/`bpe::Model`/etc. constructors) during `Load`.
> src/sentencepiece_processor_test.cc:957: `"__NOT_A_PROTO__"` is used specifically as a malformed/non-protobuf input to exercise the `ParseFromArray` failure branch.

### Other Entries (API surface combined with the target in one harness)

#### `Load(const ModelProto&)` / `Load(std::unique_ptr<ModelProto>)`
**Claim:** Alternate entry points funneling into the same core as `LoadFromSerializedProto`. `Load(const ModelProto&)` copies the proto then calls `Load(unique_ptr<ModelProto>)`; `Load(unique_ptr<ModelProto>)` is the common sink (builds `model_`, `normalizer_`, `denormalizer_`, special ids, self-test). A harness building a `ModelProto` object directly (rather than from raw bytes) would use these instead of `LoadFromSerializedProto`, but since the target IS `LoadFromSerializedProto`, the harness should prefer feeding raw fuzz bytes through it (exercises `ParseFromArray` too).
**Evidence:** src/sentencepiece_processor.cc:87-91, 100-153.
**Returns:** `absl::Status`, same error semantics as described in P2.4.
**Free:** none beyond normal object destruction (P2.5).

#### `Load(absl::string_view filename)`
**Claim:** Loads the same serialized-proto bytes from a file via `io::LoadModelProto` instead of memory, then also funnels into `Load(unique_ptr<ModelProto>)`. Not suitable for a byte-buffer fuzz harness (requires filesystem I/O) — the task description confirms this is a "source" variant, not recommended for the harness built around `LoadFromSerializedProto`.
**Evidence:** src/sentencepiece_processor.cc:77-81.

#### `SetEncodeExtraOptions(absl::string_view extra_option)` / `SetDecodeExtraOptions(absl::string_view extra_option)`
**Claim:** Must be called AFTER a successful `LoadFromSerializedProto`/`Load`, because `ParseExtraOptions` calls `RETURN_IF_ERROR(status())` first. Parameter is a colon-separated string built from {"bos","eos","reverse","unk","unk_piece"}; unknown tokens cause a `RET_CHECK` failure (non-OK Status). "bos"/"eos" additionally require that the loaded model actually defines a BOS/EOS id (`bos_id()!=-1`/`eos_id()!=-1`), else fails. Return `absl::Status` to check. No explicit cleanup (state is a `std::vector<ExtraOption>` member, destroyed with the processor).
**Evidence:** src/sentencepiece_processor.cc:155-163, 1190-1221; src/sentencepiece_processor.h:173-177.
**Combine with target:** call once after `LoadFromSerializedProto` returns OK, optionally before Encode/Decode, to toggle BOS/EOS/reverse/unk-piece insertion in later Encode/Decode calls (`ApplyExtraOptions` consumes this state, see src/sentencepiece_processor.cc:374,524,1314).

#### `Encode(absl::string_view input, std::vector<std::string>* pieces)` / `Encode(..., std::vector<int>* ids)` / `Encode(..., SentencePieceText* spt)`
**Claim:** Consumer of the loaded state; requires `status().ok()` (checked via `RET_CHECK_STATUS_STL`/`PROTO` macros at entry). `input` is arbitrary UTF-8 fuzz text (no format requirement enforced beyond being a `string_view`); dispatches to `normalizer_->Normalize()` then `model_->Encode()`/`EncodeOptimized` (unigram Viterbi or BPE merge depending on model_type loaded). Output containers are cleared and filled; caller owns them (stack-allocated `std::vector`/proto, no explicit free beyond normal destruction). Returns `absl::Status`.
**Evidence:** src/sentencepiece_processor.cc:185-193 (`Encode` → `EncodeOptimized`), :379-392 (`Encode(..., SentencePieceText*)` uses `normalizer_->Normalize` + `model_->Encode`), :1248-1315 (`EncodeOptimized` body with `RET_CHECK_STATUS_STL`, byte-fallback and unk-piece handling).
**Combine with target:** called after `LoadFromSerializedProto` succeeds, as shown in processor_text_fuzzer.cc:60-82.

#### `Decode(absl::Span<const std::string|absl::string_view> pieces, std::string*/SentencePieceText*)` and `Decode(absl::Span<const int> ids, ...)`
**Claim:** Consumer that walks the vocabulary (`PieceToId`, `IdToPiece`, `IsByte`, `IsControl`, `IsUnknown`) built at `Load` time; requires `status().ok()`. Typically fed with the output of a prior `Encode` call (pieces or ids), exercising vocabulary lookup in the reverse direction. Output string/proto cleared and filled by the call; returns `absl::Status`.
**Evidence:** src/sentencepiece_processor.cc:195-210 (`Decode` wrappers → `DecodeOptimized`), :1318-1400 (`DecodeOptimized` body, begins with `RET_CHECK_STATUS_STL`).
**Combine with target:** processor_text_fuzzer.cc:63-69, :75-81 — `Encode` then `Decode` of its own output, after a prior successful `LoadFromSerializedProto`.

#### `NBestEncode(absl::string_view input, int nbest_size, std::vector<std::vector<std::string>>*/<int>*/NBestSentencePieceText*)`
**Claim:** Consumer requiring `status().ok()` and `nbest_size <= kMaxNBestSize (512)`; also requires `model_->IsNBestEncodeAvailable()` (not all model types support nbest — e.g. BPE in sampling-only mode may not). Internally calls `normalizer_->Normalize()` then `model_->NBestEncode()`. Returns `absl::Status`; non-OK if `nbest_size` out of range, model doesn't support nbest, or result empty.
**Evidence:** src/sentencepiece_processor.cc:394-420 (`NBestEncode(..., NBestSentencePieceText*)` with `RET_CHECK_LE(nbest_size, kMaxNBestSize)`, `RET_CHECK(model_->IsNBestEncodeAvailable())`); kMaxNBestSize=512 at line 70.
**Combine with target:** processor_text_fuzzer.cc:85-93, called with `nbest_size=3` after successful load.

#### `SampleEncode(absl::string_view input, int nbest_size, float alpha, std::vector<std::string>*/<int>*/SentencePieceText*)`
**Claim:** Consumer requiring `status().ok()`; `nbest_size`/`alpha` select Unigram nbest-sampling vs. lattice-sampling (unigram) or BPE merge-dropout probability (BPE); doc comment at header clarifies semantics per model type. Returns `absl::Status`.
**Evidence:** src/sentencepiece_processor.h:214-240 (doc comments on sampling semantics); src/sentencepiece_processor.cc:252-266, 422-468 (`SampleEncode` bodies, `RET_CHECK_STATUS_STL`/`PROTO` gating).
**Combine with target:** processor_text_fuzzer.cc:95-102, called with `nbest_size=-1, alpha=0.5/0.1` after successful load; ossfuzz sample_encode_fuzzer.cc:25-36 calls the related `SampleEncodeAsPieces` convenience wrapper (not declared in the excerpted header range above — note this wrapper is a legacy/simple-API convenience over `SampleEncode`, confirming the same underlying path is fuzzed independently of `LoadFromSerializedProto` in that particular fuzzer, which constructs an *empty, unloaded* `SentencePieceProcessor` and therefore always exercises only the `status()`-failure early-return path — not a working example of P2-compliant usage).

### Build notes
**Claim:** Fuzzers are plain C++ (`-std=c++20`), compiled directly against the installed public header `sentencepiece_processor.h` under the CMake-installed prefix (`./root/include`), and statically linked against `libsentencepiece.a` (runtime) and `libsentencepiece_train.a` (trainer, used only by `trainer_fuzzer`/`generate_model`), plus vendored/fetched abseil and protobuf static archives. No special sanitizer-only defines are needed for the target; `LLVMFuzzerTestOneInput`/`LLVMFuzzerInitialize` are plain C-linkage/extern-"C" entry points as in the existing `processor_text_fuzzer.cc`.
**Evidence:**
> ossfuzz_view/.../Dockerfile:17-21: `FROM gcr.io/oss-fuzz-base/base-builder`; clones `sentencepiece` repo; copies `build.sh`,`*.cc` into `$SRC`.
> ossfuzz_view/.../build.sh:23-24: `cmake ../ -B ./ -DSPM_ENABLE_SHARED=ON -DCMAKE_INSTALL_PREFIX=./root -DSPM_BUILD_TEST=ON` then `cmake --build ./ --config Release --target install`.
> ossfuzz_view/.../build.sh:90-101 (fuzzer build loop):
> ```
> for fuzzer in $(find $SRC -name '*_fuzzer.cc' | grep -v 'third_party'); do
>   $CXX $CXXFLAGS -std=c++20 \
>       -I. -I./root/include -I../third_party $PROTOBUF_INC \
>       $fuzzer $LIB_FUZZING_ENGINE \
>       -Wl,--start-group \
>       ./root/lib/libsentencepiece_train.a ./root/lib/libsentencepiece.a \
>       $PROTOBUF_LIBS $ABSL_LIBS \
>       -Wl,--end-group \
>       -lpthread \
>       -o $OUT/$fuzz_basename
> done
> ```
> `./root/include` is the CMake install destination for public headers including `sentencepiece_processor.h` (confirmed by `-DCMAKE_INSTALL_PREFIX=./root` at build.sh:23 and the existing fuzzer `#include "sentencepiece_processor.h"` at ossfuzz_view/.../processor_text_fuzzer.cc:27 and sample_encode_fuzzer.cc:22, which compile successfully with just `-I./root/include`).
> ossfuzz_view/.../build.sh:34,40: `ABSL_LIBS=$(find ./root/lib _deps -name 'libabsl_*.a' ...)`, `PROTOBUF_LIBS=$(find _deps/protobuf-build -name 'libprotobuf*.a' -o -name 'libutf8_*.a' ...)` — abseil/protobuf archives linked explicitly because they are fetched via CMake `FetchContent` rather than vendored.
> ossfuzz_view/.../processor_text_fuzzer.cc:26-30: uses `#include <fuzzer/FuzzedDataProvider.h>` and `#include "sentencepiece_processor.h"` — standard libFuzzer harness pattern, C++ language, no C-API wrapper needed since `SentencePieceProcessor` is the public C++ class.

### Summary notes for Harness Generator
- Construct a `sentencepiece::SentencePieceProcessor` object (stack or heap), call `LoadFromSerializedProto(absl::string_view(data, size))` directly with fuzz bytes (this is the only required "init" step for the target — no separate global library init exists).
- Always check the returned `absl::Status::ok()`; if not ok, return/skip further processing for that input (the project's own `LoadInvalidModelTest`/`LoadSerializedProtoTest` tests establish that malformed bytes are expected to safely produce a non-OK Status, not a crash).
- Only after `ok()`, optionally call `SetEncodeExtraOptions`/`SetDecodeExtraOptions`, then exercise `Encode`/`Decode`/`NBestEncode`/`SampleEncode` with fuzz-derived text/ids, mirroring `processor_text_fuzzer.cc`'s structure (one persistent processor across many `LLVMFuzzerTestOneInput` calls is also a valid pattern — see `LLVMFuzzerInitialize` usage — but loading the model from the fuzz input itself, per-iteration, is closer to the stated target of fuzzing `LoadFromSerializedProto`'s own parsing/building logic).
- No explicit destroy/free call is required beyond normal C++ object lifetime (RAII via unique_ptr members, see P2.5).