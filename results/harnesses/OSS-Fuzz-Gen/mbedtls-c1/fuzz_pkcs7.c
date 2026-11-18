#include <stdint.h>
#include <stddef.h>

#include "mbedtls/pkcs7.h"
#include "common.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
#if defined(MBEDTLS_PKCS7_C)
    mbedtls_pkcs7 pkcs7;

    mbedtls_pkcs7_init(&pkcs7);

#if defined(MBEDTLS_USE_PSA_CRYPTO)
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        goto exit;
    }
#endif

    (void) mbedtls_pkcs7_parse_der(&pkcs7, Data, Size);

#if defined(MBEDTLS_USE_PSA_CRYPTO)
exit:
    mbedtls_psa_crypto_free();
#endif

    mbedtls_pkcs7_free(&pkcs7);
#else
    (void) Data;
    (void) Size;
#endif

    return 0;
}