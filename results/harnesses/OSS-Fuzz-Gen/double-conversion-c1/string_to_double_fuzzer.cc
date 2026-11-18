#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

#include "string-to-double.h"

using double_conversion::StringToDoubleConverter;

namespace {

void MaybeAppendSpaces(FuzzedDataProvider& s, std::string& out) {
  if (s.ConsumeBool()) {
    int n = s.ConsumeIntegralInRange<int>(0, 4);
    out.append(static_cast<size_t>(n), ' ');
  }
}

void MaybeAppendSign(FuzzedDataProvider& s, std::string& out) {
  if (s.ConsumeBool()) out.push_back(s.ConsumeBool() ? '+' : '-');
}

char RandDecDigit(FuzzedDataProvider& s) {
  return static_cast<char>('0' + s.ConsumeIntegralInRange<int>(0, 9));
}

char RandHexDigit(FuzzedDataProvider& s) {
  static const char kHex[] = "0123456789abcdefABCDEF";
  return kHex[s.ConsumeIntegralInRange<int>(0, 21)];
}

char RandOctDigit(FuzzedDataProvider& s) {
  return static_cast<char>('0' + s.ConsumeIntegralInRange<int>(0, 7));
}

template <typename DigitGen>
void AppendDigitsWithSeparator(FuzzedDataProvider& s, std::string& out,
                                int count, uc16 separator, DigitGen gen) {
  for (int i = 0; i < count; i++) {
    out.push_back(gen(s));
    if (separator != StringToDoubleConverter::kNoSeparator &&
        i + 1 < count && s.ConsumeBool()) {
      out.push_back(static_cast<char>(separator));
    }
  }
}

void MaybeInjectInvalidSeparator(FuzzedDataProvider& s, std::string& out,
                                  uc16 separator) {
  if (separator == StringToDoubleConverter::kNoSeparator) return;
  if (!s.ConsumeBool()) return;
  char sep = static_cast<char>(separator);
  switch (s.ConsumeIntegralInRange<int>(0, 2)) {
    case 0:
      out.insert(out.begin(), sep);
      break;
    case 1:
      out.push_back(sep);
      break;
    case 2:
      if (!out.empty()) {
        size_t pos = s.ConsumeIntegralInRange<size_t>(0, out.size() - 1);
        out.insert(out.begin() + static_cast<long>(pos), sep);
        out.insert(out.begin() + static_cast<long>(pos), sep);
      }
      break;
  }
}

void AppendBoundaryExponentDigits(FuzzedDataProvider& s, std::string& out) {
  out += "107374182";
  int next_digit = s.ConsumeIntegralInRange<int>(0, 9);
  out.push_back(static_cast<char>('0' + next_digit));
  int more = s.ConsumeIntegralInRange<int>(0, 5);
  for (int i = 0; i < more; i++) out.push_back(RandDecDigit(s));
}

std::string MulSmall(const std::string& digits, int factor) {
  std::string result;
  int carry = 0;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    int v = (*it - '0') * factor + carry;
    result.push_back(static_cast<char>('0' + v % 10));
    carry = v / 10;
  }
  while (carry) {
    result.push_back(static_cast<char>('0' + carry % 10));
    carry /= 10;
  }
  std::reverse(result.begin(), result.end());
  size_t nz = result.find_first_not_of('0');
  return nz == std::string::npos ? "0" : result.substr(nz);
}

std::string BuildBoundaryDigits(FuzzedDataProvider& s, int perturb_mode,
                                 std::string* out_exp_suffix) {

  uint64_t m = s.ConsumeIntegralInRange<uint64_t>(0, (1ULL << 53) - 1);

  int k;
  if (s.ConsumeBool()) {
    k = s.ConsumeIntegralInRange<int>(-1075, -700);
  } else {
    k = s.ConsumeIntegralInRange<int>(-1075, 971);
  }
  uint64_t numerator = 2 * m + 1;
  std::string digits = std::to_string(numerator);

  int exp_value = 0;
  if (k >= 0) {
    for (int i = 0; i < k; i++) digits = MulSmall(digits, 2);
    *out_exp_suffix = "";
  } else {
    for (int i = 0; i < -k; i++) digits = MulSmall(digits, 5);
    exp_value = k;
    *out_exp_suffix = "e" + std::to_string(exp_value);
  }

  if (perturb_mode != 0) {

    int extra = s.ConsumeIntegralInRange<int>(1, 6);
    bool round_down = (perturb_mode == 1);
    digits += std::string(static_cast<size_t>(extra), round_down ? '9' : '0');
    if (!round_down) digits.back() = '1';
    int new_k = exp_value - extra;
    *out_exp_suffix = "e" + std::to_string(new_k);
  }
  return digits;
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  int flags = StringToDoubleConverter::NO_FLAGS;
  if (stream.ConsumeBool()) flags |= StringToDoubleConverter::ALLOW_HEX;
  if (stream.ConsumeBool()) flags |= StringToDoubleConverter::ALLOW_OCTALS;
  if (stream.ConsumeBool())
    flags |= StringToDoubleConverter::ALLOW_TRAILING_JUNK;
  if (stream.ConsumeBool())
    flags |= StringToDoubleConverter::ALLOW_LEADING_SPACES;
  if (stream.ConsumeBool())
    flags |= StringToDoubleConverter::ALLOW_TRAILING_SPACES;
  if (stream.ConsumeBool())
    flags |= StringToDoubleConverter::ALLOW_SPACES_AFTER_SIGN;
  if (stream.ConsumeBool())
    flags |= StringToDoubleConverter::ALLOW_CASE_INSENSITIVITY;
  if (stream.ConsumeBool())
    flags |= StringToDoubleConverter::ALLOW_HEX_FLOATS;

  const double empty_string_value = stream.ConsumeFloatingPoint<double>();
  const double junk_string_value = stream.ConsumeFloatingPoint<double>();

  const char* infinity_symbol =
      stream.ConsumeBool() ? "infinity" : nullptr;
  const char* nan_symbol = stream.ConsumeBool() ? "nan" : nullptr;

  static const uint16_t kCommonSeparators[] = {
      static_cast<uint16_t>(StringToDoubleConverter::kNoSeparator),
      '_', ' ', '\'', ',', '.', '0', '1'
  };
  const uc16 separator = stream.PickValueInArray(kCommonSeparators);

  StringToDoubleConverter converter(flags, empty_string_value,
                                     junk_string_value, infinity_symbol,
                                     nan_symbol, separator);

  enum Mode {
    kRaw = 0,
    kDecimal,
    kHexInt,
    kHexFloat,
    kOctal,
    kInfinity,
    kNan,
    kBignumBoundary,
    kModeCount
  };

  static const uint8_t kModeWeights[] = {
      kRaw,          kRaw,
      kDecimal,      kDecimal,       kDecimal,
      kHexInt,
      kHexFloat,     kHexFloat,      kHexFloat,
      kOctal,        kOctal,         kOctal,
      kInfinity,
      kNan,
      kBignumBoundary, kBignumBoundary, kBignumBoundary, kBignumBoundary,
  };
  const int mode = stream.PickValueInArray(kModeWeights);

  std::string input_buffer;

  switch (mode) {
    case kDecimal: {
      MaybeAppendSpaces(stream, input_buffer);
      MaybeAppendSign(stream, input_buffer);
      MaybeAppendSpaces(stream, input_buffer);

      int int_digits = stream.ConsumeIntegralInRange<int>(0, 900);
      AppendDigitsWithSeparator(stream, input_buffer, int_digits, separator,
                                 RandDecDigit);
      if (stream.ConsumeBool() || int_digits == 0) {
        input_buffer.push_back('.');
        int frac_digits = stream.ConsumeIntegralInRange<int>(0, 900);
        AppendDigitsWithSeparator(stream, input_buffer, frac_digits,
                                   separator, RandDecDigit);
      }

      MaybeInjectInvalidSeparator(stream, input_buffer, separator);
      if (stream.ConsumeBool()) {
        input_buffer.push_back(stream.ConsumeBool() ? 'e' : 'E');
        if (stream.ConsumeBool())
          input_buffer.push_back(stream.ConsumeBool() ? '+' : '-');

        if (stream.ConsumeBool()) {
          AppendBoundaryExponentDigits(stream, input_buffer);
        } else {

          int exp_digits = stream.ConsumeIntegralInRange<int>(0, 15);
          for (int i = 0; i < exp_digits; i++)
            input_buffer.push_back(RandDecDigit(stream));
        }
      }
      MaybeAppendSpaces(stream, input_buffer);
      break;
    }
    case kHexInt: {
      MaybeAppendSign(stream, input_buffer);
      input_buffer += "0x";
      int digits = stream.ConsumeIntegralInRange<int>(0, 30);
      AppendDigitsWithSeparator(stream, input_buffer, digits, separator,
                                 RandHexDigit);
      MaybeInjectInvalidSeparator(stream, input_buffer, separator);
      MaybeAppendSpaces(stream, input_buffer);
      break;
    }
    case kHexFloat: {
      MaybeAppendSign(stream, input_buffer);
      input_buffer += "0x";
      int int_digits = stream.ConsumeIntegralInRange<int>(0, 20);
      AppendDigitsWithSeparator(stream, input_buffer, int_digits, separator,
                                 RandHexDigit);
      if (stream.ConsumeBool() || int_digits == 0) {
        input_buffer.push_back('.');
        int frac_digits = stream.ConsumeIntegralInRange<int>(0, 20);
        AppendDigitsWithSeparator(stream, input_buffer, frac_digits,
                                   separator, RandHexDigit);
      }
      MaybeInjectInvalidSeparator(stream, input_buffer, separator);

      if (stream.ConsumeBool()) {
        input_buffer.push_back(stream.ConsumeBool() ? 'p' : 'P');
        if (stream.ConsumeBool())
          input_buffer.push_back(stream.ConsumeBool() ? '+' : '-');

        int exp_digits = stream.ConsumeIntegralInRange<int>(0, 8);
        for (int i = 0; i < exp_digits; i++)
          input_buffer.push_back(RandDecDigit(stream));
      }
      MaybeAppendSpaces(stream, input_buffer);
      break;
    }
    case kOctal: {
      MaybeAppendSign(stream, input_buffer);
      input_buffer.push_back('0');
      int digits = stream.ConsumeIntegralInRange<int>(0, 30);
      AppendDigitsWithSeparator(stream, input_buffer, digits, separator,
                                 RandOctDigit);
      MaybeInjectInvalidSeparator(stream, input_buffer, separator);

      if (stream.ConsumeBool()) {
        input_buffer.push_back('.');
        int frac = stream.ConsumeIntegralInRange<int>(0, 10);
        for (int i = 0; i < frac; i++)
          input_buffer.push_back(RandOctDigit(stream));
      } else {

        if (stream.ConsumeBool())
          input_buffer.push_back(static_cast<char>(
              '0' + stream.ConsumeIntegralInRange<int>(8, 9)));
        if (stream.ConsumeBool()) {
          input_buffer.push_back(stream.ConsumeBool() ? 'e' : 'E');
          input_buffer.push_back(RandDecDigit(stream));
        }
      }
      MaybeAppendSpaces(stream, input_buffer);
      break;
    }
    case kInfinity:
    case kNan: {
      MaybeAppendSign(stream, input_buffer);
      MaybeAppendSpaces(stream, input_buffer);
      std::string sym = (mode == kInfinity) ? "infinity" : "nan";

      if (stream.ConsumeBool()) {
        int idx =
            stream.ConsumeIntegralInRange<int>(0, static_cast<int>(sym.size()) - 1);
        sym[static_cast<size_t>(idx)] =
            static_cast<char>('a' + stream.ConsumeIntegralInRange<int>(0, 25));
      }

      if (stream.ConsumeBool()) {
        for (char& c : sym)
          if (stream.ConsumeBool())
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      }
      input_buffer += sym;
      MaybeAppendSpaces(stream, input_buffer);
      break;
    }
    case kBignumBoundary: {

      MaybeAppendSign(stream, input_buffer);
      int perturb_mode = stream.ConsumeIntegralInRange<int>(0, 2);
      std::string exp_suffix;
      input_buffer += BuildBoundaryDigits(stream, perturb_mode, &exp_suffix);
      input_buffer += exp_suffix;
      MaybeAppendSpaces(stream, input_buffer);
      break;
    }
    case kRaw:
    default:
      input_buffer = stream.ConsumeRemainingBytesAsString();
      break;
  }

  if (mode != kRaw) {
    input_buffer += stream.ConsumeRemainingBytesAsString();
  }

  int processed_characters_count = 0;
  converter.StringToDouble(input_buffer.c_str(),
                            static_cast<int>(input_buffer.size()),
                            &processed_characters_count);

  return 0;
}