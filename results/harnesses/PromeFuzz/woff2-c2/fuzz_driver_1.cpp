#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>

#include "decode.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Data == nullptr || Size == 0) {
    return 0;
  }

  std::vector<uint8_t> input(Data, Data + Size);

  {
    std::string out_buf;
    woff2::WOFF2StringOut out(&out_buf);

    out.SetMaxSize(100 * 1024 * 1024);
    bool success = woff2::ConvertWOFF2ToTTF(input.data(), input.size(), &out);
    (void)success;
  }

  {
    size_t buf_size = 1024 * 1024;
    std::vector<uint8_t> mem_buf(buf_size, 0);
    woff2::WOFF2MemoryOut mem_out(mem_buf.data(), mem_buf.size());
    bool success = woff2::ConvertWOFF2ToTTF(input.data(), input.size(), &mem_out);
    (void)success;
  }

  {
    size_t result_length = woff2::ComputeWOFF2FinalSize(input.data(), input.size());
    if (result_length > 0 && result_length < (100 * 1024 * 1024)) {
      std::vector<uint8_t> result_buf(result_length, 0);
      bool success = woff2::ConvertWOFF2ToTTF(result_buf.data(), result_buf.size(),
                                               input.data(), input.size());
      (void)success;
    }
  }

  if (input.size() > 4) {
    std::vector<uint8_t> truncated(input.begin(), input.begin() + (input.size() / 2));
    std::string out_buf2;
    woff2::WOFF2StringOut out2(&out_buf2);
    out2.SetMaxSize(50 * 1024 * 1024);
    bool success = woff2::ConvertWOFF2ToTTF(truncated.data(), truncated.size(), &out2);
    (void)success;
  }

  return 0;
}