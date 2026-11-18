#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include "idn2.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    std::vector<char> input_buffer(Size + 1);
    memcpy(input_buffer.data(), Data, Size);
    input_buffer[Size] = '\0';

    const char *input = input_buffer.data();

    int flags_options[] = {
        0,
        IDN2_NFC_INPUT,
        IDN2_ALABEL_ROUNDTRIP,
        IDN2_TRANSITIONAL,
        IDN2_NONTRANSITIONAL,
        IDN2_ALLOW_UNASSIGNED,
        IDN2_USE_STD3_ASCII_RULES,
        IDN2_NFC_INPUT | IDN2_ALABEL_ROUNDTRIP,
        IDN2_NFC_INPUT | IDN2_TRANSITIONAL,
        IDN2_NFC_INPUT | IDN2_NONTRANSITIONAL,
        IDN2_ALABEL_ROUNDTRIP | IDN2_ALLOW_UNASSIGNED,
        IDN2_NFC_INPUT | IDN2_ALABEL_ROUNDTRIP | IDN2_USE_STD3_ASCII_RULES
    };

    size_t num_flags_options = sizeof(flags_options) / sizeof(flags_options[0]);

    size_t flag_index = (Size > 0) ? (Data[0] % num_flags_options) : 0;
    int flags = flags_options[flag_index];

    char *output = nullptr;
    int result = idn2_to_unicode_8z8z(input, &output, flags);

    if (result == IDN2_OK && output != nullptr) {

        volatile size_t len = strlen(output);
        (void)len;
        free(output);
        output = nullptr;
    } else if (output != nullptr) {

        free(output);
        output = nullptr;
    }

    if (Size > 1) {
        size_t flag_index2 = (Data[1] % num_flags_options);
        int flags2 = flags_options[flag_index2];

        char *output2 = nullptr;
        int result2 = idn2_to_unicode_8z8z(input, &output2, flags2);

        if (result2 == IDN2_OK && output2 != nullptr) {
            volatile size_t len2 = strlen(output2);
            (void)len2;
            free(output2);
            output2 = nullptr;
        } else if (output2 != nullptr) {
            free(output2);
            output2 = nullptr;
        }
    }

    if (Size > 2 && Data[2] % 10 == 0) {
        char *output3 = nullptr;
        int result3 = idn2_to_unicode_8z8z(input, &output3, 0);
        if (output3 != nullptr) {
            free(output3);
        }
        (void)result3;
    }

    FILE *fp = fopen("./dummy_file", "wb");
    if (fp != nullptr) {
        fwrite(Data, 1, Size, fp);
        fclose(fp);
    }

    return 0;
}