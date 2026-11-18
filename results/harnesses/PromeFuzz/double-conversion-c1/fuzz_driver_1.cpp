#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include "string-to-double.h"

using namespace double_conversion;

static void TestWithFlags(int flags, const uint8_t *Data, size_t Size) {

    double nan_value = std::nan("");

    StringToDoubleConverter conv1(flags,
                                   0.0,
                                   nan_value,
                                   "infinity",
                                   "nan",
                                   ' ');

    StringToDoubleConverter conv2(flags,
                                   0.0,
                                   nan_value,
                                   "Infinity",
                                   "NaN",
                                   StringToDoubleConverter::kNoSeparator);

    std::string input(reinterpret_cast<const char*>(Data), Size);

    int processed_characters_count = 0;

    double result1 = conv1.StringToDouble(input.c_str(),
                                           static_cast<int>(input.size()),
                                           &processed_characters_count);
    (void)result1;

    processed_characters_count = 0;

    double result2 = conv2.StringToDouble(input.c_str(),
                                           static_cast<int>(input.size()),
                                           &processed_characters_count);
    (void)result2;

    size_t uc16_count = Size / sizeof(uc16);
    if (uc16_count > 0) {
        std::vector<uc16> uc16_input(uc16_count);
        std::memcpy(uc16_input.data(), Data, uc16_count * sizeof(uc16));

        processed_characters_count = 0;
        double result3 = conv1.StringToDouble(uc16_input.data(),
                                               static_cast<int>(uc16_input.size()),
                                               &processed_characters_count);
        (void)result3;

        processed_characters_count = 0;
        double result4 = conv2.StringToDouble(uc16_input.data(),
                                               static_cast<int>(uc16_input.size()),
                                               &processed_characters_count);
        (void)result4;
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    uint8_t flag_selector = Data[0];
    const uint8_t *payload = Data + 1;
    size_t payload_size = Size - 1;

    int flags = StringToDoubleConverter::NO_FLAGS;

    if (flag_selector & 0x01) flags |= StringToDoubleConverter::ALLOW_HEX;
    if (flag_selector & 0x02) flags |= StringToDoubleConverter::ALLOW_OCTALS;
    if (flag_selector & 0x04) flags |= StringToDoubleConverter::ALLOW_TRAILING_JUNK;
    if (flag_selector & 0x08) flags |= StringToDoubleConverter::ALLOW_LEADING_SPACES;
    if (flag_selector & 0x10) flags |= StringToDoubleConverter::ALLOW_TRAILING_SPACES;
    if (flag_selector & 0x20) flags |= StringToDoubleConverter::ALLOW_SPACES_AFTER_SIGN;
    if (flag_selector & 0x40) flags |= StringToDoubleConverter::ALLOW_CASE_INSENSITIVITY;
    if (flag_selector & 0x80) flags |= StringToDoubleConverter::ALLOW_HEX_FLOATS;

    TestWithFlags(flags, payload, payload_size);

    TestWithFlags(StringToDoubleConverter::NO_FLAGS, payload, payload_size);

    int all_flags = StringToDoubleConverter::ALLOW_HEX |
                     StringToDoubleConverter::ALLOW_TRAILING_JUNK |
                     StringToDoubleConverter::ALLOW_LEADING_SPACES |
                     StringToDoubleConverter::ALLOW_TRAILING_SPACES |
                     StringToDoubleConverter::ALLOW_SPACES_AFTER_SIGN |
                     StringToDoubleConverter::ALLOW_CASE_INSENSITIVITY |
                     StringToDoubleConverter::ALLOW_HEX_FLOATS;
    TestWithFlags(all_flags, payload, payload_size);

    return 0;
}