#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/pkcs7.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Data == NULL || Size == 0) {
        return 0;
    }

    unsigned char *buf = (unsigned char *) malloc(Size);
    if (buf == NULL) {
        return 0;
    }
    memcpy(buf, Data, Size);

    mbedtls_pkcs7 pkcs7;
    mbedtls_pkcs7_init(&pkcs7);

    int ret = mbedtls_pkcs7_parse_der(&pkcs7, buf, Size);
    (void) ret;

    mbedtls_pkcs7_free(&pkcs7);

    mbedtls_pkcs7_init(&pkcs7);
    ret = mbedtls_pkcs7_parse_der(&pkcs7, buf, Size);
    (void) ret;
    mbedtls_pkcs7_free(&pkcs7);

    if (Size > 1) {
        mbedtls_pkcs7_init(&pkcs7);
        ret = mbedtls_pkcs7_parse_der(&pkcs7, buf, Size - 1);
        (void) ret;
        mbedtls_pkcs7_free(&pkcs7);
    }

    mbedtls_pkcs7_init(&pkcs7);
    ret = mbedtls_pkcs7_parse_der(&pkcs7, buf, 0);
    (void) ret;
    mbedtls_pkcs7_free(&pkcs7);

    free(buf);

    return 0;
}