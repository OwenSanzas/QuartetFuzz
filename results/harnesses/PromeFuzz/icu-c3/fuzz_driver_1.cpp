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
#include <cstdio>
#include <vector>
#include <string>

#include <unicode/utypes.h>
#include <unicode/ustring.h>
#include <unicode/unorm2.h>
#include <unicode/ucol.h>
#include <unicode/ucnv.h>
#include <unicode/uregex.h>
#include <unicode/udat.h>
#include <unicode/ucal.h>
#include <unicode/ulocdata.h>
#include <unicode/unum.h>
#include <unicode/udatpg.h>

static int32_t buildUCharFromUTF8(const uint8_t *data, size_t size,
                                   std::vector<UChar> &outBuf) {
    if (size == 0) {
        outBuf.clear();
        return 0;
    }

    UErrorCode status = U_ZERO_ERROR;
    int32_t destLen = 0;

    u_strFromUTF8(nullptr, 0, &destLen,
                  reinterpret_cast<const char *>(data),
                  static_cast<int32_t>(size), &status);

    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
        outBuf.clear();
        return 0;
    }

    if (destLen <= 0) {
        outBuf.clear();
        return 0;
    }

    outBuf.resize(destLen + 1);
    status = U_ZERO_ERROR;
    u_strFromUTF8(outBuf.data(), destLen + 1, &destLen,
                  reinterpret_cast<const char *>(data),
                  static_cast<int32_t>(size), &status);

    if (U_FAILURE(status)) {
        outBuf.clear();
        return 0;
    }

    return destLen;
}

static void fuzzNormalization(const UChar *str, int32_t len) {
    if (len <= 0) return;

    UErrorCode status = U_ZERO_ERROR;
    const UNormalizer2 *nfc = unorm2_getNFCInstance(&status);
    if (U_FAILURE(status) || nfc == nullptr) return;

    UChar destBuf[512];
    status = U_ZERO_ERROR;
    int32_t destLen = unorm2_normalize(nfc, str, len, destBuf,
                                        sizeof(destBuf) / sizeof(UChar),
                                        &status);
    (void)destLen;

    status = U_ZERO_ERROR;
    UBool isNorm = unorm2_isNormalized(nfc, str, len, &status);
    (void)isNorm;

    status = U_ZERO_ERROR;
    const UNormalizer2 *nfd = unorm2_getNFDInstance(&status);
    if (U_SUCCESS(status) && nfd != nullptr) {
        UChar destBuf2[512];
        status = U_ZERO_ERROR;
        unorm2_normalize(nfd, str, len, destBuf2,
                          sizeof(destBuf2) / sizeof(UChar), &status);
    }
}

static void fuzzCollation(const UChar *str, int32_t len) {
    if (len <= 0) return;

    UErrorCode status = U_ZERO_ERROR;
    UCollator *coll = ucol_open("en_US", &status);
    if (U_FAILURE(status) || coll == nullptr) {
        if (coll) ucol_close(coll);
        return;
    }

    int32_t half = len / 2;
    if (half < 1) half = 1;

    UCollationResult result = ucol_strcoll(coll, str, half, str + half,
                                            len - half);
    (void)result;

    uint8_t sortKeyBuf[1024];
    int32_t keyLen = ucol_getSortKey(coll, str, len, sortKeyBuf,
                                      sizeof(sortKeyBuf));
    (void)keyLen;

    ucol_close(coll);
}

static void fuzzConverter(const uint8_t *data, size_t size) {
    if (size == 0) return;

    UErrorCode status = U_ZERO_ERROR;
    UConverter *conv = ucnv_open("UTF-8", &status);
    if (U_FAILURE(status) || conv == nullptr) {
        if (conv) ucnv_close(conv);
        return;
    }

    UChar uBuf[1024];
    status = U_ZERO_ERROR;
    int32_t uLen = ucnv_toUChars(conv, uBuf, 1024,
                                 reinterpret_cast<const char *>(data),
                                 static_cast<int32_t>(size), &status);
    (void)uLen;

    ucnv_close(conv);
}

static void fuzzRegex(const UChar *str, int32_t len) {
    if (len <= 1) return;

    UErrorCode status = U_ZERO_ERROR;
    int32_t patLen = len / 2;
    if (patLen < 1) patLen = 1;

    UParseError parseError;
    URegularExpression *regex = uregex_open(str, patLen, 0, &parseError,
                                             &status);
    if (U_FAILURE(status) || regex == nullptr) {
        if (regex) uregex_close(regex);
        return;
    }

    status = U_ZERO_ERROR;
    uregex_setText(regex, str + patLen, len - patLen, &status);
    if (U_SUCCESS(status)) {
        status = U_ZERO_ERROR;
        UBool found = uregex_find(regex, 0, &status);
        (void)found;
    }

    uregex_close(regex);
}

static void fuzzDateFormat(const UChar *str, int32_t len) {
    if (len <= 0) return;

    UErrorCode status = U_ZERO_ERROR;
    UDateFormat *fmt = udat_open(UDAT_SHORT, UDAT_SHORT, "en_US", nullptr, 0,
                                  nullptr, 0, &status);
    if (U_FAILURE(status) || fmt == nullptr) {
        if (fmt) udat_close(fmt);
        return;
    }

    int32_t parsePos = 0;
    status = U_ZERO_ERROR;
    UDate date = udat_parse(fmt, str, len, &parsePos, &status);
    (void)date;

    UChar outBuf[256];
    status = U_ZERO_ERROR;
    int32_t outLen = udat_format(fmt, 0.0, outBuf, 256, nullptr, &status);
    (void)outLen;

    udat_close(fmt);
}

static void fuzzNumberFormat(const UChar *str, int32_t len) {
    if (len <= 0) return;

    UErrorCode status = U_ZERO_ERROR;
    UNumberFormat *nf = unum_open(UNUM_DECIMAL, nullptr, 0, "en_US", nullptr,
                                   &status);
    if (U_FAILURE(status) || nf == nullptr) {
        if (nf) unum_close(nf);
        return;
    }

    int32_t parsePos = 0;
    status = U_ZERO_ERROR;
    double val = unum_parseDouble(nf, str, len, &parsePos, &status);
    (void)val;

    UChar outBuf[256];
    status = U_ZERO_ERROR;
    int32_t outLen = unum_format(nf, 12345, outBuf, 256, nullptr, &status);
    (void)outLen;

    unum_close(nf);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Data == nullptr || Size == 0) {
        return 0;
    }

    FILE *f = fopen("./dummy_file", "wb");
    if (f != nullptr) {
        fwrite(Data, 1, Size, f);
        fclose(f);
    }

    std::vector<UChar> uBuf;
    int32_t uLen = buildUCharFromUTF8(Data, Size, uBuf);

    if (uLen > 0 && !uBuf.empty()) {
        fuzzNormalization(uBuf.data(), uLen);
        fuzzCollation(uBuf.data(), uLen);
        fuzzRegex(uBuf.data(), uLen);
        fuzzDateFormat(uBuf.data(), uLen);
        fuzzNumberFormat(uBuf.data(), uLen);
    }

    fuzzConverter(Data, Size);

    return 0;
}