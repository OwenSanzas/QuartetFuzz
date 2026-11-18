#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/x509_crt.h"
#include "mbedtls/build_info.h"
#include "mbedtls/oid.h"

#if defined(MBEDTLS_USE_PSA_CRYPTO)
#include "psa/crypto.h"
#endif

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);

static int fuzz_ext_cb(void *p_ctx,
                        mbedtls_x509_crt const *crt,
                        mbedtls_x509_buf const *oid,
                        int critical,
                        const unsigned char *p,
                        const unsigned char *end)
{
    (void) crt;
    (void) oid;
    (void) critical;
    (void) p;
    (void) end;
    int accept = (int) (intptr_t) p_ctx;
    return accept ? 0 : -1;
}

static int fuzz_vrfy_cb(void *p_ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    (void) p_ctx;
    if (crt != NULL) {

        (void) crt->version;
        (void) mbedtls_x509_crt_get_ca_istrue(crt);
    }
    (void) depth;
    (void) flags;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
#if defined(MBEDTLS_X509_CRT_PARSE_C)
    if (Size < 1) {
        return 0;
    }

#if defined(MBEDTLS_USE_PSA_CRYPTO)
    if (psa_crypto_init() != PSA_SUCCESS) {
        return 0;
    }
#endif

    uint8_t control = Data[0];
    const uint8_t *cert_data = Data + 1;
    size_t cert_len = Size - 1;

    mbedtls_x509_crt chain;
    mbedtls_x509_crt_init(&chain);

    int ret = mbedtls_x509_crt_parse(&chain, cert_data, cert_len);

    if (ret <= 0) {

        if (ret == 0 || (ret > 0 && chain.version != 0)) {

            char info_buf[2048];
            (void) mbedtls_x509_crt_info(info_buf, sizeof(info_buf), "  ", &chain);

            unsigned int ku = (unsigned int) control << 4;
            (void) mbedtls_x509_crt_check_key_usage(&chain, ku);

            if (control & 0x20) {
                (void) mbedtls_x509_crt_check_extended_key_usage(
                    &chain, MBEDTLS_OID_SERVER_AUTH, MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH));
            } else {
                (void) mbedtls_x509_crt_check_extended_key_usage(
                    &chain, MBEDTLS_OID_CLIENT_AUTH, MBEDTLS_OID_SIZE(MBEDTLS_OID_CLIENT_AUTH));
            }

            const mbedtls_x509_crt_profile *profile;
            switch (control & 0x3) {
                case 0:
                    profile = &mbedtls_x509_crt_profile_default;
                    break;
                case 1:
                    profile = &mbedtls_x509_crt_profile_next;
                    break;
                case 2:
                    profile = &mbedtls_x509_crt_profile_suiteb;
                    break;
                default:
                    profile = &mbedtls_x509_crt_profile_none;
                    break;
            }
            const char *cn = (control & 0x4) ? "localhost" : NULL;
            int (*f_vrfy)(void *, mbedtls_x509_crt *, int, uint32_t *) =
                (control & 0x40) ? fuzz_vrfy_cb : NULL;

            uint32_t flags = 0;
            (void) mbedtls_x509_crt_verify_with_profile(&chain, &chain, NULL,
                                                        profile, cn, &flags,
                                                        f_vrfy, NULL);
        }
    }

    mbedtls_x509_crt_free(&chain);

    {
        mbedtls_x509_crt chain2;
        mbedtls_x509_crt_init(&chain2);
        int make_copy = (control & 0x8) ? 1 : 0;
        void *cb_ctx = (void *) (intptr_t) ((control & 0x10) ? 1 : 0);
        (void) mbedtls_x509_crt_parse_der_with_ext_cb(&chain2, cert_data, cert_len,
                                                      make_copy, fuzz_ext_cb, cb_ctx);
        mbedtls_x509_crt_free(&chain2);
    }

    {
        unsigned char *heap_buf = (unsigned char *) malloc(cert_len > 0 ? cert_len : 1);
        if (heap_buf != NULL) {
            if (cert_len > 0) {
                memcpy(heap_buf, cert_data, cert_len);
            }
            mbedtls_x509_crt chain3;
            mbedtls_x509_crt_init(&chain3);
            (void) mbedtls_x509_crt_parse_der_nocopy(&chain3, heap_buf, cert_len);
            mbedtls_x509_crt_free(&chain3);
            free(heap_buf);
        }
    }

#if defined(MBEDTLS_USE_PSA_CRYPTO)
    mbedtls_psa_crypto_free();
#endif

#else
    (void) Data;
    (void) Size;
#endif

    return 0;
}
