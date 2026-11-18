#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sodium.h>

#include "fake_random.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1) {
    return 0;
  }

  setup_fake_random(data, size);

  uint8_t selector = data[0];
  const unsigned char *msg = data + 1;
  size_t mlen = size - 1;

  unsigned char pk_a[crypto_box_PUBLICKEYBYTES], sk_a[crypto_box_SECRETKEYBYTES];
  unsigned char pk_b[crypto_box_PUBLICKEYBYTES], sk_b[crypto_box_SECRETKEYBYTES];
  crypto_box_keypair(pk_a, sk_a);
  crypto_box_keypair(pk_b, sk_b);

  unsigned char nonce[crypto_box_NONCEBYTES];
  randombytes_buf(nonce, sizeof nonce);

  size_t clen = mlen + crypto_box_MACBYTES;
  unsigned char *c = (unsigned char *)malloc(clen);
  unsigned char *m_out = (unsigned char *)malloc(mlen > 0 ? mlen : 1);

  bool corrupt_cipher   = (selector & 0x01) != 0;
  bool mismatched_keys  = (selector & 0x08) != 0;
  bool truncate_clen    = (selector & 0x10) != 0;

  if (c != NULL && m_out != NULL) {

    crypto_box_easy(c, msg, mlen, nonce, pk_b, sk_a);

    if (corrupt_cipher) {
      c[selector % clen] ^= (unsigned char)(selector | 0x55);
    }

    const unsigned char *open_pk = mismatched_keys ? pk_b : pk_a;
    const unsigned char *open_sk = mismatched_keys ? sk_a : sk_b;

    unsigned long long open_clen =
        truncate_clen ? (unsigned long long)(selector % crypto_box_MACBYTES)
                      : (unsigned long long)clen;

    int ret = crypto_box_open_easy(m_out, c, open_clen, nonce, open_pk, open_sk);

    if (!corrupt_cipher && !mismatched_keys && !truncate_clen) {

      assert(ret == 0);
      assert(mlen == 0 || memcmp(m_out, msg, mlen) == 0);
    } else if (ret == 0) {

      volatile unsigned char acc = 0;
      for (size_t i = 0; i < mlen; i++) {
        acc ^= m_out[i];
      }
      (void)acc;
    }
  }
  free(c);
  free(m_out);

  switch ((selector >> 1) % 4) {
    case 0: {

      unsigned char *c2 = (unsigned char *)malloc(mlen > 0 ? mlen : 1);
      unsigned char *m2 = (unsigned char *)malloc(mlen > 0 ? mlen : 1);
      unsigned char mac[crypto_box_MACBYTES];
      if (c2 != NULL && m2 != NULL) {
        crypto_box_detached(c2, mac, msg, mlen, nonce, pk_b, sk_a);
        if ((selector & 0x4) != 0) {
          mac[0] ^= 0xFF;
        }
        int r2 = crypto_box_open_detached(m2, c2, mac, mlen, nonce, pk_a, sk_b);
        if (r2 == 0) {
          if ((selector & 0x4) == 0) {
            assert(mlen == 0 || memcmp(m2, msg, mlen) == 0);
          }
          volatile unsigned char acc2 = 0;
          for (size_t i = 0; i < mlen; i++) {
            acc2 ^= m2[i];
          }
          (void)acc2;
        }
      }
      free(c2);
      free(m2);
      break;
    }
    case 1: {

      unsigned char k1[crypto_box_BEFORENMBYTES];
      unsigned char k2[crypto_box_BEFORENMBYTES];
      if (crypto_box_beforenm(k1, pk_b, sk_a) == 0 &&
          crypto_box_beforenm(k2, pk_a, sk_b) == 0) {
        unsigned char *c3 = (unsigned char *)malloc(clen);
        unsigned char *m3 = (unsigned char *)malloc(mlen > 0 ? mlen : 1);
        if (c3 != NULL && m3 != NULL) {
          crypto_box_easy_afternm(c3, msg, mlen, nonce, k1);
          int r3 = crypto_box_open_easy_afternm(m3, c3, clen, nonce, k2);
          if (r3 == 0) {
            assert(mlen == 0 || memcmp(m3, msg, mlen) == 0);
            volatile unsigned char acc3 = 0;
            for (size_t i = 0; i < mlen; i++) {
              acc3 ^= m3[i];
            }
            (void)acc3;
          }
        }
        free(c3);
        free(m3);
      }
      sodium_memzero(k1, sizeof k1);
      sodium_memzero(k2, sizeof k2);
      break;
    }
    case 2: {

      size_t seal_clen = mlen + crypto_box_SEALBYTES;
      unsigned char *sc = (unsigned char *)malloc(seal_clen);
      unsigned char *sm = (unsigned char *)malloc(mlen > 0 ? mlen : 1);
      if (sc != NULL && sm != NULL) {
        crypto_box_seal(sc, msg, mlen, pk_a);
        int r4 = crypto_box_seal_open(sm, sc, seal_clen, pk_a, sk_a);
        if (r4 == 0) {
          assert(mlen == 0 || memcmp(sm, msg, mlen) == 0);
          volatile unsigned char acc4 = 0;
          for (size_t i = 0; i < mlen; i++) {
            acc4 ^= sm[i];
          }
          (void)acc4;
        }
      }
      free(sc);
      free(sm);
      break;
    }
    default:

      break;
  }

  sodium_memzero(sk_a, sizeof sk_a);
  sodium_memzero(sk_b, sizeof sk_b);

  return 0;
}
