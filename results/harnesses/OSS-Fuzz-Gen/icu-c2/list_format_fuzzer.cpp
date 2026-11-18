#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "unicode/listformatter.h"
#include "unicode/locid.h"
#include "unicode/ulistformatter.h"
#include "unicode/unistr.h"
#include "unicode/utypes.h"
#include "fuzzer_utils.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider stream(data, size);

  std::unique_ptr<icu::ListFormatter> fmt;
  UErrorCode status = U_ZERO_ERROR;

  uint8_t ctor_mode = stream.ConsumeIntegralInRange<uint8_t>(0, 1);

  static const char* kSpecialLocales[] = {
      "es", "es_ES", "es_MX", "es_419", "he", "he_IL", "iw"};

  if (ctor_mode == 0) {
    icu::Locale locale;
    uint8_t locale_mode = stream.ConsumeIntegralInRange<uint8_t>(0, 3);
    if (locale_mode == 0) {
      uint8_t idx = stream.ConsumeIntegralInRange<uint8_t>(
          0, sizeof(kSpecialLocales) / sizeof(kSpecialLocales[0]) - 1);
      locale = icu::Locale(kSpecialLocales[idx]);
    } else {
      uint16_t rnd = stream.ConsumeIntegral<uint16_t>();
      locale = GetRandomLocale(rnd);
    }

    static const UListFormatterType kTypes[] = {
        ULISTFMT_TYPE_AND, ULISTFMT_TYPE_OR, ULISTFMT_TYPE_UNITS};
    static const UListFormatterWidth kWidths[] = {
        ULISTFMT_WIDTH_WIDE, ULISTFMT_WIDTH_SHORT, ULISTFMT_WIDTH_NARROW};
    UListFormatterType type =
        kTypes[stream.ConsumeIntegralInRange<uint8_t>(0, 2)];
    UListFormatterWidth width =
        kWidths[stream.ConsumeIntegralInRange<uint8_t>(0, 2)];

    fmt.reset(
        icu::ListFormatter::createInstance(locale, type, width, status));
  } else {

    static const char* kLangs[] = {"es", "he", "iw", "en", "fr", "ar", "zh"};
    icu::Locale locale(
        kLangs[stream.ConsumeIntegralInRange<uint8_t>(0, 6)]);

    static const char16_t* kTwoEndPool[] = {
        u"{0} y {1}",
        u"{0} e {1}",
        u"{0} o {1}",
        u"{0} u {1}",
        u"{0} \u05D5{1}",
        u"{0} \u05D5-{1}",
        u"{0}, {1}",
        u"{1}, {0}",
        u"{0} and {1}",
        u"{1} and {0}",
    };

    static const char16_t* kStartMiddlePool[] = {
        u"{0}, {1}",
        u"{1}, {0}",
        u"{0}",
        u"no args here",
        u"{0} {1} {2}",
    };

    auto pick = [&](const char16_t* const* pool, size_t n) {
      return icu::UnicodeString(
          true, pool[stream.ConsumeIntegralInRange<uint8_t>(0, n - 1)], -1);
    };
    icu::UnicodeString twoPattern =
        pick(kTwoEndPool, sizeof(kTwoEndPool) / sizeof(kTwoEndPool[0]));
    icu::UnicodeString endPattern =
        pick(kTwoEndPool, sizeof(kTwoEndPool) / sizeof(kTwoEndPool[0]));
    icu::UnicodeString startPattern =
        pick(kStartMiddlePool,
             sizeof(kStartMiddlePool) / sizeof(kStartMiddlePool[0]));
    icu::UnicodeString middlePattern =
        pick(kStartMiddlePool,
             sizeof(kStartMiddlePool) / sizeof(kStartMiddlePool[0]));

    auto makeFuzzedPattern = [&]() -> icu::UnicodeString {
      std::string s;
      size_t len = stream.ConsumeIntegralInRange<size_t>(0, 24);
      for (size_t i = 0; i < len; i++) {

        uint8_t choice = stream.ConsumeIntegralInRange<uint8_t>(0, 11);
        switch (choice) {
          case 0:
            s += '{';
            break;
          case 1:
            s += '}';
            break;
          case 2:
            s += '\'';
            break;
          case 3:
            s += static_cast<char>(
                '0' + stream.ConsumeIntegralInRange<uint8_t>(0, 9));
            break;
          case 4:
            s += "{0}";
            break;
          case 5:
            s += "{1}";
            break;
          case 6:
            s += "{10}";
            break;
          case 7:
            s += "''";
            break;
          case 8:
            s += "'{literal}'";
            break;
          case 9:
            s += "{999}";

            break;
          case 10:
            s += "{256}";
            break;
          default:
            s += static_cast<char>(stream.ConsumeIntegral<uint8_t>());
        }
      }
      return icu::UnicodeString::fromUTF8(
          icu::StringPiece(s.data(), s.size()));
    };

    if (stream.ConsumeBool()) twoPattern = makeFuzzedPattern();
    if (stream.ConsumeBool()) endPattern = makeFuzzedPattern();
    if (stream.ConsumeBool()) startPattern = makeFuzzedPattern();
    if (stream.ConsumeBool()) middlePattern = makeFuzzedPattern();

    icu::ListFormatData listData(twoPattern, startPattern, middlePattern,
                                  endPattern, locale);
    fmt.reset(new icu::ListFormatter(listData, status));
  }

  if (U_FAILURE(status) || fmt == nullptr) {
    return 0;
  }

  int32_t n_items = stream.ConsumeIntegralInRange<int32_t>(0, 10);

  static const char* kTriggerPrefixes[] = {
      "hi", "Hi", "i", "I",
      "o", "O", "8", "ho", "11",
      "A", "z", "1",
      "\xD7\x90",

  };

  static const char* kExactTriggers[] = {
      "11", "11 ", "11x",
      "hia", "hie", "HIA", "HIE",
      "\xD7\x90",
      "\xD7\x90" "bcdef",
  };

  std::vector<icu::UnicodeString> items;
  items.reserve(n_items);
  for (int32_t i = 0; i < n_items; i++) {

    if (stream.remaining_bytes() == 0 && i > 0) {
      n_items = i;
      break;
    }

    std::string item_bytes;
    uint8_t mode = stream.ConsumeIntegralInRange<uint8_t>(0, 2);
    if (mode == 0) {
      uint8_t idx = stream.ConsumeIntegralInRange<uint8_t>(
          0, sizeof(kExactTriggers) / sizeof(kExactTriggers[0]) - 1);
      item_bytes = kExactTriggers[idx];
    } else if (mode == 1) {
      uint8_t pidx = stream.ConsumeIntegralInRange<uint8_t>(
          0, sizeof(kTriggerPrefixes) / sizeof(kTriggerPrefixes[0]) - 1);
      item_bytes += kTriggerPrefixes[pidx];
      size_t item_len = stream.ConsumeIntegralInRange<size_t>(0, 32);
      item_bytes += stream.ConsumeBytesAsString(item_len);
    } else {
      size_t item_len = stream.ConsumeIntegralInRange<size_t>(0, 32);
      item_bytes += stream.ConsumeBytesAsString(item_len);
    }
    items.emplace_back(icu::UnicodeString::fromUTF8(
        icu::StringPiece(item_bytes.data(), item_bytes.size())));
  }

  n_items = static_cast<int32_t>(items.size());

  icu::UnicodeString appendTo;
  if (stream.ConsumeBool()) {
    size_t prefix_len = stream.ConsumeIntegralInRange<size_t>(0, 16);
    std::string prefix_bytes = stream.ConsumeBytesAsString(prefix_len);
    appendTo = icu::UnicodeString::fromUTF8(
        icu::StringPiece(prefix_bytes.data(), prefix_bytes.size()));
  }

  status = U_ZERO_ERROR;

  fmt->format(n_items > 0 ? items.data() : nullptr, n_items, appendTo,
              status);

  return 0;
}