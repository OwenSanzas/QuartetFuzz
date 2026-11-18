#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "sentencepiece_trainer.h"

static constexpr int kMaxSentences = 64;
static constexpr int kMaxSentenceLen = 64;

namespace {

std::string ConsumeToken(FuzzedDataProvider &stream, size_t max_len) {
  size_t len = stream.ConsumeIntegralInRange<size_t>(0, max_len);
  std::string token = stream.ConsumeBytesAsString(len);
  std::string cleaned;
  cleaned.reserve(token.size());
  for (unsigned char c : token) {
    if (c == ',' || c == '\t' || c == '\n' || c == '\r' || c == '"' ||
        c == ' ' || c == '=')
      continue;
    cleaned.push_back(static_cast<char>(c));
  }
  return cleaned;
}

std::string JoinCSV(const std::vector<std::string> &items) {
  std::string out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i) out += ",";
    out += items[i];
  }
  return out;
}

std::string B(bool v) { return v ? "true" : "false"; }

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  static const char *kModelTypes[] = {"unigram", "bpe", "char", "word"};
  const char *model_type = stream.PickValueInArray(kModelTypes);
  const bool is_word_or_char =
      std::string(model_type) == "word" || std::string(model_type) == "char";
  const bool is_unigram_or_bpe = !is_word_or_char;

  const int vocab_size = stream.ConsumeIntegralInRange<int>(4, 128);

  const double character_coverage =
      stream.ConsumeFloatingPointInRange<double>(0.98, 1.0);

  const bool use_all_vocab = is_word_or_char ? stream.ConsumeBool() : false;

  const double shrinking_factor =
      stream.ConsumeFloatingPointInRange<double>(0.5, 0.95);
  const int num_sub_iterations = stream.ConsumeIntegralInRange<int>(1, 10);
  const int max_sentencepiece_length =
      stream.ConsumeIntegralInRange<int>(1, 512);
  const int max_sentence_length = stream.ConsumeIntegralInRange<int>(10, 1000);

  const int num_threads = stream.ConsumeIntegralInRange<int>(1, 2);

  const bool split_by_unicode_script = stream.ConsumeBool();
  const bool split_by_number = stream.ConsumeBool();
  const bool split_by_whitespace = stream.ConsumeBool();
  const bool split_digits = stream.ConsumeBool();
  const bool treat_whitespace_as_suffix = stream.ConsumeBool();
  const bool allow_whitespace_only_pieces = stream.ConsumeBool();

  const bool byte_fallback = is_unigram_or_bpe ? stream.ConsumeBool() : false;
  const bool hard_vocab_limit = stream.ConsumeBool();
  const bool vocabulary_output_piece_score = stream.ConsumeBool();
  const bool shuffle_input_sentence = stream.ConsumeBool();

  std::string args;
  args += "--model_type=";
  args += model_type;
  args += " --vocab_size=";
  args += std::to_string(vocab_size);
  args += " --character_coverage=";
  args += std::to_string(character_coverage);
  args += " --use_all_vocab=";
  args += B(use_all_vocab);

  args += " --self_test_sample_size=0";
  args += " --shrinking_factor=";
  args += std::to_string(shrinking_factor);
  args += " --num_sub_iterations=";
  args += std::to_string(num_sub_iterations);
  args += " --max_sentencepiece_length=";
  args += std::to_string(max_sentencepiece_length);
  args += " --max_sentence_length=";
  args += std::to_string(max_sentence_length);
  args += " --num_threads=";
  args += std::to_string(num_threads);
  args += " --split_by_unicode_script=";
  args += B(split_by_unicode_script);
  args += " --split_by_number=";
  args += B(split_by_number);
  args += " --split_by_whitespace=";
  args += B(split_by_whitespace);
  args += " --split_digits=";
  args += B(split_digits);
  args += " --treat_whitespace_as_suffix=";
  args += B(treat_whitespace_as_suffix);
  args += " --allow_whitespace_only_pieces=";
  args += B(allow_whitespace_only_pieces);
  args += " --hard_vocab_limit=";
  args += B(hard_vocab_limit);
  args += " --vocabulary_output_piece_score=";
  args += B(vocabulary_output_piece_score);
  args += " --shuffle_input_sentence=";
  args += B(shuffle_input_sentence);
  if (is_unigram_or_bpe) {
    args += " --byte_fallback=";
    args += B(byte_fallback);
  }

  if (stream.ConsumeBool()) {
    std::vector<std::string> syms;
    int n = stream.ConsumeIntegralInRange<int>(1, 3);
    for (int i = 0; i < n; ++i) {
      std::string tok = ConsumeToken(stream, 8);
      if (!tok.empty()) syms.push_back(tok);
    }
    if (!syms.empty()) {
      args += " --control_symbols=";
      args += JoinCSV(syms);
    }
  }
  if (stream.ConsumeBool()) {
    std::vector<std::string> syms;
    int n = stream.ConsumeIntegralInRange<int>(1, 3);
    for (int i = 0; i < n; ++i) {
      std::string tok = ConsumeToken(stream, 8);
      if (!tok.empty()) syms.push_back(tok);
    }
    if (!syms.empty()) {
      args += " --user_defined_symbols=";
      args += JoinCSV(syms);
    }
  }
  if (stream.ConsumeBool()) {
    std::string req = ConsumeToken(stream, 8);
    if (!req.empty()) {
      args += " --required_chars=";
      args += req;
    }
  }

  if (is_unigram_or_bpe && stream.ConsumeBool()) {
    std::string delim = ConsumeToken(stream, 1);
    if (!delim.empty()) {
      args += " --pretokenization_delimiter=";
      args += delim;
    }
  }

  const bool use_tsv = stream.ConsumeBool();
  if (use_tsv) {
    args += " --input_format=tsv";
  }

  static const char *kNormRules[] = {"nmt_nfkc", "nfkc", "nmt_nfkc_cf",
                                      "identity"};
  if (stream.ConsumeBool()) {
    args += " --normalization_rule_name=";
    args += stream.PickValueInArray(kNormRules);
  }
  args += " --add_dummy_prefix=";
  args += B(stream.ConsumeBool());
  args += " --remove_extra_whitespaces=";
  args += B(stream.ConsumeBool());
  args += " --escape_whitespaces=";
  args += B(stream.ConsumeBool());

  std::vector<std::string> sentences;
  int sentence_count = 0;
  while (stream.remaining_bytes() > 0 && sentence_count < kMaxSentences) {
    const size_t len =
        stream.ConsumeIntegralInRange<size_t>(0, kMaxSentenceLen);
    std::string sentence = stream.ConsumeBytesAsString(len);
    if (sentence.empty()) {

      if (stream.remaining_bytes() == 0) break;
      continue;
    }
    if (use_tsv) {

      std::string word;
      word.reserve(sentence.size());
      for (char c : sentence) {
        if (c != '\t') word.push_back(c);
      }
      if (word.empty()) word = "w";
      int freq = 1 + (static_cast<unsigned char>(sentence[0]) % 10);
      sentence = word + "\t" + std::to_string(freq);
    }
    sentences.push_back(std::move(sentence));
    ++sentence_count;
  }

  if (sentences.empty()) {
    sentences.push_back(use_tsv ? "hello\t1" : "hello world");
  }

  std::string serialized_model_proto;
  absl::Status status = sentencepiece::SentencePieceTrainer::Train(
      args, sentences, &serialized_model_proto);

  (void)status;

  return 0;
}