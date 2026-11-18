#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include <woff2/decode.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

  size_t final_size =
      std::min(woff2::ComputeWOFF2FinalSize(data, size),
               woff2::kDefaultMaxSize);

  std::string output(final_size, 0);
  woff2::WOFF2StringOut out(&output);

  woff2::ConvertWOFF2ToTTF(data, size, &out);

  return 0;
}