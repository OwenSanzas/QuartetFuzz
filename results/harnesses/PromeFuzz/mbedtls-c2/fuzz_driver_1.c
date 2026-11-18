#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/x509_crl.h"

#if defined(MBEDTLS_USE_PSA_CRYPTO)
#include "psa/crypto.h"
#endif

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
#if defined(MBEDTLS_USE_PSA_CRYPTO)
    static int psa_initialized = 0;
    if (!psa_initialized) {
        psa_crypto_init();
        psa_initialized = 1;
    }
#endif

    if (Data == NULL || Size == 0) {
        return 0;
    }

    {
        mbedtls_x509_crl crl;
        mbedtls_x509_crl_init(&crl);

        unsigned char *buf = (unsigned char *) malloc(Size);
        if (buf != NULL) {
            memcpy(buf, Data, Size);
            int ret = mbedtls_x509_crl_parse(&crl, buf, Size);
            (void) ret;
            free(buf);
        }

        mbedtls_x509_crl_free(&crl);
    }

    {
        mbedtls_x509_crl crl2;
        mbedtls_x509_crl_init(&crl2);

        unsigned char *buf2 = (unsigned char *) malloc(Size + 1);
        if (buf2 != NULL) {
            memcpy(buf2, Data, Size);
            buf2[Size] = '\0';
            int ret = mbedtls_x509_crl_parse(&crl2, buf2, Size + 1);
            (void) ret;
            free(buf2);
        }

        mbedtls_x509_crl_free(&crl2);
    }

    {
        mbedtls_x509_crl crl3;
        mbedtls_x509_crl_init(&crl3);

        unsigned char *buf3 = (unsigned char *) malloc(Size + 1);
        if (buf3 != NULL) {
            memcpy(buf3, Data, Size);
            buf3[Size] = '\0';

            int ret1 = mbedtls_x509_crl_parse(&crl3, buf3, Size + 1);
            (void) ret1;

            int ret2 = mbedtls_x509_crl_parse(&crl3, buf3, Size + 1);
            (void) ret2;

            free(buf3);
        }

        mbedtls_x509_crl_free(&crl3);
    }

    {
        mbedtls_x509_crl crl4;
        mbedtls_x509_crl_init(&crl4);

        unsigned char empty_buf[1] = { 0 };
        int ret = mbedtls_x509_crl_parse(&crl4, empty_buf, 0);
        (void) ret;

        mbedtls_x509_crl_free(&crl4);
    }

    return 0;
}