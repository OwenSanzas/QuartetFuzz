#include "FuzzUtils.h"
#include "simdjson.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

static void minify_children(simdjson::dom::element elem) {
  simdjson::dom::array arr;
  if (!elem.get_array().get(arr)) {
    int count = 0;
    for (simdjson::dom::element child : arr) {
      simdjson_unused auto s = simdjson::minify(child);
      if (++count >= 16) break;
    }
    return;
  }
  simdjson::dom::object obj;
  if (!elem.get_object().get(obj)) {
    int count = 0;
    for (simdjson::dom::key_value_pair field : obj) {
      simdjson_unused auto s = simdjson::minify(field.value);
      if (++count >= 16) break;
    }
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {

  FuzzData fd(Data, Size);
  auto strings = fd.splitIntoStrings();
  while (strings.size() < 3) {
    strings.emplace_back();
  }

  const std::string_view json_doc = strings[0];
  const std::string_view json_pointer = strings[1];
  const std::string_view json_path = strings[2];

  const int extra =
      json_doc.empty()
          ? 0
          : static_cast<int>(static_cast<unsigned char>(json_doc.back()) % 3);

  simdjson::dom::parser parser;
  simdjson::dom::element doc;

  simdjson::padded_string padded(json_doc);
  if (parser.parse(padded).get(doc)) return 0;

  std::string s1 = simdjson::minify(doc);

  minify_children(doc);

  {
    simdjson::dom::element leaf;
    auto res = doc.at_pointer(json_pointer);
    if (!res.get(leaf)) {
      simdjson_unused auto sleaf = simdjson::minify(leaf);
    }
  }
  if (!json_path.empty()) {
    simdjson::dom::element leaf;
    auto res = doc.at_path(json_path);
    if (!res.get(leaf)) {
      simdjson_unused auto sleaf = simdjson::minify(leaf);
    }
  }

  switch (extra) {
    case 0: {

      std::string pretty = simdjson::prettify(doc);
      simdjson::dom::parser parser3;
      simdjson::dom::element doc3;
      simdjson::padded_string padded3(pretty);
      if (!parser3.parse(padded3).get(doc3)) {
        simdjson_unused auto pretty2 = simdjson::prettify(doc3);
      }
      break;
    }
    case 1: {
      simdjson_unused auto str = simdjson::to_string(doc);
      break;
    }
    case 2: {

      std::vector<char> dst(json_doc.size() ? json_doc.size() : 1);
      size_t dst_len = 0;
      simdjson_unused auto err = simdjson::minify(
          json_doc.data(), json_doc.size(), dst.data(), dst_len);
      break;
    }
    default:
      break;
  }

  {
    simdjson::dom::parser parser2;
    simdjson::dom::element doc2;
    simdjson::padded_string padded2(s1);
    if (!parser2.parse(padded2).get(doc2)) {
      simdjson_unused auto s2 = simdjson::minify(doc2);
#if SIMDJSON_EXCEPTIONS
      try {
        simdjson_unused auto s3 = simdjson::minify(parser2.parse(padded2));
      } catch (const simdjson::simdjson_error &) {

      }
#endif
    }
  }

  return 0;
}
