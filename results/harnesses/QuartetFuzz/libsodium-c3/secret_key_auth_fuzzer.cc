#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sodium.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  int initialized = sodium_init();
  assert(initialized >= 0);

  const size_t kHeader = crypto_auth_KEYBYTES + crypto_auth_BYTES + 1;
  if (size < kHeader) {
    return 0;
  }

  unsigned char k[crypto_auth_KEYBYTES];
  unsigned char h_in[crypto_auth_BYTES];
  memcpy(k, data, crypto_auth_KEYBYTES);
  memcpy(h_in, data + crypto_auth_KEYBYTES, crypto_auth_BYTES);
  uint8_t selector = data[crypto_auth_KEYBYTES + crypto_auth_BYTES];

  const unsigned char *msg = data + kHeader;
  size_t msg_len = size - kHeader;

  unsigned char tag[crypto_auth_BYTES];
  int rc = crypto_auth(tag, msg, (unsigned long long) msg_len, k);
  assert(rc == 0);

  int verify_genuine = crypto_auth_verify(
      tag, msg, (unsigned long long) msg_len, k);
  assert(verify_genuine == 0);

  int verify_forged = crypto_auth_verify(
      h_in, msg, (unsigned long long) msg_len, k);

  unsigned char result_buf[crypto_auth_BYTES];
  if (verify_forged == 0) {
    memcpy(result_buf, h_in, sizeof(result_buf));
  } else {
    memcpy(result_buf, tag, sizeof(result_buf));
    result_buf[0] ^= (unsigned char) selector;
  }

  if (selector & 1) {
    crypto_auth_hmacsha512256_state st;
    crypto_auth_hmacsha512256_init(&st, k, crypto_auth_KEYBYTES);

    size_t split = msg_len ? (msg_len * (selector >> 1) / 256) : 0;
    if (split > msg_len) split = msg_len;

    crypto_auth_hmacsha512256_update(&st, msg, split);
    crypto_auth_hmacsha512256_update(&st, msg + split, msg_len - split);

    unsigned char streamed_tag[crypto_auth_hmacsha512256_BYTES];
    crypto_auth_hmacsha512256_final(&st, streamed_tag);

    int verify_streamed = crypto_auth_verify(
        streamed_tag, msg, (unsigned long long) msg_len, k);
    assert(verify_streamed == 0);
    assert(memcmp(streamed_tag, tag, crypto_auth_BYTES) == 0);
  }

  unsigned char k2[crypto_auth_KEYBYTES];
  crypto_auth_keygen(k2);
  int verify_wrong_key = crypto_auth_verify(
      tag, msg, (unsigned long long) msg_len, k2);
  (void) verify_wrong_key;

  unsigned char sink = 0;
  for (size_t i = 0; i < sizeof(result_buf); i++) {
    sink ^= result_buf[i];
  }
  (void) sink;

  return 0;
}
