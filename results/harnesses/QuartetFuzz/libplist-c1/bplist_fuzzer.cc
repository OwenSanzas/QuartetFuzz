#include <plist/plist.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void walk_plist(plist_t node, int depth)
{
    if (!node || depth > 32) {
        return;
    }
    plist_type type = plist_get_node_type(node);
    switch (type) {
        case PLIST_ARRAY: {
            uint32_t n = plist_array_get_size(node);

            uint32_t limit = n > 64 ? 64 : n;
            for (uint32_t i = 0; i < limit; i++) {
                plist_t item = plist_array_get_item(node, i);
                walk_plist(item, depth + 1);
            }
            break;
        }
        case PLIST_DICT: {
            plist_dict_iter iter = NULL;
            plist_dict_new_iter(node, &iter);
            if (iter) {
                uint32_t count = 0;
                while (count < 64) {
                    char *key = NULL;
                    plist_t val = NULL;
                    plist_dict_next_item(node, iter, &key, &val);
                    if (!val && !key) {
                        break;
                    }
                    if (key) {
                        free(key);
                    }
                    walk_plist(val, depth + 1);
                    count++;
                }
                free(iter);
            }
            break;
        }
        default:

            break;
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return 0;
    }

    uint8_t selector = data[size - 1];
    size_t data_len = size - 1;

    plist_t root_node = NULL;
    plist_err_t err = plist_from_bin(reinterpret_cast<const char*>(data), (uint32_t)data_len, &root_node);

    if (err == PLIST_ERR_SUCCESS && root_node != NULL) {

        walk_plist(root_node, 0);

        if (selector & 0x1) {
            char *out_bin = NULL;
            uint32_t out_len = 0;
            if (plist_to_bin(root_node, &out_bin, &out_len) == PLIST_ERR_SUCCESS && out_bin) {
                plist_mem_free(out_bin);
            }
        }
        if (selector & 0x2) {
            char *out_xml = NULL;
            uint32_t out_len = 0;
            if (plist_to_xml(root_node, &out_xml, &out_len) == PLIST_ERR_SUCCESS && out_xml) {
                plist_mem_free(out_xml);
            }
        }
        if (selector & 0x4) {
            if (PLIST_IS_DICT(root_node) || PLIST_IS_ARRAY(root_node)) {
                char *out_json = NULL;
                uint32_t out_len = 0;
                int prettify = (selector & 0x8) ? 1 : 0;
                if (plist_to_json(root_node, &out_json, &out_len, prettify) == PLIST_ERR_SUCCESS && out_json) {
                    plist_mem_free(out_json);
                }
            }
        }
        if (selector & 0x10) {
            char *out_ostep = NULL;
            uint32_t out_len = 0;
            int prettify = (selector & 0x20) ? 1 : 0;
            if (plist_to_openstep(root_node, &out_ostep, &out_len, prettify) == PLIST_ERR_SUCCESS && out_ostep) {
                plist_mem_free(out_ostep);
            }
        }
        if (selector & 0x40) {

            plist_sort(root_node);
            char *out_xml = NULL;
            uint32_t out_len = 0;
            if (plist_to_xml(root_node, &out_xml, &out_len) == PLIST_ERR_SUCCESS && out_xml) {
                plist_mem_free(out_xml);
            }
        }
    }
    plist_free(root_node);

    plist_t root_node2 = NULL;
    plist_format_t fmt = PLIST_FORMAT_NONE;
    if (plist_from_memory(reinterpret_cast<const char*>(data), (uint32_t)data_len, &root_node2, &fmt) == PLIST_ERR_SUCCESS) {
        walk_plist(root_node2, 0);
        plist_free(root_node2);
    }

    return 0;
}
