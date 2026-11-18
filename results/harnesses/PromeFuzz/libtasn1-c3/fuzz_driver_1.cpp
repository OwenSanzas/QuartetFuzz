#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <libtasn1.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    const unsigned char *der = reinterpret_cast<const unsigned char *>(Data);
    int der_len = static_cast<int>(Size);

    int str_sizes[] = {0, 1, 8, 16, 32, 64, 128, 256, 1024};

    for (size_t i = 0; i < sizeof(str_sizes) / sizeof(str_sizes[0]); ++i) {
        int str_size = str_sizes[i];
        char *str = nullptr;

        if (str_size > 0) {
            str = static_cast<char *>(malloc(str_size));
            if (!str) {
                continue;
            }
            memset(str, 0, str_size);
        }

        int ret_len = 0;
        int result = asn1_get_object_id_der(der, der_len, &ret_len, str, str_size);

        (void)result;
        (void)ret_len;

        if (str) {
            free(str);
        }
    }

    for (size_t partial_len = 1; partial_len <= Size && partial_len <= 32; ++partial_len) {
        char str_buf[256];
        memset(str_buf, 0, sizeof(str_buf));
        int ret_len = 0;

        int result = asn1_get_object_id_der(der, static_cast<int>(partial_len),
                                              &ret_len, str_buf, sizeof(str_buf));
        (void)result;
        (void)ret_len;
    }

    {
        char str_buf[256];
        memset(str_buf, 0, sizeof(str_buf));
        int ret_len = 0;
        int result = asn1_get_object_id_der(der, -1, &ret_len, str_buf, sizeof(str_buf));
        (void)result;
        (void)ret_len;
    }

    {
        char str_buf[256];
        memset(str_buf, 0, sizeof(str_buf));
        int ret_len = 0;
        int result = asn1_get_object_id_der(der, 0, &ret_len, str_buf, sizeof(str_buf));
        (void)result;
        (void)ret_len;
    }

    return 0;
}