#include <cstring>
#include <memory>
#include <vector>
#include <stdint.h>
#include <stddef.h>

#include "fuzzer_utils.h"
#include "unicode/listformatter.h"
#include "unicode/unistr.h"
#include "unicode/locid.h"
#include "unicode/ulistformatter.h"
#include "unicode/formattedvalue.h"
#include "unicode/uformattedvalue.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

  if (size < 6) return 0;

  uint16_t locale_rnd;
  std::memcpy(&locale_rnd, data, sizeof(locale_rnd));
  data += sizeof(locale_rnd);
  size -= sizeof(locale_rnd);

  uint8_t type_byte = data[0];
  uint8_t width_byte = data[1];
  uint8_t nitems_byte = data[2];
  data += 3;
  size -= 3;

  const icu::Locale& locale = GetRandomLocale(locale_rnd);

  UListFormatterType types[] = {ULISTFMT_TYPE_AND, ULISTFMT_TYPE_OR, ULISTFMT_TYPE_UNITS};
  UListFormatterWidth widths[] = {ULISTFMT_WIDTH_WIDE, ULISTFMT_WIDTH_SHORT, ULISTFMT_WIDTH_NARROW};
  UListFormatterType type = types[type_byte % 3];
  UListFormatterWidth width = widths[width_byte % 3];

  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::ListFormatter> formatter(
      icu::ListFormatter::createInstance(locale, type, width, status));
  if (U_FAILURE(status) || formatter.get() == nullptr) {
    return 0;
  }

  int32_t n_items = nitems_byte % 6;

  std::vector<icu::UnicodeString> items;
  items.reserve(n_items > 0 ? n_items : 0);

  if (n_items > 0 && size > 0) {
    size_t chunk = size / n_items;
    if (chunk == 0) chunk = 1;
    size_t offset = 0;
    for (int32_t i = 0; i < n_items; i++) {
      if (offset >= size) {
        items.emplace_back();
        continue;
      }
      size_t remaining = size - offset;
      size_t take = (i == n_items - 1) ? remaining : (chunk < remaining ? chunk : remaining);
      size_t n_units = take / 2;
      if (n_units > 0) {

        items.emplace_back(reinterpret_cast<const char16_t*>(data + offset),
                            static_cast<int32_t>(n_units));
      } else {
        items.emplace_back();
      }
      offset += take;
    }
  } else {
    for (int32_t i = 0; i < n_items; i++) items.emplace_back();
  }

  icu::UnicodeString appendTo;
  status = U_ZERO_ERROR;
  formatter->format(items.empty() ? nullptr : items.data(), n_items, appendTo, status);

  if (U_SUCCESS(status)) {
    int32_t checksum = 0;
    for (int32_t i = 0; i < appendTo.length(); i++) {
      checksum += appendTo.charAt(i);
    }

    if (checksum == 0x7fffffff) {
      appendTo.remove();
    }
  }

  status = U_ZERO_ERROR;
  icu::FormattedList fl = formatter->formatStringsToValue(
      items.empty() ? nullptr : items.data(), n_items, status);
  if (U_SUCCESS(status)) {
    status = U_ZERO_ERROR;
    icu::UnicodeString asStr = fl.toString(status);
    int32_t checksum2 = 0;
    for (int32_t i = 0; i < asStr.length(); i++) {
      checksum2 += asStr.charAt(i);
    }
    if (checksum2 == 0x7fffffff) {
      asStr.remove();
    }

    status = U_ZERO_ERROR;
    icu::UnicodeString appendBuf;
    icu::UnicodeStringAppendable appendable(appendBuf);
    fl.appendTo(appendable, status);

    status = U_ZERO_ERROR;
    icu::ConstrainedFieldPosition cfpos;
    cfpos.constrainCategory(UFIELD_CATEGORY_LIST);
    while (fl.nextPosition(cfpos, status)) {

      int32_t field = cfpos.getField();
      int32_t start = cfpos.getStart();
      int32_t limit = cfpos.getLimit();
      if (field < 0 || start < 0 || limit < start) {
        break;
      }
    }
  }

  return 0;
}
