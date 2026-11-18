#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <sodium.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static bool initialized = false;
  if (!initialized) {
    if (sodium_init() < 0) {
      return 0;
    }
    initialized = true;
  }

  FuzzedDataProvider stream(data, size);

  std::vector<uint8_t> nonce =
      stream.ConsumeBytes<uint8_t>(crypto_secretbox_NONCEBYTES);
  if (nonce.size() != crypto_secretbox_NONCEBYTES) {

    return 0;
  }

  std::vector<uint8_t> key =
      stream.ConsumeBytes<uint8_t>(crypto_secretbox_KEYBYTES);
  if (key.size() != crypto_secretbox_KEYBYTES) {

    return 0;
  }

  static const size_t kBuckets[] = {
      0,   1,   15,  16,  17,  31,  32,  33,  63,  64,  65,  95,  96,
      97,  200, 255, 256, 257, 287, 288, 289, 511, 512, 513, 543, 544,
      545, 1024};
  constexpr uint8_t kNumBuckets =
      static_cast<uint8_t>(sizeof(kBuckets) / sizeof(kBuckets[0]));
  uint8_t bucket_sel =
      stream.ConsumeIntegralInRange<uint8_t>(0, kNumBuckets - 1);
  size_t desired_mlen = kBuckets[bucket_sel];

  static const size_t kClenBuckets[] = {0, 1, 15, 16, 17, 18, 32, 33};
  constexpr uint8_t kNumClenBuckets =
      static_cast<uint8_t>(sizeof(kClenBuckets) / sizeof(kClenBuckets[0]));
  uint8_t clen_bucket_sel =
      stream.ConsumeIntegralInRange<uint8_t>(0, kNumClenBuckets - 1);
  size_t desired_clen = kClenBuckets[clen_bucket_sel];

  std::vector<uint8_t> plaintext = stream.ConsumeBytes<uint8_t>(desired_mlen);

  std::vector<uint8_t> c = stream.ConsumeBytes<uint8_t>(desired_clen);

  uint8_t dummy_byte = 0;
  const uint8_t *c_ptr = c.empty() ? &dummy_byte : c.data();
  const unsigned long long clen = static_cast<unsigned long long>(c.size());

  size_t m_size = 0;
  if (clen >= crypto_secretbox_MACBYTES) {
    m_size = static_cast<size_t>(clen - crypto_secretbox_MACBYTES);
  }
  std::vector<uint8_t> m(m_size > 0 ? m_size : 1);

  int ret = crypto_secretbox_open_easy(m.data(), c_ptr, clen, nonce.data(),
                                        key.data());
  (void)ret;

  int ret_null_m = crypto_secretbox_open_easy(nullptr, c_ptr, clen,
                                               nonce.data(), key.data());
  (void)ret_null_m;

  size_t mlen = plaintext.size();

  if (mlen > crypto_secretbox_MACBYTES) {
    std::vector<uint8_t> buf3(mlen + crypto_secretbox_MACBYTES);
    std::copy(plaintext.begin(), plaintext.end(), buf3.begin());
    int rt_enc_overlap_same = crypto_secretbox_easy(
        buf3.data(), buf3.data(), mlen, nonce.data(), key.data());
    (void)rt_enc_overlap_same;
  }

  if (mlen > 4) {
    std::vector<uint8_t> buf4(mlen + 24);
    std::copy(plaintext.begin(), plaintext.end(), buf4.begin() + 20);
    int rt_enc_overlap_rev = crypto_secretbox_easy(
        buf4.data(), buf4.data() + 20, mlen, nonce.data(), key.data());
    (void)rt_enc_overlap_rev;
  }

  std::vector<uint8_t> boxed(mlen + crypto_secretbox_MACBYTES);
  if (crypto_secretbox_easy(boxed.data(), plaintext.data(), mlen,
                             nonce.data(), key.data()) != 0) {
    return 0;
  }

  {
    std::vector<uint8_t> decrypted(mlen > 0 ? mlen : 1);
    int rt = crypto_secretbox_open_easy(decrypted.data(), boxed.data(),
                                         boxed.size(), nonce.data(),
                                         key.data());
    (void)rt;
  }

  {
    int rt_null = crypto_secretbox_open_easy(nullptr, boxed.data(),
                                              boxed.size(), nonce.data(),
                                              key.data());
    (void)rt_null;
  }

  {
    std::vector<uint8_t> inplace = boxed;
    int rt_overlap = crypto_secretbox_open_easy(
        inplace.data(), inplace.data(), inplace.size(), nonce.data(),
        key.data());
    (void)rt_overlap;
  }

  if (mlen > 4) {
    std::vector<uint8_t> buf2(boxed.size() + 4);
    std::copy(boxed.begin(), boxed.end(), buf2.begin());

    int rt_overlap2 = crypto_secretbox_open_easy(
        buf2.data() + crypto_secretbox_MACBYTES + 4, buf2.data(),
        boxed.size(), nonce.data(), key.data());
    (void)rt_overlap2;
  }

  if (!boxed.empty()) {
    std::vector<uint8_t> corrupted = boxed;
    corrupted[0] ^= 0xFF;
    std::vector<uint8_t> decrypted2(mlen > 0 ? mlen : 1);
    int rt_fail = crypto_secretbox_open_easy(decrypted2.data(),
                                              corrupted.data(),
                                              corrupted.size(), nonce.data(),
                                              key.data());
    (void)rt_fail;
  }

  if (boxed.size() >= crypto_secretbox_MACBYTES) {
    std::vector<uint8_t> bad_mac_box = boxed;
    std::fill(bad_mac_box.begin(),
              bad_mac_box.begin() + crypto_secretbox_MACBYTES, 0);
    std::vector<uint8_t> decrypted3(mlen > 0 ? mlen : 1);
    int rt_bad_mac = crypto_secretbox_open_easy(
        decrypted3.data(), bad_mac_box.data(), bad_mac_box.size(),
        nonce.data(), key.data());
    (void)rt_bad_mac;
  }

  return 0;
}