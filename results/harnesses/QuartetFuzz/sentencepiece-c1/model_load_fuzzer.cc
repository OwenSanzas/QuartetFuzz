#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>
#include "sentencepiece_processor.h"
#include "sentencepiece_model.pb.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

  if (size < 4) return 0;

  FuzzedDataProvider fdp(data, size);

  const uint8_t ops = fdp.ConsumeIntegral<uint8_t>();
  const uint8_t extra_sel = fdp.ConsumeIntegral<uint8_t>();
  const int nbest_size = fdp.ConsumeIntegralInRange<int>(-4, 8);
  const float alpha = fdp.ConsumeFloatingPoint<float>();

  const size_t text_len = fdp.ConsumeIntegralInRange<size_t>(0, 64);
  const std::string text = fdp.ConsumeBytesAsString(text_len);

  const std::string model_bytes = fdp.ConsumeRemainingBytesAsString();

  sentencepiece::SentencePieceProcessor sp;
  absl::Status status = sp.LoadFromSerializedProto(model_bytes);
  if (!status.ok()) {

    return 0;
  }

  const sentencepiece::ModelProto &loaded_proto = sp.model_proto();
  volatile int piece_count = loaded_proto.pieces_size();
  (void)piece_count;
  std::string reserialized = sp.serialized_model_proto();
  volatile size_t reserialized_len = reserialized.size();
  (void)reserialized_len;

  static const char *kExtraOptionTokens[] = {"bos", "eos", "reverse",
                                              "unk", "unk_piece"};
  if (extra_sel & 0x01) {
    std::string opt = kExtraOptionTokens[extra_sel % 5];
    if (extra_sel & 0x10)
      opt += ":" + std::string(kExtraOptionTokens[(extra_sel >> 2) % 5]);
    sp.SetEncodeExtraOptions(opt);
  }
  if (extra_sel & 0x02) {
    std::string opt = kExtraOptionTokens[(extra_sel >> 1) % 5];
    sp.SetDecodeExtraOptions(opt);
  }

  if (ops & 0x01) {
    std::vector<std::string> pieces;
    sp.Encode(text, &pieces);
    if (!pieces.empty()) {
      std::string decoded;
      sp.Decode(pieces, &decoded);
    }
  }

  if (ops & 0x02) {
    std::vector<int> ids;
    sp.Encode(text, &ids);
    if (!ids.empty()) {
      std::string decoded;
      sp.Decode(ids, &decoded);
    }
  }

  if (ops & 0x04) {
    std::vector<std::vector<std::string>> nbest_pieces;
    sp.NBestEncode(text, nbest_size, &nbest_pieces);

    std::vector<std::vector<int>> nbest_ids;
    sp.NBestEncode(text, nbest_size, &nbest_ids);
  }

  if (ops & 0x08) {
    std::vector<std::string> sampled;
    sp.SampleEncode(text, nbest_size, alpha, &sampled);

    std::vector<int> sampled_ids;
    sp.SampleEncode(text, nbest_size, alpha, &sampled_ids);
  }

  if (ops & 0x10) {
    std::string normalized;
    sp.Normalize(text, &normalized);

    std::string normalized2;
    std::vector<size_t> norm_to_orig;
    sp.Normalize(text, &normalized2, &norm_to_orig);
  }

  if (ops & 0x20) {
    int id = sp.PieceToId(text);
    const int vocab_size = sp.GetPieceSize();
    if (id >= 0 && id < vocab_size) {
      sp.IdToPiece(id);
      sp.GetScore(id);
      sp.IsUnknown(id);
      sp.IsControl(id);
      sp.IsUnused(id);
      sp.IsByte(id);
    }

    for (int i = 0; i < vocab_size && i < 8; i++) {
      sp.IdToPiece(i);
      sp.IsUnknown(i);
      sp.IsControl(i);
      sp.IsUnused(i);
      sp.IsByte(i);
    }
  }

  if (ops & 0x40) {
    (void)sp.unk_id();
    (void)sp.bos_id();
    (void)sp.eos_id();
  }

  return 0;
}
