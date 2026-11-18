#include <stdint.h>
#include "mbedtls/x509_csr.h"
#include "common.h"

#if defined(MBEDTLS_X509_CSR_PARSE_C)

static int ext_cb_accept(void *p_ctx,
                          mbedtls_x509_csr const *csr,
                          mbedtls_x509_buf const *oid,
                          int critical,
                          const unsigned char *p,
                          const unsigned char *end)
{
    (void) p_ctx;
    (void) csr;
    (void) oid;
    (void) critical;
    (void) p;
    (void) end;
    return 0;
}

static int ext_cb_reject(void *p_ctx,
                          mbedtls_x509_csr const *csr,
                          mbedtls_x509_buf const *oid,
                          int critical,
                          const unsigned char *p,
                          const unsigned char *end)
{
    (void) p_ctx;
    (void) csr;
    (void) oid;
    (void) critical;
    (void) p;
    (void) end;
    return -1;
}
#endif

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
#ifdef MBEDTLS_X509_CSR_PARSE_C
    int ret;
    mbedtls_x509_csr csr;
    mbedtls_x509_csr csr2;
    unsigned char buf[4096];

    mbedtls_x509_csr_init(&csr);
    mbedtls_x509_csr_init(&csr2);
#if defined(MBEDTLS_USE_PSA_CRYPTO)
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        goto exit;
    }
#endif

    ret = mbedtls_x509_csr_parse(&csr, Data, Size);
#if !defined(MBEDTLS_X509_REMOVE_INFO)
    if (ret == 0) {
        ret = mbedtls_x509_csr_info((char *) buf, sizeof(buf) - 1, " ", &csr);
    }
#else
    ((void) ret);
    ((void) buf);
#endif

    if (Size > 0) {
        mbedtls_x509_csr_ext_cb_t cb = (Data[0] & 1) ? ext_cb_accept : ext_cb_reject;
        int ret2 = mbedtls_x509_csr_parse_der_with_ext_cb(&csr2, Data, Size, cb, NULL);
#if !defined(MBEDTLS_X509_REMOVE_INFO)
        if (ret2 == 0) {
            (void) mbedtls_x509_csr_info((char *) buf, sizeof(buf) - 1, " ", &csr2);
        }
#else
        (void) ret2;
#endif
    }

#if defined(MBEDTLS_USE_PSA_CRYPTO)
exit:
    mbedtls_psa_crypto_free();
#endif
    mbedtls_x509_csr_free(&csr);
    mbedtls_x509_csr_free(&csr2);
#else
    (void) Data;
    (void) Size;
#endif

    return 0;
}
