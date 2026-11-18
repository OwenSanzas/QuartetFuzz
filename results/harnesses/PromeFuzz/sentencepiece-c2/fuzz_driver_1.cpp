#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <fstream>

#include "sentencepiece_processor.h"
#include "sentencepiece_trainer.h"
#include "sentencepiece.pb.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size == 0) {
    return 0;
  }

  std::string input(reinterpret_cast<const char *>(Data), Size);

  const char *kDummyFile = "./dummy_file";
  {
    std::ofstream ofs(kDummyFile, std::ios::binary | std::ios::trunc);
    if (!ofs) {
      return 0;
    }
    ofs.write(input.data(), static_cast<std::streamsize>(input.size()));
    if (!ofs) {
      return 0;
    }
  }

  const std::string model_prefix = "./dummy_model";

  std::string train_args =
      "--input=" + std::string(kDummyFile) +
      " --model_prefix=" + model_prefix +
      " --vocab_size=16"
      " --model_type=unigram"
      " --character_coverage=1.0"
      " --pad_id=-1"
      " --unk_id=0"
      " --bos_id=-1"
      " --eos_id=-1"
      " --minloglevel=2"
      " --hard_vocab_limit=false";

  const auto train_status = sentencepiece::SentencePieceTrainer::Train(train_args);
  if (!train_status.ok()) {

    return 0;
  }

  sentencepiece::SentencePieceProcessor processor;
  const auto load_status = processor.Load(model_prefix + ".model");
  if (!load_status.ok()) {
    return 0;
  }

  absl::string_view text(input);

  {
    std::vector<std::string> pieces;
    const auto status = processor.Encode(text, &pieces);
    if (!status.ok()) {

    }
  }

  {
    std::vector<int> ids;
    const auto status = processor.Encode(text, &ids);
    if (!status.ok()) {

    }
  }

  {
    sentencepiece::SentencePieceText spt;
    const auto status = processor.Encode(text, &spt);
    if (!status.ok()) {

    }
  }

  std::remove((model_prefix + ".model").c_str());
  std::remove((model_prefix + ".vocab").c_str());
  std::remove(kDummyFile);

  return 0;
}