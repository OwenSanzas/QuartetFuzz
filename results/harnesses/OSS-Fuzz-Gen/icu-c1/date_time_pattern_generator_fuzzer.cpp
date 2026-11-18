#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "fuzzer_utils.h"

#include "unicode/dtptngen.h"
#include "unicode/locid.h"
#include "unicode/unistr.h"
#include "unicode/utypes.h"

namespace {

const char16_t kSkeletonAlphabet[] = {
    u'G', u'y', u'Y', u'u', u'U', u'r', u'Q', u'q', u'M', u'L', u'l',
    u'w', u'W', u'E', u'e', u'c', u'D', u'F', u'd', u'g', u'a',
    u'b', u'B', u'H', u'k', u'h', u'K', u'J', u'j', u'C', u'm',
    u's', u'A', u'S', u'v', u'z', u'Z', u'O', u'X', u'x', u'V',
    u'\'', u' ', u'-', u',', u':', u'.', u'/',
};
constexpr size_t kAlphabetSize = sizeof(kSkeletonAlphabet) / sizeof(kSkeletonAlphabet[0]);

const char* const kHoursKeywordValues[] = {nullptr, "h11", "h12", "h23", "h24"};
constexpr size_t kHoursKeywordCount =
    sizeof(kHoursKeywordValues) / sizeof(kHoursKeywordValues[0]);

const char* const kCalendarKeywordValues[] = {
    nullptr, "gregorian", "japanese", "buddhist", "roc", "persian",
    "islamic-civil", "islamic", "hebrew", "chinese", "indian", "coptic",
    "ethiopic", "ethiopic-amete-alem", "iso8601", "dangi",
    "islamic-umalqura", "islamic-tbla", "islamic-rgsa",
};
constexpr size_t kCalendarKeywordCount =
    sizeof(kCalendarKeywordValues) / sizeof(kCalendarKeywordValues[0]);

std::unordered_map<uint32_t, std::unique_ptr<icu::DateTimePatternGenerator>> gGenCache;
constexpr size_t kMaxCacheEntries = 512;

icu::DateTimePatternGenerator* GetOrCreateGenerator(uint32_t key,
                                                     const icu::Locale& locale) {
  auto it = gGenCache.find(key);
  if (it != gGenCache.end()) {
    return it->second.get();
  }
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::DateTimePatternGenerator> gen(
      icu::DateTimePatternGenerator::createInstance(locale, status));
  if (U_FAILURE(status) || gen == nullptr) {
    return nullptr;
  }
  if (gGenCache.size() >= kMaxCacheEntries) {

    gGenCache.clear();
  }
  icu::DateTimePatternGenerator* raw = gen.get();
  gGenCache.emplace(key, std::move(gen));
  return raw;
}

icu::UnicodeString BuildSkeleton(const uint8_t* data, size_t size) {
  icu::UnicodeString input;
  size_t i = 0;
  while (i + 1 < size) {
    uint8_t sel = data[i++];
    uint8_t aux = data[i++];
    if (sel & 0x80) {

      input.append(static_cast<char16_t>(aux));
    } else {
      char16_t c = kSkeletonAlphabet[sel % kAlphabetSize];
      int runLen = (aux % 8) + 1;
      for (int r = 0; r < runLen; ++r) {
        input.append(c);
      }
    }
  }
  if (i < size) {

    input.append(static_cast<char16_t>(data[i]));
  }
  return input;
}
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 4 * sizeof(uint16_t)) {
    return 0;
  }

  uint16_t rnd, optsSeed, hoursSeed, calSeed;
  std::memcpy(&rnd, data, sizeof(rnd));
  data += sizeof(rnd);
  std::memcpy(&optsSeed, data, sizeof(optsSeed));
  data += sizeof(optsSeed);
  std::memcpy(&hoursSeed, data, sizeof(hoursSeed));
  data += sizeof(hoursSeed);
  std::memcpy(&calSeed, data, sizeof(calSeed));
  data += sizeof(calSeed);
  size -= 4 * sizeof(uint16_t);

  int32_t num_locales = 0;
  const icu::Locale* locales = icu::Locale::getAvailableLocales(num_locales);
  if (num_locales <= 0) {
    return 0;
  }
  int32_t locIdx = rnd % num_locales;
  icu::Locale locale = locales[locIdx];

  size_t hoursIdx = hoursSeed % kHoursKeywordCount;
  const char* hoursValue = kHoursKeywordValues[hoursIdx];
  if (hoursValue != nullptr) {
    UErrorCode kwStatus = U_ZERO_ERROR;
    locale.setKeywordValue("hours", hoursValue, kwStatus);

  }

  size_t calIdx = calSeed % kCalendarKeywordCount;
  const char* calValue = kCalendarKeywordValues[calIdx];
  if (calValue != nullptr) {
    UErrorCode kwStatus = U_ZERO_ERROR;
    locale.setKeywordValue("calendar", calValue, kwStatus);

  }

  uint32_t cacheKey =
      (static_cast<uint32_t>(locIdx) * static_cast<uint32_t>(kHoursKeywordCount) +
       static_cast<uint32_t>(hoursIdx)) *
          static_cast<uint32_t>(kCalendarKeywordCount) +
      static_cast<uint32_t>(calIdx);
  icu::DateTimePatternGenerator* generator = GetOrCreateGenerator(cacheKey, locale);
  if (generator == nullptr) {
    return 0;
  }

  icu::UnicodeString input = BuildSkeleton(data, size);

  int32_t start = 0;
  int32_t len = input.length();
  while (start < len) {
    int32_t nl = input.indexOf(static_cast<char16_t>('\n'), start);
    int32_t end = (nl < 0) ? len : nl;
    icu::UnicodeString skeleton(input, start, end - start);
    start = (nl < 0) ? len : nl + 1;

    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString bestPattern =
        generator->getBestPattern(skeleton, status);
    (void)bestPattern;

    UDateTimePatternMatchOptions opts = static_cast<UDateTimePatternMatchOptions>(
        optsSeed & (UDATPG_MATCH_HOUR_FIELD_LENGTH |
                    UDATPG_MATCH_MINUTE_FIELD_LENGTH |
                    UDATPG_MATCH_SECOND_FIELD_LENGTH));
    status = U_ZERO_ERROR;
    icu::UnicodeString bestPattern2 =
        generator->getBestPattern(skeleton, opts, status);
    (void)bestPattern2;
  }

  return 0;
}