#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include "sodium.h"
#include "crypto_sign.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (sodium_init() < 0) {
        return 0;
    }

    unsigned char sig[crypto_sign_BYTES];
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];

    size_t needed = crypto_sign_BYTES + crypto_sign_PUBLICKEYBYTES;
    if (Size < needed) {
        return 0;
    }

    memcpy(sig, Data, crypto_sign_BYTES);
    memcpy(pk, Data + crypto_sign_BYTES, crypto_sign_PUBLICKEYBYTES);

    const unsigned char *m = Data + needed;
    unsigned long long mlen = Size - needed;

    crypto_sign_verify_detached(sig, m, mlen, pk);

    crypto_sign_verify_detached(sig, nullptr, 0, pk);

    unsigned char seed[crypto_sign_SEEDBYTES];
    if (Size >= crypto_sign_SEEDBYTES) {
        memcpy(seed, Data, crypto_sign_SEEDBYTES);
    } else {
        memset(seed, 0, crypto_sign_SEEDBYTES);
        memcpy(seed, Data, Size);
    }

    unsigned char gen_pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char gen_sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_seed_keypair(gen_pk, gen_sk, seed);

    unsigned char gen_sig[crypto_sign_BYTES];
    unsigned long long gen_siglen;
    crypto_sign_detached(gen_sig, &gen_siglen, m, mlen, gen_sk);

    crypto_sign_verify_detached(gen_sig, m, mlen, gen_pk);

    if (gen_siglen > 0) {
        gen_sig[0] ^= 0xFF;
        crypto_sign_verify_detached(gen_sig, m, mlen, gen_pk);
    }

    return 0;
}