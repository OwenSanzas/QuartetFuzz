/*
 * oplist_fuzzer.cc
 * libplist post-parse tree manipulation fuzz target for libFuzzer
 *
 * Copyright (c) 2023 Nikias Bassen All Rights Reserved.
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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" int LLVMFuzzerTestOneInput(const unsigned char* data, size_t size)
{
    if (size == 0 || size > (size_t)UINT32_MAX) {
        return 0;
    }

    plist_t root_node = NULL;
    plist_err_t err = plist_from_memory(reinterpret_cast<const char*>(data), (uint32_t)size, &root_node, NULL);
    if (err != PLIST_ERR_SUCCESS || !root_node) {
        return 0;
    }

    /* Exercise plist_copy: deep recursive clone of the entire node tree */
    plist_t copy_node = plist_copy(root_node);

    /* Exercise plist_sort: recursively sorts dict keys and array elements */
    plist_sort(root_node);

    /* Exercise plist_compare_node_value: compare original and copy */
    if (copy_node) {
        plist_compare_node_value(root_node, copy_node);
    }

    /* Exercise plist_dict_merge: if parsed plist is a dict, merge into a new dict */
    if (plist_get_node_type(root_node) == PLIST_DICT) {
        plist_t target = plist_new_dict();
        if (target) {
            plist_dict_merge(&target, root_node);
            plist_free(target);
        }
    }

    /* Also exercise plist_sort on the copy */
    if (copy_node) {
        plist_sort(copy_node);
    }

    /* Exercise plist_dict_merge with copy as source too */
    if (copy_node && plist_get_node_type(copy_node) == PLIST_DICT) {
        plist_t target2 = plist_new_dict();
        if (target2) {
            plist_dict_merge(&target2, copy_node);
            plist_free(target2);
        }
    }

    /* Cleanup */
    if (copy_node) {
        plist_free(copy_node);
    }
    plist_free(root_node);

    return 0;
}
