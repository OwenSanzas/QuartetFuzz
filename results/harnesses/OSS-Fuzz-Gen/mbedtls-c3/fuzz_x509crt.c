#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "mbedtls/x509_crt.h"
#include "test/certs.h"
#include "common.h"

#if defined(MBEDTLS_X509_CRT_PARSE_C)
static const unsigned char fuzz_pem_header[] = "-----BEGIN CERTIFICATE-----\n";
static const unsigned char fuzz_pem_footer[] = "\n-----END CERTIFICATE-----\n";

static const char *const fuzz_dek_info_lines[] = {
    "DEK-Info: DES-EDE3-CBC,0123456789ABCDEF\n\n",
    "DEK-Info: DES-CBC,0123456789ABCDEF\n\n",
    "DEK-Info: AES-128-CBC,0123456789ABCDEF0123456789ABCDEF\n\n",
    "DEK-Info: AES-192-CBC,0123456789ABCDEF0123456789ABCDEF\n\n",
    "DEK-Info: AES-256-CBC,0123456789ABCDEF0123456789ABCDEF\n\n",
};
static const char fuzz_proc_type_line[] = "Proc-Type: 4,ENCRYPTED\n";
#endif

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
#ifdef MBEDTLS_X509_CRT_PARSE_C
    int ret;
    mbedtls_x509_crt chain;
    unsigned char info_buf[4096];

    mbedtls_x509_crt_init(&chain);

#if defined(MBEDTLS_USE_PSA_CRYPTO)
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        goto exit;
    }
#endif

    {
        struct {
            const unsigned char *der;
            size_t len;
        } seed_certs[] = {
            { mbedtls_test_srv_crt_ec_der, mbedtls_test_srv_crt_ec_der_len },
            { mbedtls_test_ca_crt_ec_der,  mbedtls_test_ca_crt_ec_der_len },
        };
        unsigned idx = (Size > 0) ?
                       (unsigned) (Data[0] % (sizeof(seed_certs) / sizeof(seed_certs[0]))) : 0;
        (void) mbedtls_x509_crt_parse(&chain, seed_certs[idx].der, seed_certs[idx].len);
    }

    ret = mbedtls_x509_crt_parse(&chain, Data, Size);

    unsigned char *nul_buf = (unsigned char *) malloc(Size + 1);
    if (nul_buf != NULL) {
        if (Size > 0) {
            memcpy(nul_buf, Data, Size);
        }
        nul_buf[Size] = '\0';
        int ret2 = mbedtls_x509_crt_parse(&chain, nul_buf, Size + 1);
        if (ret != 0) {
            ret = ret2;
        }
        free(nul_buf);
    }

    {
        size_t pem_len = sizeof(fuzz_pem_header) - 1 + Size +
                          sizeof(fuzz_pem_footer) - 1 + 1;
        unsigned char *pem_buf = (unsigned char *) malloc(pem_len);
        if (pem_buf != NULL) {
            size_t off = 0;
            memcpy(pem_buf + off, fuzz_pem_header, sizeof(fuzz_pem_header) - 1);
            off += sizeof(fuzz_pem_header) - 1;
            if (Size > 0) {
                memcpy(pem_buf + off, Data, Size);
                off += Size;
            }
            memcpy(pem_buf + off, fuzz_pem_footer, sizeof(fuzz_pem_footer) - 1);
            off += sizeof(fuzz_pem_footer) - 1;
            pem_buf[off] = '\0';
            off += 1;
            int ret3 = mbedtls_x509_crt_parse(&chain, pem_buf, off);
            if (ret != 0) {
                ret = ret3;
            }
            free(pem_buf);
        }
    }

    {
        size_t half = Size / 2;
        size_t second_half = Size - half;
        size_t multi_len = 2 * (sizeof(fuzz_pem_header) - 1 + sizeof(fuzz_pem_footer) - 1)
                           + Size + 1;
        unsigned char *multi_buf = (unsigned char *) malloc(multi_len);
        if (multi_buf != NULL) {
            size_t off = 0;

            memcpy(multi_buf + off, fuzz_pem_header, sizeof(fuzz_pem_header) - 1);
            off += sizeof(fuzz_pem_header) - 1;
            if (half > 0) {
                memcpy(multi_buf + off, Data, half);
                off += half;
            }
            memcpy(multi_buf + off, fuzz_pem_footer, sizeof(fuzz_pem_footer) - 1);
            off += sizeof(fuzz_pem_footer) - 1;

            memcpy(multi_buf + off, fuzz_pem_header, sizeof(fuzz_pem_header) - 1);
            off += sizeof(fuzz_pem_header) - 1;
            if (second_half > 0) {
                memcpy(multi_buf + off, Data + half, second_half);
                off += second_half;
            }
            memcpy(multi_buf + off, fuzz_pem_footer, sizeof(fuzz_pem_footer) - 1);
            off += sizeof(fuzz_pem_footer) - 1;

            multi_buf[off] = '\0';
            off += 1;

            int ret4 = mbedtls_x509_crt_parse(&chain, multi_buf, off);
            if (ret != 0) {
                ret = ret4;
            }
            free(multi_buf);
        }
    }

    {
        unsigned dek_idx = (Size > 0) ?
            (unsigned) (Data[0] % (sizeof(fuzz_dek_info_lines) / sizeof(fuzz_dek_info_lines[0]))) : 0;
        const char *dek_line = fuzz_dek_info_lines[dek_idx];
        size_t dek_line_len = strlen(dek_line);
        size_t proc_type_len = sizeof(fuzz_proc_type_line) - 1;

        size_t enc_len = sizeof(fuzz_pem_header) - 1 + proc_type_len + dek_line_len +
                          Size + sizeof(fuzz_pem_footer) - 1 + 1;
        unsigned char *enc_buf = (unsigned char *) malloc(enc_len);
        if (enc_buf != NULL) {
            size_t off = 0;

            memcpy(enc_buf + off, fuzz_pem_header, sizeof(fuzz_pem_header) - 1);
            off += sizeof(fuzz_pem_header) - 1;

            memcpy(enc_buf + off, fuzz_proc_type_line, proc_type_len);
            off += proc_type_len;

            memcpy(enc_buf + off, dek_line, dek_line_len);
            off += dek_line_len;

            if (Size > 0) {
                memcpy(enc_buf + off, Data, Size);
                off += Size;
            }

            memcpy(enc_buf + off, fuzz_pem_footer, sizeof(fuzz_pem_footer) - 1);
            off += sizeof(fuzz_pem_footer) - 1;

            enc_buf[off] = '\0';
            off += 1;

            int ret5 = mbedtls_x509_crt_parse(&chain, enc_buf, off);
            if (ret != 0) {
                ret = ret5;
            }
            free(enc_buf);
        }
    }

#if !defined(MBEDTLS_X509_REMOVE_INFO)

    for (mbedtls_x509_crt *cur = &chain; cur != NULL; cur = cur->next) {
        (void) mbedtls_x509_crt_info((char *) info_buf, sizeof(info_buf) - 1,
                                      " ", cur);
    }
#else
    ((void) ret);
    ((void) info_buf);
#endif

#if defined(MBEDTLS_USE_PSA_CRYPTO)
exit:
    mbedtls_psa_crypto_free();
#endif

    mbedtls_x509_crt_free(&chain);
#else
    (void) Data;
    (void) Size;
#endif

    return 0;
}