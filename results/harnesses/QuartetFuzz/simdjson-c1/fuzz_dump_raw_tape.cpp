#include "simdjson.h"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

#include "NullBuffer.h"

static void print_json(std::ostream& os, simdjson::dom::element element) noexcept {
  const char endl='\n';
  switch (element.type()) {
  case simdjson::dom::element_type::ARRAY:
    os << "[";
    {
      simdjson::dom::array array = element.get_array().value_unsafe();
      for (simdjson::dom::element child : array) {
        print_json(os, child);
        os << ",";
      }
    }
    os << "]";
    break;
  case simdjson::dom::element_type::OBJECT:
    os << "{";
    {
      simdjson::dom::object object = element.get_object().value_unsafe();
      for (simdjson::dom::key_value_pair field : object) {
        os << "\"" << field.key << "\": ";
        print_json(os, field.value);
      }
    }
    os << "}";
    break;
  case simdjson::dom::element_type::INT64:
    os << element.get_int64().value_unsafe() << endl;
    break;
  case simdjson::dom::element_type::UINT64:
    os << element.get_uint64().value_unsafe() << endl;
    break;
  case simdjson::dom::element_type::DOUBLE:
    os << element.get_double().value_unsafe() << endl;
    break;
  case simdjson::dom::element_type::STRING:
    os << element.get_string().value_unsafe() << endl;
    break;
  case simdjson::dom::element_type::BOOL:
    os << element.get_bool().value_unsafe() << endl;
    break;
  case simdjson::dom::element_type::NULL_VALUE:
    os << "null" << endl;
    break;
  case simdjson::dom::element_type::BIGINT:
    os << element.get_bigint().value_unsafe() << endl;
    break;
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size < 1) { return 0; }

  bool enable_number_as_string = (Data[0] & 1) != 0;
  const uint8_t *json_data = Data + 1;
  size_t json_size = Size - 1;

  simdjson::dom::parser parser;
  parser.number_as_string(enable_number_as_string);

  simdjson::dom::element elem;
  auto error = parser.parse(json_data, json_size).get(elem);
  if (error) { return 0; }

  NulOStream os;
  bool ok = elem.dump_raw_tape(os);
  (void)ok;

  simdjson::dom::element root_elem = parser.doc.root();
  NulOStream os2;
  bool ok2 = root_elem.dump_raw_tape(os2);
  (void)ok2;

  NulOStream os3;
  bool ok3 = parser.dump_raw_tape(os3);
  (void)ok3;

  NulOStream os4;
  print_json(os4, elem);

  return 0;
}
