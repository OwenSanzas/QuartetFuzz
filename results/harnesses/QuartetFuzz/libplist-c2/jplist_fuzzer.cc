#include <plist/plist.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void walk_plist(plist_t node, int depth)
{
    if (!node || depth > 64) {
        return;
    }

    plist_type type = plist_get_node_type(node);
    switch (type) {
        case PLIST_DICT: {
            plist_dict_iter iter = NULL;
            plist_dict_new_iter(node, &iter);
            if (iter) {
                char *key = NULL;
                plist_t val = NULL;
                plist_dict_next_item(node, iter, &key, &val);
                while (key || val) {
                    if (val) {
                        walk_plist(val, depth + 1);
                    }
                    if (key) {
                        plist_mem_free(key);
                    }
                    key = NULL;
                    val = NULL;
                    plist_dict_next_item(node, iter, &key, &val);
                }
                plist_dict_free_iter(iter);
            }
            break;
        }
        case PLIST_ARRAY: {
            uint32_t n = plist_array_get_size(node);
            for (uint32_t i = 0; i < n; i++) {
                plist_t item = plist_array_get_item(node, i);
                walk_plist(item, depth + 1);
            }
            break;
        }
        default: {

            plist_t c = plist_copy(node);
            if (c) {
                plist_free(c);
            }
            break;
        }
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1) {
        return 0;
    }

    uint8_t ctrl = data[0];
    const char* json = reinterpret_cast<const char*>(data + 1);
    uint32_t json_len = static_cast<uint32_t>(size - 1);

    plist_t root_node = NULL;
    plist_err_t err = plist_from_json(json, json_len, &root_node);
    if (err == PLIST_ERR_SUCCESS && root_node) {

        walk_plist(root_node, 0);

        plist_write_options_t opts = PLIST_OPT_NONE;
        if (ctrl & 0x1) opts = static_cast<plist_write_options_t>(opts | PLIST_OPT_COMPACT);
        if (ctrl & 0x2) opts = static_cast<plist_write_options_t>(opts | PLIST_OPT_COERCE);

        char* json_out = NULL;
        uint32_t json_out_len = 0;
        plist_err_t werr = plist_to_json_with_options(root_node, &json_out, &json_out_len, opts);
        if (werr == PLIST_ERR_SUCCESS && json_out) {
            plist_mem_free(json_out);
        }

        char* json_out2 = NULL;
        uint32_t json_out2_len = 0;
        int prettify = (ctrl & 0x4) ? 1 : 0;
        plist_err_t werr2 = plist_to_json(root_node, &json_out2, &json_out2_len, prettify);
        if (werr2 == PLIST_ERR_SUCCESS && json_out2) {
            plist_mem_free(json_out2);
        }

        char* bin_out = NULL;
        uint32_t bin_out_len = 0;
        plist_err_t berr = plist_to_bin(root_node, &bin_out, &bin_out_len);
        if (berr == PLIST_ERR_SUCCESS && bin_out) {
            plist_t root_from_bin = NULL;
            if (plist_from_bin(bin_out, bin_out_len, &root_from_bin) == PLIST_ERR_SUCCESS && root_from_bin) {
                plist_free(root_from_bin);
            }
            plist_mem_free(bin_out);
        }

        plist_free(root_node);
    }

    plist_t root_node2 = NULL;
    plist_format_t fmt = PLIST_FORMAT_NONE;
    plist_err_t err2 = plist_from_memory(json, json_len, &root_node2, &fmt);
    if (err2 == PLIST_ERR_SUCCESS && root_node2) {
        plist_free(root_node2);
    }

    return 0;
}
