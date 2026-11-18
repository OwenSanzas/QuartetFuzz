#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/x509_csr.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Data == NULL || Size == 0) {
        return 0;
    }

    unsigned char *buf = (unsigned char *)malloc(Size + 1);
    if (buf == NULL) {
        return 0;
    }
    memcpy(buf, Data, Size);
    buf[Size] = '\0';

    mbedtls_x509_csr csr;

    mbedtls_x509_csr_init(&csr);
    (void) mbedtls_x509_csr_parse(&csr, buf, Size + 1);
    mbedtls_x509_csr_free(&csr);

    mbedtls_x509_csr_init(&csr);
    (void) mbedtls_x509_csr_parse(&csr, buf, Size);
    mbedtls_x509_csr_free(&csr);

    {
        FILE *fp = fopen("./dummy_file", "wb");
        if (fp != NULL) {
            fwrite(Data, 1, Size, fp);
            fclose(fp);
        }
    }

    if (Size > 1) {
        size_t truncated_size = Size / 2;
        unsigned char *truncated_buf = (unsigned char *)malloc(truncated_size + 1);
        if (truncated_buf != NULL) {
            memcpy(truncated_buf, Data, truncated_size);
            truncated_buf[truncated_size] = '\0';

            mbedtls_x509_csr_init(&csr);
            (void) mbedtls_x509_csr_parse(&csr, truncated_buf, truncated_size + 1);
            mbedtls_x509_csr_free(&csr);

            free(truncated_buf);
        }
    }

    free(buf);

    return 0;
}