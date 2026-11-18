#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "fast_float/fast_float.h"

namespace {

using cf = fast_float::chars_format;

template <typename UC>
unsigned chain_parse(const UC *first, const UC *last, cf fmt) {
  const UC *p = first;
  int iterations = 0;
  unsigned checksum = 0;
  while (p < last && iterations < 8) {
    double value = 0.0;
    auto res = fast_float::from_chars<double, UC>(p, last, value, fmt);
    if (static_cast<bool>(res)) {

      checksum = checksum * 31u + static_cast<unsigned>(value * 1000.0);
      if (res.ptr < last) {
        checksum ^= static_cast<unsigned>(*res.ptr);
      }
    } else {
      checksum ^= static_cast<unsigned>(res.ec == std::errc::result_out_of_range);
    }
    if (res.ptr == p) {
      break;
    }
    p = res.ptr;
    ++iterations;
  }
  return checksum;
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1) {
    return 0;
  }

  const uint8_t ctrl = data[0];
  const uint8_t *text = data + 1;
  const size_t text_len = size - 1;
  const char *first = reinterpret_cast<const char *>(text);
  const char *last = first + text_len;

  volatile double main_sink = 0.0;
  {
    double value = 0.0;
    auto res = fast_float::from_chars<double>(first, last, value,
                                               fast_float::chars_format::general);
    if (static_cast<bool>(res)) {
      main_sink = value;

      volatile ptrdiff_t consumed = res.ptr - first;
      (void)consumed;
    }
  }
  (void)main_sink;

  cf fmt;
  switch (ctrl & 0x07u) {
    case 0: fmt = cf::general; break;
    case 1: fmt = cf::scientific; break;
    case 2: fmt = cf::fixed; break;
    case 3: fmt = cf::no_infnan; break;
    case 4: fmt = cf::json; break;
    case 5: fmt = cf::json_or_infnan; break;
    case 6: fmt = cf::fortran; break;
    default: fmt = cf::general; break;
  }
  uint64_t fmt_bits = static_cast<uint64_t>(fmt);
  if (ctrl & 0x08u) fmt_bits |= static_cast<uint64_t>(cf::allow_leading_plus);
  if (ctrl & 0x10u) fmt_bits |= static_cast<uint64_t>(cf::skip_white_space);
  fmt = static_cast<cf>(fmt_bits);

  volatile unsigned checksum = chain_parse<char>(first, last, fmt);
  (void)checksum;

  if (ctrl & 0x20u) {
    float fvalue = 0.0f;
    auto res = fast_float::from_chars<float>(first, last, fvalue, fmt);
    if (static_cast<bool>(res)) {
      volatile float sink = fvalue;
      (void)sink;
    }
  }

  if (ctrl & 0x40u) {
    char dot = text_len > 0 ? static_cast<char>(text[0]) : '.';
    fast_float::parse_options_t<char> options(fmt, dot, 10);
    double value = 0.0;
    auto res = fast_float::from_chars_advanced<double>(first, last, value, options);
    if (static_cast<bool>(res)) {
      volatile double sink = value;
      (void)sink;
    }
  }

  if (ctrl & 0x80u) {
    const int base = 2 + (ctrl % 35);
    long long ivalue = 0;
    auto res = fast_float::from_chars<long long>(first, last, ivalue, base);
    if (static_cast<bool>(res)) {
      volatile long long sink = ivalue;
      (void)sink;
    }
  } else if (text_len > 0) {

    if (ctrl & 0x01u) {
      std::vector<char16_t> wide(text_len);
      for (size_t i = 0; i < text_len; ++i) {
        wide[i] = static_cast<char16_t>(text[i]);
      }
      double value = 0.0;
      auto res = fast_float::from_chars<double, char16_t>(
          wide.data(), wide.data() + wide.size(), value, fmt);
      if (static_cast<bool>(res)) {
        volatile double sink = value;
        (void)sink;
      }
    } else {
      std::vector<char32_t> wide(text_len);
      for (size_t i = 0; i < text_len; ++i) {
        wide[i] = static_cast<char32_t>(text[i]);
      }
      double value = 0.0;
      auto res = fast_float::from_chars<double, char32_t>(
          wide.data(), wide.data() + wide.size(), value, fmt);
      if (static_cast<bool>(res)) {
        volatile double sink = value;
        (void)sink;
      }
    }
  }

  return 0;
}
