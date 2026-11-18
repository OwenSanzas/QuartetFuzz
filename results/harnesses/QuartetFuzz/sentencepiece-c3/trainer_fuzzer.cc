#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "sentencepiece_processor.h"
#include "sentencepiece_trainer.h"

namespace {

const char* kModelTypes[] = {"unigram", "bpe", "word", "char"};

const char* kNormRules[] = {"nmt_nfkc", "nfkc", "nmt_nfkc_cf", "nfkc_cf",
                             "identity"};

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 4) return 0;

  FuzzedDataProvider fdp(data, size);

  const int model_type_idx = fdp.ConsumeIntegralInRange<int>(0, 3);
  const char* model_type = kModelTypes[model_type_idx];
  const bool use_tsv = fdp.ConsumeBool();
  const int vocab_size = fdp.ConsumeIntegralInRange<int>(6, 60);
  const int char_coverage_pct = fdp.ConsumeIntegralInRange<int>(98, 100);
  const bool split_by_whitespace = fdp.ConsumeBool();
  const bool add_dummy_prefix = fdp.ConsumeBool();
  const bool escape_whitespaces = fdp.ConsumeBool();
  const bool remove_extra_whitespaces = fdp.ConsumeBool();
  const char* norm_rule =
      kNormRules[fdp.ConsumeIntegralInRange<int>(0, 4)];
  const int max_sentence_length = fdp.ConsumeIntegralInRange<int>(10, 2048);
  const int max_sentencepiece_length = fdp.ConsumeIntegralInRange<int>(1, 32);
  const int self_test_sample_size = fdp.ConsumeIntegralInRange<int>(0, 20);
  const int num_sub_iterations = fdp.ConsumeIntegralInRange<int>(1, 4);
  const int shrinking_factor_pct = fdp.ConsumeIntegralInRange<int>(50, 95);

  const bool use_all_vocab =
      (model_type_idx == 2 || model_type_idx == 3) && fdp.ConsumeBool();

  std::vector<std::string> sentences;

  while (fdp.remaining_bytes() > 0 && sentences.size() < 64) {
    const size_t max_len = fdp.remaining_bytes() > 128 ? 128
                                                         : fdp.remaining_bytes();
    size_t len = fdp.ConsumeIntegralInRange<size_t>(0, max_len);
    std::string sentence = fdp.ConsumeBytesAsString(len);
    if (use_tsv) {

      int freq = static_cast<int>(fdp.ConsumeIntegralInRange<int>(1, 50));
      sentence = sentence + "\t" + std::to_string(freq);
    }
    sentences.push_back(std::move(sentence));

    if (fdp.remaining_bytes() > 0) {
      fdp.ConsumeIntegral<uint8_t>();
    }
  }
  if (sentences.empty()) {
    sentences.push_back("hello world");
  }

  std::string args;
  args += "--model_type=";
  args += model_type;
  args += " --vocab_size=";
  args += std::to_string(vocab_size);
  args += " --character_coverage=0.";
  args += std::to_string(char_coverage_pct >= 100 ? 9999 : char_coverage_pct * 100 + 1);
  args += " --input_format=";
  args += use_tsv ? "tsv" : "text";
  args += " --split_by_whitespace=";
  args += split_by_whitespace ? "true" : "false";
  args += " --add_dummy_prefix=";
  args += add_dummy_prefix ? "true" : "false";
  args += " --escape_whitespaces=";
  args += escape_whitespaces ? "true" : "false";
  args += " --remove_extra_whitespaces=";
  args += remove_extra_whitespaces ? "true" : "false";
  args += " --normalization_rule_name=";
  args += norm_rule;
  args += " --max_sentence_length=";
  args += std::to_string(max_sentence_length);
  args += " --max_sentencepiece_length=";
  args += std::to_string(max_sentencepiece_length);
  args += " --self_test_sample_size=";
  args += std::to_string(self_test_sample_size);
  args += " --num_sub_iterations=";
  args += std::to_string(num_sub_iterations);
  args += " --shrinking_factor=0.";
  args += std::to_string(shrinking_factor_pct);
  args += " --use_all_vocab=";
  args += use_all_vocab ? "true" : "false";
  args += " --num_threads=1";

  std::string serialized_model_proto;
  const absl::Status status = sentencepiece::SentencePieceTrainer::Train(
      args, sentences, &serialized_model_proto);

  if (!status.ok() || serialized_model_proto.empty()) {
    return 0;
  }

  sentencepiece::SentencePieceProcessor processor;
  const absl::Status load_status =
      processor.LoadFromSerializedProto(serialized_model_proto);
  if (!load_status.ok()) {
    return 0;
  }

  for (const auto& sentence : sentences) {
    std::vector<std::string> pieces;
    processor.Encode(sentence, &pieces);
    if (!pieces.empty()) {
      std::string decoded;
      processor.Decode(pieces, &decoded);
    }

    std::vector<int> ids;
    processor.Encode(sentence, &ids);
    if (!ids.empty()) {
      std::string decoded_ids;
      processor.Decode(ids, &decoded_ids);
    }

    auto nbest_pieces = processor.NBestEncodeAsPieces(sentence, 3);
    (void)nbest_pieces;
    auto sampled_pieces = processor.SampleEncodeAsPieces(sentence, -1, 0.5);
    (void)sampled_pieces;

    std::string normalized;
    processor.Normalize(sentence, &normalized);

    const int id = processor.PieceToId(sentence);
    if (id >= 0 && id < processor.GetPieceSize()) {
      (void)processor.IdToPiece(id);
      (void)processor.GetScore(id);
      (void)processor.IsUnknown(id);
      (void)processor.IsControl(id);
      (void)processor.IsUnused(id);
      (void)processor.IsByte(id);
    }
  }

  return 0;
}
