#include <fuzzer/FuzzedDataProvider.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <sodium.h>

static const unsigned char kSmallOrderPk[][crypto_box_PUBLICKEYBYTES] = {

    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
     0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},

    {0xe0,0xeb,0x7a,0x7c,0x3b,0x41,0xb8,0xae,0x16,0x56,0xe3,0xfa,0xf1,0x9f,0xc4,0x6a,
     0xda,0x09,0x8d,0xeb,0x9c,0x32,0xb1,0xfd,0x86,0x62,0x05,0x16,0x5f,0x49,0xb8,0x00},
};

static unsigned char g_pk[crypto_box_PUBLICKEYBYTES];
static unsigned char g_sk[crypto_box_SECRETKEYBYTES];

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  int initialized = sodium_init();
  assert(initialized >= 0);
  crypto_box_keypair(g_pk, g_sk);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  {
    unsigned char nonce[crypto_box_NONCEBYTES] = {0};

    unsigned char tiny_c[crypto_box_MACBYTES - 1] = {0};
    unsigned char tiny_m[sizeof(tiny_c)];
    int ret_tiny = crypto_box_open_easy(tiny_m, tiny_c, sizeof(tiny_c), nonce,
                                         g_pk, g_sk);
    (void)ret_tiny;

    unsigned char exact_c[crypto_box_MACBYTES] = {0};
    int ret_exact =
        crypto_box_open_easy(nullptr, exact_c, sizeof(exact_c), nonce, g_pk,
                              g_sk);
    (void)ret_exact;
  }

  {
    unsigned char nonce[crypto_box_NONCEBYTES] = {0};
    unsigned char c[crypto_box_MACBYTES + 16] = {0};
    unsigned char m[sizeof(c)];

    for (size_t idx = 0;
         idx < sizeof(kSmallOrderPk) / sizeof(kSmallOrderPk[0]); idx++) {

      int ret1 = crypto_box_open_easy(nullptr, c, sizeof(c), nonce,
                                       kSmallOrderPk[idx], g_sk);
      (void)ret1;

      int ret2 =
          crypto_box_open_easy(m, c, sizeof(c), nonce, kSmallOrderPk[idx],
                                g_sk);
      (void)ret2;

      unsigned char inplace[sizeof(c)];
      memcpy(inplace, c, sizeof(c));
      int ret3 = crypto_box_open_easy(inplace, inplace, sizeof(inplace),
                                       nonce, kSmallOrderPk[idx], g_sk);
      (void)ret3;
    }
  }

  {
    static const size_t kSizes[] = {0,   1,   15,  16,  17,  31,   32,
                                     33,  63,  64,  65,  255, 256,  257,
                                     511, 512, 513, 1023, 1024, 1025};
    unsigned char nonce[crypto_box_NONCEBYTES] = {0};
    for (size_t s : kSizes) {
      std::vector<uint8_t> msg(s, 0x42);
      std::vector<uint8_t> c(s + crypto_box_MACBYTES);
      if (crypto_box_easy(c.data(), s ? msg.data() : nullptr, s, nonce, g_pk,
                           g_sk) == 0) {

        std::vector<unsigned char> m(c.size());
        int r1 = crypto_box_open_easy(m.data(), c.data(), c.size(), nonce,
                                       g_pk, g_sk);
        (void)r1;

        int r2 = crypto_box_open_easy(nullptr, c.data(), c.size(), nonce,
                                       g_pk, g_sk);
        (void)r2;

        std::vector<unsigned char> inplace = c;
        int r3 = crypto_box_open_easy(inplace.data(), inplace.data(),
                                       inplace.size(), nonce, g_pk, g_sk);
        (void)r3;
      }
    }
  }

  {
    std::vector<uint8_t> nonce_bytes =
        stream.ConsumeBytes<uint8_t>(crypto_box_NONCEBYTES);
    if (nonce_bytes.size() == static_cast<size_t>(crypto_box_NONCEBYTES)) {
      unsigned char nonce[crypto_box_NONCEBYTES];
      memcpy(nonce, nonce_bytes.data(), sizeof(nonce));

      size_t msg_len = stream.ConsumeIntegralInRange<size_t>(0, 2048);
      std::vector<uint8_t> msg = stream.ConsumeBytes<uint8_t>(msg_len);

      std::vector<uint8_t> c(msg.size() + crypto_box_MACBYTES);
      if (crypto_box_easy(c.data(), msg.empty() ? nullptr : msg.data(),
                           msg.size(), nonce, g_pk, g_sk) == 0) {

        {
          std::vector<unsigned char> m(c.size());
          int ret = crypto_box_open_easy(m.data(), c.data(), c.size(), nonce,
                                          g_pk, g_sk);
          (void)ret;
        }

        {
          int ret = crypto_box_open_easy(nullptr, c.data(), c.size(), nonce,
                                          g_pk, g_sk);
          (void)ret;
        }

        {
          std::vector<unsigned char> inplace = c;
          int ret = crypto_box_open_easy(inplace.data(), inplace.data(),
                                          inplace.size(), nonce, g_pk, g_sk);
          (void)ret;
        }

        if (!c.empty()) {
          std::vector<uint8_t> bad = c;
          size_t pos = size % bad.size();
          bad[pos] ^= 0xFF;
          std::vector<unsigned char> m(bad.size());
          int ret = crypto_box_open_easy(m.data(), bad.data(), bad.size(),
                                          nonce, g_pk, g_sk);
          (void)ret;
        }
      }
    }
  }

  {
    std::vector<uint8_t> nonce =
        stream.ConsumeBytes<uint8_t>(crypto_box_NONCEBYTES);
    std::vector<uint8_t> pk =
        stream.ConsumeBytes<uint8_t>(crypto_box_PUBLICKEYBYTES);
    std::vector<uint8_t> sk =
        stream.ConsumeBytes<uint8_t>(crypto_box_SECRETKEYBYTES);

    if (nonce.size() == static_cast<size_t>(crypto_box_NONCEBYTES) &&
        pk.size() == static_cast<size_t>(crypto_box_PUBLICKEYBYTES) &&
        sk.size() == static_cast<size_t>(crypto_box_SECRETKEYBYTES)) {

      bool use_small_order = stream.ConsumeBool();
      if (use_small_order) {
        size_t idx = stream.ConsumeIntegralInRange<size_t>(
            0, sizeof(kSmallOrderPk) / sizeof(kSmallOrderPk[0]) - 1);
        memcpy(pk.data(), kSmallOrderPk[idx], crypto_box_PUBLICKEYBYTES);
      }

      size_t clen_cap = stream.ConsumeIntegralInRange<size_t>(0, 256);
      std::vector<uint8_t> c = stream.ConsumeBytes<uint8_t>(clen_cap);

      bool verify_only = stream.ConsumeBool();

      unsigned char *m = nullptr;
      if (!c.empty() && !verify_only) {
        m = static_cast<unsigned char *>(malloc(c.size()));
        if (m == nullptr) {
          return 0;
        }
      }

      int ret = crypto_box_open_easy(m, c.data(), c.size(), nonce.data(),
                                      pk.data(), sk.data());
      (void)ret;

      free(m);
    }
  }

  return 0;
}