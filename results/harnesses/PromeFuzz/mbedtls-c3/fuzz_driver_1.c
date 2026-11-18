#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/x509_crt.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size == 0) {
        return 0;
    }

    mbedtls_x509_crt chain1;
    mbedtls_x509_crt_init(&chain1);

    (void) mbedtls_x509_crt_parse(&chain1, Data, Size);

    mbedtls_x509_crt_free(&chain1);

    unsigned char *buf = (unsigned char *) malloc(Size + 1);
    if (buf == NULL) {
        return 0;
    }
    memcpy(buf, Data, Size);
    buf[Size] = '\0';

    mbedtls_x509_crt chain2;
    mbedtls_x509_crt_init(&chain2);

    (void) mbedtls_x509_crt_parse(&chain2, buf, Size + 1);

    (void) mbedtls_x509_crt_parse(&chain2, buf, Size + 1);

    mbedtls_x509_crt_free(&chain2);

    free(buf);

    return 0;
}