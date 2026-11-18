#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "unicode/locid.h"
#include "unicode/utypes.h"

#include "charstr.h"
#include "ulocimp.h"

namespace {
const char* kLanguages[] = {
    "en", "zh", "sr", "und", "iw", "in", "ji", "mo", "he", "ar", "fr",
    "de", "es", "ru", "ja", "ko", "pt", "th", "tr", "nl", "pl", "art",
    "yue", "fil", "nb", "nn", "sh", "tl", "cel", "gsw"
};
const char* kFourLetterLanguages[] = {
    "Aaaa", "Latn", "Zyyy", "Qaaa"
};
const char* kScripts[] = {
    "Latn", "Cyrl", "Hans", "Hant", "Arab", "Hebr", "Zzzz", "Jpan", "Kore",
    "Mong", "Cyrs", "Qaai"
};
const char* kRegions[] = {
    "US", "GB", "CN", "TW", "RS", "DE", "FR", "ZZ", "BU", "ZR", "TP", "001",
    "419", "150", "XK", "HK", "MO", "YD", "DD", "SU"
};
const char* kKeys[] = {
    "calendar", "collation", "currency", "numbers", "va"
};
const char* kVals[] = {
    "gregorian", "phonebook", "standard", "latn", "posix"
};
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  std::string language, country, variant;

  uint8_t langMode = stream.ConsumeIntegral<uint8_t>() % 4;
  switch (langMode) {
    case 0:
      language = stream.PickValueInArray(kLanguages);
      break;
    case 1:

      language = stream.PickValueInArray(kFourLetterLanguages);
      break;
    case 2:

      language = "abcdefghij";
      break;
    default: {
      uint8_t len = stream.ConsumeIntegral<uint8_t>();
      language = stream.ConsumeBytesAsString(len);
    }
  }

  if (stream.ConsumeBool()) {
    country = stream.PickValueInArray(kRegions);
  } else {
    uint8_t len = stream.ConsumeIntegral<uint8_t>();
    country = stream.ConsumeBytesAsString(len);
  }

  uint8_t varMode = stream.ConsumeIntegral<uint8_t>() % 3;
  if (varMode == 0) {
    variant = stream.PickValueInArray(kScripts);
  } else if (varMode == 1) {
    int n = 9 + (stream.ConsumeIntegral<uint8_t>() % 8);
    for (int i = 0; i < n; i++) {
      variant += static_cast<char>('a' + (stream.ConsumeIntegral<uint8_t>() % 26));
    }
  } else {
    uint8_t len = stream.ConsumeIntegral<uint8_t>();
    variant = stream.ConsumeBytesAsString(len);
  }

  if (stream.ConsumeBool()) {
    variant += "_";
    variant += stream.ConsumeBytesAsString(12);
  }

  std::string kv_pairs;
  if (stream.ConsumeBool()) {
    kv_pairs = stream.PickValueInArray(kKeys);
    kv_pairs += "=";
    kv_pairs += stream.PickValueInArray(kVals);
    if (stream.ConsumeBool()) {
      kv_pairs += ";";
      kv_pairs += stream.PickValueInArray(kKeys);
      kv_pairs += "=";
      kv_pairs += stream.PickValueInArray(kVals);
    }
  } else {
    kv_pairs = stream.ConsumeRemainingBytesAsString();
  }

  icu::Locale locale(language.c_str(), country.c_str(), variant.c_str(),
                      kv_pairs.c_str());

  UErrorCode status = U_ZERO_ERROR;
  locale.addLikelySubtags(status);

  UErrorCode status2 = U_ZERO_ERROR;
  icu::Locale locale2(locale);
  locale2.minimizeSubtags(status2);

  std::string rawLocaleId = language;
  if (!country.empty()) {
    rawLocaleId += "_";
    rawLocaleId += country;
  }
  if (!variant.empty()) {
    rawLocaleId += "_";
    rawLocaleId += variant;
  }
  if (!kv_pairs.empty()) {
    rawLocaleId += "@";
    rawLocaleId += kv_pairs;
  }

  UErrorCode rawStatus = U_ZERO_ERROR;
  icu::CharString maxResult =
      ulocimp_addLikelySubtags(rawLocaleId.c_str(), rawStatus);
  (void)maxResult;

  UErrorCode minStatusA = U_ZERO_ERROR;
  icu::CharString minA =
      ulocimp_minimizeSubtags(rawLocaleId.c_str(),  false,
                               minStatusA);
  (void)minA;

  UErrorCode minStatusB = U_ZERO_ERROR;
  icu::CharString minB =
      ulocimp_minimizeSubtags(rawLocaleId.c_str(),  true,
                               minStatusB);
  (void)minB;

  if (U_SUCCESS(rawStatus) && !maxResult.isEmpty()) {
    UErrorCode roundTripStatus = U_ZERO_ERROR;
    icu::CharString roundTripMax =
        ulocimp_addLikelySubtags(maxResult.data(), roundTripStatus);
    (void)roundTripMax;

    UErrorCode roundTripMinA = U_ZERO_ERROR;
    icu::CharString minRoundA = ulocimp_minimizeSubtags(
        maxResult.data(),  false, roundTripMinA);
    (void)minRoundA;

    UErrorCode roundTripMinB = U_ZERO_ERROR;
    icu::CharString minRoundB = ulocimp_minimizeSubtags(
        maxResult.data(),  true, roundTripMinB);
    (void)minRoundB;
  }

  std::string bcp47RawLocaleId = language;
  if (!country.empty()) {
    bcp47RawLocaleId += "-";
    bcp47RawLocaleId += country;
  }
  if (!variant.empty()) {
    bcp47RawLocaleId += "-";
    bcp47RawLocaleId += variant;
  }

  uint8_t extMode = stream.ConsumeIntegral<uint8_t>() % 4;
  switch (extMode) {
    case 0:

      bcp47RawLocaleId += "-u-va-posix";
      break;
    case 1:
      bcp47RawLocaleId += "-t-";
      bcp47RawLocaleId += stream.ConsumeBytesAsString(
          stream.ConsumeIntegralInRange<uint8_t>(1, 8));
      break;
    case 2:
      bcp47RawLocaleId += "-u-";
      bcp47RawLocaleId += stream.ConsumeBytesAsString(
          stream.ConsumeIntegralInRange<uint8_t>(1, 8));
      break;
    default:
      bcp47RawLocaleId += "-x-";
      bcp47RawLocaleId += stream.ConsumeBytesAsString(
          stream.ConsumeIntegralInRange<uint8_t>(1, 8));
      break;
  }
  if (!kv_pairs.empty()) {
    bcp47RawLocaleId += "@";
    bcp47RawLocaleId += kv_pairs;
  }

  UErrorCode bcp47Status = U_ZERO_ERROR;
  icu::CharString bcp47Max =
      ulocimp_addLikelySubtags(bcp47RawLocaleId.c_str(), bcp47Status);
  (void)bcp47Max;

  UErrorCode bcp47MinA = U_ZERO_ERROR;
  icu::CharString bcp47MinAResult = ulocimp_minimizeSubtags(
      bcp47RawLocaleId.c_str(),  false, bcp47MinA);
  (void)bcp47MinAResult;

  UErrorCode bcp47MinB = U_ZERO_ERROR;
  icu::CharString bcp47MinBResult = ulocimp_minimizeSubtags(
      bcp47RawLocaleId.c_str(),  true, bcp47MinB);
  (void)bcp47MinBResult;

  if (U_SUCCESS(bcp47Status) && !bcp47Max.isEmpty()) {
    UErrorCode bcp47RoundTripStatus = U_ZERO_ERROR;
    icu::CharString bcp47RoundTripMax =
        ulocimp_addLikelySubtags(bcp47Max.data(), bcp47RoundTripStatus);
    (void)bcp47RoundTripMax;

    UErrorCode bcp47RoundTripMinA = U_ZERO_ERROR;
    icu::CharString bcp47MinRoundA = ulocimp_minimizeSubtags(
        bcp47Max.data(),  false, bcp47RoundTripMinA);
    (void)bcp47MinRoundA;

    UErrorCode bcp47RoundTripMinB = U_ZERO_ERROR;
    icu::CharString bcp47MinRoundB = ulocimp_minimizeSubtags(
        bcp47Max.data(),  true, bcp47RoundTripMinB);
    (void)bcp47MinRoundB;
  }

  return 0;
}