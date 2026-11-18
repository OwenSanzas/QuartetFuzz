#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "sodium.h"

static const unsigned char kSmallOrder[7][32] = {

    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },

    { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },

    { 0x26, 0xe8, 0x95, 0x8f, 0xc2, 0xb2, 0x27, 0xb0, 0x45, 0xc3, 0xf4,
      0x89, 0xf2, 0xef, 0x98, 0xf0, 0xd5, 0xdf, 0xac, 0x05, 0xd3, 0xc6,
      0x33, 0x39, 0xb1, 0x38, 0x02, 0x88, 0x6d, 0x53, 0xfc, 0x05 },

    { 0xc7, 0x17, 0x6a, 0x70, 0x3d, 0x4d, 0xd8, 0x4f, 0xba, 0x3c, 0x0b,
      0x76, 0x0d, 0x10, 0x67, 0x0f, 0x2a, 0x20, 0x53, 0xfa, 0x2c, 0x39,
      0xcc, 0xc6, 0x4e, 0xc7, 0xfd, 0x77, 0x92, 0xac, 0x03, 0x7a },

    { 0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f },

    { 0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f },

    { 0xee, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f },
};

static const size_t kInterestingLens[] = {
    0,   1,   31,  32,  63,  64,  65,  111, 112, 119, 120,
    127, 128, 129, 191, 192, 193, 255, 256, 257, 383, 384, 512
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static bool initialized = false;
  if (!initialized) {
    if (sodium_init() < 0) {
      return 0;
    }
    initialized = true;
  }

  FuzzedDataProvider stream(data, size);

  std::vector<uint8_t> seed =
      stream.ConsumeBytes<uint8_t>(crypto_sign_SEEDBYTES);
  if (seed.size() != crypto_sign_SEEDBYTES) {
    return 0;
  }

  unsigned char pk[crypto_sign_PUBLICKEYBYTES];
  unsigned char sk[crypto_sign_SECRETKEYBYTES];
  if (crypto_sign_seed_keypair(pk, sk, seed.data()) != 0) {
    return 0;
  }

  uint8_t mode = stream.ConsumeIntegral<uint8_t>();
  uint8_t smallorder_mode = stream.ConsumeIntegral<uint8_t>();
  uint8_t scalar_idx =
      stream.ConsumeIntegralInRange<uint8_t>(0, 31);
  uint8_t r_idx =
      stream.ConsumeIntegralInRange<uint8_t>(0, 31);
  uint8_t pk_idx = stream.ConsumeIntegralInRange<uint8_t>(
      0, crypto_sign_PUBLICKEYBYTES - 1);

  uint8_t len_knob = stream.ConsumeIntegral<uint8_t>();

  std::vector<uint8_t> seed2 =
      stream.ConsumeBytes<uint8_t>(crypto_sign_SEEDBYTES);

  std::vector<uint8_t> rnd_sig =
      stream.ConsumeBytes<uint8_t>(crypto_sign_BYTES);
  std::vector<uint8_t> rnd_pk =
      stream.ConsumeBytes<uint8_t>(crypto_sign_PUBLICKEYBYTES);
  rnd_sig.resize(crypto_sign_BYTES, 0);
  rnd_pk.resize(crypto_sign_PUBLICKEYBYTES, 0);

  std::vector<uint8_t> m = stream.ConsumeRemainingBytes<uint8_t>();
  if (m.size() > crypto_sign_MESSAGEBYTES_MAX) {
    m.resize(crypto_sign_MESSAGEBYTES_MAX);
  }

  if (len_knob & 0x1) {
    size_t target = kInterestingLens[(len_knob >> 1) %
                                      (sizeof(kInterestingLens) /
                                       sizeof(kInterestingLens[0]))];
    if (target > crypto_sign_MESSAGEBYTES_MAX) {
      target = crypto_sign_MESSAGEBYTES_MAX;
    }
    m.resize(target, 0x42);
  }

  unsigned char sig[crypto_sign_BYTES];
  unsigned long long siglen = 0;
  if (crypto_sign_detached(sig, &siglen, m.empty() ? nullptr : m.data(),
                            m.size(), sk) != 0) {
    return 0;
  }

  if (mode & 0x1) {

    sig[32 + scalar_idx] ^= 0xFF;
  }

  if (smallorder_mode & 0x1) {
    std::memcpy(sig, kSmallOrder[(smallorder_mode >> 1) % 7], 32);
  } else if (mode & 0x2) {

    sig[r_idx] ^= 0xFF;
  }

  if (smallorder_mode & 0x2) {
    std::memcpy(pk, kSmallOrder[(smallorder_mode >> 2) % 7], 32);
  } else if (mode & 0x4) {

    pk[pk_idx] ^= 0xFF;
  }

  if ((mode & 0x8) && !m.empty()) {

    m[m.size() / 2] ^= 0xFF;
  }
  if (mode & 0x10) {

    sig[63] = 0xFF;
  }
  if (mode & 0x20) {

    pk[31] |= 0x80;
  }

  int ret = crypto_sign_verify_detached(sig, m.empty() ? nullptr : m.data(),
                                         m.size(), pk);
  (void)ret;

  int ret2 = crypto_sign_verify_detached(
      rnd_sig.data(), m.empty() ? nullptr : m.data(), m.size(), rnd_pk.data());
  (void)ret2;

  if (seed2.size() == crypto_sign_SEEDBYTES) {
    unsigned char pk2[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk2[crypto_sign_SECRETKEYBYTES];
    if (crypto_sign_seed_keypair(pk2, sk2, seed2.data()) == 0) {
      unsigned char sig2[crypto_sign_BYTES];
      unsigned long long siglen2 = 0;
      if (crypto_sign_detached(sig2, &siglen2, m.empty() ? nullptr : m.data(),
                                m.size(), sk2) == 0) {

        int ret3 = crypto_sign_verify_detached(
            sig, m.empty() ? nullptr : m.data(), m.size(), pk2);
        (void)ret3;
        int ret4 = crypto_sign_verify_detached(
            sig2, m.empty() ? nullptr : m.data(), m.size(), pk);
        (void)ret4;

        int ret5 = crypto_sign_verify_detached(
            sig2, m.empty() ? nullptr : m.data(), m.size(), pk2);
        (void)ret5;
      }
    }
  }

  return 0;
}