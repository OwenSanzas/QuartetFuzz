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
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "decode.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size == 0) {
    return 0;
  }

  FILE *fp = fopen("./dummy_file", "wb");
  if (fp) {
    fwrite(Data, 1, Size, fp);
    fclose(fp);
  }

  {
    std::string output_string;
    woff2::WOFF2StringOut out(&output_string);
    out.SetMaxSize(30 * 1024 * 1024);
    bool ok = woff2::ConvertWOFF2ToTTF(Data, Size, &out);
    (void)ok;
  }

  {
    size_t final_size = woff2::ComputeWOFF2FinalSize(Data, Size);
    if (final_size > 0 && final_size < (30 * 1024 * 1024)) {
      std::vector<uint8_t> buf(final_size);
      woff2::WOFF2MemoryOut out(buf.data(), buf.size());
      bool ok = woff2::ConvertWOFF2ToTTF(Data, Size, &out);
      (void)ok;
    }
  }

  {
    size_t result_length = woff2::ComputeWOFF2FinalSize(Data, Size);
    if (result_length > 0 && result_length < (30 * 1024 * 1024)) {
      std::vector<uint8_t> result(result_length);
      bool ok = woff2::ConvertWOFF2ToTTF(result.data(), result_length, Data,
                                          Size);
      (void)ok;
    }
  }

  return 0;
}