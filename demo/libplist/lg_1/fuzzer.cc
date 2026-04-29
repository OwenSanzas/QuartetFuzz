/*
 * bplist_fuzzer.cc
 * cross-format serialization fuzz target for libFuzzer
 *
 * Copyright (c) 2021 Nikias Bassen All Rights Reserved.
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
#include <stddef.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0) {
        return 0;
    }

    // Step 1: Parse fuzz input into a plist tree (auto-detects format)
    plist_t root_node = NULL;
    plist_format_t detected_fmt = PLIST_FORMAT_NONE;
    plist_err_t err = plist_from_memory(
        reinterpret_cast<const char*>(data),
        static_cast<uint32_t>(size),
        &root_node,
        &detected_fmt
    );

    if (err != PLIST_ERR_SUCCESS || root_node == NULL) {
        return 0;
    }

    // Step 2: Serialize to binary format (exercises serialize_plist)
    {
        char *bin_out = NULL;
        uint32_t bin_len = 0;
        err = plist_to_bin(root_node, &bin_out, &bin_len);
        if (err == PLIST_ERR_SUCCESS && bin_out != NULL) {
            plist_mem_free(bin_out);
        }
    }

    // Step 3: Serialize to XML format (exercises node_to_xml)
    {
        char *xml_out = NULL;
        uint32_t xml_len = 0;
        err = plist_write_to_string(root_node, &xml_out, &xml_len,
                                    PLIST_FORMAT_XML, PLIST_OPT_NONE);
        if (err == PLIST_ERR_SUCCESS && xml_out != NULL) {
            plist_mem_free(xml_out);
        }
    }

    // Step 4: Serialize to JSON format (exercises node_to_json)
    // Use PLIST_OPT_COERCE to handle DATE/DATA/UID types without error
    {
        char *json_out = NULL;
        uint32_t json_len = 0;
        err = plist_write_to_string(root_node, &json_out, &json_len,
                                    PLIST_FORMAT_JSON, PLIST_OPT_COERCE);
        if (err == PLIST_ERR_SUCCESS && json_out != NULL) {
            plist_mem_free(json_out);
        }
    }

    // Step 5: Serialize to OpenStep format (exercises node_to_openstep)
    {
        char *ostep_out = NULL;
        uint32_t ostep_len = 0;
        err = plist_write_to_string(root_node, &ostep_out, &ostep_len,
                                    PLIST_FORMAT_OSTEP, PLIST_OPT_NONE);
        if (err == PLIST_ERR_SUCCESS && ostep_out != NULL) {
            plist_mem_free(ostep_out);
        }
    }

    // Step 6: Serialize to PRINT format (exercises node_to_string default)
    {
        char *print_out = NULL;
        uint32_t print_len = 0;
        err = plist_write_to_string(root_node, &print_out, &print_len,
                                    PLIST_FORMAT_PRINT, PLIST_OPT_NONE);
        if (err == PLIST_ERR_SUCCESS && print_out != NULL) {
            plist_mem_free(print_out);
        }
    }

    // Step 7: Serialize to PLUTIL format (exercises node_to_string plutil)
    {
        char *plutil_out = NULL;
        uint32_t plutil_len = 0;
        err = plist_write_to_string(root_node, &plutil_out, &plutil_len,
                                    PLIST_FORMAT_PLUTIL, PLIST_OPT_NONE);
        if (err == PLIST_ERR_SUCCESS && plutil_out != NULL) {
            plist_mem_free(plutil_out);
        }
    }

    // Step 8: Cleanup
    plist_free(root_node);

    return 0;
}
