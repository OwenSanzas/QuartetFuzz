/*
 * jplist_fuzzer.cc - binary plist round-trip fuzzer
 * Copyright (c) 2021 Nikias Bassen All Rights Reserved.
 * LGPL v2.1+
 */
#include <plist/plist.h>
#include <stdint.h>
#include <stddef.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    plist_t root_node = NULL;
    plist_err_t err = plist_from_bin(reinterpret_cast<const char*>(data),
                                     static_cast<uint32_t>(size),
                                     &root_node);
    if (err != PLIST_ERR_SUCCESS || root_node == NULL) {
        return 0;
    }
    char *bin_out = NULL;
    uint32_t bin_len = 0;
    err = plist_to_bin(root_node, &bin_out, &bin_len);
    if (err == PLIST_ERR_SUCCESS && bin_out != NULL) {
        plist_mem_free(bin_out);
    }
    plist_free(root_node);
    return 0;
}
