#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

#include "unicode/bytestream.h"
#include "unicode/locid.h"
#include "unicode/stringpiece.h"
#include "unicode/uobject.h"
#include "unicode/utypes.h"

namespace {

void ConsumeNBytes(const uint8_t** data, size_t* size, size_t N) {
  *data += N;
  *size -= N;
}

uint8_t ConsumeUint8(const uint8_t** data, size_t* size) {
  uint8_t tmp = 0;
  if (*size >= 1) {
    tmp = (*data)[0];
    ConsumeNBytes(data, size, 1);
  }
  return tmp;
}

std::string ConsumeSubstring(const uint8_t** data, size_t* size) {
  const size_t request_size = ConsumeUint8(data, size);
  const char* substring_start = reinterpret_cast<const char*>(*data);
  const size_t substring_size = std::min(*size, request_size);
  ConsumeNBytes(data, size, substring_size);
  return std::string(substring_start, substring_size);
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 2) {
    return 0;
  }

  const uint8_t mode = ConsumeUint8(&data, &size) % 4;
  const uint8_t flags = ConsumeUint8(&data, &size);
  const bool do_canonicalize_first = (flags & 0x01) != 0;
  const bool do_minimize = (flags & 0x04) != 0;

  UErrorCode status = U_ZERO_ERROR;
  icu::Locale locale;

  switch (mode) {
    case 0: {

      icu::StringPiece tag(reinterpret_cast<const char*>(data), size);
      locale = icu::Locale::forLanguageTag(tag, status);
      break;
    }
    case 1: {

      std::string name(reinterpret_cast<const char*>(data), size);
      locale = icu::Locale::createFromName(name.c_str());
      break;
    }
    case 2: {

      if (size == 0) {
        return 0;
      }
      std::string name(reinterpret_cast<const char*>(data), size);
      locale = icu::Locale::createCanonical(name.c_str());
      break;
    }
    case 3:
    default: {

      const std::string language = ConsumeSubstring(&data, &size);
      const std::string country = ConsumeSubstring(&data, &size);
      const std::string variant = ConsumeSubstring(&data, &size);
      const std::string kv_pairs = ConsumeSubstring(&data, &size);
      locale = icu::Locale(language.c_str(), country.c_str(), variant.c_str(),
                            kv_pairs.c_str());
      break;
    }
  }

  status = U_ZERO_ERROR;

  if (do_canonicalize_first) {
    locale.canonicalize(status);
    status = U_ZERO_ERROR;
  }

  locale.addLikelySubtags(status);

  if (U_SUCCESS(status)) {

    const char* name = locale.getName();
    const char* base_name = locale.getBaseName();
    const char* lang = locale.getLanguage();
    const char* script = locale.getScript();
    const char* country = locale.getCountry();
    const char* variant = locale.getVariant();

    volatile unsigned checksum = 0;
    for (const char* s : {name, base_name, lang, script, country, variant}) {
      for (const char* p = s; p && *p; ++p) {
        checksum = checksum * 131u + static_cast<unsigned char>(*p);
      }
    }
    (void)checksum;

    std::string out;
    icu::StringByteSink<std::string> sink(&out);
    UErrorCode tag_status = U_ZERO_ERROR;
    locale.toLanguageTag(sink, tag_status);

    if (do_minimize) {
      UErrorCode min_status = U_ZERO_ERROR;
      locale.minimizeSubtags(min_status);
      if (U_SUCCESS(min_status)) {
        std::string min_out;
        icu::StringByteSink<std::string> min_sink(&min_out);
        UErrorCode min_tag_status = U_ZERO_ERROR;
        locale.toLanguageTag(min_sink, min_tag_status);
      }
    }
  }

  return 0;
}
