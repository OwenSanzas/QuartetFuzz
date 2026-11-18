#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <vector>

#include <woff2/decode.h>
#include <woff2/output.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size == 0) {
    return 0;
  }

  std::vector<uint8_t> buf(data, data + size);
  if (buf.size() >= 4) {
    buf[0] = 'w';
    buf[1] = 'O';
    buf[2] = 'F';
    buf[3] = '2';
  }
  if (buf.size() >= 12) {
    uint32_t len = static_cast<uint32_t>(buf.size());
    buf[8] = static_cast<uint8_t>((len >> 24) & 0xff);
    buf[9] = static_cast<uint8_t>((len >> 16) & 0xff);
    buf[10] = static_cast<uint8_t>((len >> 8) & 0xff);
    buf[11] = static_cast<uint8_t>(len & 0xff);
  }
  const uint8_t *fdata = buf.data();
  const size_t fsize = buf.size();

  {
    size_t hinted_size = woff2::ComputeWOFF2FinalSize(fdata, fsize);
    size_t buf_size = std::min(hinted_size, woff2::kDefaultMaxSize);
    std::string output(buf_size, 0);
    woff2::WOFF2StringOut out(&output);

    bool ok = woff2::ConvertWOFF2ToTTF(fdata, fsize, &out);
    if (ok) {

      size_t produced = out.Size();
      if (produced <= output.size()) {
        unsigned long checksum = 0;
        for (size_t i = 0; i < produced; ++i) {
          checksum += static_cast<unsigned char>(output[i]);
        }

        if (checksum == static_cast<unsigned long>(-1)) {
          return 1;
        }
      }
    }

  }

  {
    size_t hinted_size2 = woff2::ComputeWOFF2FinalSize(fdata, fsize);
    size_t buf_size2 = std::min(hinted_size2, woff2::kDefaultMaxSize);

    size_t shrink = fdata[0] % 8;
    size_t adjusted = buf_size2 - (buf_size2 / 8) * shrink;

    std::vector<uint8_t> result_buf(adjusted);
    bool ok2 = woff2::ConvertWOFF2ToTTF(
        result_buf.empty() ? nullptr : result_buf.data(), result_buf.size(),
        fdata, fsize);
    if (ok2) {
      unsigned long checksum2 = 0;
      for (size_t i = 0; i < result_buf.size(); ++i) {
        checksum2 += result_buf[i];
      }
      if (checksum2 == static_cast<unsigned long>(-1)) {
        return 1;
      }
    }

  }

  return 0;
}
