#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "mbedtls/build_info.h"
#include "mbedtls/platform.h"
#if defined(MBEDTLS_HAVE_TIME)
#include "mbedtls/platform_time.h"
#endif
#include "mbedtls/pkcs7.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/md.h"
#include "test/certs.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#if defined(MBEDTLS_PLATFORM_TIME_ALT)

static mbedtls_time_t fuzz_real_time(mbedtls_time_t *t)
{
    mbedtls_time_t now = (mbedtls_time_t) time(NULL);
    if (t != NULL) {
        *t = now;
    }
    return now;
}
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
#if defined(MBEDTLS_PLATFORM_TIME_ALT)
    mbedtls_platform_set_time(fuzz_real_time);
#endif

    uint8_t selector = 0;
    const unsigned char *der = data;
    size_t derlen = size;

    if (size > 0) {
        selector = data[0];
        der = data + 1;
        derlen = size - 1;
    }

    mbedtls_pkcs7 pkcs7;
    mbedtls_pkcs7_init(&pkcs7);

    int ret = mbedtls_pkcs7_parse_der(&pkcs7, der, derlen);

    if (ret == MBEDTLS_PKCS7_SIGNED_DATA) {

        if (pkcs7.signed_data.no_of_certs > 0) {
            char infobuf[1024];
            (void) mbedtls_x509_crt_info(infobuf, sizeof(infobuf), " ",
                                         &pkcs7.signed_data.certs);
        }

        mbedtls_x509_crt cert;
        mbedtls_x509_crt_init(&cert);
        if (mbedtls_x509_crt_parse_der(&cert,
                                       mbedtls_test_srv_crt_rsa_sha256_der,
                                       mbedtls_test_srv_crt_rsa_sha256_der_len) == 0) {
            if (selector & 1u) {

                const mbedtls_md_info_t *md_info =
                    mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
                if (md_info != NULL) {
                    unsigned char hash[64];
                    if (mbedtls_md(md_info, der, derlen, hash) == 0) {
                        (void) mbedtls_pkcs7_signed_hash_verify(
                            &pkcs7, &cert, hash, mbedtls_md_get_size(md_info));
                    }
                }
            } else {

                (void) mbedtls_pkcs7_signed_data_verify(&pkcs7, &cert, der, derlen);
            }
        }
        mbedtls_x509_crt_free(&cert);
    }

    mbedtls_pkcs7_free(&pkcs7);

    return 0;
}
