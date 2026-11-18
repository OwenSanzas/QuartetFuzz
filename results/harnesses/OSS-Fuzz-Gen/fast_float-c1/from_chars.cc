#include <fuzzer/FuzzedDataProvider.h>

#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>

#include "fast_float/fast_float.h"

namespace fast_float {

template from_chars_result_t<char> from_chars<double, char>(
    char const *first, char const *last, double &value, chars_format fmt);
template from_chars_result_t<char> from_chars<float, char>(
    char const *first, char const *last, float &value, chars_format fmt);
}

using DoubleFromCharsFn = fast_float::from_chars_result_t<char> (*)(
    char const *, char const *, double &, fast_float::chars_format);
using FloatFromCharsFn = fast_float::from_chars_result_t<char> (*)(
    char const *, char const *, float &, fast_float::chars_format);

static DoubleFromCharsFn volatile g_from_chars_double =
    &fast_float::from_chars<double, char>;
static FloatFromCharsFn volatile g_from_chars_float =
    &fast_float::from_chars<float, char>;

namespace {

std::string BuildHardRoundNumber(FuzzedDataProvider &stream, bool for_double) {
  const int q = for_double ? stream.ConsumeIntegralInRange<int>(18, 27)
                            : stream.ConsumeIntegralInRange<int>(1, 10);
  uint64_t power5 = 1;
  for (int i = 0; i < q; ++i) {
    power5 *= 5;
  }
  const int bits = for_double ? 53 : 24;
  const uint64_t low_threshold = (uint64_t(1) << bits) / power5 + 1;
  const uint64_t threshold = UINT64_MAX / power5;
  uint64_t k;
  if (low_threshold >= threshold) {
    k = low_threshold;
  } else {
    k = stream.ConsumeIntegralInRange<uint64_t>(low_threshold, threshold);
  }
  const uint64_t mantissa = k * power5;

  std::string s;
  if (stream.ConsumeBool()) {
    s += '-';
  }

  std::string mant_str = std::to_string(mantissa);
  if (stream.ConsumeBool()) {
    const size_t extra = stream.ConsumeIntegralInRange<size_t>(1, 10);
    for (size_t i = 0; i < extra; ++i) {
      mant_str +=
          static_cast<char>('0' + stream.ConsumeIntegralInRange<int>(0, 9));
    }
  }
  s += mant_str;
  s += 'e';
  s += '-';
  s += std::to_string(q);
  return s;
}

std::string BuildStructuredNumber(FuzzedDataProvider &stream, bool use_double,
                                   bool &force_scientific) {
  std::string s;
  force_scientific = false;

  const uint8_t category = stream.ConsumeIntegralInRange<uint8_t>(0, 10);

  if (category == 0) {
    static const char *json_edge_cases[] = {
        "-.5", ".5",  "1.",  "00.5", "01",
        "-0",  "0.",  "-",   "+1",   "1.e5"};
    constexpr size_t n_cases =
        sizeof(json_edge_cases) / sizeof(json_edge_cases[0]);
    s += json_edge_cases[stream.ConsumeIntegralInRange<uint8_t>(
        0, static_cast<uint8_t>(n_cases - 1))];
    return s;
  }

  if (category == 1) {
    force_scientific = true;
    return BuildHardRoundNumber(stream, use_double);
  }

  if (stream.ConsumeBool()) {
    s += stream.ConsumeBool() ? '-' : '+';
  }

  const uint8_t special = stream.ConsumeIntegralInRange<uint8_t>(0, 9);
  if (special == 0) {
    s += "inf";
    return s;
  }
  if (special == 1) {
    s += "infinity";
    return s;
  }
  if (special == 2) {
    s += "nan";

    const uint8_t paren_kind = stream.ConsumeIntegralInRange<uint8_t>(0, 3);
    if (paren_kind >= 1) {
      s += '(';
      const size_t n = stream.ConsumeIntegralInRange<size_t>(0, 10);
      static const char good_alphabet[] =
          "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
      static const char bad_alphabet[] = "!@#$%^&* \t)(,.";
      constexpr size_t good_len = sizeof(good_alphabet) - 2;
      constexpr size_t bad_len = sizeof(bad_alphabet) - 2;
      for (size_t i = 0; i < n; ++i) {
        if (paren_kind == 2 && i == n / 2) {
          s += bad_alphabet[stream.ConsumeIntegralInRange<uint8_t>(
              0, static_cast<uint8_t>(bad_len))];
        } else {
          s += good_alphabet[stream.ConsumeIntegralInRange<uint8_t>(
              0, static_cast<uint8_t>(good_len))];
        }
      }

      if (paren_kind != 3) {
        s += ')';
      }
    }
    return s;
  }

  const size_t n_digits = stream.ConsumeIntegralInRange<size_t>(0, 400);
  for (size_t i = 0; i < n_digits; ++i) {
    s += static_cast<char>('0' + stream.ConsumeIntegralInRange<int>(0, 9));
  }

  if (stream.ConsumeBool()) {
    s += '.';
    const size_t n_frac = stream.ConsumeIntegralInRange<size_t>(0, 400);
    for (size_t i = 0; i < n_frac; ++i) {
      s += static_cast<char>('0' + stream.ConsumeIntegralInRange<int>(0, 9));
    }
  }

  if (stream.ConsumeBool()) {

    const uint8_t marker_kind = stream.ConsumeIntegralInRange<uint8_t>(0, 4);
    switch (marker_kind) {
    case 0:
      s += 'e';
      break;
    case 1:
      s += 'E';
      break;
    case 2:
      s += 'd';
      break;
    case 3:
      s += 'D';
      break;
    case 4:

      break;
    }

    if (stream.ConsumeBool()) {
      s += stream.ConsumeBool() ? '-' : '+';
    }

    const size_t n_exp = stream.ConsumeIntegralInRange<size_t>(0, 12);
    for (size_t i = 0; i < n_exp; ++i) {
      s += static_cast<char>('0' + stream.ConsumeIntegralInRange<int>(0, 9));
    }
  }

  return s;
}

const char *PickBoundaryLiteral(FuzzedDataProvider &stream) {
  static const char *boundary_literals[] = {
      "1.7976931348623157e308",  "2.2250738585072014e-308",
      "4.9406564584124654e-324", "9007199254740993",
      "17976931348623159e292",   "5e-324",
      "1e-400",                  "1e400",
  };
  constexpr size_t n =
      sizeof(boundary_literals) / sizeof(boundary_literals[0]);
  return boundary_literals[stream.ConsumeIntegralInRange<uint8_t>(
      0, static_cast<uint8_t>(n - 1))];
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  static const int kRoundingModes[] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD,
                                        FE_TOWARDZERO};
  const uint8_t round_choice = stream.ConsumeIntegralInRange<uint8_t>(0, 3);
  std::fesetround(kRoundingModes[round_choice]);

  const bool use_double = stream.ConsumeBool();

  constexpr uint64_t kFlagMask =
      uint64_t(fast_float::chars_format::scientific) |
      uint64_t(fast_float::chars_format::fixed) |
      uint64_t(fast_float::chars_format::no_infnan) |
      uint64_t(fast_float::chars_format::allow_leading_plus) |
      uint64_t(fast_float::chars_format::skip_white_space) |
      (uint64_t(1) << 5)   |
      (uint64_t(1) << 6)  ;

  const uint16_t raw_bits = stream.ConsumeIntegral<uint16_t>();
  fast_float::chars_format fmt =
      static_cast<fast_float::chars_format>(raw_bits & kFlagMask);

  const uint8_t strategy = stream.ConsumeIntegralInRange<uint8_t>(0, 9);

  std::string input;
  bool force_scientific = false;
  if (strategy == 0) {
    input = PickBoundaryLiteral(stream);
    input += stream.ConsumeRemainingBytesAsString();
  } else if (strategy <= 7) {
    input = BuildStructuredNumber(stream, use_double, force_scientific);

    input += stream.ConsumeRemainingBytesAsString();
  } else {
    input = stream.ConsumeRemainingBytesAsString();
  }

  if (force_scientific) {
    fmt = static_cast<fast_float::chars_format>(
        uint64_t(fmt) | uint64_t(fast_float::chars_format::scientific));
  }

  const char *first = input.data();
  const char *last = first + input.size();

  if (use_double) {
    double value = 0.0;

    DoubleFromCharsFn fn = g_from_chars_double;
    fast_float::from_chars_result_t<char> answer = fn(first, last, value, fmt);
    if (answer.ec == std::errc()) {

      value += 0.0;
    }
  } else {
    float value = 0.0f;
    FloatFromCharsFn fn = g_from_chars_float;
    fast_float::from_chars_result_t<char> answer = fn(first, last, value, fmt);
    if (answer.ec == std::errc()) {

      value += 0.0f;
    }
  }

  std::fesetround(FE_TONEAREST);

  return 0;
}