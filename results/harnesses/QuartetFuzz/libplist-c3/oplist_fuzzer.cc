#include <plist/plist.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1) {
        return 0;
    }

    uint8_t selector = data[0];
    const char* buf = reinterpret_cast<const char*>(data + 1);
    uint32_t len = static_cast<uint32_t>(size - 1);

    plist_t root_node = NULL;

    if ((selector & 0x1) && len > 0) {

        plist_format_t fmt = PLIST_FORMAT_NONE;
        plist_from_memory(buf, len, &root_node, &fmt);
    } else {

        plist_from_openstep(buf, len, &root_node);
    }

    if (root_node) {

        plist_write_options_t opts = PLIST_OPT_NONE;
        if (selector & 0x2) opts = (plist_write_options_t)(opts | PLIST_OPT_COMPACT);
        if (selector & 0x4) opts = (plist_write_options_t)(opts | PLIST_OPT_COERCE);

        char* ostep_out = NULL;
        uint32_t ostep_len = 0;
        if (plist_to_openstep_with_options(root_node, &ostep_out, &ostep_len, opts) == PLIST_ERR_SUCCESS && ostep_out) {
            plist_mem_free(ostep_out);
        }

        char* bin_out = NULL;
        uint32_t bin_len = 0;
        if (plist_to_bin(root_node, &bin_out, &bin_len) == PLIST_ERR_SUCCESS && bin_out) {
            plist_t root2 = NULL;
            if (plist_from_bin(bin_out, bin_len, &root2) == PLIST_ERR_SUCCESS && root2) {
                char* ostep2_out = NULL;
                uint32_t ostep2_len = 0;
                if (plist_to_openstep(root2, &ostep2_out, &ostep2_len, selector & 0x8) == PLIST_ERR_SUCCESS && ostep2_out) {
                    plist_mem_free(ostep2_out);
                }
            }
            plist_free(root2);
            plist_mem_free(bin_out);
        }

        plist_free(root_node);
    }

    return 0;
}
