#include <cstring>
#include <stddef.h>
#include <stdint.h>

#include "fuzzer_utils.h"
#include "unicode/dtptngen.h"
#include "unicode/locid.h"
#include "unicode/smpdtfmt.h"
#include "unicode/unistr.h"

namespace {

icu::StringPiece TakeField(const uint8_t *&ptr, size_t &rem) {
  size_t len = 0;
  while (len < rem && ptr[len] != 0) {
    len++;
  }
  icu::StringPiece piece(reinterpret_cast<const char *>(ptr),
                         static_cast<int32_t>(len));
  if (len < rem) {
    ptr += len + 1;
    rem -= len + 1;
  } else {
    ptr += len;
    rem -= len;
  }
  return piece;
}
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 4) {
    return 0;
  }

  uint8_t ctrl = data[0];
  uint8_t ctrl2 = data[1];
  uint16_t localeRnd;
  std::memcpy(&localeRnd, data + 2, sizeof(localeRnd));
  const uint8_t *p = data + 4;
  size_t remaining = size - 4;

  UErrorCode status = U_ZERO_ERROR;
  icu::DateTimePatternGenerator *gen = nullptr;
  bool useEmpty = (ctrl & 0x1) != 0;
  if (useEmpty) {
    gen = icu::DateTimePatternGenerator::createEmptyInstance(status);
  } else {
    icu::Locale locale = GetRandomLocale(localeRnd);
    gen = icu::DateTimePatternGenerator::createInstance(locale, status);
  }
  if (U_FAILURE(status) || gen == nullptr) {
    delete gen;
    return 0;
  }

  if (useEmpty) {
    icu::UnicodeString seedPattern = UNICODE_STRING_SIMPLE("yyyy-MM-dd HH:mm:ss");
    icu::UnicodeString seedConflict;
    gen->addPattern(seedPattern, true, seedConflict, status);
    status = U_ZERO_ERROR;
  }

  icu::StringPiece skelPiece = TakeField(p, remaining);
  icu::StringPiece patPiece = TakeField(p, remaining);
  icu::StringPiece skelUsePiece = TakeField(p, remaining);

  icu::UnicodeString skeleton = icu::UnicodeString::fromUTF8(skelPiece);
  icu::UnicodeString patternToAdd = icu::UnicodeString::fromUTF8(patPiece);
  icu::UnicodeString skeletonToUse = icu::UnicodeString::fromUTF8(skelUsePiece);

  if (ctrl & 0x2) {
    icu::StringPiece decPiece(reinterpret_cast<const char *>(p),
                              static_cast<int32_t>(remaining > 4 ? 4 : remaining));
    icu::UnicodeString dec = icu::UnicodeString::fromUTF8(decPiece);
    gen->setDecimal(dec);
  }

  if ((ctrl & 0x4) && patternToAdd.length() > 0) {
    icu::UnicodeString conflicting;
    UBool doOverride = (ctrl & 0x8) != 0;
    if ((ctrl & 0x10) && skeletonToUse.length() > 0) {
      gen->addPatternWithSkeleton(patternToAdd, skeletonToUse, doOverride,
                                  conflicting, status);
    } else {
      gen->addPattern(patternToAdd, doOverride, conflicting, status);
    }
    status = U_ZERO_ERROR;
  }

  UDateTimePatternMatchOptions options = static_cast<UDateTimePatternMatchOptions>(
      ctrl2 & UDATPG_MATCH_ALL_FIELDS_LENGTH);

  status = U_ZERO_ERROR;
  icu::UnicodeString bestPattern = gen->getBestPattern(skeleton, status);
  bool haveBestPattern = U_SUCCESS(status) && bestPattern.length() > 0;

  if (haveBestPattern) {

    icu::Locale fmtLocale = GetRandomLocale(localeRnd);
    UErrorCode fmtStatus = U_ZERO_ERROR;
    icu::SimpleDateFormat sdf(bestPattern, fmtLocale, fmtStatus);
    if (U_SUCCESS(fmtStatus)) {
      UDate date = static_cast<UDate>(
          (static_cast<int32_t>(ctrl) << 16) ^ localeRnd);
      icu::UnicodeString out;
      sdf.format(date, out);
    }
  }

  icu::UnicodeString resultOrFallback = haveBestPattern ? bestPattern : patternToAdd;

  status = U_ZERO_ERROR;
  switch (ctrl & 0x60) {
    case 0x00: {

      icu::UnicodeString trial = gen->getBestPattern(resultOrFallback, options, status);
      (void)trial;
      break;
    }
    case 0x20: {
      icu::UnicodeString replaced =
          gen->replaceFieldTypes(resultOrFallback, skeleton, options, status);
      (void)replaced;
      break;
    }
    case 0x40: {
      icu::UnicodeString sk = gen->getSkeleton(resultOrFallback, status);
      (void)sk;
      break;
    }
    default: {
      icu::UnicodeString bsk = gen->getBaseSkeleton(resultOrFallback, status);
      (void)bsk;
      break;
    }
  }

  delete gen;
  return 0;
}
