#include "simdjson.h"
#include <cstddef>
#include <cstdint>
#include <string>

using namespace simdjson;

static std::string build_leaf(const uint8_t *data, size_t size, size_t &pos) {
  if (pos >= size) return "0";
  uint8_t kind = data[pos++] % 10;
  switch (kind) {
    case 0: return "0";
    case 1: return "-123456.789e10";
    case 2: {

      std::string s = "\"";
      for (int i = 0; i < 10 && pos < size; i++, pos++) {
        uint8_t c = data[pos];
        if (c == '"') {
          s += "\\\"";
        } else if (c == '\\') {
          s += "\\\\";
        } else if (c < 0x20) {

          static const char *escs[] = {"\\b", "\\t", "\\n", "\\f",
                                        "\\r", "\\u0001", "\\u001f"};
          s += escs[c % 7];
        } else {
          s += char(c);
        }
      }
      s += "\"";
      return s;
    }
    case 3: {
      std::string digits;
      while (digits.size() < 21 && pos < size) digits.push_back(char('0' + (data[pos++] % 10)));
      while (digits.size() < 21) digits.push_back('9');
      if (pos < size && (data[pos++] & 1)) digits = "-" + digits;
      return digits;
    }
    case 4: return "true";
    case 5: return "null";
    case 6: return "false";
    case 7: return "[]";
    case 8: return "{}";
    case 9: {

      if (pos < size && (data[pos++] & 1)) {
        return "18446744073709551615";
      } else {
        return "-9223372036854775808";
      }
    }
  }
  return "0";
}

static std::string build_json(const uint8_t *data, size_t size) {
  if (size < 3) {
    return std::string(reinterpret_cast<const char *>(data), size);
  }

  uint8_t mode = data[0] % 6;
  size_t pos = 1;

  int depth = (data[pos++] % 200) + 1;

  switch (mode) {
    case 1: {
      std::string s;
      for (int i = 0; i < depth; i++) {
        s += '[';
        if (pos < size && (data[pos++] & 1)) {
          s += build_leaf(data, size, pos);
          s += ',';
        }
      }
      s += build_leaf(data, size, pos);
      for (int i = 0; i < depth; i++) s += ']';
      return s;
    }
    case 2: {

      std::string s;
      for (int i = 0; i < depth; i++) {
        s += "{\"a\":";
        if (pos < size && (data[pos++] & 1)) {
          s += build_leaf(data, size, pos);
          s += ",\"b\":";
        }
      }
      s += build_leaf(data, size, pos);
      for (int i = 0; i < depth; i++) s += '}';
      return s;
    }
    case 3: {
      std::string s = "[";
      for (int i = 0; i < 3; i++) {
        s += build_leaf(data, size, pos);
        if (i != 2) s += ",";
      }
      s += "]";
      return s;
    }
    case 4: {
      std::string s;
      int md = (depth % 20) + 1;
      for (int i = 0; i < md; i++) {
        s += (i % 2 == 0) ? "[" : "{\"k\":";
      }
      s += build_leaf(data, size, pos);
      for (int i = md - 1; i >= 0; i--) {
        s += (i % 2 == 0) ? "]" : "}";
      }
      return s;
    }
    case 5: {

      std::string s = "[";
      int md = (depth % 50) + 1;
      for (int i = 0; i < md; i++) s += "[";
      s += build_leaf(data, size, pos);
      for (int i = 0; i < md; i++) s += "]";
      s += ",";
      s += build_leaf(data, size, pos);
      s += ",";
      s += build_leaf(data, size, pos);
      s += "]";
      return s;
    }
    default:
      return std::string(reinterpret_cast<const char *>(data), size);
  }
}

static void exercise(simdjson::dom::parser &parser, const std::string &json) {
#if SIMDJSON_EXCEPTIONS
  try {

    simdjson::simdjson_result<simdjson::dom::element> result =
        parser.parse(json);
    simdjson_unused std::string minified = simdjson::minify(result);

    simdjson::dom::element elem = result.value();

    simdjson_unused std::string minified2 = simdjson::minify(elem);

    if (elem.is_array()) {
      simdjson::dom::array arr = elem.get_array().value();
      simdjson_unused std::string arr_min = simdjson::minify(arr);
    } else if (elem.is_object()) {
      simdjson::dom::object obj = elem.get_object().value();
      simdjson_unused std::string obj_min = simdjson::minify(obj);
      for (simdjson::dom::key_value_pair field : obj) {
        simdjson_unused std::string kv_min = simdjson::minify(field);
      }
    }
  } catch (std::exception &) {

  }
#else
  simdjson::dom::element elem;
  auto error = parser.parse(json).get(elem);
  if (error) {
    return;
  }
  simdjson_unused std::string minified = simdjson::minify(elem);
  if (elem.is_array()) {
    simdjson::dom::array arr;
    if (!elem.get_array().get(arr)) {
      simdjson_unused std::string arr_min = simdjson::minify(arr);
    }
  } else if (elem.is_object()) {
    simdjson::dom::object obj;
    if (!elem.get_object().get(obj)) {
      simdjson_unused std::string obj_min = simdjson::minify(obj);
      for (simdjson::dom::key_value_pair field : obj) {
        simdjson_unused std::string kv_min = simdjson::minify(field);
      }
    }
  }
#endif
}

static void run_with_all_backends(const std::string &json) {
  for (const simdjson::implementation *impl :
       simdjson::get_available_implementations()) {
    if (!impl || !impl->supported_by_runtime_system()) continue;

    simdjson::get_active_implementation() = impl;

    simdjson::dom::parser parser;

    parser.number_as_string(true);
    exercise(parser, json);
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

  std::string synthesized = build_json(data, size);
  run_with_all_backends(synthesized);

  std::string raw(reinterpret_cast<const char *>(data), size);
  run_with_all_backends(raw);

  return 0;
}