#include <cstring>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string>

#include "unicode/normalizer2.h"
#include "unicode/unistr.h"
#include "unicode/utypes.h"

using icu::Normalizer2;
using icu::UnicodeString;

static const Normalizer2 *selectInstance(uint8_t selector, UErrorCode &status) {
    switch (selector % 9) {
        case 0:
            return Normalizer2::getNFCInstance(status);
        case 1:
            return Normalizer2::getNFDInstance(status);
        case 2:
            return Normalizer2::getNFKCInstance(status);
        case 3:
            return Normalizer2::getNFKDInstance(status);
        case 4:
            return Normalizer2::getNFKCCasefoldInstance(status);
        case 5:
            return Normalizer2::getNFKCSimpleCasefoldInstance(status);
        case 6:
            return Normalizer2::getInstance(nullptr, "nfkc", UNORM2_COMPOSE_CONTIGUOUS, status);
        case 7:
            return Normalizer2::getInstance(nullptr, "nfc", UNORM2_COMPOSE_CONTIGUOUS, status);
        case 8:
        default:
            return Normalizer2::getInstance(nullptr, "nfc", UNORM2_FCD, status);
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) {
        return 0;
    }

    uint8_t selector = data[0];
    uint8_t splitByte = data[1];
    data += 2;
    size -= 2;

    size_t numUnits = size / 2;
    std::unique_ptr<char16_t[]> unitsBuf(new char16_t[numUnits > 0 ? numUnits : 1]);
    if (numUnits > 0) {
        std::memcpy(unitsBuf.get(), data, numUnits * sizeof(char16_t));
    }

    UnicodeString src(false  ,
                       unitsBuf.get(),
                       static_cast<int32_t>(numUnits));

    UErrorCode status = U_ZERO_ERROR;
    const Normalizer2 *n2 = selectInstance(selector, status);
    if (U_FAILURE(status) || n2 == nullptr) {
        return 0;
    }

    UErrorCode normStatus = U_ZERO_ERROR;
    UnicodeString normalized = n2->normalize(src, normStatus);
    if (U_SUCCESS(normStatus)) {

        UErrorCode qcStatus = U_ZERO_ERROR;
        (void)n2->isNormalized(normalized, qcStatus);

        UErrorCode spanStatus = U_ZERO_ERROR;
        int32_t spanEnd = n2->spanQuickCheckYes(normalized, spanStatus);
        (void)spanEnd;

        int32_t len = normalized.length();
        int32_t i = 0;
        UChar32 prev = 0;
        bool havePrev = false;
        while (i < len) {
            UChar32 c = normalized.char32At(i);
            UnicodeString decomposition;
            (void)n2->getDecomposition(c, decomposition);
            (void)n2->hasBoundaryBefore(c);
            (void)n2->hasBoundaryAfter(c);
            (void)n2->isInert(c);
            (void)n2->getCombiningClass(c);
            if (havePrev) {

                (void)n2->composePair(prev, c);
            }
            prev = c;
            havePrev = true;
            i += U16_LENGTH(c);
        }

        std::string utf8;
        normalized.toUTF8String(utf8);
    }

    UErrorCode destStatus = U_ZERO_ERROR;
    UnicodeString dest;
    n2->normalize(src, dest, destStatus);

    UErrorCode qc2Status = U_ZERO_ERROR;
    (void)n2->quickCheck(src, qc2Status);

    if (numUnits >= 2) {
        int32_t splitAt = static_cast<int32_t>(splitByte % numUnits);
        UnicodeString firstPart(src, 0, splitAt);
        UnicodeString secondPart(src, splitAt);

        UErrorCode firstNormStatus = U_ZERO_ERROR;
        UnicodeString first = n2->normalize(firstPart, firstNormStatus);
        if (U_SUCCESS(firstNormStatus)) {
            UErrorCode appendStatus = U_ZERO_ERROR;
            if (selector & 1) {
                n2->normalizeSecondAndAppend(first, secondPart, appendStatus);
            } else {
                n2->append(first, secondPart, appendStatus);
            }
            if (U_SUCCESS(appendStatus)) {
                std::string utf8two;
                first.toUTF8String(utf8two);
            }
        }
    }

    return 0;
}
