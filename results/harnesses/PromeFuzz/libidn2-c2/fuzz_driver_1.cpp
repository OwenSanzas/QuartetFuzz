#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <idn2.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    std::vector<char> input_buf(Size + 1);
    std::memcpy(input_buf.data(), Data, Size);
    input_buf[Size] = '\0';

    const char *input = input_buf.data();

    int flags_options[] = {
        IDN2_NFC_INPUT,
        IDN2_ALABEL_ROUNDTRIP,
        IDN2_TRANSITIONAL,
        IDN2_NONTRANSITIONAL,
        IDN2_ALLOW_UNASSIGNED,
        IDN2_USE_STD3_ASCII_RULES,
        0
    };

    for (int flags : flags_options) {
        char *output = nullptr;
        int rc = idn2_to_ascii_8z(input, &output, flags);
        if (rc == IDN2_OK && output != nullptr) {

            volatile size_t len = std::strlen(output);
            (void)len;
        }
        if (output != nullptr) {
            idn2_free(output);
            output = nullptr;
        }
    }

    char *output_combined = nullptr;
    int combined_flags = IDN2_NFC_INPUT | IDN2_ALLOW_UNASSIGNED;
    int rc_combined = idn2_to_ascii_8z(input, &output_combined, combined_flags);
    if (rc_combined == IDN2_OK && output_combined != nullptr) {
        volatile size_t len = std::strlen(output_combined);
        (void)len;
    }
    if (output_combined != nullptr) {
        idn2_free(output_combined);
    }

    if (Size > 1) {
        std::vector<char> partial_buf(Size);
        std::memcpy(partial_buf.data(), Data, Size - 1);
        partial_buf[Size - 1] = '\0';

        char *partial_output = nullptr;
        int partial_rc = idn2_to_ascii_8z(partial_buf.data(), &partial_output, 0);
        if (partial_rc == IDN2_OK && partial_output != nullptr) {
            volatile size_t len = std::strlen(partial_output);
            (void)len;
        }
        if (partial_output != nullptr) {
            idn2_free(partial_output);
        }
    }

    return 0;
}