#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "sentencepiece_processor.h"

#include "sentencepiece_model.pb.h"

#include "sentencepiece.pb.h"

#include "builder.h"

namespace {

using sentencepiece::ModelProto;
using sentencepiece::TrainerSpec;

TrainerSpec::ModelType PickModelType(FuzzedDataProvider &fdp) {
  switch (fdp.ConsumeIntegralInRange<int>(0, 3)) {
    case 0:
      return TrainerSpec::UNIGRAM;
    case 1:
      return TrainerSpec::BPE;
    case 2:
      return TrainerSpec::WORD;
    default:
      return TrainerSpec::CHAR;
  }
}

ModelProto::SentencePiece::Type PickNonByteType(FuzzedDataProvider &fdp) {
  switch (fdp.ConsumeIntegralInRange<int>(0, 3)) {
    case 0:
      return ModelProto::SentencePiece::NORMAL;
    case 1:
      return ModelProto::SentencePiece::CONTROL;
    case 2:
      return ModelProto::SentencePiece::USER_DEFINED;
    default:
      return ModelProto::SentencePiece::UNUSED;
  }
}

float SanitizeScore(FuzzedDataProvider &fdp, float f) {
  if (fdp.ConsumeIntegralInRange<int>(0, 31) == 0) return f;
  if (std::isnan(f) || std::isinf(f)) return 0.0f;
  if (f > 1e6f) return 1e6f;
  if (f < -1e6f) return -1e6f;
  return f;
}

void AddPiece(ModelProto *model, FuzzedDataProvider &fdp,
              const std::string &piece, float score,
              ModelProto::SentencePiece::Type type) {
  auto *sp = model->add_pieces();
  sp->set_piece(piece);
  sp->set_score(SanitizeScore(fdp, score));
  sp->set_type(type);
}

std::string SanitizePieceText(std::string piece, FuzzedDataProvider &fdp) {
  piece.erase(std::remove(piece.begin(), piece.end(), '\0'), piece.end());
  if (piece.empty()) piece = "x";
  if (fdp.ConsumeIntegralInRange<int>(0, 63) == 0) {
    piece.resize(8001, 'a');
  } else if (piece.size() >= 100) {
    piece.resize(100);
  }
  return piece;
}

constexpr const char *kExtraOpts[] = {
    "",
    "bos",
    "eos",
    "reverse",
    "unk",
    "unk_piece",
    "bos:eos",
    "bos:eos:reverse",
    "bos:eos:unk_piece",
    "bogus_option",
    "bos:bogus",
};
constexpr size_t kNumExtraOpts = sizeof(kExtraOpts) / sizeof(kExtraOpts[0]);

constexpr const char *kCharsMapNames[] = {
    "identity", "nfkc", "nfkc_cf", "nmt_nfkc", "nmt_nfkc_cf",
};
constexpr size_t kNumCharsMapNames =
    sizeof(kCharsMapNames) / sizeof(kCharsMapNames[0]);

void MaybeCorrupt(FuzzedDataProvider &fdp, std::string *blob) {
  if (blob->empty()) return;
  switch (fdp.ConsumeIntegralInRange<int>(0, 4)) {
    case 0: {

      const size_t pos =
          fdp.ConsumeIntegralInRange<size_t>(0, blob->size() - 1);
      (*blob)[pos] = static_cast<char>(fdp.ConsumeIntegral<uint8_t>());
      break;
    }
    case 1: {

      if (blob->size() > 4) {
        uint32_t bogus = static_cast<uint32_t>(blob->size());
        std::memcpy(&(*blob)[0], &bogus, sizeof(bogus));
      }
      break;
    }
    case 2: {

      if (blob->size() > 4) {
        uint32_t bogus = 1025;
        std::memcpy(&(*blob)[0], &bogus, sizeof(bogus));
      }
      break;
    }
    case 3: {

      blob->resize(fdp.ConsumeIntegralInRange<size_t>(0, 4));
      break;
    }
    default:
      break;
  }
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fdp(data, size);

  const std::string text_input = fdp.ConsumeRandomLengthString(128);

  if (fdp.ConsumeIntegralInRange<int>(0, 9) == 0) {
    const std::string raw = fdp.ConsumeRemainingBytesAsString();
    sentencepiece::SentencePieceProcessor raw_sp;
    (void)raw_sp.LoadFromSerializedProto(raw);
    return 0;
  }

  ModelProto model_proto;

  auto *trainer_spec = model_proto.mutable_trainer_spec();
  const TrainerSpec::ModelType model_type = PickModelType(fdp);
  trainer_spec->set_model_type(model_type);

  const bool byte_fallback = fdp.ConsumeBool();
  trainer_spec->set_byte_fallback(byte_fallback);
  trainer_spec->set_treat_whitespace_as_suffix(fdp.ConsumeBool());

  auto *norm_spec = model_proto.mutable_normalizer_spec();
  norm_spec->set_add_dummy_prefix(fdp.ConsumeBool());
  norm_spec->set_remove_extra_whitespaces(fdp.ConsumeBool());
  norm_spec->set_escape_whitespaces(fdp.ConsumeBool());

  if (fdp.ConsumeBool()) {
    const std::string name = kCharsMapNames[fdp.ConsumeIntegralInRange<size_t>(
        0, kNumCharsMapNames - 1)];
    std::string charsmap;
    if (sentencepiece::normalizer::Builder::GetPrecompiledCharsMap(name,
                                                                    &charsmap)
            .ok() &&
        !charsmap.empty()) {
      MaybeCorrupt(fdp, &charsmap);
      norm_spec->set_precompiled_charsmap(charsmap);
    }
  }

  if (fdp.ConsumeBool()) {
    const std::string name = kCharsMapNames[fdp.ConsumeIntegralInRange<size_t>(
        0, kNumCharsMapNames - 1)];
    std::string denorm_charsmap;
    if (sentencepiece::normalizer::Builder::GetPrecompiledCharsMap(
            name, &denorm_charsmap)
            .ok() &&
        !denorm_charsmap.empty()) {
      MaybeCorrupt(fdp, &denorm_charsmap);
      model_proto.mutable_denormalizer_spec()->set_precompiled_charsmap(
          denorm_charsmap);
    }
  }

  std::set<std::string> used_pieces;

  used_pieces.insert("<unk>");
  AddPiece(&model_proto, fdp, "<unk>", 0.0f,
           ModelProto::SentencePiece::UNKNOWN);

  const bool add_bos_eos_pieces = fdp.ConsumeBool();
  if (add_bos_eos_pieces) {
    used_pieces.insert("<s>");
    AddPiece(&model_proto, fdp, "<s>", 0.0f,
             ModelProto::SentencePiece::CONTROL);
    used_pieces.insert("</s>");
    AddPiece(&model_proto, fdp, "</s>", 0.0f,
             ModelProto::SentencePiece::CONTROL);
  }

  static const char *const kBaseVocab[] = {
      "\xe2\x96\x81",
      "a", "b", "c", "d", "e", "f", "g", "h", "i",
      "th", "he", "in", "er", "on", "an", "re", "en",
  };
  for (const char *piece : kBaseVocab) {
    if (!used_pieces.insert(piece).second) continue;
    AddPiece(&model_proto, fdp, piece, fdp.ConsumeFloatingPoint<float>(),
             ModelProto::SentencePiece::NORMAL);
  }

  if (byte_fallback) {
    char buf[8];
    for (int b = 0; b < 256; ++b) {
      std::snprintf(buf, sizeof(buf), "<0x%02X>", b);
      used_pieces.insert(buf);
    }
  }

  const int num_pieces = fdp.ConsumeIntegralInRange<int>(0, 64);
  for (int i = 0; i < num_pieces && fdp.remaining_bytes() > 0; ++i) {
    std::string piece =
        SanitizePieceText(fdp.ConsumeRandomLengthString(16), fdp);
    if (!used_pieces.insert(piece).second) {

      piece += "_" + std::to_string(i);
      if (!used_pieces.insert(piece).second) continue;
    }
    const float score = fdp.ConsumeFloatingPoint<float>();
    AddPiece(&model_proto, fdp, piece, score, PickNonByteType(fdp));
  }

  if (byte_fallback) {
    char buf[8];
    for (int b = 0; b < 256; ++b) {
      std::snprintf(buf, sizeof(buf), "<0x%02X>", b);
      AddPiece(&model_proto, fdp, buf, -10.0f,
               ModelProto::SentencePiece::BYTE);
    }
  }

  if (fdp.ConsumeBool()) {
    sentencepiece::SentencePieceProcessor probe;
    const std::string probe_serialized = model_proto.SerializeAsString();
    (void)probe.LoadFromSerializedProto(probe_serialized);
    if (probe.status().ok()) {
      auto *self_test = model_proto.mutable_self_test_data();
      const int num_samples = fdp.ConsumeIntegralInRange<int>(0, 4);
      for (int i = 0; i < num_samples && fdp.remaining_bytes() > 0; ++i) {
        const std::string sample_input = fdp.ConsumeRandomLengthString(32);
        std::vector<std::string> sample_pieces_vec;
        (void)probe.Encode(sample_input, &sample_pieces_vec);
        std::string expected;
        for (size_t j = 0; j < sample_pieces_vec.size(); ++j) {
          if (j != 0) expected += " ";
          expected += sample_pieces_vec[j];
        }
        if (fdp.ConsumeIntegralInRange<int>(0, 7) == 0) {
          expected += "_x";
        }
        auto *sample = self_test->add_samples();
        sample->set_input(sample_input);
        sample->set_expected(expected);
      }
    }
  }

  const std::string serialized_proto = model_proto.SerializeAsString();

  sentencepiece::SentencePieceProcessor fuzz_sp_processor;

  absl::string_view serialized(serialized_proto);

  (void)fuzz_sp_processor.LoadFromSerializedProto(serialized);

  if (!fuzz_sp_processor.status().ok()) {

    return 0;
  }

  const std::string encode_opt =
      kExtraOpts[fdp.ConsumeIntegralInRange<size_t>(0, kNumExtraOpts - 1)];
  const std::string decode_opt =
      kExtraOpts[fdp.ConsumeIntegralInRange<size_t>(0, kNumExtraOpts - 1)];
  (void)fuzz_sp_processor.SetEncodeExtraOptions(encode_opt);
  (void)fuzz_sp_processor.SetDecodeExtraOptions(decode_opt);

  std::vector<std::string> pieces;
  (void)fuzz_sp_processor.Encode(text_input, &pieces);

  std::vector<int> ids;
  (void)fuzz_sp_processor.Encode(text_input, &ids);

  (void)fuzz_sp_processor.EncodeAsPieces(text_input);
  (void)fuzz_sp_processor.EncodeAsIds(text_input);

  std::vector<std::vector<std::string>> nbest_pieces;
  (void)fuzz_sp_processor.NBestEncode(text_input, 5, &nbest_pieces);

  std::vector<std::vector<int>> nbest_ids;
  (void)fuzz_sp_processor.NBestEncode(text_input, 5, &nbest_ids);

  std::vector<std::string> sample_pieces;
  (void)fuzz_sp_processor.SampleEncode(text_input, -1, 0.3f, &sample_pieces);

  std::vector<int> sample_ids;
  (void)fuzz_sp_processor.SampleEncode(text_input, -1, 0.3f, &sample_ids);

  std::string detok;
  (void)fuzz_sp_processor.Decode(absl::Span<const std::string>(pieces),
                                  &detok);
  std::string detok_ids;
  (void)fuzz_sp_processor.Decode(absl::Span<const int>(ids), &detok_ids);

  std::vector<absl::string_view> sv_pieces(pieces.begin(), pieces.end());
  std::string detok_sv;
  (void)fuzz_sp_processor.Decode(absl::Span<const absl::string_view>(sv_pieces),
                                  &detok_sv);

  {
    sentencepiece::ThreadPool pool(2);
    std::vector<std::string> par_pieces;
    (void)fuzz_sp_processor.ParallelEncode(text_input, 8, pool, &par_pieces);
    std::vector<int> par_ids;
    (void)fuzz_sp_processor.ParallelEncode(text_input, 8, pool, &par_ids);
    (void)fuzz_sp_processor.ParallelEncodeAsPieces(text_input, 8, pool);
    (void)fuzz_sp_processor.ParallelEncodeAsIds(text_input, 8, pool);
  }

  (void)fuzz_sp_processor.DecodePieces(sv_pieces);
  (void)fuzz_sp_processor.DecodeIds(ids);

  sentencepiece::SentencePieceText encode_spt;
  (void)fuzz_sp_processor.Encode(text_input, &encode_spt);

  sentencepiece::NBestSentencePieceText nbest_spt;
  (void)fuzz_sp_processor.NBestEncode(text_input, 5, &nbest_spt);

  sentencepiece::SentencePieceText sample_spt;
  (void)fuzz_sp_processor.SampleEncode(text_input, -1, 0.3f, &sample_spt);

  sentencepiece::SentencePieceText decode_sv_spt;
  (void)fuzz_sp_processor.Decode(absl::Span<const absl::string_view>(sv_pieces),
                                  &decode_sv_spt);

  sentencepiece::SentencePieceText decode_ids_spt;
  (void)fuzz_sp_processor.Decode(absl::Span<const int>(ids), &decode_ids_spt);

  std::string normalized;
  (void)fuzz_sp_processor.Normalize(text_input, &normalized);

  std::string normalized2;
  std::vector<size_t> norm_to_orig;
  (void)fuzz_sp_processor.Normalize(text_input, &normalized2, &norm_to_orig);

  (void)fuzz_sp_processor.GetPieceSize();
  (void)fuzz_sp_processor.PieceToId(text_input);

  if (!ids.empty()) {
    const int id = ids[0];
    (void)fuzz_sp_processor.IsUnknown(id);
    (void)fuzz_sp_processor.IsControl(id);
    (void)fuzz_sp_processor.IsByte(id);
    (void)fuzz_sp_processor.IsUnused(id);
    (void)fuzz_sp_processor.GetScore(id);
    (void)fuzz_sp_processor.IdToPiece(id);
    std::string piece_out;
    (void)fuzz_sp_processor.SafeIdToPiece(id, &piece_out);
  }

  return 0;
}