/*
 * jplist_fuzzer.cc
 * JSON plist fuzz target for libFuzzer
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
#include <stdlib.h>
#include <string.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) return 0;

    // plist_from_json takes uint32_t for length.
    if (size > UINT32_MAX) return 0;

    plist_t plist = NULL;

    // Direct entry to JSON parser.
    // This exercises the custom tokenizer (JSMN) and recursive descent parser.
    plist_err_t err = plist_from_json((const char *)data, (uint32_t)size, &plist);

    if (err == PLIST_ERR_SUCCESS && plist) {
        char *json_out = NULL;
        uint32_t length = 0;
        
        // Test compact output with coercion to exercise the JSON generator logic.
        // Coercion allows non-native JSON types to be serialized, increasing coverage.
        if (plist_to_json_with_options(plist, &json_out, &length, (plist_write_options_t)(PLIST_OPT_COMPACT | PLIST_OPT_COERCE)) == PLIST_ERR_SUCCESS) {
            plist_mem_free(json_out);
        }
        
        json_out = NULL;
        // Test prettified output with coercion.
        if (plist_to_json_with_options(plist, &json_out, &length, PLIST_OPT_COERCE) == PLIST_ERR_SUCCESS) {
            plist_mem_free(json_out);
        }
    }

    if (plist) {
        plist_free(plist);
    }

    // Also test plist_from_memory for the format detection logic (dispatching to JSON, XML, etc.)
    plist_t plist2 = NULL;
    plist_format_t format = PLIST_FORMAT_NONE;
    if (plist_from_memory((const char *)data, (uint32_t)size, &plist2, &format) == PLIST_ERR_SUCCESS) {
        if (plist2) {
            plist_free(plist2);
        }
    }

    return 0;
}
