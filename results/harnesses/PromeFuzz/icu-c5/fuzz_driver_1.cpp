#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include <unicode/utypes.h>
#include <unicode/ustring.h>
#include <unicode/ucol.h>
#include <unicode/unum.h>
#include <unicode/udat.h>
#include <unicode/ucnv.h>
#include <unicode/uloc.h>
#include <unicode/ucal.h>
#include <unicode/unorm2.h>
#include <unicode/ubrk.h>

static void fuzzCollator(const UChar *str1, int32_t len1,
                          const UChar *str2, int32_t len2,
                          const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UCollator *coll = ucol_open(locale, &status);
    if (U_FAILURE(status) || coll == nullptr) {
        if (coll) ucol_close(coll);
        return;
    }

    ucol_strcoll(coll, str1, len1, str2, len2);

    UColAttributeValue strength = UCOL_DEFAULT;
    status = U_ZERO_ERROR;
    ucol_setAttribute(coll, UCOL_STRENGTH, UCOL_PRIMARY, &status);
    strength = ucol_getAttribute(coll, UCOL_STRENGTH, &status);
    (void)strength;

    status = U_ZERO_ERROR;
    uint8_t sortKeyBuf[256];
    ucol_getSortKey(coll, str1, len1, sortKeyBuf, sizeof(sortKeyBuf));

    ucol_close(coll);
}

static void fuzzNumberFormat(const UChar *pattern, int32_t patternLen,
                              const UChar *input, int32_t inputLen,
                              const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UNumberFormat *fmt = unum_open(UNUM_DEFAULT, pattern, patternLen,
                                    locale, nullptr, &status);
    if (U_FAILURE(status) || fmt == nullptr) {
        if (fmt) unum_close(fmt);
        return;
    }

    UChar outBuf[256];
    double parsedDouble = 0.0;
    int32_t parsePos = 0;

    status = U_ZERO_ERROR;
    parsedDouble = unum_parseDouble(fmt, input, inputLen, &parsePos, &status);
    (void)parsedDouble;

    status = U_ZERO_ERROR;
    int32_t written = unum_formatDouble(fmt, 3.14159, outBuf,
                                         sizeof(outBuf) / sizeof(UChar),
                                         nullptr, &status);
    (void)written;

    status = U_ZERO_ERROR;
    int64_t parsedInt64 = unum_parseInt64(fmt, input, inputLen, &parsePos, &status);
    (void)parsedInt64;

    unum_close(fmt);
}

static void fuzzDateFormat(const UChar *pattern, int32_t patternLen,
                            const UChar *input, int32_t inputLen,
                            const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UDateFormat *dfmt = udat_open(UDAT_DEFAULT, UDAT_DEFAULT, locale,
                                   nullptr, 0, pattern, patternLen, &status);
    if (U_FAILURE(status) || dfmt == nullptr) {
        if (dfmt) udat_close(dfmt);
        return;
    }

    int32_t parsePos = 0;
    status = U_ZERO_ERROR;
    UDate date = udat_parse(dfmt, input, inputLen, &parsePos, &status);

    status = U_ZERO_ERROR;
    UChar outBuf[256];
    int32_t written = udat_format(dfmt, date, outBuf,
                                   sizeof(outBuf) / sizeof(UChar),
                                   nullptr, &status);
    (void)written;

    udat_close(dfmt);
}

static void fuzzConverter(const uint8_t *data, size_t size) {
    UErrorCode status = U_ZERO_ERROR;
    UConverter *conv = ucnv_open("UTF-8", &status);
    if (U_FAILURE(status) || conv == nullptr) {
        if (conv) ucnv_close(conv);
        return;
    }

    UChar uBuf[512];
    status = U_ZERO_ERROR;
    int32_t uLen = ucnv_toUChars(conv, uBuf, sizeof(uBuf) / sizeof(UChar),
                                  reinterpret_cast<const char *>(data),
                                  static_cast<int32_t>(size), &status);

    if (U_SUCCESS(status) && uLen > 0) {
        char outBuf[1024];
        status = U_ZERO_ERROR;
        int32_t outLen = ucnv_fromUChars(conv, outBuf, sizeof(outBuf),
                                          uBuf, uLen, &status);
        (void)outLen;
    }

    ucnv_close(conv);
}

static void fuzzNormalizer(const UChar *str, int32_t len) {
    UErrorCode status = U_ZERO_ERROR;
    const UNormalizer2 *norm = unorm2_getNFCInstance(&status);
    if (U_FAILURE(status) || norm == nullptr) {
        return;
    }

    UChar outBuf[512];
    status = U_ZERO_ERROR;
    int32_t written = unorm2_normalize(norm, str, len, outBuf,
                                        sizeof(outBuf) / sizeof(UChar),
                                        &status);
    (void)written;

    status = U_ZERO_ERROR;
    UBool isNormalized = unorm2_isNormalized(norm, str, len, &status);
    (void)isNormalized;
}

static void fuzzBreakIterator(const UChar *str, int32_t len,
                               const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UBreakIterator *bi = ubrk_open(UBRK_WORD, locale, str, len, &status);
    if (U_FAILURE(status) || bi == nullptr) {
        if (bi) ubrk_close(bi);
        return;
    }

    int32_t pos = ubrk_first(bi);
    int count = 0;
    while (pos != UBRK_DONE && count < 100) {
        pos = ubrk_next(bi);
        count++;
    }

    ubrk_close(bi);
}

static void fuzzLocale(const char *localeStr) {
    UErrorCode status = U_ZERO_ERROR;
    UChar displayName[256];
    int32_t len = uloc_getDisplayName(localeStr, nullptr, displayName,
                                       sizeof(displayName) / sizeof(UChar),
                                       &status);
    (void)len;

    status = U_ZERO_ERROR;
    char language[32];
    uloc_getLanguage(localeStr, language, sizeof(language), &status);

    status = U_ZERO_ERROR;
    char country[32];
    uloc_getCountry(localeStr, country, sizeof(country), &status);

    status = U_ZERO_ERROR;
    char canonical[256];
    uloc_canonicalize(localeStr, canonical, sizeof(canonical), &status);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size < 4) {
        return 0;
    }

    size_t halfSize = Size / 2;
    const uint8_t *firstHalf = Data;
    const uint8_t *secondHalf = Data + halfSize;
    size_t secondLen = Size - halfSize;

    size_t uLen1 = halfSize / sizeof(UChar);
    size_t uLen2 = secondLen / sizeof(UChar);

    std::vector<UChar> buf1(uLen1 > 0 ? uLen1 : 1, 0);
    std::vector<UChar> buf2(uLen2 > 0 ? uLen2 : 1, 0);

    if (uLen1 > 0) {
        memcpy(buf1.data(), firstHalf, uLen1 * sizeof(UChar));
    }
    if (uLen2 > 0) {
        memcpy(buf2.data(), secondHalf, uLen2 * sizeof(UChar));
    }

    static const char *localeCandidates[] = {
        "en_US", "fr_FR", "de_DE", "ja_JP", "zh_CN", "ar_SA", "ru_RU", "",
    };
    uint8_t localeIndex = Data[0] % 8;
    const char *locale = localeCandidates[localeIndex];

    fuzzLocale(locale);

    fuzzCollator(buf1.data(), static_cast<int32_t>(uLen1),
                 buf2.data(), static_cast<int32_t>(uLen2), locale);

    fuzzNumberFormat(nullptr, 0, buf2.data(), static_cast<int32_t>(uLen2),
                      locale);

    int32_t patLen = static_cast<int32_t>(uLen1 > 32 ? 32 : uLen1);
    if (patLen > 0) {
        fuzzNumberFormat(buf1.data(), patLen, buf2.data(),
                          static_cast<int32_t>(uLen2), locale);
    }

    fuzzDateFormat(nullptr, 0, buf2.data(), static_cast<int32_t>(uLen2),
                    locale);
    if (patLen > 0) {
        fuzzDateFormat(buf1.data(), patLen, buf2.data(),
                        static_cast<int32_t>(uLen2), locale);
    }

    fuzzConverter(Data, Size);

    fuzzNormalizer(buf1.data(), static_cast<int32_t>(uLen1));
    fuzzNormalizer(buf2.data(), static_cast<int32_t>(uLen2));

    fuzzBreakIterator(buf1.data(), static_cast<int32_t>(uLen1), locale);
    fuzzBreakIterator(buf2.data(), static_cast<int32_t>(uLen2), locale);

    return 0;
}