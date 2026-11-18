#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "unicode/normalizer2.h"
#include "unicode/unistr.h"
#include "unicode/uniset.h"
#include "unicode/unorm2.h"
#include "unicode/utypes.h"

using icu::FilteredNormalizer2;
using icu::Normalizer2;
using icu::UnicodeSet;
using icu::UnicodeString;

#if defined(__clang__)
__attribute__((optnone))
#elif defined(__GNUC__)
__attribute__((optimize("O0")))
#endif
static UnicodeString CallNormalizeOneArgOverload(const Normalizer2 *norm2,
                                                  const UnicodeString &src,
                                                  UErrorCode &errorCode) {

  return norm2->normalize(src, errorCode);
}

static const UChar32 kInterestingSequences[][4] = {
    {0x1100, 0x1161, 0x11A8, 0},
    {0x1100, 0x1161, 0, 0},
    {0xAC00, 0x11A8, 0, 0},
    {0x0061, 0x0301, 0, 0},
    {0x0041, 0x030A, 0, 0},
    {0x0345, 0x0301, 0, 0},
    {0x0344, 0, 0, 0},
    {0x2126, 0, 0, 0},
    {0x212A, 0, 0, 0},
    {0x212B, 0, 0, 0},
    {0x0958, 0, 0, 0},
    {0x0F71, 0x0F72, 0, 0},
    {0x1E0A, 0x0323, 0, 0},
};
static constexpr int kNumInterestingSequences =
    sizeof(kInterestingSequences) / sizeof(kInterestingSequences[0]);

static const UChar32 kMapsToNothing[] = {
    0x00AD, 0x034F, 0x061C, 0x115F, 0x1160, 0x17B4, 0x17B5,
    0x180B, 0x180C, 0x180D, 0x180E, 0x200B, 0x200C, 0x200D,
    0x200E, 0x200F, 0x202A, 0x202B, 0x202C, 0x202D, 0x202E,
    0x2060, 0x2061, 0x2062, 0x2063, 0x2064, 0x206A, 0x206B,
    0x206C, 0x206D, 0x206E, 0x206F, 0xFEFF, 0xFFF0, 0xFFFC,
};
static constexpr int kNumMapsToNothing =
    sizeof(kMapsToNothing) / sizeof(kMapsToNothing[0]);

static const UChar32 kDiscontiguousChains[][4] = {
    {0x0061, 0x0316, 0x0301, 0},
    {0x0065, 0x0327, 0x0301, 0},
    {0x0073, 0x0323, 0x0307, 0},
    {0x006F, 0x0316, 0x0302, 0},
};
static constexpr int kNumDiscontiguousChains =
    sizeof(kDiscontiguousChains) / sizeof(kDiscontiguousChains[0]);

static const UChar32 kRanges[][2] = {
    {0x0300, 0x036F},
    {0x00C0, 0x017F},
    {0x1100, 0x11FF},
    {0xAC00, 0xD7A3},
    {0xF900, 0xFAFF},
    {0xFB00, 0xFDFF},
    {0xFE70, 0xFEFF},
    {0xFF00, 0xFFEF},
    {0x1D100, 0x1D1FF},
    {0x2F800, 0x2FA1D},
    {0x0591, 0x05C7},
    {0x0E31, 0x0E3A},
    {0x0F18, 0x0F39},
    {0x0F71, 0x0F84},
    {0x0900, 0x097F},
    {0xFE00, 0xFE0F},
    {0xE0100, 0xE01EF},
    {0x0000, 0x10FFFF},
};
static constexpr int kNumRanges = sizeof(kRanges) / sizeof(kRanges[0]);

static void AppendRandomRangeChar(FuzzedDataProvider &stream,
                                   UnicodeString &src) {
  if (stream.remaining_bytes() == 0) return;
  const uint8_t rangeIdx =
      stream.ConsumeIntegralInRange<uint8_t>(0, kNumRanges - 1);
  const UChar32 lo = kRanges[rangeIdx][0];
  const UChar32 hi = kRanges[rangeIdx][1];
  const UChar32 cp =
      lo + static_cast<UChar32>(stream.ConsumeIntegralInRange<uint32_t>(
               0, static_cast<uint32_t>(hi - lo)));
  if (cp >= 0xD800 && cp <= 0xDFFF) {
    return;
  }
  src.append(cp);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  const int choice = stream.ConsumeIntegralInRange<int>(0, 17);

  UErrorCode errorCode = U_ZERO_ERROR;
  const Normalizer2 *norm2 = nullptr;

  switch (choice) {
    case 0:
      norm2 = Normalizer2::getNFCInstance(errorCode);
      break;
    case 1:
      norm2 = Normalizer2::getNFDInstance(errorCode);
      break;
    case 2:
      norm2 = Normalizer2::getNFKCInstance(errorCode);
      break;
    case 3:
      norm2 = Normalizer2::getNFKDInstance(errorCode);
      break;
    case 4:
      norm2 = Normalizer2::getNFKCCasefoldInstance(errorCode);
      break;
    case 5:
      norm2 = Normalizer2::getNFKCSimpleCasefoldInstance(errorCode);
      break;
    case 6:
      norm2 = Normalizer2::getInstance(nullptr, "nfc", UNORM2_FCD, errorCode);
      break;
    case 7:
      norm2 = Normalizer2::getInstance(nullptr, "nfkc", UNORM2_FCD, errorCode);
      break;
    case 8:
      norm2 = Normalizer2::getInstance(nullptr, "nfc",
                                        UNORM2_COMPOSE_CONTIGUOUS, errorCode);
      break;
    case 9:
      norm2 = Normalizer2::getInstance(nullptr, "nfkc",
                                        UNORM2_COMPOSE_CONTIGUOUS, errorCode);
      break;
    case 10:

      norm2 =
          Normalizer2::getInstance(nullptr, "uts46", UNORM2_COMPOSE, errorCode);
      break;
    case 11:

      norm2 = Normalizer2::getInstance(nullptr, "uts46", UNORM2_DECOMPOSE,
                                        errorCode);
      break;
    case 12:
      norm2 = Normalizer2::getInstance(nullptr, "nfkc_cf", UNORM2_FCD,
                                        errorCode);
      break;
    case 13:
      norm2 = Normalizer2::getInstance(nullptr, "nfkc_cf",
                                        UNORM2_COMPOSE_CONTIGUOUS, errorCode);
      break;
    case 14:
      norm2 = Normalizer2::getInstance(nullptr, "uts46", UNORM2_FCD,
                                        errorCode);
      break;
    case 15:
      norm2 = Normalizer2::getInstance(nullptr, "uts46",
                                        UNORM2_COMPOSE_CONTIGUOUS, errorCode);
      break;
    case 16:
      norm2 = Normalizer2::getInstance(nullptr, "nfkc_scf", UNORM2_COMPOSE,
                                        errorCode);
      break;
    case 17:
    default:
      norm2 = Normalizer2::getInstance(nullptr, "nfkc_scf", UNORM2_DECOMPOSE,
                                        errorCode);
      break;
  }

  if (U_FAILURE(errorCode) || norm2 == nullptr) {

    return 0;
  }

  const bool useFilter = stream.ConsumeBool();

  std::vector<std::pair<UChar32, UChar32>> filterRanges;
  if (useFilter) {
    const int numRanges = stream.ConsumeIntegralInRange<int>(1, 4);
    for (int i = 0; i < numRanges; i++) {
      UChar32 lo = stream.ConsumeIntegralInRange<UChar32>(0, 0x10FFFF);
      UChar32 hi = stream.ConsumeIntegralInRange<UChar32>(0, 0x10FFFF);
      if (lo > hi) {
        std::swap(lo, hi);
      }
      filterRanges.emplace_back(lo, hi);
    }
  }

  const bool rawMode = stream.ConsumeIntegralInRange<uint8_t>(0, 4) == 0;
  UnicodeString src;

  if (rawMode) {

    std::vector<uint8_t> raw = stream.ConsumeRemainingBytes<uint8_t>();

    size_t num_units = raw.size() / sizeof(char16_t);
    const char16_t *text_ptr = num_units > 0
                                    ? reinterpret_cast<const char16_t *>(raw.data())
                                    : nullptr;

    src = UnicodeString(text_ptr, static_cast<int32_t>(num_units));
  } else {
    const int numChars = stream.ConsumeIntegralInRange<int>(0, 64);
    for (int i = 0; i < numChars && stream.remaining_bytes() > 0; i++) {

      const uint8_t kind = stream.ConsumeIntegralInRange<uint8_t>(0, 9);

      if (kind == 0) {

        if (stream.ConsumeBool()) {
          AppendRandomRangeChar(stream, src);
        }
        const uint8_t seqIdx = stream.ConsumeIntegralInRange<uint8_t>(
            0, static_cast<uint8_t>(kNumInterestingSequences - 1));
        for (int j = 0; j < 4; j++) {
          UChar32 cp = kInterestingSequences[seqIdx][j];
          if (cp == 0) break;
          src.append(cp);
        }

        if (stream.ConsumeBool()) {
          const uint8_t seqIdx2 = stream.ConsumeIntegralInRange<uint8_t>(
              0, static_cast<uint8_t>(kNumInterestingSequences - 1));
          for (int j = 0; j < 4; j++) {
            UChar32 cp = kInterestingSequences[seqIdx2][j];
            if (cp == 0) break;
            src.append(cp);
          }
        }

        if (stream.ConsumeBool()) {
          AppendRandomRangeChar(stream, src);
        }
        continue;
      }

      if (kind == 1) {

        if (stream.ConsumeBool()) {
          AppendRandomRangeChar(stream, src);
        }
        const uint8_t idx = stream.ConsumeIntegralInRange<uint8_t>(
            0, static_cast<uint8_t>(kNumMapsToNothing - 1));
        src.append(kMapsToNothing[idx]);
        if (stream.ConsumeBool()) {
          AppendRandomRangeChar(stream, src);
        }
        continue;
      }

      if (kind == 2) {

        if (stream.ConsumeBool()) {
          AppendRandomRangeChar(stream, src);
        }
        const uint8_t idx = stream.ConsumeIntegralInRange<uint8_t>(
            0, static_cast<uint8_t>(kNumDiscontiguousChains - 1));
        for (int j = 0; j < 4; j++) {
          UChar32 cp = kDiscontiguousChains[idx][j];
          if (cp == 0) break;
          src.append(cp);
        }
        if (stream.ConsumeBool()) {
          AppendRandomRangeChar(stream, src);
        }
        continue;
      }

      AppendRandomRangeChar(stream, src);
    }
  }

  UnicodeSet filterSet;

  FilteredNormalizer2 filtered(*norm2, filterSet);
  const Normalizer2 *activeNorm2 = norm2;
  if (useFilter) {
    for (const auto &range : filterRanges) {
      filterSet.add(range.first, range.second);
    }

    filterSet.freeze();
    activeNorm2 = &filtered;
  }

  UErrorCode normErrorCode = U_ZERO_ERROR;

  UnicodeString result =
      CallNormalizeOneArgOverload(activeNorm2, src, normErrorCode);

  if (result.length() < 0) {

    return 1;
  }

  return 0;
}