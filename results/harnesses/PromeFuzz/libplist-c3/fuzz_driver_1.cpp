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
#include <plist/plist.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    char *buffer = (char *)malloc(Size + 1);
    if (!buffer) {
        return 0;
    }
    memcpy(buffer, Data, Size);
    buffer[Size] = '\0';

    plist_t plist = nullptr;

    plist_err_t err = plist_from_openstep(buffer, (uint32_t)Size, &plist);
    if (err == PLIST_ERR_SUCCESS && plist != nullptr) {
        plist_free(plist);
        plist = nullptr;
    }

    uint32_t strlen_based = (uint32_t)strlen(buffer);
    if (strlen_based != (uint32_t)Size) {
        plist_t plist2 = nullptr;
        plist_err_t err2 = plist_from_openstep(buffer, strlen_based, &plist2);
        if (err2 == PLIST_ERR_SUCCESS && plist2 != nullptr) {
            plist_free(plist2);
            plist2 = nullptr;
        }
    }

    plist_t plist3 = nullptr;
    plist_err_t err3 = plist_from_openstep(buffer, 0, &plist3);
    if (err3 == PLIST_ERR_SUCCESS && plist3 != nullptr) {
        plist_free(plist3);
        plist3 = nullptr;
    }

    plist_t plist4 = nullptr;
    plist_err_t err4 = plist_from_openstep(nullptr, (uint32_t)Size, &plist4);
    (void)err4;
    if (plist4 != nullptr) {
        plist_free(plist4);
        plist4 = nullptr;
    }

    plist_err_t err5 = plist_from_openstep(buffer, (uint32_t)Size, nullptr);
    (void)err5;

    FILE *f = fopen("./dummy_file", "wb");
    if (f) {
        fwrite(buffer, 1, Size, f);
        fclose(f);
    }

    free(buffer);

    return 0;
}