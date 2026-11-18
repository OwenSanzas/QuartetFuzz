## target: sentencepiece::SentencePieceTrainer::Train(absl::string_view args, const std::vector<std::string>& sentences, std::string* serialized_model_proto = nullptr)

### Overview / dispatch chain
**Claim:** The targeted overload is a thin wrapper: it builds a `VectorSentenceIterator` over `sentences` and forwards to `Train(absl::string_view args, SentenceIterator*, std::string*)`, which builds a `TrainerComponents` and forwards to `Train(absl::string_view args, const TrainerComponents&, std::string*)`, which calls `MergeSpecsFromArgs(args, &components)` then `Train(const TrainerComponents&, std::string*)`. That last overload calls `PopulateNormalizerSpec` twice (normalizer + denormalizer), `TrainerFactory::Create(trainer_spec, normalizer_spec, denormalizer_spec)` to pick a concrete `TrainerInterface` subclass, then `trainer->Train(components, &model_proto)` (or `nullptr`), which internally calls `LoadSentences()` then the model-specific `Train()` loop.
**Evidence:**
> src/sentencepiece_trainer.cc:375-380: `Train(args, sentences, proto)` → `VectorSentenceIterator iter(sentences); return Train(args, &iter, serialized_model_proto);`
> src/sentencepiece_trainer.cc:216-222: `Train(args, SentenceIterator*, proto)` → builds `TrainerComponents components; components.sentence_iterator = sentence_iterator; return Train(args, components, serialized_model_proto);`
> src/sentencepiece_trainer.cc:163-170: `Train(args, components, proto)` → `RETURN_IF_ERROR(MergeSpecsFromArgs(args, &copied_components)); return Train(copied_components, serialized_model_proto);`
> src/sentencepiece_trainer.cc:131-160: `Train(components, proto)` → `PopulateNormalizerSpec` (x2) → `TrainerFactory::Create(...)` → `trainer->Train(components, &model_proto)` or `trainer->Train(components, nullptr)`.
> src/trainer_factory.cc:28-55: `TrainerFactory::Create` switches on `trainer_spec.model_type()` to instantiate `unigram::Trainer`, `bpe::Trainer`, `word::Trainer`, or `character::Trainer` (default falls through to `LOG(FATAL)` then unigram).
> src/trainer_interface.h:107-112: `TrainerInterface::Train(const TrainerComponents&, ModelProto*)` stores `components_`/`output_model_proto_` and calls `Train()` (the per-model override), which in turn calls `LoadSentences()` (see e.g. src/bpe_model_trainer.cc:214, src/word_model_trainer.cc:37, src/char_model_trainer.cc:35, src/unigram_model_trainer.cc:836).

### P2.1 Init Sequence
**Claim:** No global library initialization is required before calling `Train`. The only mandatory "setup" is constructing the `args` string and the `sentences` vector themselves; everything else (spec merging, normalizer population, trainer construction) happens inside `Train`.
**Evidence:**
> src/sentencepiece_trainer.cc:163-170: first thing `Train(args, components, proto)` does is `MergeSpecsFromArgs(args, &copied_components)` — no external init call precedes it.
> src/spm_train_main.cc (CLI driver) only sets up `TrainerSpec`/`NormalizerSpec` fields from ABSL flags before calling `SentencePieceTrainer::Train(trainer_spec, normalizer_spec, denormalizer_spec)` (src/spm_train_main.cc:273-274) — no separate "library init" call.
> src/sentencepiece_trainer_test.cc:60-64: tests call `SentencePieceTrainer::Train(args_string)` directly with no prior setup beyond building the `args` string.

### P2.2 Parameter Construction
**Claim:**
- `args` (`absl::string_view`): a space-separated `--key=value` (or bare `--key`) string, e.g. `"--model_prefix=m --vocab_size=1000 --model_type=bpe"`. Parsed by splitting on `' '`, stripping a leading `--`, then splitting on the first `'='`. Keys not recognized by `SetProtoField` on either `TrainerSpec` or `NormalizerSpec` make `MergeSpecsFromArgs` return a `NotFound`-derived error that propagates up.
- `sentences` (`const std::vector<std::string>&`): any vector of raw byte strings (no required encoding checked at this layer; invalid UTF-8 is tolerated structurally but mis-segmented by `string_util::UTF8ToUnicodeText` deeper in the per-model trainer). Each is loaded through a `VectorSentenceIterator`; the trainer's `LoadSentences()` applies filtering (`max_sentence_length`, `tsv` parsing, `kUNKStr` reserved-char skip).
- `serialized_model_proto` (`std::string*`, optional): if non-null, receives `ModelProto::SerializeAsString()`; if null, the trained model is **not** retrievable from this call (would have gone to a file via `model_prefix`, which conflicts with passing `sentence_iterator`/in-memory sentences — see P2.8).
**Evidence:**
> src/sentencepiece_trainer.cc:243-268: `MergeSpecsFromArgs(absl::string_view, ...)` splits `args` on `" "`, strips `--` prefix, splits on `=`.
> src/sentencepiece_trainer.cc:321-332: unknown key in both trainer_spec and normalizer_spec → returns the `NotFound` status from `SetProtoField`.
> src/trainer_interface.cc:403-435 (`LoadSentences`): tsv parsing via `absl::StrSplit(sentence,'\t')` and `absl::SimpleAtoi`; `max_sentence_length` filter; skip if `sentence.find(kUNKStr) != npos`.
> src/sentencepiece_trainer.cc:151-157: `if (serialized_model_proto) { ...; *serialized_model_proto = model_proto.SerializeAsString(); } else { trainer->Train(components, nullptr); }`.

### P2.3 Object Lifecycle
**Claim:** There is no persistent/stateful "object" the caller must create/configure/destroy around `Train`; `Train` is fully self-contained per call (it builds its own `TrainerComponents`, its own `TrainerInterface` subclass via `unique_ptr`, and its own `ModelProto`). The caller's only lifecycle responsibility is around the **output**: if `serialized_model_proto` is supplied, the caller typically feeds the resulting bytes into a `SentencePieceProcessor`/`SentencePieceNormalizer` (see P2.9) which then owns its own lifecycle (Load → use → destructor).
**Evidence:**
> src/sentencepiece_trainer.cc:137-139: `auto trainer = TrainerFactory::Create(...)` — a local `std::unique_ptr<TrainerInterface>`, destroyed automatically at end of `Train`.
> src/sentencepiece_trainer.cc:152-156: local `ModelProto model_proto;` serialized out; no proto object survives the call except via the output string.

### P2.4 Return Value Handling
**Claim:** `Train` returns `absl::Status`. Callers must check `.ok()` (or use `RETURN_IF_ERROR`/`QCHECK_OK`/`ASSERT_TRUE(...ok())`) before trusting `*serialized_model_proto`. On error, `*serialized_model_proto` is left in whatever state it had before the call (the assignment to `*serialized_model_proto` only happens after `trainer->Train()` succeeds, since `RETURN_IF_ERROR` short-circuits on failure).
**Evidence:**
> src/sentencepiece_trainer.cc:151-157: `RETURN_IF_ERROR(trainer->Train(components, &model_proto)); *serialized_model_proto = model_proto.SerializeAsString();` — the assignment is skipped entirely if `trainer->Train` fails.
> src/spm_train_main.cc:273-274: `QCHECK_OK(sentencepiece::SentencePieceTrainer::Train(...))` — real caller aborts the process on non-OK status.
> src/sentencepiece_trainer_test.cc:60-64, 133-152: tests use `ASSERT_TRUE(... .ok())` before calling `CheckVocab`/`CheckNormalizer` which load the produced model file.
> python/src/sentencepiece/sentencepiece_pybind.cc:1541-1544, 1584-1588: `if (!status.ok()) throw status;` before using the result.

### P2.5 Cleanup Sequence
**Claim:** No explicit cleanup is required for `Train` itself (all internal objects are stack/unique_ptr-scoped). If the caller writes to disk (`model_prefix` set, no `serialized_model_proto`), files `*.model`/`*.vocab` are created by `TrainerInterface::Save()` and must be cleaned up by the test/harness itself (not by the library). If the caller instead captures `serialized_model_proto`, the only "cleanup" is normal `std::string` destruction; if that buffer is then loaded into a `SentencePieceProcessor`/`SentencePieceNormalizer`, that consumer object's own destructor handles its internal cleanup.
**Evidence:**
> src/trainer_interface.cc:729-737 (`TrainerInterface::Save`): writes `model_prefix + ".model"` and `.vocab"` files when `output_model_proto_ == nullptr`.
> src/sentencepiece_trainer_test.cc:58, 123: tests use `::testing::TempDir()` for `model_prefix`, implying the test harness is responsible for any file cleanup, not the library.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase at the cited locations (confirmed via direct `Grep`/`Read`).
**Evidence:**
> `SentencePieceTrainer::Train` (all overloads): src/sentencepiece_trainer.h:92-167, defined src/sentencepiece_trainer.cc:131-389.
> `SentencePieceTrainer::MergeSpecsFromArgs`: src/sentencepiece_trainer.h:181-198, defined src/sentencepiece_trainer.cc:243-355.
> `SentencePieceTrainer::GetNormalizerSpec`: src/sentencepiece_trainer.h:172, defined src/sentencepiece_trainer.cc:234-240.
> `SentencePieceTrainer::PopulateNormalizerSpec`: src/sentencepiece_trainer.h:176-177, defined src/sentencepiece_trainer.cc:392-417.
> `SentencePieceTrainer::PopulateModelTypeFromString`: src/sentencepiece_trainer.h:215-216, defined src/sentencepiece_trainer.cc:420-435.
> `SentencePieceTrainer::SetProtoField` (TrainerSpec/NormalizerSpec overloads): src/sentencepiece_trainer.h:205-211, defined src/spec_parser.h:200, 259.
> `TrainerFactory::Create`: src/trainer_factory.h (declared), defined src/trainer_factory.cc:28-55.
> `TrainerInterface::LoadSentences`: src/trainer_interface.h:135, defined src/trainer_interface.cc:362-461.
> `TrainerInterface::Train` (both signatures + the no-op base): src/trainer_interface.h:107-123.
> `bpe::Trainer::Train`, `unigram::Trainer::Train`, `word::Trainer::Train`, `character::Trainer::Train`: src/bpe_model_trainer.h:43 (def src/bpe_model_trainer.cc:201), src/unigram_model_trainer.h:76 (def src/unigram_model_trainer.cc:~836), src/word_model_trainer.h:38 (def src/word_model_trainer.cc:37), src/char_model_trainer.h:34 (def src/char_model_trainer.cc:35).
> `normalizer::Builder::GetPrecompiledCharsMap`: defined src/builder.cc:344-385.

### P2.7 Co-call Constraints
**Claim:** There are no lock/unlock-style paired APIs around `Train` itself. The closest "paired" constraints are mutual-exclusion checks enforced by `RET_CHECK` inside the call chain, which act as preconditions rather than begin/end pairs:
1. `_serialized_normalizer_spec` kwarg is mutually exclusive with any of the other normalizer-related keys (`normalization_rule_name`, `normalization_rule_tsv`, `add_dummy_prefix`, `escape_whitespaces`, `remove_extra_whitespaces`, `name`, `precompiled_charsmap`).
2. `SentenceIterator` (i.e., passing `sentences`/a custom iterator) is mutually exclusive with `trainer_spec.input()` (file-based input list) — exactly one must be set.
3. `output_model_proto` (i.e., passing non-null `serialized_model_proto`) is mutually exclusive with `trainer_spec.model_prefix()` — exactly one must be set.
4. `normalization_rule_tsv` is mutually exclusive with a pre-set `precompiled_charsmap` (checked inside `PopulateNormalizerSpec`).
**Evidence:**
> src/sentencepiece_trainer.cc:44-62 (`kNormalizerKeys`, `ContainsNormalizerKeys`) and lines 282-289: `if (has_serialized_normalizer) { ... return absl::InvalidArgumentError(...) }` when both `_serialized_normalizer_spec` and a normalizer key are present.
> src/trainer_interface.cc:372-376: `RET_CHECK((components_.sentence_iterator != nullptr && trainer_spec_.input().empty()) || (components_.sentence_iterator == nullptr && !trainer_spec_.input().empty())) << "SentenceIterator and trainer_spec.input() must be exclusive."`
> src/trainer_interface.cc:378-382: `RET_CHECK((output_model_proto_ != nullptr && trainer_spec_.model_prefix().empty()) || (output_model_proto_ == nullptr && !trainer_spec_.model_prefix().empty())) << "ModelProto and trainer_spec.model_prefix() must be exclusive."`
> src/sentencepiece_trainer.cc:396-398 (`PopulateNormalizerSpec`): `RET_CHECK(normalizer_spec->precompiled_charsmap().empty()) << "precompiled_charsmap is already defined."` when `normalization_rule_tsv` is also set.

**Harness implication:** Since the target `Train(args, sentences, proto)` always sets a `SentenceIterator` internally (via `VectorSentenceIterator`) and takes `serialized_model_proto` (a non-null `std::string*` the harness provides), `args` must NOT set `--input=...` and must NOT set `--model_prefix=...` (or the `RET_CHECK`s above will fail and `Train` returns an error Status — not a crash, but a guaranteed early-exit if violated).

### P2.8 Prerequisite State
**Claim:** No external context/connections are needed. The only "state" that must exist before `Train` succeeds is a syntactically/semantically valid combination of `args` fields such that `VerifySpec` and `InitMetaPieces` pass:
- `vocab_size > 0`.
- If `model_type` is UNIGRAM or BPE: `use_all_vocab` must be false.
- `seed_sentencepieces_file` only valid for UNIGRAM.
- Range checks: `character_coverage` in [0.98,1.0], `max_sentencepiece_length` in [1,512], `num_sub_iterations` in [1,10], `num_threads` in [1,1024], `self_test_sample_size` in [0,1000], `shrinking_factor` in [0.5,0.95], `max_sentence_length` in [10,1073741824].
- `input_sentence_size` must be `<= 0` or `> 100`.
- `unk_piece`/`bos_piece`/`eos_piece`/`pad_piece` must all be non-empty (they have non-empty defaults, but can be emptied via `args`, which would then fail).
- `InitMetaPieces` additionally requires exactly one `UNK`-named meta piece and no duplicate symbol among unk/bos/eos/pad/control/user-defined symbols.
- Pretokenizer-related: `pretokenization_delimiter` (TrainerSpec field, settable via args) only valid for UNIGRAM/BPE, and cannot be combined with an in-process `pretokenizer` callback (N/A for the `args`-only entry since that requires `TrainerComponents` directly, not reachable purely through `args`/`sentences`).
- ABSL flag `--auto_character_coverage` (process-wide, not part of `args`) requires UNIGRAM + `byte_fallback=true` + empty `required_chars`.
**Evidence:**
> src/trainer_interface.cc:85-141 (`VerifySpec`): all the range/consistency checks listed above (`RET_CHECK_GT(trainer_spec.vocab_size(), 0)` at line 87; `CHECK_RANGE` macros lines 100-110; flag checks lines 131-139).
> src/trainer_interface.cc:739-803ish (`InitMetaPieces`): `RET_CHECK(has_unk) << trainer_spec_.unk_piece() << " must be defined."` (line 766); duplicate-symbol check (lines 768-784).
> src/sentencepiece_model.proto:54,57,83: proto defaults `model_type=UNIGRAM`, `vocab_size=8000`, `character_coverage=0.9995` — these are the implicit baseline if `args` doesn't override them.

### P2.9 Result Usage
**Claim:** The filled `serialized_model_proto` (a serialized `sentencepiece::ModelProto`) is consumed by `SentencePieceProcessor::LoadFromSerializedProto` (to load a tokenizer for encode/decode) and/or by `SentencePieceNormalizer::Load(std::unique_ptr<ModelProto>)` / `LoadFromSerializedProto` (to load just the normalizer half). In the on-disk path (no `serialized_model_proto`), the produced `<model_prefix>.model` file is loaded via `SentencePieceProcessor::Load(filename)`, then queried (`model_proto()`, `GetPieceSize`, `PieceToId`, etc.).
**Evidence:**
> src/sentencepiece_processor.h:168: `virtual absl::Status LoadFromSerializedProto(absl::string_view serialized);`
> python/src/sentencepiece/sentencepiece_pybind.cc:1581-1593: trainer writes to `std::string model_proto;` via `Train(args, components, return_model_proto ? &model_proto : nullptr)`, then returns `py::bytes(model_proto)` to Python, where (elsewhere in the Python wrapper layer) it is fed back via `LoadFromSerializedProto`.
> src/sentencepiece_trainer_test.cc:36-42 (`CheckVocab`) and 44-53 (`CheckNormalizer`): `SentencePieceProcessor sp; ASSERT_TRUE(sp.Load(filename.data()).ok()); ... sp.model_proto().trainer_spec().vocab_size(); ... sp.model_proto().pieces_size();` — real caller loads the trained `.model` file and queries `model_proto()`, `trainer_spec()`, `normalizer_spec()`, `denormalizer_spec()`, `pieces()`.
> src/sentencepiece_processor.cc / .h: `SentencePieceProcessor::model_proto()` accessor used by tests to inspect `pieces()`, `trainer_spec()`, `normalizer_spec()` of the trained model.

### P2.10 Behaviour Controls
**Claim:** Key behavior-selecting fields settable via `args` (reflected through `SetProtoField` onto `TrainerSpec`/`NormalizerSpec`):
- `model_type` (`unigram|bpe|word|char`) → selects which `TrainerFactory::Create` branch (and thus which core `Train()` algorithm) runs.
- `input_format` (`"text"` default vs `"tsv"`) → in `LoadSentences`, `tsv` triggers `word\tfreq` splitting + `absl::SimpleAtoi` frequency parsing with `RET_CHECK_EQ(v.size(),2)`.
- `vocab_size`, `character_coverage`, `max_sentence_length`, `max_sentencepiece_length`, `shrinking_factor`, `num_sub_iterations`, `num_threads`, `self_test_sample_size`, `input_sentence_size`, `shuffle_input_sentence` → range-checked knobs controlling per-model loop iteration/pruning behavior.
- `split_by_whitespace` → toggles `SplitSentencesByWhitespace()` call before the per-model symbol construction (see src/bpe_model_trainer.cc:215-217).
- `use_all_vocab`, `hard_vocab_limit` → only valid/meaningful for WORD/CHAR models; `use_all_vocab=true` with UNIGRAM/BPE is rejected by `VerifySpec` (line 91-93).
- `normalization_rule_name`, `normalization_rule_tsv`, `add_dummy_prefix`, `escape_whitespaces`, `remove_extra_whitespaces`, `precompiled_charsmap`, `_serialized_normalizer_spec` → select which of (built-in named charsmap via `GetPrecompiledCharsMap`) / (user TSV compiled via `Builder::LoadCharsMap`+`CompileCharsMap`) / (raw serialized `NormalizerSpec`) is used; mutually exclusive as described in P2.7.
- `denormalization_rule_tsv` (special-cased key in `MergeSpecsFromArgs`, line 308-313) sets the *denormalizer* spec's tsv and forces `add_dummy_prefix=false`, `remove_extra_whitespaces=false`, `escape_whitespaces=false`.
- `minloglevel` (special-cased key, line 314-318) calls `SetMinLogLevel(v)` — a global logging side effect, not a proto field.
- ABSL flags (`--use_sparse_pruning`, `--auto_character_coverage`, `--fixed_sparse_lambda`, `--post_l1_debias`, `--seed_piece_length_power`, `--min_freq_alpha`) are process-global (set via `absl::SetFlag`/command line, NOT through the `args` string) and change which branches run inside `unigram::Trainer::Train`'s EM loop.
**Evidence:**
> src/sentencepiece_trainer.cc:420-435 (`PopulateModelTypeFromString`): maps `"unigram"/"bpe"/"word"/"char"` (case-insensitive) to `TrainerSpec::ModelType`.
> src/trainer_interface.cc:367-370: `RET_CHECK(trainer_spec_.input_format().empty() || == "text" || == "tsv") << "Supported formats are 'text' and 'tsv'."`
> src/trainer_interface.cc:405-413: tsv branch parsing logic.
> src/trainer_interface.cc:89-93: `use_all_vocab` + UNIGRAM/BPE rejection.
> src/sentencepiece_trainer.cc:300-333: generic `SetProtoField` dispatch loop with the three special-cased keys (`normalization_rule_name`, `denormalization_rule_tsv`, `minloglevel`) handled before falling through to reflection-based `SetProtoField`.
> src/sentencepiece_trainer.cc:392-417 (`PopulateNormalizerSpec`): tsv vs named vs already-set precompiled_charsmap branching.
> src/trainer_interface.cc:53-71 (ABSL_FLAG declarations) and 131-139 (`auto_character_coverage` constraint check in `VerifySpec`).

### P2.11 Input Expectations
**Claim:**
- `args`: plain ASCII-ish string tokenized on literal space characters (`absl::StrSplit(args, " ")`); no quoting support, so sentence/path values containing spaces cannot be represented. Each token optionally starts with `--`; `key=value` or bare `key` (value defaults to empty string). Unknown keys (not matched in either `TrainerSpec` or `NormalizerSpec`'s generated `SetProtoField`) cause `Train` to fail with the `NotFound`-derived status from `SetProtoField`.
- `sentences`: each element is a raw `std::string` (arbitrary bytes). `LoadSentences()` only special-cases: empty strings (skipped), strings longer than `max_sentence_length` (skipped, default range [10, 2^30]), strings containing the reserved UNK byte sequence `kUNKStr = "\xe2\x96\x85"` (skipped with a log), and — if `input_format=="tsv"` — each entry is first split on `'\t'` expecting exactly 2 fields `word<TAB>freq` where `freq` must parse via `absl::SimpleAtoi` and be `>= 1`. No UTF-8 validity is enforced at this stage; invalid UTF-8 propagates into per-model `string_util::UTF8ToUnicodeText` conversions deeper in `bpe::Trainer::Train`/`unigram::Trainer::Train`/etc.
- `serialized_model_proto`: output-only; no input expectations (may be `nullptr` to discard the proto, but then `model_prefix` must be set for `Train` to succeed per the exclusivity check in P2.7).
**Evidence:**
> src/sentencepiece_trainer.cc:253-264: tokenization on `" "`, `ConsumePrefix(&arg, "--")`, split on first `=`.
> src/trainer_interface.cc:405-413 (tsv), 415-417 (empty skip), 419-430 (`max_sentence_length` skip with warning), 432-435 (`kUNKStr` skip).
> src/trainer_interface.cc:75-82: `kUNKStr[] = "\xe2\x96\x85"` reserved marker constant.
> src/trainer_interface.cc:109 (`CHECK_RANGE(trainer_spec.max_sentence_length(), 10, 1073741824)`), confirming the documented size bound on input sentence length.
> src/bpe_model_trainer.cc:221-226: `string_util::UTF8ToUnicodeText(sentences_[i].first)` — per-model UTF-8 decoding happens only after `LoadSentences`, with no earlier validity gate on the raw sentence bytes.

## Other entries in the Logic Group

### Entry: sentencepiece::SentencePieceTrainer::MergeSpecsFromArgs(absl::string_view args, TrainerSpec* trainer_spec, NormalizerSpec* normalizer_spec, NormalizerSpec* denormalizer_spec)
**Claim:** A pure parsing/configuration function, not a trainer. It can be fuzzed standalone (no sentences needed) but to combine with the target in one harness it should be called on **fresh, default-constructed** `TrainerSpec`/`NormalizerSpec`/`NormalizerSpec` objects (or simply be exercised implicitly, since `Train(args, sentences, proto)` already calls it internally via `MergeSpecsFromArgs(args, &copied_components)`).
**Call protocol:**
- **Before it:** nothing — `trainer_spec`, `normalizer_spec`, `denormalizer_spec` just need to be valid non-null pointers to (default-OK) proto messages. `RET_CHECK` enforces non-null (src/sentencepiece_trainer.cc:246-248).
- **Parameters / flags:** `args` same format as the target's `args` (space-separated `--k=v` tokens). Special-cased keys: `_serialized_normalizer_spec` (binary-serialized `NormalizerSpec`, parsed via `NormalizerSpec::ParseFromString`, mutually exclusive with other normalizer keys — src/sentencepiece_trainer.cc:279-298), `normalization_rule_name`, `denormalization_rule_tsv`, `minloglevel` (src/sentencepiece_trainer.cc:304-319).
- **Return value:** `absl::Status`; `absl::InvalidArgumentError` on conflicting normalizer keys, propagated `SetProtoField` errors on bad field names/values, `RET_CHECK` failures on null pointers.
- **What to free:** nothing explicit; it mutates the caller-owned proto objects in place, no allocation ownership transferred.
- **Combination with target:** If a harness wants to test `MergeSpecsFromArgs` directly (e.g., to isolate parser bugs from the full training pipeline), it should NOT also call the target with the same `args` unless it wants the full pipeline; both can be called in the same harness on different/independent `TrainerSpec` instances to broaden coverage of the args-parsing surface, since `Train(args, sentences, proto)` already exercises this code path once per call.
**Evidence:**
> src/sentencepiece_trainer.h:181-184 (3-spec overload declaration), src/sentencepiece_trainer.cc:271-336 (definition).
> src/sentencepiece_trainer.cc:300-333: generic `SetProtoField` dispatch returning the first non-OK, non-NotFound status, or the trainer_spec NotFound status if neither matched.

### Entry: sentencepiece::SentencePieceTrainer::GetNormalizerSpec(absl::string_view name)
**Claim:** A static helper returning a `NormalizerSpec` by value; it is explicitly documented as unsafe for untrusted input: **it CHECK-crashes (via `CHECK_OK`) if `name` is not a valid precompiled charsmap name**, rather than returning a `Status`. This makes it unsuitable to call directly on arbitrary fuzz-derived strings in a harness that must not crash on invalid input — the comment says "Do not use this method in production as it crashes When `name` is invalid. Useful for unittesting."
**Call protocol:**
- **Before it:** nothing.
- **Parameters:** `name` must be one of the whitelisted embedded charsmap names, or `"identity"` (special-cased to clear the map and return OK), or match `[a-z_-]+` AND exist in the embedded `kNormalizationRules` table (or, if `DISABLE_EMBEDDED_DATA`, a `<name>.bin` file in `GetDataDir()`); any other `name` triggers `CHECK_OK(...)` abort (process crash), not a recoverable `absl::Status` return to the caller of `GetNormalizerSpec` itself.
- **Return value:** `NormalizerSpec` (by value, not a Status) — success case only; failure is a crash.
- **What to free:** nothing; return-by-value, normal RAII.
- **Combination with target:** The spec returned by `GetNormalizerSpec` is designed to be assigned into `TrainerComponents::mutable_normalizer_spec()` (or the deprecated `Train(trainer_spec, normalizer_spec, ...)` overload) before calling a `Train` overload that accepts a pre-built `NormalizerSpec`/`TrainerComponents` — NOT the target `Train(args, sentences, proto)` overload, which only accepts `args`/`sentences`. If a harness wants to combine `GetNormalizerSpec` with the args+sentences entry point, it would have to go through the `_serialized_normalizer_spec` kwarg (serialize the spec and inject it via `args`/kwargs) rather than passing the `NormalizerSpec` object directly, since the target overload has no parameter for a pre-built spec.
- **Fuzzing caveat:** Because of the `CHECK_OK` abort on invalid names, a harness MUST NOT feed arbitrary fuzzer bytes directly as `name` to `GetNormalizerSpec` (that would be an intentional-but-undesired "find the crash" harness unless the goal is specifically to test this known-crashing helper); if used, it should be restricted to a small fixed allowlist of valid names, or the harness should accept crashing on this call as expected/documented behavior.
**Evidence:**
> src/sentencepiece_trainer.h:169-172: "Do not use this method in production as it crashes When `name` is invalid. Useful for unittesting."
> src/sentencepiece_trainer.cc:234-240: `CHECK_OK(normalizer::Builder::GetPrecompiledCharsMap(spec.name(), spec.mutable_precompiled_charsmap()));`
> src/builder.cc:344-385 (`Builder::GetPrecompiledCharsMap`): `"identity"` fast path, `[a-z_-]+` name validation returning `InvalidArgumentError` otherwise, table/file lookup returning `NotFoundError` if absent — these are the Status values that `CHECK_OK` would turn into a process abort when non-OK.

### Entry interactions not part of this feature
**Claim:** `TrainerFactory::Create`, `TrainerInterface::LoadSentences`, `TrainerInterface::Train`, and the four per-model `Trainer::Train` methods (`bpe::Trainer::Train`, `unigram::Trainer::Train`, `word::Trainer::Train`, `character::Trainer::Train`) are internal "core" functions, not part of the public API surface exposed to harness code directly — they are reached exclusively through `SentencePieceTrainer::Train`. `SetProtoField` is likewise internal plumbing invoked by `MergeSpecsFromArgs`, not meant to be called standalone by a harness (it is declared public in the header for the auto-generated parser pattern but is not an entry point of this feature per the Logic Group's own characterization — it is listed under "Core").
**Evidence:**
> src/trainer_factory.h / src/trainer_factory.cc:28 — `TrainerFactory::Create` has no direct non-test caller other than `SentencePieceTrainer::Train` (src/sentencepiece_trainer.cc:137-139) and `trainer_factory_test.cc`.
> src/trainer_interface.h:135, 107-123 — `LoadSentences`/`Train` are called from within `TrainerInterface`/per-model subclasses only, never from `sentencepiece_trainer.cc` or any public-facing code outside the Trainer hierarchy itself.

## Build notes

**Claim:** Fuzzer sources for this project are compiled as C++20 (`-std=c++20`), with the target's header reachable via the CMake-installed include tree (`./root/include`, which contains the generated/installed `sentencepiece_trainer.h`), plus `-I../third_party` and protobuf include paths, and linked against the project's static libraries `libsentencepiece_train.a` and `libsentencepiece.a` plus bundled abseil and protobuf static archives.
**Evidence:**
> ossfuzz_view/sentencepiece-c3/build.sh:23-24: `cmake ../ -B ./ -DSPM_ENABLE_SHARED=ON -DCMAKE_INSTALL_PREFIX=./root -DSPM_BUILD_TEST=ON` then `cmake --build ./ --config Release --target install ...` — installs headers (including `sentencepiece_trainer.h`, confirmed installed per src/CMakeLists.txt:423) under `./root/include`.
> ossfuzz_view/sentencepiece-c3/build.sh:90-101 (fuzzer build loop):
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
> ossfuzz_view/sentencepiece-c3/build.sh:39-40: `PROTOBUF_INC="-I_deps/protobuf-src/src -I_deps/protobuf-src/third_party/utf8_range"`, `PROTOBUF_LIBS=$(find _deps/protobuf-build -name 'libprotobuf*.a' -o -name 'libutf8_*.a' ...)`.
> ossfuzz_view/sentencepiece-c3/build.sh:34: `ABSL_LIBS=$(find ./root/lib _deps -name 'libabsl_*.a' ...)` — abseil archives linked explicitly since the installed sentencepiece static libs reference but don't bundle absl symbols.
> ossfuzz_view/sentencepiece-c3/Dockerfile:17-21: base image `gcr.io/oss-fuzz-base/base-builder`; source cloned to `sentencepiece/`; `run_tests.sh build.sh *.cc` copied into `$SRC/` — i.e., fuzzer `.cc` files living alongside `build.sh`/`Dockerfile` (like the target `trainer_fuzzer.cc`) are what gets compiled by the loop above.
> src/CMakeLists.txt:423: `install(FILES "${GENERATED_HEADERS_DIR}/sentencepiece_trainer.h" "${GENERATED_HEADERS_DIR}/sentencepiece_processor.h" ...)` confirms `sentencepiece_trainer.h` is part of the installed public header set a fuzzer includes via `#include "sentencepiece_trainer.h"` with `-I./root/include`.

**Note on existing trainer_fuzzer.cc:** The current file at `ossfuzz_view/sentencepiece-c3/trainer_fuzzer.cc` is a stub/placeholder ("Gold harness replaced by an empty stub for baseline generation") that only force-links the mangled symbols of all `SentencePieceTrainer::Train` overloads via `__asm__` references and returns 0 from `LLVMFuzzerTestOneInput` without calling anything — it is NOT a real harness and gives no additional behavioral evidence beyond confirming the mangled overload set matches the header declarations in src/sentencepiece_trainer.h:92-167.