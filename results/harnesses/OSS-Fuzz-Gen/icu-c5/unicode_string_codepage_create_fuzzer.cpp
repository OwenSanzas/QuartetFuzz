#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "unicode/unistr.h"
#include "unicode/ucnv.h"

static bool IsStatefulSlowConverter(const char *name) {
  if (name == nullptr || *name == '\0') return false;
  UErrorCode err = U_ZERO_ERROR;
  UConverter *tmp = ucnv_open(name, &err);
  if (U_FAILURE(err) || tmp == nullptr) return false;
  UConverterType type = ucnv_getType(tmp);
  ucnv_close(tmp);
  switch (type) {
    case UCNV_ISO_2022:
    case UCNV_SCSU:
    case UCNV_BOCU1:
    case UCNV_HZ:
    case UCNV_LMBCS_1:
    case UCNV_LMBCS_2:
    case UCNV_LMBCS_3:
    case UCNV_LMBCS_4:
    case UCNV_LMBCS_5:
    case UCNV_LMBCS_6:
    case UCNV_LMBCS_8:
    case UCNV_LMBCS_11:
    case UCNV_LMBCS_16:
    case UCNV_LMBCS_17:
    case UCNV_LMBCS_18:
    case UCNV_LMBCS_19:
      return true;
    default:
      return false;
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  uint8_t codepage_mode = stream.ConsumeIntegralInRange<uint8_t>(0, 5);

  std::string codepage_storage;
  const char *codepage_ptr = nullptr;

  bool slow_converter = false;

  auto pick_canonical = [&]() -> const char * {
    int32_t available = ucnv_countAvailable();
    if (available <= 0) return nullptr;
    if (stream.remaining_bytes() == 0) return nullptr;
    uint8_t idx_byte = stream.ConsumeIntegral<uint8_t>();
    uint16_t idx = static_cast<uint16_t>(idx_byte) % available;
    return ucnv_getAvailableName(idx);
  };

  switch (codepage_mode) {
    case 0: {
      codepage_ptr = nullptr;
      break;
    }
    case 1: {
      codepage_storage = "";
      codepage_ptr = codepage_storage.c_str();
      break;
    }
    case 2: {
      codepage_ptr = pick_canonical();
      slow_converter = IsStatefulSlowConverter(codepage_ptr);
      break;
    }
    case 3: {

      size_t name_len = stream.ConsumeIntegralInRange<size_t>(0, 16);
      codepage_storage = stream.ConsumeBytesAsString(name_len);
      codepage_ptr = codepage_storage.c_str();

      slow_converter = IsStatefulSlowConverter(codepage_ptr);
      break;
    }
    case 4: {

      const char *canon = pick_canonical();
      if (canon != nullptr) {

        slow_converter = IsStatefulSlowConverter(canon);

        UErrorCode err = U_ZERO_ERROR;
        uint16_t n = ucnv_countAliases(canon, &err);
        if (U_SUCCESS(err) && n > 0) {
          uint8_t aidx_byte = stream.ConsumeIntegral<uint8_t>();
          uint16_t aidx = static_cast<uint16_t>(aidx_byte) % n;
          UErrorCode err2 = U_ZERO_ERROR;
          const char *alias = ucnv_getAlias(canon, aidx, &err2);
          if (U_SUCCESS(err2) && alias != nullptr) {
            codepage_storage = alias;
            codepage_ptr = codepage_storage.c_str();
          } else {
            codepage_storage = canon;
            codepage_ptr = codepage_storage.c_str();
          }
        } else {
          codepage_storage = canon;
          codepage_ptr = codepage_storage.c_str();
        }
      }
      break;
    }
    case 5: {

      const char *canon = pick_canonical();
      static const char *kOptions[] = {",locale=en", ",swaplfnl",
                                        ",version=0", ",version=1"};
      if (canon != nullptr) {

        slow_converter = IsStatefulSlowConverter(canon);

        uint8_t oidx = stream.ConsumeIntegralInRange<uint8_t>(0, 3);
        codepage_storage = std::string(canon) + kOptions[oidx];
        codepage_ptr = codepage_storage.c_str();
      }
      break;
    }
    default:
      codepage_ptr = nullptr;
      break;
  }

  static constexpr size_t kMaxPayloadBytes = 48;
  static constexpr size_t kMaxPayloadBytesSlow = 16;

  const size_t effective_cap =
      slow_converter ? kMaxPayloadBytesSlow : kMaxPayloadBytes;

  uint8_t length_mode = stream.ConsumeIntegralInRange<uint8_t>(0, 3);

  if (length_mode == 0) {

    std::string codepage_data_str = stream.ConsumeRemainingBytesAsString();
    if (codepage_data_str.size() > effective_cap) {
      codepage_data_str.resize(effective_cap);
    }
    const char *codepage_data_ptr = codepage_data_str.c_str();

    icu::UnicodeString fuzz_string(codepage_data_ptr, -1, codepage_ptr);

    (void)fuzz_string.length();
    return 0;
  }

  std::vector<char> codepage_data_vec = stream.ConsumeRemainingBytes<char>();
  if (codepage_data_vec.size() > effective_cap) {
    codepage_data_vec.resize(effective_cap);
  }

  if (length_mode == 2) {

    const char *codepage_data_ptr =
        codepage_data_vec.empty() ? nullptr : codepage_data_vec.data();
    icu::UnicodeString fuzz_string(codepage_data_ptr, -2, codepage_ptr);
    (void)fuzz_string.length();
    return 0;
  }

  if (length_mode == 3) {

    static const char kDummyByte = 'A';
    icu::UnicodeString fuzz_string(&kDummyByte, 0, codepage_ptr);
    (void)fuzz_string.length();
    return 0;
  }

  const char *codepage_data_ptr =
      codepage_data_vec.empty() ? nullptr : codepage_data_vec.data();
  int32_t data_length = static_cast<int32_t>(codepage_data_vec.size());

  icu::UnicodeString fuzz_string(codepage_data_ptr, data_length,
                                  codepage_ptr);

  (void)fuzz_string.length();

  return 0;
}