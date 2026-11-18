#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>

#include "util.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    std::string input(reinterpret_cast<const char *>(Data), Size);

    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));
    int outlen = sizeof(ss);

    int ret = evutil_parse_sockaddr_port(input.c_str(),
                                          reinterpret_cast<struct sockaddr *>(&ss),
                                          &outlen);
    (void)ret;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    int outlen2 = sizeof(sin);
    ret = evutil_parse_sockaddr_port(input.c_str(),
                                      reinterpret_cast<struct sockaddr *>(&sin),
                                      &outlen2);
    (void)ret;

    struct sockaddr_in6 sin6;
    memset(&sin6, 0, sizeof(sin6));
    int outlen3 = sizeof(sin6);
    ret = evutil_parse_sockaddr_port(input.c_str(),
                                      reinterpret_cast<struct sockaddr *>(&sin6),
                                      &outlen3);
    (void)ret;

    if (Size > 1) {
        std::string partial(reinterpret_cast<const char *>(Data), Size - 1);
        struct sockaddr_storage ss2;
        memset(&ss2, 0, sizeof(ss2));
        int outlen4 = sizeof(ss2);
        ret = evutil_parse_sockaddr_port(partial.c_str(),
                                          reinterpret_cast<struct sockaddr *>(&ss2),
                                          &outlen4);
        (void)ret;
    }

    return 0;
}