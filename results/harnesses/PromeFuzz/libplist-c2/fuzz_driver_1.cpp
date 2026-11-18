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
#include <cstddef>
#include <cstring>
#include <plist/plist.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Data == nullptr || Size == 0) {

        plist_t plist = nullptr;
        plist_from_json(nullptr, 0, &plist);
        if (plist) {
            plist_free(plist);
        }
        return 0;
    }

    char *json_buf = new char[Size];
    memcpy(json_buf, Data, Size);

    plist_t plist = nullptr;
    plist_err_t ret = plist_from_json(json_buf, static_cast<uint32_t>(Size), &plist);

    if (ret == PLIST_ERR_SUCCESS && plist != nullptr) {

        char *out_json = nullptr;
        uint32_t out_len = 0;
        plist_to_json(plist, &out_json, &out_len, 0);
        if (out_json) {
            free(out_json);
        }

        char *out_xml = nullptr;
        uint32_t out_xml_len = 0;
        plist_to_xml(plist, &out_xml, &out_xml_len);
        if (out_xml) {
            free(out_xml);
        }

        char *out_bin = nullptr;
        uint32_t out_bin_len = 0;
        plist_to_bin(plist, &out_bin, &out_bin_len);
        if (out_bin) {
            free(out_bin);
        }

        plist_free(plist);
    }

    plist_t plist2 = nullptr;
    plist_from_json(json_buf, 0, &plist2);
    if (plist2) {
        plist_free(plist2);
    }

    plist_from_json(json_buf, static_cast<uint32_t>(Size), nullptr);

    delete[] json_buf;

    return 0;
}