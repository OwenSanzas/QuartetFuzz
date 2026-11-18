#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <vector>

#include <woff2/decode.h>
#include <woff2/output.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

  size_t hint = woff2::ComputeWOFF2FinalSize(data, size);

  std::string buf;
  if (hint > 0 && hint <= (1u << 20)) {
    buf.assign(hint, 0);
  }

  woff2::WOFF2StringOut out(&buf);
  out.SetMaxSize(30 * 1024 * 1024);

  bool ok = woff2::ConvertWOFF2ToTTF(data, size, &out);
  if (ok) {

    size_t n = out.Size();
    if (n > buf.size()) n = buf.size();

    volatile uint8_t sink = 0;
    for (size_t i = 0; i < n; ++i) {
      sink ^= static_cast<uint8_t>(buf[i]);
    }
    (void)sink;
  }

  uint8_t sel = (size > 0) ? data[size - 1] : 0;

  if ((sel & 0x0F) == 0) {
    size_t small_size = 4096;
    if (ok) {

      small_size = std::min<size_t>(out.Size() > 0 ? out.Size() : 4096,
                                     (size_t)(64 * 1024));
      if (small_size == 0) small_size = 1;
    }
    std::vector<uint8_t> fixed_buf(small_size, 0);
    bool ok2 = woff2::ConvertWOFF2ToTTF(fixed_buf.data(), fixed_buf.size(),
                                         data, size);
    if (ok2) {
      volatile uint8_t sink2 = 0;
      for (size_t i = 0; i < fixed_buf.size(); ++i) {
        sink2 ^= fixed_buf[i];
      }
      (void)sink2;
    }
  }

  if ((sel & 0xF0) == 0) {
    std::string buf3;
    woff2::WOFF2StringOut out3(&buf3);
    out3.SetMaxSize(1024);
    bool ok3 = woff2::ConvertWOFF2ToTTF(data, size, &out3);
    if (ok3) {
      size_t n3 = std::min(out3.Size(), buf3.size());
      volatile uint8_t sink3 = 0;
      for (size_t i = 0; i < n3; ++i) {
        sink3 ^= static_cast<uint8_t>(buf3[i]);
      }
      (void)sink3;
    }
  }

  return 0;
}
