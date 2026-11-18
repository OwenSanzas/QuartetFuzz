#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <fstream>
#include <memory>
#include <stdexcept>

#include "sentencepiece_trainer.h"
#include "sentencepiece_model.pb.h"

using namespace sentencepiece;

static const char* kDummyFile = "./dummy_file";

class FuzzSentenceIterator : public SentenceIterator {
 public:
  explicit FuzzSentenceIterator(const std::vector<std::string>& sentences)
      : sentences_(sentences), iter_(sentences_.begin()) {}

  bool done() const override { return iter_ == sentences_.end(); }
  void Next() override {
    if (iter_ != sentences_.end()) ++iter_;
  }
  const std::string& value() const override { return *iter_; }
  absl::Status status() const override { return absl::OkStatus(); }

 private:
  std::vector<std::string> sentences_;
  std::vector<std::string>::const_iterator iter_;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  if (Size < 4) return 0;

  {
    std::ofstream ofs(kDummyFile, std::ios::binary | std::ios::trunc);
    if (ofs) {
      ofs.write(reinterpret_cast<const char*>(Data),
                static_cast<std::streamsize>(Size));
    }
  }

  std::string text(reinterpret_cast<const char*>(Data), Size);
  std::vector<std::string> sentences;
  {
    std::string cur;
    for (char c : text) {
      if (c == '\n') {
        if (!cur.empty()) sentences.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
      if (sentences.size() > 1000) break;
    }
    if (!cur.empty() && sentences.size() <= 1000) sentences.push_back(cur);
  }
  if (sentences.empty()) {
    sentences.push_back("hello world");
    sentences.push_back("fuzzing sentencepiece trainer");
  }
  if (sentences.size() > 1000) sentences.resize(1000);

  int vocab_size = 8 + static_cast<int>(Size % 16);
  if (vocab_size < 8) vocab_size = 8;

  std::string serialized_model;

  try {
    TrainerComponents components;
    FuzzSentenceIterator iter(sentences);
    components.sentence_iterator = &iter;
    auto status = SentencePieceTrainer::Train(components, &serialized_model);
    (void)status;
  } catch (const std::exception&) {
  } catch (...) {
  }

  try {
    TrainerComponents components;
    FuzzSentenceIterator iter(sentences);
    components.sentence_iterator = &iter;
    std::string args = "--model_prefix=./dummy_model2 --vocab_size=" +
                        std::to_string(vocab_size);
    auto status = SentencePieceTrainer::Train(args, components, &serialized_model);
    (void)status;
  } catch (const std::exception&) {
  } catch (...) {
  }

  try {
    TrainerComponents components;
    FuzzSentenceIterator iter(sentences);
    components.sentence_iterator = &iter;
    std::unordered_map<std::string, std::string> kwargs;
    kwargs["model_prefix"] = "./dummy_model3";
    kwargs["vocab_size"] = std::to_string(vocab_size);
    auto status = SentencePieceTrainer::Train(kwargs, components, &serialized_model);
    (void)status;
  } catch (const std::exception&) {
  } catch (...) {
  }

  try {
    TrainerSpec trainer_spec;
    trainer_spec.set_model_prefix("./dummy_model4");
    trainer_spec.set_vocab_size(vocab_size);
    FuzzSentenceIterator iter(sentences);
    auto status = SentencePieceTrainer::Train(trainer_spec, &iter, &serialized_model);
    (void)status;
  } catch (const std::exception&) {
  } catch (...) {
  }

  try {
    TrainerSpec trainer_spec;
    trainer_spec.set_model_prefix("./dummy_model5");
    trainer_spec.set_vocab_size(vocab_size);
    NormalizerSpec normalizer_spec;
    FuzzSentenceIterator iter(sentences);
    auto status = SentencePieceTrainer::Train(trainer_spec, normalizer_spec, &iter,
                                               &serialized_model);
    (void)status;
  } catch (const std::exception&) {
  } catch (...) {
  }

  try {
    TrainerSpec trainer_spec;
    trainer_spec.set_model_prefix("./dummy_model6");
    trainer_spec.set_vocab_size(vocab_size);
    NormalizerSpec normalizer_spec;
    NormalizerSpec denormalizer_spec;
    FuzzSentenceIterator iter(sentences);
    auto status = SentencePieceTrainer::Train(trainer_spec, normalizer_spec,
                                               denormalizer_spec, &iter,
                                               &serialized_model);
    (void)status;
  } catch (const std::exception&) {
  } catch (...) {
  }

  try {
    std::string args = "--model_prefix=./dummy_model7 --vocab_size=" +
                        std::to_string(vocab_size);
    FuzzSentenceIterator iter(sentences);
    auto status = SentencePieceTrainer::Train(args, &iter, &serialized_model);
    (void)status;
  } catch (const std::exception&) {
  } catch (...) {
  }

  try {
    std::unordered_map<std::string, std::string> kwargs;
    kwargs["model_prefix"] = "./dummy_model8";
    kwargs["vocab_size"] = std::to_string(vocab_size);
    FuzzSentenceIterator iter(sentences);
    auto status = SentencePieceTrainer::Train(kwargs, &iter, &serialized_model);
    (void)status;
  } catch (const std::exception&) {
  } catch (...) {
  }

  try {
    std::string args = "--model_prefix=./dummy_model9 --vocab_size=" +
                        std::to_string(vocab_size);
    auto status = SentencePieceTrainer::Train(args, sentences, &serialized_model);
    (void)status;
  } catch (const std::exception&) {
  } catch (...) {
  }

  try {
    std::unordered_map<std::string, std::string> kwargs;
    kwargs["model_prefix"] = "./dummy_model10";
    kwargs["vocab_size"] = std::to_string(vocab_size);
    auto status = SentencePieceTrainer::Train(kwargs, sentences, &serialized_model);
    (void)status;
  } catch (const std::exception&) {
  } catch (...) {
  }

  return 0;
}