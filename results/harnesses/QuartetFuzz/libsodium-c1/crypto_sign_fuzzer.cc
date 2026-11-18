#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sodium.h>

static size_t copy_avail(const uint8_t *data, size_t size, size_t *offset,
                          unsigned char *dst, size_t want) {
  size_t avail = (*offset < size) ? size - *offset : 0;
  size_t take = avail < want ? avail : want;
  if (take > 0) {
    memcpy(dst, data + *offset, take);
  }
  *offset += take;
  return take;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (sodium_init() < 0) {
    return 0;
  }

  if (size < 1) {
    return 0;
  }

  size_t offset = 0;
  uint8_t mode = data[offset++];

  unsigned char seed[crypto_sign_SEEDBYTES];
  memset(seed, 0, sizeof seed);
  copy_avail(data, size, &offset, seed, sizeof seed);

  unsigned char pk[crypto_sign_PUBLICKEYBYTES];
  unsigned char sk[crypto_sign_SECRETKEYBYTES];
  crypto_sign_seed_keypair(pk, sk, seed);

  unsigned char raw_sig[crypto_sign_BYTES];
  memset(raw_sig, 0, sizeof raw_sig);
  copy_avail(data, size, &offset, raw_sig, sizeof raw_sig);

  unsigned char raw_pk[crypto_sign_PUBLICKEYBYTES];
  memcpy(raw_pk, pk, sizeof raw_pk);
  copy_avail(data, size, &offset, raw_pk, sizeof raw_pk);

  const unsigned char *msg = data + offset;
  unsigned long long mlen = (offset < size) ? (unsigned long long)(size - offset) : 0;

  const unsigned char *use_pk = (mode & 0x1) ? raw_pk : pk;
  int rc1 = crypto_sign_verify_detached(raw_sig, mlen > 0 ? msg : NULL, mlen, use_pk);
  (void)rc1;

  unsigned char sig[crypto_sign_BYTES];
  unsigned long long siglen = 0;
  crypto_sign_detached(sig, &siglen, mlen > 0 ? msg : NULL, mlen, sk);

  if (mode & 0x2) {
    size_t idx = mode % crypto_sign_BYTES;
    sig[idx] ^= (unsigned char)(mode | 0x80);
  }

  int rc2 = crypto_sign_verify_detached(sig, mlen > 0 ? msg : NULL, mlen, pk);
  if (rc2 == 0) {

    unsigned char checksum = 0;
    for (unsigned long long i = 0; i < mlen; i++) {
      checksum ^= msg[i];
    }
    (void)checksum;
  }

  crypto_sign_state st_sign;
  crypto_sign_init(&st_sign);
  if (mlen > 0 && (mode & 0x4)) {
    unsigned long long half = mlen / 2;
    crypto_sign_update(&st_sign, msg, half);
    crypto_sign_update(&st_sign, msg + half, mlen - half);
  } else if (mlen > 0) {
    crypto_sign_update(&st_sign, msg, mlen);
  }
  unsigned char sig2[crypto_sign_BYTES];
  crypto_sign_final_create(&st_sign, sig2, NULL, sk);

  crypto_sign_state st_verify;
  crypto_sign_init(&st_verify);
  if (mlen > 0 && (mode & 0x4)) {
    unsigned long long half = mlen / 2;
    crypto_sign_update(&st_verify, msg, half);
    crypto_sign_update(&st_verify, msg + half, mlen - half);
  } else if (mlen > 0) {
    crypto_sign_update(&st_verify, msg, mlen);
  }
  int rc3 = crypto_sign_final_verify(&st_verify, sig2, pk);
  (void)rc3;

  size_t smlen = (size_t)crypto_sign_BYTES + (size_t)mlen;
  unsigned char *sm = (unsigned char *)malloc(smlen > 0 ? smlen : 1);
  if (sm != NULL) {
    memcpy(sm, sig, crypto_sign_BYTES);
    if (mlen > 0) {
      memcpy(sm + crypto_sign_BYTES, msg, mlen);
    }
    unsigned char *m_out = (unsigned char *)malloc(mlen > 0 ? (size_t)mlen : 1);
    if (m_out != NULL) {
      unsigned long long mlen_out = 0;
      int rc4 = crypto_sign_open(m_out, &mlen_out, sm, smlen, pk);
      if (rc4 == 0) {
        unsigned char checksum2 = 0;
        for (unsigned long long i = 0; i < mlen_out; i++) {
          checksum2 ^= m_out[i];
        }
        (void)checksum2;
      }
      free(m_out);
    }
    free(sm);
  }

  return 0;
}
