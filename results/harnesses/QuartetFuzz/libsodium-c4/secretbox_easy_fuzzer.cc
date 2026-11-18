#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sodium.h>

#define HEADER_LEN (crypto_secretbox_KEYBYTES + crypto_secretbox_NONCEBYTES + 1)
#define MAX_MSG_LEN (1u << 16)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < HEADER_LEN) {
    return 0;
  }

  int initialized = sodium_init();
  assert(initialized >= 0);

  unsigned char key[crypto_secretbox_KEYBYTES];
  unsigned char nonce[crypto_secretbox_NONCEBYTES];
  memcpy(key, data, crypto_secretbox_KEYBYTES);
  memcpy(nonce, data + crypto_secretbox_KEYBYTES, crypto_secretbox_NONCEBYTES);
  uint8_t ctrl = data[crypto_secretbox_KEYBYTES + crypto_secretbox_NONCEBYTES];

  const uint8_t *msg = data + HEADER_LEN;
  size_t mlen = size - HEADER_LEN;
  if (mlen > MAX_MSG_LEN) {
    mlen = MAX_MSG_LEN;
  }

  {
    unsigned long long short_clen = ctrl % crypto_secretbox_MACBYTES;
    unsigned char dummy_out[crypto_secretbox_MACBYTES];
    int short_ret = crypto_secretbox_open_easy(dummy_out, msg, short_clen,
                                                nonce, key);
    (void)short_ret;
  }

  size_t clen = mlen + crypto_secretbox_MACBYTES;
  unsigned char *c = (unsigned char *)malloc(clen);
  if (c == NULL) {
    return 0;
  }

  crypto_secretbox_easy(c, msg, mlen, nonce, key);

  if ((ctrl & 0x20) && mlen > 0) {
    unsigned char *buf = (unsigned char *)malloc(clen);
    if (buf != NULL) {
      memcpy(buf + crypto_secretbox_MACBYTES, msg, mlen);
      crypto_secretbox_easy(buf, buf + crypto_secretbox_MACBYTES, mlen, nonce,
                             key);
      free(buf);
    }
  }

  int tampered = 0;
  if (ctrl & 0x1) {

    c[0] ^= 0xFF;
    tampered = 1;
  }
  if ((ctrl & 0x2) && mlen > 0) {

    size_t idx = crypto_secretbox_MACBYTES + (ctrl % mlen);
    c[idx] ^= 0xFF;
    tampered = 1;
  }

  unsigned char *out = (unsigned char *)malloc(mlen + 1);
  if (out == NULL) {
    free(c);
    return 0;
  }
  int ret = crypto_secretbox_open_easy(out, c, clen, nonce, key);
  if (ret == 0) {

    volatile unsigned char acc = 0;
    for (size_t i = 0; i < mlen; i++) {
      acc ^= out[i];
    }
    (void)acc;
    if (!tampered) {
      assert(memcmp(out, msg, mlen) == 0);
    }
  }
  free(out);

  if (ctrl & 0x40) {
    int ret_v = crypto_secretbox_open_easy(NULL, c, clen, nonce, key);
    (void)ret_v;
  }

  if (ctrl & 0x4) {
    unsigned char *b = (unsigned char *)malloc(clen);
    if (b != NULL) {
      memcpy(b, c, clen);
      int ret2 = crypto_secretbox_open_easy(b, b, clen, nonce, key);
      if (ret2 == 0) {
        volatile unsigned char acc2 = 0;
        for (size_t i = 0; i < mlen; i++) {
          acc2 ^= b[i];
        }
        (void)acc2;
      }
      free(b);
    }
  }

  unsigned char mac[crypto_secretbox_MACBYTES];
  unsigned char *cbody = (unsigned char *)malloc(mlen > 0 ? mlen : 1);
  if (cbody != NULL) {
    crypto_secretbox_detached(cbody, mac, msg, mlen, nonce, key);
    if (ctrl & 0x8) {
      mac[0] ^= 0xFF;
    }
    unsigned char *out2 = (unsigned char *)malloc(mlen + 1);
    if (out2 != NULL) {
      int ret3 = crypto_secretbox_open_detached(out2, cbody, mac, mlen, nonce, key);
      if (ret3 == 0) {
        volatile unsigned char acc3 = 0;
        for (size_t i = 0; i < mlen; i++) {
          acc3 ^= out2[i];
        }
        (void)acc3;
        if (!(ctrl & 0x8)) {
          assert(memcmp(out2, msg, mlen) == 0);
        }
      }
      free(out2);
    }
    free(cbody);
  }

  if (ctrl & 0x10) {
    unsigned char rk[crypto_secretbox_KEYBYTES];
    crypto_secretbox_keygen(rk);
    unsigned char *out4 = (unsigned char *)malloc(mlen + 1);
    if (out4 != NULL) {

      int ret4 = crypto_secretbox_open_easy(out4, c, clen, nonce, rk);
      (void)ret4;
      free(out4);
    }
  }

  free(c);
  return 0;
}
