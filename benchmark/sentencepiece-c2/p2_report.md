## target: sentencepiece::SentencePieceProcessor::Encode

### Declaration & definition
- Declared: `src/sentencepiece_processor.h:183` — `virtual absl::Status Encode(absl::string_view input, std::vector<std::string>* pieces) const;` (overloads at :187 for `std::vector<int>* ids`, :251 for `SentencePieceText* spt`).
- Defined: `src/sentencepiece_processor.cc:185-188` (pieces overload) delegates to `EncodeOptimized<std::string>`; `:190-193` (ids overload) delegates to `EncodeOptimized<int>`; `:379-392` (SentencePieceText overload) calls `normalizer_->Normalize` then `model_->Encode` then `PopulateSentencePieceText`.
- `EncodeOptimized<T>` template: `src/sentencepiece_processor.cc:1247-1315`.

### P2.1 Init Sequence
**Claim:** A model must be loaded (via `Load`/`Load(ModelProto)`/`Load(unique_ptr<ModelProto>)`/`LoadFromSerializedProto`, or test-only `SetModel`+`SetNormalizer`) before `Encode` is called; `Encode` internally re-checks via `status()`. No other global init is required.
**Evidence:**
> src/sentencepiece_processor.cc:1250: `EncodeOptimized` begins with `RET_CHECK_STATUS_STL(output)` macro which expands to `RETURN_IF_ERROR(status()); RET_CHECK(container) ...` (macro at :173-176).
> src/sentencepiece_processor.cc:165-171: `status()` requires `model_` and `normalizer_` to be non-null and both their `status()` to be OK — these are only set in `Load(...)` (:100-153) or test-only `SetModel`/`SetNormalizer` (:1224-1231).
> src/sentencepiece_processor_test.cc:134-149: typical test sets `sp.SetModel(std::move(mock)); sp.SetNormalizer(std::make_unique<normalizer::Normalizer>(normalization_spec));` before calling `sp.Encode(...)`.
> ossfuzz_view/sentencepiece-c2/model_load_fuzzer.cc:40-52: real-world order is `processor.LoadFromSerializedProto(model_data)` → check `status.ok()` → then `processor.Encode(test_text, &pieces)`.

### P2.2 Parameter Construction
**Claim:** `input` is `absl::string_view` over arbitrary bytes (no required encoding check inside `Encode` itself — invalid UTF-8 is tolerated/handled downstream by the normalizer/byte-fallback, not rejected). `pieces`/`ids`/`spt` output pointer must be non-null; `Encode` clears it first.
**Evidence:**
> src/sentencepiece_processor.h:183-184, 187-188, 251-252: parameter types.
> src/sentencepiece_processor.cc:173-176 (`RET_CHECK_STATUS_STL`) and :178-181 (`RET_CHECK_STATUS_PROTO`): both macros do `RET_CHECK(container/proto) << "output ... is null"` then clear it — passing `nullptr` returns an error status rather than crashing.
> src/sentencepiece_processor.cc:1252-1255: empty `input` is a valid, explicitly handled fast path (`output->clear(); return ApplyExtraOptions(...)`).

### P2.3 Object Lifecycle
**Claim:** Construct `SentencePieceProcessor` (stack/heap, no args) → `Load*` once → optionally `SetEncodeExtraOptions`/`SetDecodeExtraOptions` → call `Encode`/`NBestEncode`/`SampleEncode`/`Decode` any number of times (const, thread-safe for read-only use) → destroy processor (regular destructor, no explicit close call).
**Evidence:**
> src/sentencepiece_processor.h:146-148: `SentencePieceProcessor(); virtual ~SentencePieceProcessor();` — default ctor/dtor, no factory function.
> src/sentencepiece_processor.cc:74-75: trivial empty ctor/dtor — no explicit "close"/"unload" API exists; destroying the object (or re-`Load`ing) releases `model_`/`normalizer_` unique_ptrs.
> src/sentencepiece_processor.cc:100-153: `Load` is where `model_` and `normalizer_` (and `denormalizer_`) are (re)assigned; calling `Load` again on the same object safely replaces the previous model (unique_ptr assignment).

### P2.4 Return Value Handling
**Claim:** `Encode` returns `absl::Status`; callers must check `.ok()` before trusting the output container, since on error the container is left cleared (or cleared again inside `ApplyExtraOptions`'s `default:` branch). Ignoring the status (`.IgnoreError()`) is explicitly supported in header docs but output content is then only reliable when status was OK.
**Evidence:**
> src/sentencepiece_processor.h:57: doc comment: `sp.Encode("hello world.", &sps).IgnoreError();`
> src/sentencepiece_processor.cc:1269: `RET_CHECK(!piece.first.empty()) << "Empty piece is not allowed."` — a model/engine bug surfaces as a non-OK status, not a crash.
> src/sentencepiece_processor_test.cc:152,156,160: tests always do `EXPECT_TRUE(sp.Encode(...).ok())` before reading the output.
> ossfuzz_view/sentencepiece-c2/model_load_fuzzer.cc:40-52: harness checks `status.ok()` after `LoadFromSerializedProto` but calls `Encode` unconditionally afterward and does not check its own return (acceptable since output container is always in a defined state — cleared on error).

### P2.5 Cleanup Sequence
**Claim:** No manual free is required for `Encode`'s output (`std::vector<std::string>`, `std::vector<int>`, or `SentencePieceText` are all owned, stack-allocated or regular heap objects with automatic/RAII destruction). `model_`/`normalizer_` owned by the processor are freed when the processor is destroyed or re-`Load`ed.
**Evidence:**
> src/sentencepiece_processor.h:146-148: only ctor/dtor; no `Close`/`Release`/`Free` method declared in the class.
> src/sentencepiece_processor.cc:212-230 and 252-266: `NBestEncode`/`SampleEncode` (pieces overloads) use `Arena arena; auto* spt = Arena::Create<...>(&arena);` — arena-allocated proto freed automatically when `arena` goes out of scope (stack).

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase (verified by Grep/Read), except `EncodeAsSerializedProto`/`SampleEncodeAsSerializedProto`, which the harness comments say were "dropped/removed upstream" — these must NOT be used.
**Evidence:**
> src/sentencepiece_processor.h:183-336 (declares Encode, Decode, NBestEncode, SampleEncode, SetEncodeExtraOptions, SetDecodeExtraOptions, EncodeAsPieces, EncodeAsIds, NBestEncodeAsPieces/Ids, SampleEncodeAsPieces/Ids, ParallelEncode*).
> ossfuzz_view/sentencepiece-c2/model_load_fuzzer.cc:102-105: "Upstream dropped EncodeAsSerializedProto/SampleEncodeAsSerializedProto. The piece-returning overloads reach the same encode paths" — confirms absence of those two APIs in this codebase version.
> src/sentencepiece_processor.cc:77-98, 100-153, 155-163, 185-281, 379-469: definitions for Load/Load/Load/LoadFromSerializedProto, SetEncodeExtraOptions/SetDecodeExtraOptions, Encode/NBestEncode/SampleEncode all confirmed present.

### P2.7 Co-call Constraints
**Claim:** `Load*` must precede `Encode`/`Decode`/`NBestEncode`/`SampleEncode`/`SetEncodeExtraOptions` (enforced via `status()` check in each). `SetEncodeExtraOptions` must be called after `Load` (it calls `status()` and reads `bos_id()`/`eos_id()`, which are only valid post-Load). There is no explicit lock/unlock or begin/commit pairing — `Encode` is `const` and callers can call it repeatedly/concurrently once the model is loaded.
**Evidence:**
> src/sentencepiece_processor.cc:155-158: `SetEncodeExtraOptions` → `ParseExtraOptions` → src/sentencepiece_processor.cc:1198 `RETURN_IF_ERROR(status())` then :1213,1217 checks `bos_id()`/`eos_id()` (populated at Load time, :122-126).
> src/sentencepiece_processor_test.cc:1060-1073: `sp.SetEncodeExtraOptions("bos")` is called only after the test harness already has a loaded model (via an earlier `Load`/`SetModel` in the fixture), then `sp.Encode(...)`.

### P2.8 Prerequisite State
**Claim:** Prerequisite in-memory state before `Encode`: non-null `model_` (a concrete `ModelInterface` subclass chosen by `model_type` in the loaded `ModelProto.trainer_spec()`), non-null `normalizer_` built from `ModelProto.normalizer_spec()`/`trainer_spec()`, and `model_->status()`/`normalizer_->status()` both OK (e.g., trie built successfully, no NaN/Inf piece scores, valid normalizer charsmap). No file descriptors/sockets/global singletons are required.
**Evidence:**
> src/sentencepiece_processor.cc:100-115: `Load` builds `model_ = ModelFactory::Create(*model_proto_)` and `normalizer_ = std::make_unique<normalizer::Normalizer>(model_proto_->normalizer_spec(), model_proto_->trainer_spec())`, then `RETURN_IF_ERROR(status())`.
> src/model_factory.cc:28-52: `ModelFactory::Create` dispatches on `trainer_spec.model_type()` to `unigram::Model`, `bpe::Model`, `word::Model`, `character::Model` (or returns nullptr on unknown type, which later fails `status()`'s null check).

### P2.9 Result Usage
**Claim:** `Encode`'s output containers are consumed by `Decode` (round trip), by `ApplyExtraOptions` internally (post-processing before return), and in real callers are read/iterated directly (printed, joined, compared) or passed to `Decode`.
**Evidence:**
> src/sentencepiece_processor.h:56-67 (usage doc): `sp.Encode("hello world.", &sps); ... sp.Decode(sps, &detok);` and `sp.Encode("hello world.", &ids); sp.Decode(ids, &detok);` — canonical Encode→Decode round trip.
> ossfuzz_view/sentencepiece-c2/model_load_fuzzer.cc:46-64: `processor.Encode(test_text, &pieces)` then, if non-empty, `processor.Decode(pieces, &decoded)`; same for `ids`.
> src/sentencepiece_processor.cc:1314: inside `EncodeOptimized`, result vector is passed to `ApplyExtraOptions(encode_extra_options_, output)` before returning to caller (reverse/bos/eos/unk_piece applied in place).
> src/sentencepiece_processor_test.cc:152-183: test reads `output`/`ids`/`spt.pieces(i)` fields (`.piece()`, `.id()`, `.begin()`, `.end()`, `.surface()`) directly after `Encode`.

### P2.10 Behaviour Controls
**Claim:** Behaviour of `Encode` and sibling entries is controlled by: (a) `trainer_spec.model_type()` (UNIGRAM/BPE/WORD/CHAR) selecting the `ModelInterface` subclass and hence the segmentation algorithm; (b) `encode_extra_options_` set via `SetEncodeExtraOptions("bos"/"eos"/"reverse"/"unk"/"unk_piece")`, applied by `ApplyExtraOptions`; (c) for `NBestEncode`, `nbest_size` (must be `<= kMaxNBestSize=512`, checked for the `SentencePieceText` overload; the `vector<vector<...>>` overloads do not re-check but call through `NBestEncode(..., spt)` which does); (d) for `SampleEncode`, `nbest_size` sign/value and `alpha` select among lattice backward-sampling (`nbest_size<0` or model has no NBestEncode), best-path (`nbest_size==0||1`), or NBest-based categorical sampling (`nbest_size>1`); `alpha` must be `>=0` and not NaN; (e) `model_->ByteFallbackEnabled()` and `HasUnkPieceOption()` select byte-fallback / `<unk>`-literal substitution branches inside `EncodeOptimized`.
**Evidence:**
> src/model_factory.cc:32-44: switch on `trainer_spec.model_type()`.
> src/sentencepiece_processor.cc:1104-1180 (`ApplyExtraOptions`): `switch(extra_option)` over `REVERSE/EOS/BOS/UNK_PIECE`, default returns `InternalError`.
> src/sentencepiece_processor.cc:399-400: `RET_CHECK_LE(nbest_size, kMaxNBestSize) << "nbest_size must be nbest_size <= " << kMaxNBestSize;` (kMaxNBestSize=512 at :70).
> src/sentencepiece_processor.cc:427-428: `RET_CHECK(!std::isnan(alpha)); RET_CHECK_GE(alpha, 0.0);` in `SampleEncode(spt)`.
> src/sentencepiece_processor.cc:436-466: branch on `!model_->IsNBestEncodeAvailable() || nbest_size < 0` (sampling), `nbest_size==1||0` (best path), `nbest_size>1` (NBest + categorical sampling).
> src/sentencepiece_processor.cc:1261-1262,1280-1310: `byte_fallback_enabled`/`has_unk_piece` gate the per-byte fallback loop vs. normal piece emission/merging.
> sentencepiece_processor.h:218-233 (doc comment on SampleEncode): documents BPE dropout `alpha` vs unigram forward-filter/backward-sampling semantics.

### P2.11 Input Expectations
**Claim:** `Encode`'s `input` is documented as "UTF8 input" but is not validated as UTF-8 by `Encode` itself (invalid bytes are tolerated and may surface via byte-fallback / `<unk>` pieces produced by the normalizer+model, not rejected with an error). Only structural checks are: non-null output pointer, and (for `NBestEncode`/`SampleEncode`) `nbest_size <= 512`/`alpha` constraints described above. There is no explicit input length/size limit enforced in `Encode`'s own code (bounded only by downstream normalizer/model/lattice complexity, which is the known fuzzing attack surface).
**Evidence:**
> src/sentencepiece_processor.h:182: "Given a UTF8 input, encodes it into a sequence of sentence pieces." (doc, not enforced).
> src/sentencepiece_processor.cc:1252-1255: empty input is explicitly a valid, fast-accepted case.
> src/sentencepiece_processor.cc:1269: the only hard input-shape invariant checked is `RET_CHECK(!piece.first.empty())` on the *model's* output pieces, not on raw input bytes.
> src/normalizer.h:89,95: `Normalize` takes arbitrary `absl::string_view` without a format/length precondition in the signature.

## Other Logic-Group Entries (API surface around Encode)

### Entry: `SentencePieceProcessor::Load` (3 overloads) and `LoadFromSerializedProto`
**Call protocol:** Must be called exactly once (or again to replace the model) before any `Encode`/`Decode`/`NBestEncode`/`SampleEncode`/`SetEncodeExtraOptions` call. No prior state required beyond a constructed `SentencePieceProcessor`. `Load(filename)` reads from disk via `io::LoadModelProto`; `Load(const ModelProto&)` copies; `Load(unique_ptr<ModelProto>)` takes ownership (the "real" implementation, all others funnel into it); `LoadFromSerializedProto(serialized)` parses a raw byte string into a `ModelProto` via `ParseFromArray` then calls `Load(unique_ptr<ModelProto>)`.
**Parameters/flags selecting paths:** the content of the `ModelProto` (`trainer_spec.model_type()`, `normalizer_spec`, `pieces`) fully determines later `Encode` behaviour (see P2.10). For the fuzzing scenario, `LoadFromSerializedProto` is the direct way to feed an attacker-controlled serialized protobuf.
**Return value:** `absl::Status`; non-OK on parse failure (`RET_CHECK(model_proto->ParseFromArray(...))`), on file load failure, on `ModelFactory::Create` producing a model whose `status()` is not OK (e.g. malformed trie), on normalizer construction failure, or on self-test sample mismatches (`"Self-test failures. See LOG(INFO)."`).
**What to free:** nothing explicit; `model_proto_`, `model_`, `normalizer_`, `denormalizer_` are `unique_ptr` members reset on each `Load` call or on processor destruction.
**Evidence:**
> src/sentencepiece_processor.cc:77-81 (`Load(filename)`), :87-91 (`Load(const ModelProto&)`), :93-98 (`LoadFromSerializedProto`), :100-153 (`Load(unique_ptr<ModelProto>)` — the shared implementation, including self-test at :131-150 which itself calls `Encode(s.input(), &sps)` at :134).
> src/sentencepiece_processor.cc:96: `RET_CHECK(model_proto->ParseFromArray(serialized.data(), serialized.size()));` — malformed bytes rejected here (protobuf-level parse), not necessarily semantic validity.
> ossfuzz_view/sentencepiece-c2/model_load_fuzzer.cc:40-42: `auto status = processor.LoadFromSerializedProto(model_data); if (!status.ok()) return 0;` — canonical "load, bail out if bad" pattern before exercising Encode et al.

### Entry: `SentencePieceProcessor::SetEncodeExtraOptions`
**Call protocol:** Call after `Load*` succeeded (it calls `status()` and reads `bos_id()`/`eos_id()`, populated only post-Load) and before the `Encode`/`NBestEncode`/`SampleEncode` calls whose extra-option behaviour it should affect. Parameter: `absl::string_view extra_option`, a colon-separated list from `{"bos","eos","reverse","unk","unk_piece"}` (anything else → non-OK status, see `ParseExtraOptions`). Returns `absl::Status`; non-OK if option unknown, or if `bos`/`eos` requested but the model lacks a `<s>`/`</s>` id. Nothing to free — stores into `encode_extra_options_` member vector.
**Evidence:**
> src/sentencepiece_processor.cc:155-158, 1190-1222 (`ParseExtraOptions`): valid strings map at :1200-1205; `RET_CHECK(bos_id() != -1)`/`RET_CHECK(eos_id() != -1)` at :1213,1217.
> src/sentencepiece_processor_test.cc:1060,1079,1099,1113: `sp.SetEncodeExtraOptions("bos")`, `"eos"`, `"reverse"`, `"bos:eos"` called, then `sp.Encode(...)` is re-invoked to observe the new post-processing.

### Entry: `SentencePieceProcessor::NBestEncode` (pieces/ids/`NBestSentencePieceText` overloads)
**Call protocol:** Requires `Load*` done first (same `status()` precondition). Parameters: `input` (string_view), `nbest_size` (int, must be `<= kMaxNBestSize=512`; checked explicitly in the `SentencePieceText` overload at :399-400, the pieces/ids overloads delegate to it at :219,239), output pointer. Internally requires `model_->IsNBestEncodeAvailable()` (true for unigram, see unigram_model.h:141); for BPE/word/char models this flag's value determines whether NBestEncode degrades or errors — check model-specific `IsNBestEncodeAvailable()` override. Returns `absl::Status`; non-OK if nbest unsupported by model, or nbest empty.
**What it produces / cleanup:** pieces/ids overloads use a stack `Arena arena;` + `Arena::Create<NBestSentencePieceText>(&arena)`, auto-freed when `arena` goes out of scope — no manual delete. Output is `vector<vector<string>>` or `vector<vector<int>>`, standard RAII.
**Evidence:**
> src/sentencepiece_processor.cc:212-230 (pieces overload, uses Arena), :232-250 (ids overload), :394-420 (`NBestSentencePieceText*` overload, the "real" implementation): `RET_CHECK_LE(nbest_size, kMaxNBestSize)` at :399; `RET_CHECK(model_->IsNBestEncodeAvailable())` at :406; `RET_CHECK(!nbests.empty())` at :410.
> src/unigram_model.h:133-141: `NBestEncodeResult NBestEncode(...)`, `bool IsNBestEncodeAvailable() const override { return true; }`.
> ossfuzz_view/sentencepiece-c2/model_load_fuzzer.cc:94-96: `processor.NBestEncode(test_text, 2, &nbest_pieces);` called right after basic `Encode`/`Decode`, no extra setup.

### Entry: `SentencePieceProcessor::SampleEncode` (pieces/ids/`SentencePieceText` overloads)
**Call protocol:** Requires `Load*` first. Parameters: `input`, `nbest_size` (int; sign/magnitude select code path, `<= kMaxNBestSize`), `alpha` (float, must be `>=0` and not NaN — `RET_CHECK` enforced only in the `SentencePieceText` overload at :427-428; the pieces/ids overloads funnel through it at :259,274), output pointer. Dispatches to `model_->SampleEncode(normalized, alpha)` (requires `model_->IsSampleEncodeAvailable()`, e.g. `bpe::Model::SampleEncode`) when `nbest_size<0` or NBest unavailable, else best-path or NBest-based categorical sampling using `random::GetRandomGenerator()`.
**What to free:** same Arena pattern as `NBestEncode` for the pieces/ids overloads (:257-258,273-274) — automatic.
**Evidence:**
> src/sentencepiece_processor.cc:252-281 (pieces/ids overloads), :422-469 (`SentencePieceText*` overload — core logic, branches at :436,442,446).
> sentencepiece_processor.h:234-240 (declaration), :217-233 (doc comment on alpha/nbest_size semantics for unigram vs BPE).
> ossfuzz_view/sentencepiece-c2/sample_encode_fuzzer.cc:24-36: harness reads `nbest_size`/`alpha` from `FuzzedDataProvider`, then calls `fuzz_sp_processor.SampleEncodeAsPieces(in_string, nbest_size, alpha)` — note: this wrapper requires a model to already be loaded; as written this fuzzer never calls `Load`, so `SampleEncode` will hit the `status()` precondition and return a non-OK status (fails fast, does not crash) unless/until a model is loaded — a gap the harness generator should be aware of if reusing this pattern.
> src/sentencepiece_processor_test.cc:507-531: `SampleEncode("...", 1024, 0.5, &output)` expected to FAIL (`EXPECT_FALSE`, since 1024 > kMaxNBestSize=512); `nbest_size` 0 and 1 expected to succeed via best-path.

### Entry: `SentencePieceProcessor::Decode` (3 `std::string*`-detokenized overloads + 2 `SentencePieceText*` overloads)
**Call protocol:** Requires `Load*` first (`status()` check via `RET_CHECK_STATUS_STL`/`RET_CHECK_STATUS_PROTO` macros, same as Encode). Natural consumer of `Encode`'s output: takes `absl::Span<const std::string>` / `absl::Span<const absl::string_view>` / `absl::Span<const int>` (the very `pieces`/`ids` vectors `Encode` filled) and a `std::string* detokenized` (or `SentencePieceText* spt`) output pointer. Internally `DecodeOptimized<T>`; reads `model_proto_->trainer_spec().unk_surface()` if set. Returns `absl::Status`.
**What to free:** output `std::string`/`SentencePieceText` is caller-owned/stack; nothing extra.
**Evidence:**
> src/sentencepiece_processor.cc:195-210 (string-output overloads → `DecodeOptimized`), :471+ (`SentencePieceText*` overload).
> src/sentencepiece_processor.h:190-200: Decode doc/declarations directly following Encode doc block, documenting the round-trip contract `Decode(Encode(input)) == input`.
> ossfuzz_view/sentencepiece-c2/model_load_fuzzer.cc:54-64: `if (!pieces.empty()) { std::string decoded; processor.Decode(pieces, &decoded); }` and same for `ids` — Decode called only when Encode produced non-empty output, directly chained after Encode in the same harness.

### How the harness should combine these entries
**Claim:** The realistic, protocol-correct harness sequence (as evidenced by `model_load_fuzzer.cc`) is: construct `SentencePieceProcessor` → feed fuzz bytes to `LoadFromSerializedProto` → bail out (`return 0`) if status not OK → optionally call `SetEncodeExtraOptions` with a fuzzed/fixed valid string → call `Encode` (pieces and/or ids and/or `SentencePieceText` overloads) → feed the resulting pieces/ids into `Decode` → optionally call `NBestEncode`/`SampleEncode` with fuzzed `nbest_size`/`alpha` (clamped only by the library's own `RET_CHECK`s, so arbitrary values are safe to pass and simply yield error status). No manual cleanup beyond normal C++ object destruction is required anywhere in this sequence.
**Evidence:**
> ossfuzz_view/sentencepiece-c2/model_load_fuzzer.cc:37-100 (full sequence: Load → Encode(pieces) → Encode(ids) → Decode(pieces) → Decode(ids) → Normalize → vocab ops → NBestEncode → SampleEncode).

## Build notes

**Language:** C++ (compiled at `-std=c++20`, see build.sh:50,93).
**Header include path for target:** `sentencepiece_processor.h` is included directly (`#include "sentencepiece_processor.h"`, e.g. ossfuzz_view/sentencepiece-c2/model_load_fuzzer.cc:25 and sample_encode_fuzzer.cc:22). The fuzzer compile command passes `-I. -I./root/include -I../third_party $PROTOBUF_INC` (build.sh:93-94); `./root/include` is the CMake-installed public-header directory (`cmake --build ... --target install`, build.sh:23-24), where `sentencepiece_processor.h` is installed.
**Libraries linked:** `./root/lib/libsentencepiece_train.a`, `./root/lib/libsentencepiece.a`, protobuf static libs (`$PROTOBUF_LIBS`, found under `_deps/protobuf-build`), abseil static libs (`$ABSL_LIBS`, found under `./root/lib` and `_deps`), plus `-lpthread`. Linked inside a `-Wl,--start-group ... --end-group` to resolve circular static-lib dependencies (build.sh:96-99).
**Fuzzer driver:** `$LIB_FUZZING_ENGINE` is linked in (build.sh:95), i.e. standard libFuzzer `LLVMFuzzerTestOneInput` entry point, confirmed by both `model_load_fuzzer.cc:27` and `sample_encode_fuzzer.cc:24` defining `extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)`.
**Evidence:**
> ossfuzz_view/sentencepiece-c2/build.sh:90-101: fuzzer build loop — `for fuzzer in $(find $SRC -name '*_fuzzer.cc' ...); do $CXX $CXXFLAGS -std=c++20 -I. -I./root/include -I../third_party $PROTOBUF_INC $fuzzer $LIB_FUZZING_ENGINE -Wl,--start-group ./root/lib/libsentencepiece_train.a ./root/lib/libsentencepiece.a $PROTOBUF_LIBS $ABSL_LIBS -Wl,--end-group -lpthread -o $OUT/$fuzz_basename; done`.
> ossfuzz_view/sentencepiece-c2/build.sh:22-24: `mkdir build; cd build; cmake ../ -B ./ -DSPM_ENABLE_SHARED=ON -DCMAKE_INSTALL_PREFIX=./root -DSPM_BUILD_TEST=ON; cmake --build ./ --config Release --target install ...` — produces `./root/include/sentencepiece_processor.h` and `./root/lib/libsentencepiece*.a`.
> ossfuzz_view/sentencepiece-c2/model_load_fuzzer.cc:19-25: minimal includes for a working harness — `<fuzzer/FuzzedDataProvider.h>` and `"sentencepiece_processor.h"` only (no protobuf headers needed unless using the `SentencePieceText`/`ModelProto` proto-typed overloads directly, which require also including `sentencepiece.pb.h` per the header's own comment at src/sentencepiece_processor.h:249 "When using these APIs, sentencepiece.pb.h header files must be included.").