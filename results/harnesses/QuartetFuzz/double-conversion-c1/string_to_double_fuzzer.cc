#include <cstdint>
#include <cstddef>
#include <climits>
#include <cmath>
#include <limits>
#include <vector>

#include "string-to-double.h"

using namespace double_conversion;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size == 0) return 0;
  if (size > static_cast<size_t>(INT_MAX)) return 0;

  const char* buffer = reinterpret_cast<const char*>(data);
  const int length = static_cast<int>(size);

  {
    const int flags = StringToDoubleConverter::ALLOW_HEX |
                       StringToDoubleConverter::ALLOW_OCTALS |
                       StringToDoubleConverter::ALLOW_TRAILING_JUNK |
                       StringToDoubleConverter::ALLOW_LEADING_SPACES |
                       StringToDoubleConverter::ALLOW_TRAILING_SPACES |
                       StringToDoubleConverter::ALLOW_SPACES_AFTER_SIGN |
                       StringToDoubleConverter::ALLOW_CASE_INSENSITIVITY |
                       StringToDoubleConverter::ALLOW_HEX_FLOATS;
    StringToDoubleConverter converter(
        flags,
        0.0,
        std::numeric_limits<double>::quiet_NaN(),
        "infinity", "nan");

    int processed = 0;
    double result = converter.StringToDouble(buffer, length, &processed);

    volatile bool fully_consumed = (processed == length);
    volatile bool is_junk = std::isnan(result);
    (void)fully_consumed;
    (void)is_junk;
  }

  if (size >= 2) {
    const uint8_t cfg_byte = data[0];
    const uint8_t sel_byte = data[1];

    const int flags2 = cfg_byte;
    const uc16 separator =
        (sel_byte & 1) ? static_cast<uc16>(' ')
                       : static_cast<uc16>(StringToDoubleConverter::kNoSeparator);
    const bool use_symbols = (sel_byte & 2) != 0;
    const char* inf_sym = use_symbols ? "infinity" : nullptr;
    const char* nan_sym = use_symbols ? "nan" : nullptr;

    StringToDoubleConverter converter2(
        flags2,
        0.0,
        std::numeric_limits<double>::quiet_NaN(),
        inf_sym, nan_sym, separator);

    const uint8_t variant = (sel_byte >> 2) & 0x3;
    int processed2 = 0;

    switch (variant) {
      case 0: {
        double r = converter2.StringToDouble(buffer, length, &processed2);
        volatile double sink = r;
        (void)sink;
        break;
      }
      case 1: {
        float r = converter2.StringToFloat(buffer, length, &processed2);
        volatile float sink = r;
        (void)sink;
        break;
      }
      case 2: {
        std::vector<uc16> wide(data, data + size);
        double r = converter2.StringToDouble(wide.data(),
                                              static_cast<int>(wide.size()),
                                              &processed2);
        volatile double sink = r;
        (void)sink;
        break;
      }
      default: {
        std::vector<uc16> wide(data, data + size);
        float r = converter2.StringToFloat(wide.data(),
                                            static_cast<int>(wide.size()),
                                            &processed2);
        volatile float sink = r;
        (void)sink;
        break;
      }
    }

    volatile bool fully_consumed2 = (processed2 == length);
    (void)fully_consumed2;
  }

  return 0;
}
