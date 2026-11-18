#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include "crypto_secretbox.h"
#include "sodium.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (sodium_init() < 0) {
        return 0;
    }

    const size_t keybytes = crypto_secretbox_KEYBYTES;
    const size_t noncebytes = crypto_secretbox_NONCEBYTES;
    const size_t macbytes = crypto_secretbox_MACBYTES;

    if (Size < keybytes + noncebytes) {
        return 0;
    }

    unsigned char key[crypto_secretbox_KEYBYTES];
    unsigned char nonce[crypto_secretbox_NONCEBYTES];

    memcpy(key, Data, keybytes);
    memcpy(nonce, Data + keybytes, noncebytes);

    size_t offset = keybytes + noncebytes;
    size_t remaining = Size - offset;

    std::vector<unsigned char> ciphertext(Data + offset, Data + offset + remaining);
    unsigned long long clen = ciphertext.size();

    if (clen >= macbytes) {
        std::vector<unsigned char> plaintext(clen - macbytes > 0 ? clen - macbytes : 1);
        int ret = crypto_secretbox_open_easy(
            plaintext.data(),
            ciphertext.data(),
            clen,
            nonce,
            key
        );
        (void)ret;
    } else {

        std::vector<unsigned char> plaintext(1);
        int ret = crypto_secretbox_open_easy(
            plaintext.data(),
            ciphertext.data(),
            clen,
            nonce,
            key
        );
        (void)ret;
    }

    if (remaining > 0) {
        size_t msg_len = remaining % 256;
        std::vector<unsigned char> message(msg_len, 0);
        if (msg_len > 0) {
            memcpy(message.data(), Data + offset, msg_len > remaining ? remaining : msg_len);
        }

        std::vector<unsigned char> encrypted(msg_len + macbytes);
        if (crypto_secretbox_easy(encrypted.data(), message.data(), msg_len, nonce, key) == 0) {
            std::vector<unsigned char> decrypted(msg_len > 0 ? msg_len : 1);
            int ret = crypto_secretbox_open_easy(
                decrypted.data(),
                encrypted.data(),
                encrypted.size(),
                nonce,
                key
            );
            (void)ret;

            if (!encrypted.empty()) {
                encrypted[0] ^= 0xFF;
                ret = crypto_secretbox_open_easy(
                    decrypted.data(),
                    encrypted.data(),
                    encrypted.size(),
                    nonce,
                    key
                );
                (void)ret;
            }
        }
    }

    return 0;
}