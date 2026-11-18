#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "mbedtls/x509_crl.h"
#include "common.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
#if defined(MBEDTLS_X509_CRL_PARSE_C)
    int ret1, ret2;
    mbedtls_x509_crl crl;
    char infobuf[4096];

    mbedtls_x509_crl_init(&crl);

#if defined(MBEDTLS_USE_PSA_CRYPTO)
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        goto exit;
    }
#endif

    ret1 = mbedtls_x509_crl_parse(&crl, Data, Size);

    ret2 = mbedtls_x509_crl_parse(&crl, Data, Size);

#if !defined(MBEDTLS_X509_REMOVE_INFO)
    if (ret1 == 0 || ret2 == 0) {

        (void) mbedtls_x509_crl_info(infobuf, sizeof(infobuf), "", &crl);
    }
#else
    ((void) ret1);
    ((void) ret2);
#endif

#if defined(MBEDTLS_USE_PSA_CRYPTO)
exit:
    mbedtls_psa_crypto_free();
#endif

    mbedtls_x509_crl_free(&crl);
#else
    (void) Data;
    (void) Size;
#endif

    return 0;
}
