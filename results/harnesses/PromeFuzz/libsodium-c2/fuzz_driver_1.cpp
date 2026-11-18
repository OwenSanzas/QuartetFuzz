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
#include <sodium.h>
#include <sodium/crypto_box.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (sodium_init() < 0) {
        return 0;
    }

    const size_t NONCE_LEN = crypto_box_NONCEBYTES;
    const size_t MAC_LEN = crypto_box_MACBYTES;

    if (Size < NONCE_LEN) {
        return 0;
    }

    unsigned char sender_pk[crypto_box_PUBLICKEYBYTES];
    unsigned char sender_sk[crypto_box_SECRETKEYBYTES];
    unsigned char recipient_pk[crypto_box_PUBLICKEYBYTES];
    unsigned char recipient_sk[crypto_box_SECRETKEYBYTES];

    if (Size >= NONCE_LEN + crypto_box_SEEDBYTES * 2) {
        unsigned char seed1[crypto_box_SEEDBYTES];
        unsigned char seed2[crypto_box_SEEDBYTES];
        memcpy(seed1, Data + NONCE_LEN, crypto_box_SEEDBYTES);
        memcpy(seed2, Data + NONCE_LEN + crypto_box_SEEDBYTES, crypto_box_SEEDBYTES);
        crypto_box_seed_keypair(sender_pk, sender_sk, seed1);
        crypto_box_seed_keypair(recipient_pk, recipient_sk, seed2);
    } else {
        crypto_box_keypair(sender_pk, sender_sk);
        crypto_box_keypair(recipient_pk, recipient_sk);
    }

    unsigned char nonce[crypto_box_NONCEBYTES];
    memcpy(nonce, Data, NONCE_LEN);

    size_t offset = NONCE_LEN;
    if (Size >= NONCE_LEN + crypto_box_SEEDBYTES * 2) {
        offset += crypto_box_SEEDBYTES * 2;
    }

    if (offset > Size) {
        offset = Size;
    }

    const uint8_t *cipher_data = Data + offset;
    size_t cipher_len = Size - offset;

    {
        std::vector<unsigned char> ciphertext(cipher_data, cipher_data + cipher_len);
        std::vector<unsigned char> message;
        if (cipher_len >= MAC_LEN) {
            message.resize(cipher_len - MAC_LEN);
        } else {
            message.resize(1);
        }

        int ret = crypto_box_open_easy(
            message.data(),
            ciphertext.data(),
            (unsigned long long)ciphertext.size(),
            nonce,
            sender_pk,
            recipient_sk
        );
        (void)ret;
    }

    if (cipher_len > 0) {
        size_t msg_len = cipher_len;
        std::vector<unsigned char> message_in(cipher_data, cipher_data + msg_len);
        std::vector<unsigned char> ciphertext_out(msg_len + MAC_LEN);

        int enc_ret = crypto_box_easy(
            ciphertext_out.data(),
            message_in.data(),
            (unsigned long long)message_in.size(),
            nonce,
            recipient_pk,
            sender_sk
        );

        if (enc_ret == 0) {
            std::vector<unsigned char> decrypted(msg_len);
            int dec_ret = crypto_box_open_easy(
                decrypted.data(),
                ciphertext_out.data(),
                (unsigned long long)ciphertext_out.size(),
                nonce,
                sender_pk,
                recipient_sk
            );
            (void)dec_ret;

            if (!ciphertext_out.empty()) {
                ciphertext_out[0] ^= 0xFF;
                int dec_ret2 = crypto_box_open_easy(
                    decrypted.data(),
                    ciphertext_out.data(),
                    (unsigned long long)ciphertext_out.size(),
                    nonce,
                    sender_pk,
                    recipient_sk
                );
                (void)dec_ret2;
            }
        }
    }

    {
        unsigned char dummy_m[1];
        unsigned char dummy_c[1] = {0};
        int ret = crypto_box_open_easy(
            dummy_m,
            dummy_c,
            0,
            nonce,
            sender_pk,
            recipient_sk
        );
        (void)ret;
    }

    return 0;
}