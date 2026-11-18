#include "FuzzUtils.h"
#include "NullBuffer.h"
#include "simdjson.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

static void walk(std::ostream &os, simdjson::dom::element element, int depth) noexcept {
  if (depth > 64) { return; }
  using simdjson::dom::element_type;
  switch (element.type()) {
    case element_type::ARRAY: {
      os << "[";
      simdjson::dom::array array;
      if (!element.get_array().get(array)) {
        for (simdjson::dom::element child : array) {
          walk(os, child, depth + 1);
          os << ",";
        }
      }
      os << "]";
      break;
    }
    case element_type::OBJECT: {
      os << "{";
      simdjson::dom::object object;
      if (!element.get_object().get(object)) {
        for (simdjson::dom::key_value_pair field : object) {
          os << field.key << ":";
          walk(os, field.value, depth + 1);
        }
      }
      os << "}";
      break;
    }
    case element_type::INT64: {
      int64_t v;
      if (!element.get_int64().get(v)) { os << v; }
      break;
    }
    case element_type::UINT64: {
      uint64_t v;
      if (!element.get_uint64().get(v)) { os << v; }
      break;
    }
    case element_type::DOUBLE: {
      double v;
      if (!element.get_double().get(v)) { os << v; }
      break;
    }
    case element_type::STRING: {
      std::string_view v;
      if (!element.get_string().get(v)) { os << v; }
      break;
    }
    case element_type::BOOL: {
      bool v;
      if (!element.get_bool().get(v)) { os << v; }
      break;
    }
    case element_type::NULL_VALUE:
      os << "null";
      break;
    case element_type::BIGINT: {
      std::string_view v;
      if (!element.get_bigint().get(v)) { os << v; }
      break;
    }
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  FuzzData fd(Data, Size);

  const bool realloc_if_needed = (fd.getInt<0, 1>() != 0);
  const bool use_preset_capacity = (fd.getInt<0, 1>() != 0);
  const int depth_choice = fd.getInt<0, 3>();
  const int variant = fd.getInt<0, 3>();

  auto strings = fd.splitIntoStrings();
  while (strings.size() < 2) { strings.emplace_back(); }
  const std::string_view pointer_str = strings[0];
  const std::string_view doc_str = strings[1];

  const simdjson::padded_string padded_doc(doc_str);

  simdjson::dom::parser parser;

  simdjson::dom::document provided_doc;

  if (use_preset_capacity) {
    size_t max_depth;
    switch (depth_choice) {
      case 0: max_depth = 1; break;
      case 1: max_depth = 4; break;
      case 2: max_depth = 16; break;
      default: max_depth = simdjson::DEFAULT_MAX_DEPTH; break;
    }

    size_t capacity = doc_str.size() / 2;
    simdjson_unused auto alloc_err = parser.allocate(capacity, max_depth);
    (void)alloc_err;
  }

  simdjson::dom::element elem;
  simdjson::error_code error;

  switch (variant) {
    case 0: {

      error = parser.parse(
          padded_doc.u8data(), padded_doc.size(),
          realloc_if_needed).get(elem);
      break;
    }
    case 1: {

      error = parser.parse_unpadded(
          reinterpret_cast<const uint8_t *>(doc_str.data()), doc_str.size())
          .get(elem);
      break;
    }
    case 2: {

      error = parser.parse_into_document(
          provided_doc,
          padded_doc.u8data(), padded_doc.size(),
          realloc_if_needed).get(elem);
      break;
    }
    default: {

      const size_t batch_size =
          static_cast<size_t>(fd.getInt<0, 200>()) + simdjson::dom::MINIMAL_BATCH_SIZE;
      const int format_choice = fd.getInt<0, 4>();
      simdjson::stream_format format;
      switch (format_choice) {
        case 0: format = simdjson::stream_format::whitespace_delimited; break;
        case 1: format = simdjson::stream_format::json_sequence; break;
        case 2: format = simdjson::stream_format::comma_delimited; break;
        case 3: format = simdjson::stream_format::comma_delimited_array; break;
        default: format = simdjson::stream_format::newline_delimited; break;
      }
      simdjson::dom::document_stream docs;
      error = parser.parse_many(
          padded_doc.u8data(), padded_doc.size(),
          batch_size, format).get(docs);
      if (error) { return 0; }
      NulOStream os;
      for (auto doc : docs) {
        simdjson::dom::element e;
        if (doc.get(e)) { continue; }
        walk(os, e, 0);
      }
      return 0;
    }
  }

  if (error) { return 0; }

  simdjson::dom::element leaf;
  if (!elem.at_pointer(pointer_str).get(leaf)) {
    std::string_view sv;
    simdjson_unused auto ignored = leaf.get_string().get(sv);
  }

  NulOStream os;
  walk(os, elem, 0);

  simdjson_unused auto minified = simdjson::minify(elem);

  return 0;
}
