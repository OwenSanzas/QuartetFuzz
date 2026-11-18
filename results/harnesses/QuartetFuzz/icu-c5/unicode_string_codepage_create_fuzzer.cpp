#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "unicode/unistr.h"
#include "unicode/ucnv.h"

using icu::UnicodeString;

static const char *const kCodepages[] = {
    nullptr,
    "",
    "UTF-8",
    "UTF-16BE",
    "UTF-16LE",
    "ISO-8859-1",
    "Shift_JIS",
    "GB18030",
    "ISO-2022-JP",
    "US-ASCII",
    "not-a-real-codepage",
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 3) {
    return 0;
  }

  uint8_t cp_selector = data[0];
  const char *decode_codepage =
      kCodepages[cp_selector % (sizeof(kCodepages) / sizeof(kCodepages[0]))];

  uint8_t length_mode = data[1] % 5;

  data += 2;
  size -= 2;

  std::unique_ptr<char[]> buf(new char[size + 1]);
  std::memcpy(buf.get(), data, size);
  buf[size] = '\0';

  int32_t dataLength;
  switch (length_mode) {
    case 0:
      dataLength = static_cast<int32_t>(size);
      break;
    case 1:
      dataLength = -1;
      break;
    case 2:

      dataLength = static_cast<int32_t>(size + 1);
      break;
    case 3:
      dataLength = 0;
      break;
    default:
      dataLength = -5;
      break;
  }

  UnicodeString str(buf.get(), dataLength, decode_codepage);

  if (str.isBogus()) {

    return 0;
  }

  const char16_t *term = str.getTerminatedBuffer();
  uint32_t checksum = 0;
  if (term != nullptr) {
    for (int32_t i = 0; i < str.length(); i++) {
      checksum = checksum * 131 + term[i];
    }
  }

  UnicodeString invariantStr(buf.get(), static_cast<int32_t>(size), "");
  bool isEqual = (str == invariantStr);
  bool isNotEqual = (str != invariantStr);
  checksum += (isEqual ? 1u : 0u) + (isNotEqual ? 2u : 0u);

  uint8_t enc_selector =
      size > 0 ? data[size - 1] : static_cast<uint8_t>(cp_selector + 1);
  const char *encode_codepage =
      kCodepages[enc_selector % (sizeof(kCodepages) / sizeof(kCodepages[0]))];

  char out_stack[256];
  int32_t needed =
      str.extract(0, str.length(), out_stack, sizeof(out_stack), encode_codepage);
  if (needed > static_cast<int32_t>(sizeof(out_stack))) {

    std::unique_ptr<char[]> out_heap(new char[needed + 1]);
    checksum += static_cast<uint32_t>(str.extract(0, str.length(), out_heap.get(),
                                                   needed + 1, encode_codepage));
  } else {
    checksum += static_cast<uint32_t>(needed);
  }

  int32_t avail = ucnv_countAvailable();
  if (avail > 0) {
    int32_t idx = cp_selector % avail;
    const char *conv_name = ucnv_getAvailableName(idx);
    UErrorCode status = U_ZERO_ERROR;
    UConverter *cnv = ucnv_open(conv_name, &status);
    if (U_SUCCESS(status) && cnv != nullptr) {
      UErrorCode ctor_status = U_ZERO_ERROR;
      UnicodeString str2(buf.get(), dataLength < 0 ? -1 : dataLength, cnv,
                          ctor_status);
      if (U_SUCCESS(ctor_status) && !str2.isBogus()) {
        char out2[256];
        UErrorCode extract_status = U_ZERO_ERROR;
        checksum += static_cast<uint32_t>(
            str2.extract(out2, sizeof(out2), cnv, extract_status));

        checksum += (str2 == str) ? 4u : 8u;
      }
      ucnv_close(cnv);
    }
  }

  if (checksum == 0xDEADBEEF) {

    return 1;
  }

  return 0;
}
