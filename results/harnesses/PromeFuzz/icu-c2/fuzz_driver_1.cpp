#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <fstream>

#include <unicode/utypes.h>
#include <unicode/ustring.h>
#include <unicode/unistr.h>
#include <unicode/ucol.h>
#include <unicode/unum.h>
#include <unicode/udat.h>
#include <unicode/uregex.h>
#include <unicode/ucnv.h>
#include <unicode/ucal.h>
#include <unicode/ulocdata.h>
#include <unicode/uloc.h>
#include <unicode/ubrk.h>
#include <unicode/unorm2.h>

static std::vector<UChar> BytesToUChar(const uint8_t *data, size_t size) {
    std::vector<UChar> out;
    if (size == 0) {
        out.push_back(0);
        return out;
    }

    size_t n = size / sizeof(UChar);
    size_t rem = size % sizeof(UChar);

    out.resize(n + (rem ? 1 : 0) + 1, 0);

    if (n > 0) {
        std::memcpy(out.data(), data, n * sizeof(UChar));
    }

    if (rem > 0) {

        std::memcpy(reinterpret_cast<uint8_t *>(&out[n]), data + n * sizeof(UChar), rem);
    }

    return out;
}

static void FuzzCollator(const UChar *str1, int32_t len1,
                          const UChar *str2, int32_t len2,
                          const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UCollator *coll = ucol_open(locale, &status);
    if (U_FAILURE(status) || coll == nullptr) {
        if (coll) ucol_close(coll);
        return;
    }
    ucol_setStrength(coll, UCOL_TERTIARY);
    ucol_strcoll(coll, str1, len1, str2, len2);

    UErrorCode attrStatus = U_ZERO_ERROR;
    ucol_setAttribute(coll, UCOL_CASE_FIRST, UCOL_UPPER_FIRST, &attrStatus);

    uint8_t sortKeyBuf[256];
    ucol_getSortKey(coll, str1, len1, sortKeyBuf, sizeof(sortKeyBuf));

    ucol_close(coll);
}

static void FuzzNumberFormat(const UChar *pattern, int32_t patternLen,
                              double value, const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UNumberFormat *fmt = unum_open(UNUM_PATTERN_DECIMAL, pattern, patternLen,
                                    locale, nullptr, &status);
    if (U_FAILURE(status) || fmt == nullptr) {
        if (fmt) unum_close(fmt);

        UErrorCode status2 = U_ZERO_ERROR;
        UNumberFormat *defFmt = unum_open(UNUM_DECIMAL, nullptr, 0, locale, nullptr, &status2);
        if (U_SUCCESS(status2) && defFmt) {
            UChar outBuf[128];
            unum_formatDouble(defFmt, value, outBuf, 128, nullptr, &status2);
            unum_close(defFmt);
        }
        return;
    }

    UChar outBuf[256];
    UErrorCode fmtStatus = U_ZERO_ERROR;
    unum_formatDouble(fmt, value, outBuf, 256, nullptr, &fmtStatus);

    UErrorCode parseStatus = U_ZERO_ERROR;
    int32_t parsePos = 0;
    unum_parseDouble(fmt, outBuf, u_strlen(outBuf), &parsePos, &parseStatus);

    unum_close(fmt);
}

static void FuzzDateFormat(const UChar *pattern, int32_t patternLen,
                            double dateValue, const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UDateFormat *df = udat_open(UDAT_PATTERN, UDAT_PATTERN, locale, nullptr, 0,
                                 pattern, patternLen, &status);
    if (U_FAILURE(status) || df == nullptr) {
        if (df) udat_close(df);
        return;
    }

    UChar outBuf[256];
    UErrorCode fmtStatus = U_ZERO_ERROR;
    udat_format(df, dateValue, outBuf, 256, nullptr, &fmtStatus);

    UErrorCode parseStatus = U_ZERO_ERROR;
    int32_t parsePos = 0;
    udat_parse(df, outBuf, u_strlen(outBuf), &parsePos, &parseStatus);

    udat_close(df);
}

static void FuzzRegex(const UChar *pattern, int32_t patternLen,
                       const UChar *text, int32_t textLen) {
    UErrorCode status = U_ZERO_ERROR;
    UParseError parseError;
    URegularExpression *re = uregex_open(pattern, patternLen, 0, &parseError, &status);
    if (U_FAILURE(status) || re == nullptr) {
        if (re) uregex_close(re);
        return;
    }

    UErrorCode setStatus = U_ZERO_ERROR;
    uregex_setText(re, text, textLen, &setStatus);
    if (U_SUCCESS(setStatus)) {
        UErrorCode findStatus = U_ZERO_ERROR;
        UBool found = uregex_find(re, 0, &findStatus);
        if (found && U_SUCCESS(findStatus)) {
            UErrorCode startStatus = U_ZERO_ERROR;
            uregex_start(re, 0, &startStatus);
            UErrorCode endStatus = U_ZERO_ERROR;
            uregex_end(re, 0, &endStatus);
        }

        UErrorCode replaceStatus = U_ZERO_ERROR;
        UChar replBuf[128];
        UChar replacement[] = {u'X', 0};
        uregex_replaceAll(re, replacement, 1, replBuf, 128, &replaceStatus);
    }

    uregex_close(re);
}

static void FuzzConverter(const uint8_t *data, size_t size) {
    UErrorCode status = U_ZERO_ERROR;
    UConverter *conv = ucnv_open("UTF-8", &status);
    if (U_FAILURE(status) || conv == nullptr) {
        if (conv) ucnv_close(conv);
        return;
    }

    std::vector<UChar> target(size + 1, 0);
    UErrorCode convStatus = U_ZERO_ERROR;
    int32_t len = ucnv_toUChars(conv, target.data(), (int32_t)target.size(),
                                reinterpret_cast<const char *>(data),
                                (int32_t)size, &convStatus);

    if (U_SUCCESS(convStatus) && len > 0) {
        std::vector<char> backBuf(size * 4 + 16, 0);
        UErrorCode fromStatus = U_ZERO_ERROR;
        ucnv_fromUChars(conv, backBuf.data(), (int32_t)backBuf.size(),
                         target.data(), len, &fromStatus);
    }

    ucnv_close(conv);
}

static void FuzzBreakIterator(const UChar *text, int32_t textLen, const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UBreakIterator *bi = ubrk_open(UBRK_WORD, locale, text, textLen, &status);
    if (U_FAILURE(status) || bi == nullptr) {
        if (bi) ubrk_close(bi);
        return;
    }

    int32_t pos = ubrk_first(bi);
    while (pos != UBRK_DONE) {
        pos = ubrk_next(bi);
    }

    ubrk_close(bi);
}

static void FuzzNormalizer(const UChar *text, int32_t textLen) {
    UErrorCode status = U_ZERO_ERROR;
    const UNormalizer2 *norm = unorm2_getNFCInstance(&status);
    if (U_FAILURE(status) || norm == nullptr) {
        return;
    }

    UChar outBuf[512];
    UErrorCode normStatus = U_ZERO_ERROR;
    unorm2_normalize(norm, text, textLen, outBuf, 512, &normStatus);

    UErrorCode checkStatus = U_ZERO_ERROR;
    unorm2_isNormalized(norm, text, textLen, &checkStatus);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    {
        std::ofstream ofs("./dummy_file", std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            ofs.write(reinterpret_cast<const char *>(Data), Size);
            ofs.close();
        }
    }

    size_t half = Size / 2;
    std::vector<UChar> str1 = BytesToUChar(Data, half);
    std::vector<UChar> str2 = BytesToUChar(Data + half, Size - half);

    int32_t len1 = (int32_t)(str1.size() > 0 ? str1.size() - 1 : 0);
    int32_t len2 = (int32_t)(str2.size() > 0 ? str2.size() - 1 : 0);

    const char *locales[] = {"en_US", "fr_FR", "ja_JP", "de_DE", "ar_SA", "zh_CN"};
    const char *locale = locales[Size % 6];

    FuzzCollator(str1.data(), len1, str2.data(), len2, locale);

    double numericValue = 0.0;
    if (Size >= sizeof(double)) {
        std::memcpy(&numericValue, Data, sizeof(double));
    } else {
        numericValue = (double)Size;
    }
    FuzzNumberFormat(str1.data(), len1, numericValue, locale);

    double dateValue = 0.0;
    if (Size >= sizeof(double) * 2) {
        std::memcpy(&dateValue, Data + sizeof(double), sizeof(double));
    } else {
        dateValue = (double)(Size * 1000);
    }
    FuzzDateFormat(str2.data(), len2, dateValue, locale);

    FuzzRegex(str1.data(), len1, str2.data(), len2);

    FuzzConverter(Data, Size);

    FuzzBreakIterator(str1.data(), len1, locale);

    FuzzNormalizer(str2.data(), len2);

    return 0;
}