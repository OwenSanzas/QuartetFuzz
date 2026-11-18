#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sodium.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static bool initialized = false;
  if (!initialized) {
    if (sodium_init() < 0) {
      return 0;
    }
    initialized = true;
  }

  FuzzedDataProvider stream(data, size);

  std::vector<uint8_t> h = stream.ConsumeBytes<uint8_t>(crypto_auth_BYTES);
  if (h.size() != crypto_auth_BYTES) {
    return 0;
  }

  std::vector<uint8_t> k = stream.ConsumeBytes<uint8_t>(crypto_auth_KEYBYTES);
  if (k.size() != crypto_auth_KEYBYTES) {
    return 0;
  }

  std::vector<uint8_t> in = stream.ConsumeRemainingBytes<uint8_t>();

  static const uint8_t fallback_byte = 0;
  const unsigned char *in_ptr =
      in.empty() ? &fallback_byte : reinterpret_cast<const unsigned char *>(in.data());

  (void)crypto_auth_verify(h.data(), in_ptr,
                            static_cast<unsigned long long>(in.size()),
                            k.data());

  return 0;
}