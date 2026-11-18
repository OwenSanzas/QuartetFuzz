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
#include <cstring>
#include <string>
#include <fstream>

#include "unicode/unistr.h"
#include "unicode/ustring.h"
#include "unicode/ucnv.h"
#include "unicode/ucol.h"
#include "unicode/unum.h"
#include "unicode/udat.h"
#include "unicode/uloc.h"
#include "unicode/translit.h"
#include "unicode/normalizer2.h"
#include "unicode/regex.h"
#include "unicode/numfmt.h"
#include "unicode/datefmt.h"
#include "unicode/calendar.h"
#include "unicode/locid.h"
#include "unicode/coll.h"
#include "unicode/uenum.h"

using namespace icu;

static void testUnicodeStringOps(const uint8_t *data, size_t size) {
    if (size == 0) return;

    std::string input(reinterpret_cast<const char*>(data), size);
    UnicodeString ustr = UnicodeString::fromUTF8(input);

    UnicodeString upper(ustr);
    upper.toUpper();

    UnicodeString lower(ustr);
    lower.toLower();

    UnicodeString reversed(ustr);
    reversed.reverse();

    if (ustr.length() > 1) {
        UnicodeString sub = ustr.tempSubString(0, ustr.length() / 2);
        (void)sub;
    }

    std::string out;
    ustr.toUTF8String(out);
}

static void testConverter(const uint8_t *data, size_t size) {
    UErrorCode status = U_ZERO_ERROR;
    UConverter *conv = ucnv_open("UTF-8", &status);
    if (U_FAILURE(status) || conv == nullptr) {
        if (conv) ucnv_close(conv);
        return;
    }

    UChar buffer[512];
    int32_t len = ucnv_toUChars(conv, buffer, 512,
                                 reinterpret_cast<const char*>(data), (int32_t)size, &status);
    if (U_SUCCESS(status) && len > 0) {
        char outBuf[1024];
        UErrorCode status2 = U_ZERO_ERROR;
        ucnv_fromUChars(conv, outBuf, 1024, buffer, len, &status2);
    }

    ucnv_close(conv);
}

static void testCollator(const uint8_t *data, size_t size) {
    if (size < 2) return;

    UErrorCode status = U_ZERO_ERROR;
    UCollator *coll = ucol_open("en_US", &status);
    if (U_FAILURE(status) || coll == nullptr) {
        if (coll) ucol_close(coll);
        return;
    }

    size_t half = size / 2;
    std::string s1(reinterpret_cast<const char*>(data), half);
    std::string s2(reinterpret_cast<const char*>(data + half), size - half);

    UnicodeString u1 = UnicodeString::fromUTF8(s1);
    UnicodeString u2 = UnicodeString::fromUTF8(s2);

    UCollationResult result = ucol_strcoll(coll,
                                            u1.getBuffer(), u1.length(),
                                            u2.getBuffer(), u2.length());
    (void)result;

    ucol_close(coll);
}

static void testNumberFormat(const uint8_t *data, size_t size) {
    if (size == 0) return;

    UErrorCode status = U_ZERO_ERROR;
    UNumberFormat *nf = unum_open(UNUM_DECIMAL, nullptr, 0, "en_US", nullptr, &status);
    if (U_FAILURE(status) || nf == nullptr) {
        if (nf) unum_close(nf);
        return;
    }

    std::string input(reinterpret_cast<const char*>(data), size);
    UnicodeString ustr = UnicodeString::fromUTF8(input);

    UErrorCode parseStatus = U_ZERO_ERROR;
    int32_t parsePos = 0;
    double value = unum_parseDouble(nf, ustr.getBuffer(), ustr.length(), &parsePos, &parseStatus);
    (void)value;

    UChar outBuf[256];
    UErrorCode formatStatus = U_ZERO_ERROR;
    unum_formatDouble(nf, 12345.6789, outBuf, 256, nullptr, &formatStatus);

    unum_close(nf);
}

static void testDateFormat(const uint8_t *data, size_t size) {
    if (size == 0) return;

    UErrorCode status = U_ZERO_ERROR;
    UDateFormat *df = udat_open(UDAT_SHORT, UDAT_SHORT, "en_US", nullptr, 0, nullptr, 0, &status);
    if (U_FAILURE(status) || df == nullptr) {
        if (df) udat_close(df);
        return;
    }

    std::string input(reinterpret_cast<const char*>(data), size);
    UnicodeString ustr = UnicodeString::fromUTF8(input);

    UErrorCode parseStatus = U_ZERO_ERROR;
    int32_t parsePos = 0;
    UDate date = udat_parse(df, ustr.getBuffer(), ustr.length(), &parsePos, &parseStatus);
    (void)date;

    UChar outBuf[256];
    UErrorCode formatStatus = U_ZERO_ERROR;
    udat_format(df, 1234567890.0, outBuf, 256, nullptr, &formatStatus);

    udat_close(df);
}

static void testRegex(const uint8_t *data, size_t size) {
    if (size < 2) return;

    size_t half = size / 2;
    std::string patternStr(reinterpret_cast<const char*>(data), half);
    std::string inputStr(reinterpret_cast<const char*>(data + half), size - half);

    UnicodeString pattern = UnicodeString::fromUTF8(patternStr);
    UnicodeString input = UnicodeString::fromUTF8(inputStr);

    UErrorCode status = U_ZERO_ERROR;
    RegexMatcher *matcher = new RegexMatcher(pattern, 0, status);
    if (U_SUCCESS(status) && matcher != nullptr) {
        matcher->reset(input);
        UErrorCode findStatus = U_ZERO_ERROR;
        UBool found = matcher->find(findStatus);
        (void)found;
    }
    delete matcher;
}

static void testNormalizer(const uint8_t *data, size_t size) {
    if (size == 0) return;

    std::string input(reinterpret_cast<const char*>(data), size);
    UnicodeString ustr = UnicodeString::fromUTF8(input);

    UErrorCode status = U_ZERO_ERROR;
    const Normalizer2 *norm = Normalizer2::getNFCInstance(status);
    if (U_SUCCESS(status) && norm != nullptr) {
        UnicodeString result = norm->normalize(ustr, status);
        (void)result;
    }
}

static void testTransliterator(const uint8_t *data, size_t size) {
    if (size == 0) return;

    std::string input(reinterpret_cast<const char*>(data), size);
    UnicodeString ustr = UnicodeString::fromUTF8(input);

    UErrorCode status = U_ZERO_ERROR;
    Transliterator *trans = Transliterator::createInstance(
        "Latin-ASCII", UTRANS_FORWARD, status);
    if (U_SUCCESS(status) && trans != nullptr) {
        trans->transliterate(ustr);
    }
    delete trans;
}

static void testLocale(const uint8_t *data, size_t size) {
    if (size == 0) return;

    std::string localeStr(reinterpret_cast<const char*>(data), size);

    if (localeStr.size() > 64) {
        localeStr.resize(64);
    }

    Locale loc(localeStr.c_str());
    const char *lang = loc.getLanguage();
    (void)lang;

    UErrorCode status = U_ZERO_ERROR;
    char buffer[256];
    uloc_canonicalize(localeStr.c_str(), buffer, 256, &status);
}

static void writeDummyFile(const uint8_t *data, size_t size) {
    std::ofstream ofs("./dummy_file", std::ios::binary);
    if (ofs.is_open()) {
        ofs.write(reinterpret_cast<const char*>(data), size);
        ofs.close();
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    writeDummyFile(Data, Size);

    testUnicodeStringOps(Data, Size);
    testConverter(Data, Size);
    testCollator(Data, Size);
    testNumberFormat(Data, Size);
    testDateFormat(Data, Size);
    testRegex(Data, Size);
    testNormalizer(Data, Size);
    testTransliterator(Data, Size);
    testLocale(Data, Size);

    return 0;
}