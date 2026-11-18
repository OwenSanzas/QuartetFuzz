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
#include <unicode/ucnv.h>
#include <unicode/ucol.h>
#include <unicode/unum.h>
#include <unicode/udat.h>
#include <unicode/ubrk.h>
#include <unicode/unorm2.h>
#include <unicode/uloc.h>
#include <unicode/ucal.h>

static void BuildUCharBuffer(const uint8_t *data, size_t size,
                              std::vector<UChar> &out) {
    size_t numUnits = size / 2;
    out.resize(numUnits);
    for (size_t i = 0; i < numUnits; ++i) {
        UChar c = static_cast<UChar>(data[2 * i]) |
                  (static_cast<UChar>(data[2 * i + 1]) << 8);
        out[i] = c;
    }
}

static void FuzzConverter(const uint8_t *data, size_t size) {
    UErrorCode status = U_ZERO_ERROR;
    const char *converterNames[] = {"UTF-8", "ISO-8859-1", "UTF-16", "UTF-32"};
    size_t idx = size > 0 ? data[0] % 4 : 0;

    UConverter *conv = ucnv_open(converterNames[idx], &status);
    if (U_FAILURE(status) || conv == nullptr) {
        return;
    }

    std::vector<UChar> target(size + 1);
    UChar *targetStart = target.data();
    const char *sourceStart = reinterpret_cast<const char *>(data);
    const char *sourceLimit = sourceStart + size;
    UChar *targetLimit = targetStart + target.size();

    status = U_ZERO_ERROR;
    ucnv_toUnicode(conv, &targetStart, targetLimit, &sourceStart, sourceLimit,
                    nullptr, (UBool)true, &status);

    std::vector<char> back(size * 4 + 8);
    char *backStart = back.data();
    char *backLimit = backStart + back.size();
    const UChar *srcU = target.data();
    const UChar *srcULimit = targetStart;

    status = U_ZERO_ERROR;
    ucnv_fromUnicode(conv, &backStart, backLimit, &srcU, srcULimit, nullptr,
                       (UBool)true, &status);

    ucnv_close(conv);
}

static void FuzzCollator(const std::vector<UChar> &buf, const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UCollator *coll = ucol_open(locale, &status);
    if (U_FAILURE(status) || coll == nullptr) {
        return;
    }

    if (buf.size() >= 2) {
        size_t half = buf.size() / 2;
        UCollationResult result = ucol_strcoll(coll, buf.data(), half,
                                                buf.data() + half,
                                                buf.size() - half);
        (void)result;
    }

    if (!buf.empty()) {
        std::vector<uint8_t> sortKey(buf.size() * 4 + 16);
        int32_t keyLen = ucol_getSortKey(coll, buf.data(),
                                          static_cast<int32_t>(buf.size()),
                                          sortKey.data(),
                                          static_cast<int32_t>(sortKey.size()));
        (void)keyLen;
    }

    ucol_close(coll);
}

static void FuzzNumberFormat(const std::vector<UChar> &buf, const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UNumberFormat *nf = unum_open(UNUM_DECIMAL, nullptr, 0, locale, nullptr,
                                   &status);
    if (U_FAILURE(status) || nf == nullptr) {
        return;
    }

    if (!buf.empty()) {
        status = U_ZERO_ERROR;
        double parsed = unum_parseDouble(nf, buf.data(),
                                          static_cast<int32_t>(buf.size()),
                                          nullptr, &status);
        if (U_SUCCESS(status)) {
            UChar outBuf[64];
            status = U_ZERO_ERROR;
            int32_t len = unum_formatDouble(nf, parsed, outBuf, 64, nullptr,
                                             &status);
            (void)len;
        }
    }

    unum_close(nf);
}

static void FuzzDateFormat(const std::vector<UChar> &buf, const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UDateFormat *df = udat_open(UDAT_SHORT, UDAT_SHORT, locale, nullptr, -1,
                                 nullptr, 0, &status);
    if (U_FAILURE(status) || df == nullptr) {
        return;
    }

    if (!buf.empty()) {
        status = U_ZERO_ERROR;
        int32_t parsePos = 0;
        UDate date = udat_parse(df, buf.data(),
                                 static_cast<int32_t>(buf.size()), &parsePos,
                                 &status);
        if (U_SUCCESS(status)) {
            UChar outBuf[128];
            status = U_ZERO_ERROR;
            int32_t len = udat_format(df, date, outBuf, 128, nullptr,
                                       &status);
            (void)len;
        }
    }

    udat_close(df);
}

static void FuzzBreakIterator(const std::vector<UChar> &buf, const char *locale) {
    UErrorCode status = U_ZERO_ERROR;
    UBreakIterator *bi = ubrk_open(UBRK_WORD, locale, buf.data(),
                                    static_cast<int32_t>(buf.size()), &status);
    if (U_FAILURE(status) || bi == nullptr) {
        return;
    }

    int32_t pos = ubrk_first(bi);
    int iterations = 0;
    while (pos != UBRK_DONE && iterations < 1000) {
        pos = ubrk_next(bi);
        iterations++;
    }

    ubrk_close(bi);
}

static void FuzzNormalizer(const std::vector<UChar> &buf) {
    UErrorCode status = U_ZERO_ERROR;
    const UNormalizer2 *norm = unorm2_getNFCInstance(&status);
    if (U_FAILURE(status) || norm == nullptr) {
        return;
    }

    if (!buf.empty()) {
        std::vector<UChar> dest(buf.size() * 2 + 16);
        status = U_ZERO_ERROR;
        int32_t len = unorm2_normalize(norm, buf.data(),
                                        static_cast<int32_t>(buf.size()),
                                        dest.data(),
                                        static_cast<int32_t>(dest.size()),
                                        &status);
        (void)len;
    }
}

static void FuzzLocale(const uint8_t *data, size_t size) {
    if (size == 0) {
        return;
    }

    std::string localeStr(reinterpret_cast<const char *>(data),
                           size < 32 ? size : 32);

    for (auto &c : localeStr) {
        if (c == '\0') {
            c = '_';
        }
    }

    UErrorCode status = U_ZERO_ERROR;
    char canonical[64];
    int32_t len = uloc_canonicalize(localeStr.c_str(), canonical,
                                     sizeof(canonical), &status);
    (void)len;

    status = U_ZERO_ERROR;
    char lang[32];
    len = uloc_getLanguage(localeStr.c_str(), lang, sizeof(lang), &status);
    (void)len;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    std::vector<UChar> ubuf;
    BuildUCharBuffer(Data, Size, ubuf);

    std::string localeCandidate;
    if (Size > 4) {
        size_t localeLen = Data[0] % 16;
        if (localeLen > Size - 1) {
            localeLen = Size - 1;
        }
        localeCandidate.assign(reinterpret_cast<const char *>(Data + 1),
                                localeLen);
        for (auto &c : localeCandidate) {
            if (c == '\0') {
                c = '_';
            }
        }
    }
    const char *locale = localeCandidate.empty() ? "en_US"
                                                   : localeCandidate.c_str();

    FuzzConverter(Data, Size);
    FuzzCollator(ubuf, locale);
    FuzzNumberFormat(ubuf, locale);
    FuzzDateFormat(ubuf, locale);
    FuzzBreakIterator(ubuf, locale);
    FuzzNormalizer(ubuf);
    FuzzLocale(Data, Size);

    return 0;
}