#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "sentencepiece_processor.h"
#include "sentencepiece_model.pb.h"

namespace {

using sentencepiece::ModelProto;
using sentencepiece::TrainerSpec;

void AddSpecialPieces(ModelProto *model) {
  auto *unk = model->add_pieces();
  unk->set_piece("<unk>");
  unk->set_type(ModelProto::SentencePiece::UNKNOWN);
  unk->set_score(0.0);

  auto *bos = model->add_pieces();
  bos->set_piece("<s>");
  bos->set_type(ModelProto::SentencePiece::CONTROL);
  bos->set_score(0.0);

  auto *eos = model->add_pieces();
  eos->set_piece("</s>");
  eos->set_type(ModelProto::SentencePiece::CONTROL);
  eos->set_score(0.0);
}

void AddPiece(ModelProto *model, const std::string &piece, float score,
              ModelProto::SentencePiece::Type type =
                  ModelProto::SentencePiece::NORMAL) {
  auto *sp = model->add_pieces();
  sp->set_piece(piece);
  sp->set_score(score);
  sp->set_type(type);
}

void AddBytePiece(ModelProto *model, unsigned char byte) {
  char buf[8];
  snprintf(buf, sizeof(buf), "<0x%02X>", byte);
  auto *sp = model->add_pieces();
  sp->set_piece(buf);
  sp->set_type(ModelProto::SentencePiece::BYTE);
  sp->set_score(-10.0);
}

std::string BuildModelProto(TrainerSpec::ModelType model_type, bool rich) {
  ModelProto model;
  AddSpecialPieces(&model);

  const std::string WS = "\xe2\x96\x81";

  auto *trainer_spec = model.mutable_trainer_spec();
  trainer_spec->set_model_type(model_type);
  trainer_spec->set_vocab_size(50);
  trainer_spec->set_byte_fallback(true);

  if (rich) {
    trainer_spec->set_treat_whitespace_as_suffix(true);
    trainer_spec->add_user_defined_symbols("<custom1>");
  }

  AddPiece(&model, WS, 0.0);
  AddPiece(&model, "a", -0.1);
  AddPiece(&model, "b", -0.2);
  AddPiece(&model, "c", -0.3);
  AddPiece(&model, "d", -0.4);
  AddPiece(&model, "e", -0.1);
  AddPiece(&model, "f", -0.5);
  AddPiece(&model, "g", -0.5);
  AddPiece(&model, "h", -0.3);
  AddPiece(&model, "i", -0.2);
  AddPiece(&model, "l", -0.3);
  AddPiece(&model, "n", -0.2);
  AddPiece(&model, "o", -0.2);
  AddPiece(&model, "r", -0.3);
  AddPiece(&model, "s", -0.2);
  AddPiece(&model, "t", -0.2);
  AddPiece(&model, "u", -0.4);
  AddPiece(&model, "w", -0.5);
  AddPiece(&model, "x", -0.6);
  AddPiece(&model, "y", -0.5);
  AddPiece(&model, "z", -0.7);

  AddPiece(&model, WS + "the", -1.0);
  AddPiece(&model, WS + "a", -1.5);
  AddPiece(&model, WS + "is", -1.8);
  AddPiece(&model, "er", -2.0);
  AddPiece(&model, "in", -2.0);
  AddPiece(&model, "on", -2.0);
  AddPiece(&model, "an", -2.0);

  AddPiece(&model, "th", -1.2,
           rich ? ModelProto::SentencePiece::UNUSED
                : ModelProto::SentencePiece::NORMAL);
  AddPiece(&model, "he", -1.3);
  AddPiece(&model, "en", -2.0);
  AddPiece(&model, "re", -2.0);
  AddPiece(&model, "es", -2.2);
  AddPiece(&model, "ing", -1.5);
  AddPiece(&model, "tion", -2.0);
  AddPiece(&model, WS + "hello", -3.0);
  AddPiece(&model, WS + "world", -3.0);
  AddPiece(&model, WS + "test", -3.0);
  AddPiece(&model, WS + "fuzz", -4.0);

  if (rich) {
    AddPiece(&model, "<custom1>", 0.0,
             ModelProto::SentencePiece::USER_DEFINED);
  }

  for (int i = 0; i < 256; i++) {
    AddBytePiece(&model, static_cast<unsigned char>(i));
  }

  auto *norm_spec = model.mutable_normalizer_spec();
  if (rich) {
    norm_spec->set_add_dummy_prefix(true);
    norm_spec->set_remove_extra_whitespaces(false);
    norm_spec->set_escape_whitespaces(false);
  } else {
    norm_spec->set_add_dummy_prefix(true);
    norm_spec->set_remove_extra_whitespaces(true);
    norm_spec->set_escape_whitespaces(true);
  }

  std::string serialized;
  model.SerializeToString(&serialized);
  return serialized;
}

sentencepiece::SentencePieceProcessor *MakeProcessorFromProto(
    TrainerSpec::ModelType model_type, bool rich) {
  const std::string serialized = BuildModelProto(model_type, rich);
  auto *sp = new sentencepiece::SentencePieceProcessor();
  const auto status = sp->LoadFromSerializedProto(serialized);
  if (!status.ok()) {
    delete sp;
    return nullptr;
  }
  return sp;
}

sentencepiece::SentencePieceProcessor *MakeNoByteFallbackCharProcessor() {
  ModelProto model;

  auto add_piece = [&](const std::string &piece, float score,
                        ModelProto::SentencePiece::Type type =
                            ModelProto::SentencePiece::NORMAL) {
    auto *sp = model.add_pieces();
    sp->set_piece(piece);
    sp->set_score(score);
    sp->set_type(type);
  };

  add_piece("<unk>", 0.0, ModelProto::SentencePiece::UNKNOWN);
  add_piece("<s>", 0.0, ModelProto::SentencePiece::CONTROL);
  add_piece("</s>", 0.0, ModelProto::SentencePiece::CONTROL);

  const std::string WS = "\xe2\x96\x81";
  add_piece(WS, 0.0);
  for (char c = 'a'; c <= 'z'; ++c) {
    add_piece(std::string(1, c), -0.2);
  }

  auto *trainer_spec = model.mutable_trainer_spec();
  trainer_spec->set_model_type(TrainerSpec::CHAR);
  trainer_spec->set_vocab_size(model.pieces_size());

  trainer_spec->set_byte_fallback(false);

  std::string serialized;
  model.SerializeToString(&serialized);

  auto *sp = new sentencepiece::SentencePieceProcessor();
  const auto status = sp->LoadFromSerializedProto(serialized);
  if (!status.ok()) {
    delete sp;
    return nullptr;
  }
  return sp;
}

constexpr uint8_t kNumModels = 9;

sentencepiece::SentencePieceProcessor *GetProcessor(uint8_t model_index) {
  static sentencepiece::SentencePieceProcessor *const procs[kNumModels] = {
      MakeProcessorFromProto(TrainerSpec::UNIGRAM,  false),
      MakeProcessorFromProto(TrainerSpec::BPE,  false),
      MakeProcessorFromProto(TrainerSpec::WORD,  false),
      MakeProcessorFromProto(TrainerSpec::CHAR,  false),
      MakeProcessorFromProto(TrainerSpec::UNIGRAM,  true),
      MakeProcessorFromProto(TrainerSpec::BPE,  true),
      MakeProcessorFromProto(TrainerSpec::WORD,  true),
      MakeProcessorFromProto(TrainerSpec::CHAR,  true),
      MakeNoByteFallbackCharProcessor(),
  };
  return procs[model_index % kNumModels];
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider data_provider(data, size);

  const uint8_t model_selector = data_provider.ConsumeIntegral<uint8_t>();
  sentencepiece::SentencePieceProcessor *processor =
      GetProcessor(model_selector);
  if (processor == nullptr) {
    return 0;
  }

  const uint8_t opt_bits = data_provider.ConsumeIntegral<uint8_t>();
  std::vector<std::string> options;
  if (opt_bits & 0x1) options.push_back("bos");
  if (opt_bits & 0x2) options.push_back("eos");
  if (opt_bits & 0x4) options.push_back("reverse");
  if (opt_bits & 0x8) options.push_back("unk_piece");

  std::string joined_options;
  for (size_t i = 0; i < options.size(); ++i) {
    if (i > 0) joined_options += ":";
    joined_options += options[i];
  }

  if (data_provider.ConsumeBool()) {
    if (!joined_options.empty()) joined_options += ":";
    joined_options += "not_a_real_option";
  }

  const auto extra_options_status =
      processor->SetEncodeExtraOptions(joined_options);
  (void)extra_options_status;

  const std::string input_text = data_provider.ConsumeRemainingBytesAsString();

  std::vector<std::string> pieces;
  const auto status = processor->Encode(input_text, &pieces);
  (void)status;
  (void)pieces.size();

  return 0;
}