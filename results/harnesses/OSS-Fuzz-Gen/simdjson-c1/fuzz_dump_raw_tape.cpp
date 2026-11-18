#include "simdjson.h"
#include <cstddef>
#include <cstdint>
#include <string>

#include "NullBuffer.h"
#include <fuzzer/FuzzedDataProvider.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size < 1) {
    return 0;
  }

  uint8_t ctrl = Data[0];
  bool use_bigint_as_string = (ctrl & 1) != 0;
  bool inject_bignum        = (ctrl & 2)  != 0 && use_bigint_as_string;
  bool inject_unsigned      = (ctrl & 4)  != 0;
  bool inject_escapes       = (ctrl & 8)  != 0;
  bool inject_neg_bignum    = (ctrl & 16) != 0 && use_bigint_as_string;

  const uint8_t *remaining_data = Data + 1;
  size_t remaining_size = Size - 1;

  FuzzedDataProvider fdp(remaining_data, remaining_size);
  int nest_depth = fdp.ConsumeIntegralInRange<int>(0, 5);

  uint8_t corruption_mode = fdp.ConsumeIntegralInRange<uint8_t>(0, 6);

  std::vector<uint8_t> payload_vec = fdp.ConsumeRemainingBytes<uint8_t>();
  const uint8_t *json_data = payload_vec.data();
  size_t json_size = payload_vec.size();

  simdjson::dom::parser parser;
  parser.number_as_string(use_bigint_as_string);

  std::string nested;

  if (corruption_mode >= 4) {

    switch (corruption_mode) {
      case 4:
        nested = "-1234567890123";
        break;
      case 5:
        nested = "9223372036854775808";
        break;
      default:
        nested = "1.5";
        break;
    }
  } else {
    std::string buf = "[";
    if (inject_bignum) {

      buf += "99999999999999999999999999999999,";
    }
    if (inject_neg_bignum) {

      buf += "-99999999999999999999999999999999,";
    }
    if (inject_unsigned) {

      buf += "9223372036854775808,";
    }
    if (inject_escapes) {

      buf += "\"\\b\\f\\n\\r\\\"\\t\\\\\\u0001\",";
    }

    if (json_size > 0) {
      buf.append(reinterpret_cast<const char *>(json_data), json_size);
    } else {

      buf += "null";
    }
    buf += "]";

    nested.reserve(buf.size() + static_cast<size_t>(nest_depth) * 2);
    for (int i = 0; i < nest_depth; i++) {
      nested += '[';
    }
    nested += buf;
    for (int i = 0; i < nest_depth; i++) {
      nested += ']';
    }
  }

  const uint8_t *final_data = reinterpret_cast<const uint8_t *>(nested.data());
  size_t final_size = nested.size();

  simdjson::dom::element elem;
  auto error = parser.parse(final_data, final_size).get(elem);

  if (error) {
    return 0;
  }

  constexpr uint64_t kValueMask = 0x00FFFFFFFFFFFFFFULL;
  uint64_t how_many = parser.doc.tape[0] & kValueMask;

  switch (corruption_mode) {
    case 1:

      parser.doc.tape[0] = (uint64_t(0xFFu) << 56) | how_many;
      break;
    case 2:

      if (how_many > 1) {
        parser.doc.tape[1] =
            (uint64_t(0xFFu) << 56) | (parser.doc.tape[1] & kValueMask);
      }
      break;
    case 3:

      if (how_many > 1) {
        parser.doc.tape[1] =
            (uint64_t('r') << 56) | (parser.doc.tape[1] & kValueMask);
      }
      break;
    case 4:
    case 5:
    case 6:

      if (how_many >= 2) {
        parser.doc.tape[0] = (uint64_t('r') << 56) | (how_many - 1);
      }
      break;
    default:

      break;
  }

  NulOStream os;
  simdjson_unused auto dumpstatus = elem.dump_raw_tape(os);
  (void)dumpstatus;

  return 0;
}