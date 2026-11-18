#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

#include "sentencepiece_processor.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Data == nullptr || Size == 0) {
    return 0;
  }

  std::string serialized(reinterpret_cast<const char *>(Data), Size);

  {
    sentencepiece::SentencePieceProcessor processor;
    absl::string_view sv(serialized.data(), serialized.size());
    absl::Status status = processor.LoadFromSerializedProto(sv);

    if (status.ok()) {

      std::vector<int> ids;
      processor.Encode("fuzz test string", &ids);

      std::vector<std::string> pieces;
      processor.Encode("fuzz test string", &pieces);

      std::string decoded;
      processor.Decode(ids, &decoded);

      int vocab_size = processor.GetPieceSize();
      if (vocab_size > 0) {
        std::string piece = processor.IdToPiece(0);
        int id = processor.PieceToId(piece);
        (void)id;
      }
    }
  }

  {
    sentencepiece::SentencePieceProcessor processor2;
    absl::string_view empty_sv("", 0);
    absl::Status status2 = processor2.LoadFromSerializedProto(empty_sv);
    (void)status2;
  }

  if (Size > 4) {
    std::string truncated(reinterpret_cast<const char *>(Data), Size / 2);
    sentencepiece::SentencePieceProcessor processor3;
    absl::string_view trunc_sv(truncated.data(), truncated.size());
    absl::Status status3 = processor3.LoadFromSerializedProto(trunc_sv);
    (void)status3;
  }

  {
    FILE *f = fopen("./dummy_file", "wb");
    if (f != nullptr) {
      fwrite(Data, 1, Size, f);
      fclose(f);
    }
  }

  return 0;
}