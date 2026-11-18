#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
extern "C" {
#include "crypto_auth.h"
#include "core.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (sodium_init() < 0) {
        return 0;
    }

    unsigned char h[crypto_auth_BYTES];
    unsigned char k[crypto_auth_KEYBYTES];

    memset(h, 0, sizeof(h));
    memset(k, 0, sizeof(k));

    size_t offset = 0;

    if (Size > 0) {
        size_t h_copy = (Size < sizeof(h)) ? Size : sizeof(h);
        memcpy(h, Data, h_copy);
        offset += h_copy;
    }

    if (Size > offset) {
        size_t remaining = Size - offset;
        size_t k_copy = (remaining < sizeof(k)) ? remaining : sizeof(k);
        memcpy(k, Data + offset, k_copy);
        offset += k_copy;
    }

    const unsigned char *message = nullptr;
    unsigned long long message_len = 0;

    if (Size > offset) {
        message = Data + offset;
        message_len = Size - offset;
    } else {

        message = h;
        message_len = 0;
    }

    int result = crypto_auth_verify(h, message, message_len, k);
    (void)result;

    if (Size > sizeof(h) + sizeof(k)) {
        unsigned char h2[crypto_auth_BYTES];
        unsigned char k2[crypto_auth_KEYBYTES];
        memset(h2, 0, sizeof(h2));
        memset(k2, 0, sizeof(k2));

        size_t mid = Size / 2;
        size_t h2_copy = (mid < sizeof(h2)) ? mid : sizeof(h2);
        memcpy(h2, Data, h2_copy);

        size_t k2_start = mid;
        size_t k2_available = Size - k2_start;
        size_t k2_copy = (k2_available < sizeof(k2)) ? k2_available : sizeof(k2);
        if (k2_copy > 0) {
            memcpy(k2, Data + k2_start, k2_copy);
        }

        const unsigned char *message2 = Data;
        unsigned long long message2_len = Size;

        int result2 = crypto_auth_verify(h2, message2, message2_len, k2);
        (void)result2;
    }

    return 0;
}