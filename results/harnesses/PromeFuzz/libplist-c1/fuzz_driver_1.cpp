#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <stdint.h>
#include <stddef.h>
#include <plist/plist.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        plist_t plist = nullptr;
        plist_err_t ret = plist_from_bin(nullptr, 0, &plist);
        (void)ret;
        return 0;
    }

    plist_t plist = nullptr;
    plist_err_t ret = plist_from_bin(reinterpret_cast<const char *>(Data), static_cast<uint32_t>(Size), &plist);

    if (ret == PLIST_ERR_SUCCESS && plist != nullptr) {
        plist_free(plist);
    }
    plist = nullptr;

    plist_err_t ret2 = plist_from_bin(reinterpret_cast<const char *>(Data), static_cast<uint32_t>(Size), nullptr);
    (void)ret2;

    plist_t plist2 = nullptr;
    plist_err_t ret3 = plist_from_bin(reinterpret_cast<const char *>(Data), 0, &plist2);
    if (ret3 == PLIST_ERR_SUCCESS && plist2 != nullptr) {
        plist_free(plist2);
    }

    return 0;
}