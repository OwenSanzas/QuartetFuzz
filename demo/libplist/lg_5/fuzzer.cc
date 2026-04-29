/*
 * bplist_fuzzer.cc
 * pretty-print output formatters fuzz target for libFuzzer
 *
 * Copyright (c) 2017 Nikias Bassen All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <plist/plist.h>
#include <stdint.h>
#include <stdio.h>

extern "C" int LLVMFuzzerTestOneInput(const unsigned char* data, size_t size)
{
    if (size == 0) {
        return 0;
    }

    plist_t root_node = NULL;
    plist_format_t fmt = PLIST_FORMAT_NONE;

    plist_err_t err = plist_from_memory(reinterpret_cast<const char*>(data),
                                        static_cast<uint32_t>(size),
                                        &root_node, &fmt);
    if (err != PLIST_ERR_SUCCESS || root_node == NULL) {
        plist_free(root_node);
        return 0;
    }

    /* Exercise all three pretty-print formatters */
    static const plist_format_t print_formats[] = {
        PLIST_FORMAT_PRINT,
        PLIST_FORMAT_LIMD,
        PLIST_FORMAT_PLUTIL,
    };

    for (int i = 0; i < 3; i++) {
        char *output = NULL;
        uint32_t length = 0;
        err = plist_write_to_string(root_node, &output, &length,
                                    print_formats[i], PLIST_OPT_NONE);
        if (err == PLIST_ERR_SUCCESS && output != NULL) {
            plist_mem_free(output);
        }
    }

    /* Also exercise with PLIST_OPT_PARTIAL_DATA for PRINT format */
    {
        char *output = NULL;
        uint32_t length = 0;
        err = plist_write_to_string(root_node, &output, &length,
                                    PLIST_FORMAT_PRINT, PLIST_OPT_PARTIAL_DATA);
        if (err == PLIST_ERR_SUCCESS && output != NULL) {
            plist_mem_free(output);
        }
    }

    /* Exercise with PLIST_OPT_NO_NEWLINE for LIMD format */
    {
        char *output = NULL;
        uint32_t length = 0;
        err = plist_write_to_string(root_node, &output, &length,
                                    PLIST_FORMAT_LIMD, PLIST_OPT_NO_NEWLINE);
        if (err == PLIST_ERR_SUCCESS && output != NULL) {
            plist_mem_free(output);
        }
    }

    plist_free(root_node);
    return 0;
}
