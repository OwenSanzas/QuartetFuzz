#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>
#include "sentencepiece_processor.h"
#include "sentencepiece_model.pb.h"
#include "sentencepiece.pb.h"

using sentencepiece::ModelProto;
using sentencepiece::TrainerSpec;

namespace {

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

void AddPiece(ModelProto *model, const std::string &piece, float score) {
  auto *sp = model->add_pieces();
  sp->set_piece(piece);
  sp->set_score(score);
}

void AddBytePiece(ModelProto *model, unsigned char byte) {
  char buf[8];
  std::snprintf(buf, sizeof(buf), "<0x%02X>", byte);
  auto *sp = model->add_pieces();
  sp->set_piece(buf);
  sp->set_type(ModelProto::SentencePiece::BYTE);
  sp->set_score(-10.0);
}

void BuildModel(FuzzedDataProvider *fdp, ModelProto *model) {
  AddSpecialPieces(model);

  const std::string WS = "\xe2\x96\x81";

  static const TrainerSpec::ModelType kModelTypes[] = {
      TrainerSpec::UNIGRAM, TrainerSpec::BPE, TrainerSpec::WORD,
      TrainerSpec::CHAR};
  const TrainerSpec::ModelType model_type =
      kModelTypes[fdp->ConsumeIntegralInRange<uint8_t>(0, 3)];
  model->mutable_trainer_spec()->set_model_type(model_type);
  model->mutable_trainer_spec()->set_vocab_size(50);
  model->mutable_trainer_spec()->set_byte_fallback(fdp->ConsumeBool());
  model->mutable_trainer_spec()->set_treat_whitespace_as_suffix(
      fdp->ConsumeBool());

  AddPiece(model, WS, 0.0);
  AddPiece(model, "a", -0.1);
  AddPiece(model, "b", -0.2);
  AddPiece(model, "c", -0.3);
  AddPiece(model, "d", -0.4);
  AddPiece(model, "e", -0.1);
  AddPiece(model, "f", -0.5);
  AddPiece(model, "g", -0.5);
  AddPiece(model, "h", -0.3);
  AddPiece(model, "i", -0.2);
  AddPiece(model, "l", -0.3);
  AddPiece(model, "n", -0.2);
  AddPiece(model, "o", -0.2);
  AddPiece(model, "r", -0.3);
  AddPiece(model, "s", -0.2);
  AddPiece(model, "t", -0.2);
  AddPiece(model, "u", -0.4);
  AddPiece(model, "w", -0.5);
  AddPiece(model, "x", -0.6);
  AddPiece(model, "y", -0.5);
  AddPiece(model, "z", -0.7);

  AddPiece(model, WS + "the", -1.0);
  AddPiece(model, WS + "a", -1.5);
  AddPiece(model, WS + "is", -1.8);
  AddPiece(model, "er", -2.0);
  AddPiece(model, "in", -2.0);
  AddPiece(model, "on", -2.0);
  AddPiece(model, "an", -2.0);
  AddPiece(model, "th", -1.2);
  AddPiece(model, "he", -1.3);
  AddPiece(model, "en", -2.0);
  AddPiece(model, "re", -2.0);
  AddPiece(model, "es", -2.2);
  AddPiece(model, "ing", -1.5);
  AddPiece(model, "tion", -2.0);
  AddPiece(model, WS + "hello", -3.0);
  AddPiece(model, WS + "world", -3.0);
  AddPiece(model, WS + "test", -3.0);
  AddPiece(model, WS + "fuzz", -4.0);

  for (int i = 0; i < 256; i++) {
    AddBytePiece(model, static_cast<unsigned char>(i));
  }

  auto *norm_spec = model->mutable_normalizer_spec();
  norm_spec->set_add_dummy_prefix(fdp->ConsumeBool());
  norm_spec->set_remove_extra_whitespaces(fdp->ConsumeBool());
  norm_spec->set_escape_whitespaces(fdp->ConsumeBool());
}

uint64_t WalkSentencePieceText(const sentencepiece::SentencePieceText &spt) {
  uint64_t acc = spt.text().size();
  for (const auto &piece : spt.pieces()) {
    acc += piece.piece().size();
    acc += piece.id();
    acc += piece.surface().size();
    acc += piece.begin();
    acc += piece.end();
  }
  return acc;
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fdp(data, size);

  ModelProto model;
  BuildModel(&fdp, &model);

  const int nbest_size = fdp.ConsumeIntegralInRange<int>(-16, 520);
  const float alpha = fdp.ConsumeFloatingPoint<float>();

  static const char *kExtraOptionChoices[] = {
      "",      "bos",   "eos",        "reverse",
      "unk",   "bos:eos", "reverse:unk:bos:eos", "bogus_option",
  };
  constexpr uint8_t kNumChoices =
      sizeof(kExtraOptionChoices) / sizeof(kExtraOptionChoices[0]);
  const std::string extra_option =
      kExtraOptionChoices[fdp.ConsumeIntegralInRange<uint8_t>(
          0, kNumChoices - 1)];

  const std::string test_text = fdp.ConsumeRemainingBytesAsString();

  std::string serialized;
  if (!model.SerializeToString(&serialized)) {
    return 0;
  }

  sentencepiece::SentencePieceProcessor processor;
  if (!processor.LoadFromSerializedProto(serialized).ok()) {

    return 0;
  }

  processor.SetEncodeExtraOptions(extra_option).IgnoreError();

  volatile uint64_t sink = 0;

  std::vector<std::string> pieces;
  const auto encode_status = processor.Encode(test_text, &pieces);

  std::vector<int> ids;
  processor.Encode(test_text, &ids).IgnoreError();

  sentencepiece::SentencePieceText spt;
  const auto encode_spt_status = processor.Encode(test_text, &spt);
  if (encode_spt_status.ok()) {
    sink += WalkSentencePieceText(spt);
  }

  if (encode_status.ok() && !pieces.empty()) {
    std::string detok;
    processor.Decode(pieces, &detok).IgnoreError();

    std::vector<absl::string_view> piece_views(pieces.begin(), pieces.end());
    sentencepiece::SentencePieceText decoded_spt;
    if (processor.Decode(piece_views, &decoded_spt).ok()) {
      sink += WalkSentencePieceText(decoded_spt);
    }
  }
  if (!ids.empty()) {
    std::string detok_ids;
    processor.Decode(ids, &detok_ids).IgnoreError();

    sentencepiece::SentencePieceText decoded_ids_spt;
    if (processor.Decode(ids, &decoded_ids_spt).ok()) {
      sink += WalkSentencePieceText(decoded_ids_spt);
    }
  }

  std::vector<std::vector<std::string>> nbest_pieces;
  processor.NBestEncode(test_text, nbest_size, &nbest_pieces).IgnoreError();
  for (const auto &nbest : nbest_pieces) {
    if (!nbest.empty()) {
      std::string detok;
      processor.Decode(nbest, &detok).IgnoreError();
    }
  }

  sentencepiece::NBestSentencePieceText nbest_spt;
  if (processor.NBestEncode(test_text, nbest_size, &nbest_spt).ok()) {
    for (const auto &one_best : nbest_spt.nbests()) {
      sink += WalkSentencePieceText(one_best);
    }
  }

  std::vector<std::string> sampled_pieces;
  processor.SampleEncode(test_text, nbest_size, alpha, &sampled_pieces)
      .IgnoreError();
  if (!sampled_pieces.empty()) {
    std::string detok;
    processor.Decode(sampled_pieces, &detok).IgnoreError();
  }

  sentencepiece::SentencePieceText sampled_spt;
  if (processor.SampleEncode(test_text, nbest_size, alpha, &sampled_spt)
          .ok()) {
    sink += WalkSentencePieceText(sampled_spt);
  }

  (void)sink;
  return 0;
}
