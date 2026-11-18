#include "simdjson.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "NullBuffer.h"
#include "supported_implementations.h"

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

namespace {

namespace fs = std::filesystem;

void copy_json_seeds_from(const fs::path &src_dir, const fs::path &dst_dir,
                           uintmax_t max_file_size, size_t &budget) {
  std::error_code ec;
  if (!fs::exists(src_dir, ec) || ec) {
    return;
  }
  fs::recursive_directory_iterator it(
      src_dir, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;
  if (ec) {
    return;
  }
  for (; it != end && budget > 0;) {
    const fs::directory_entry entry = *it;
    std::error_code fec;
    bool is_file = entry.is_regular_file(fec);
    if (!fec && is_file) {
      const fs::path &p = entry.path();
      std::string ext = p.extension().string();
      if (ext == ".json" || ext == ".ndjson") {
        std::error_code sec;
        uintmax_t sz = fs::file_size(p, sec);
        if (!sec && sz > 0 && sz <= max_file_size) {

          std::string unique_name =
              std::to_string(std::hash<std::string>{}(p.string())) + "_" +
              p.filename().string();
          fs::path dst = dst_dir / unique_name;
          std::error_code cec;
          fs::copy_file(p, dst, fs::copy_options::overwrite_existing, cec);
          if (!cec) {
            --budget;
          }
        }
      }
    }
    it.increment(ec);
    if (ec) {
      break;
    }
  }
}

void write_literal_seed(const fs::path &dst_dir, const char *name,
                         std::string_view content) {
  std::ofstream out(dst_dir / name, std::ios::binary | std::ios::trunc);
  if (out) {
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
  }
}

void seed_handwritten_edge_cases(const fs::path &dst_dir) {

  write_literal_seed(dst_dir, "seed_deep_array.json",
                      std::string(200, '[') + std::string(200, ']'));
  {
    std::string s;
    for (int i = 0; i < 64; i++) s += "{\"a\":";
    s += "null";
    for (int i = 0; i < 64; i++) s += "}";
    write_literal_seed(dst_dir, "seed_deep_object.json", s);
  }
  write_literal_seed(
      dst_dir, "seed_bigint.json",
      "[123456789012345678901234567890123456789012345678901234567890]");
  write_literal_seed(dst_dir, "seed_numbers.json",
                      "[0, -0, 1e400, -1e-400, 1.7976931348623157e308, "
                      "-9223372036854775808, 18446744073709551615, "
                      "3.14159265358979, 1E10, -0.0]");
  write_literal_seed(
      dst_dir, "seed_strings.json",
      "[\"\\u0041\\u0042\", \"\\ud83d\\ude00\", "
      "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\", \"\\u0000\", \"plain string\"]");
  write_literal_seed(
      dst_dir, "seed_mixed.json",
      "{\"a\":[1,2,3],\"b\":{\"c\":null,\"d\":true,\"e\":false},\"f\":"
      "\"text\",\"g\":1.5e10}");
  write_literal_seed(dst_dir, "seed_empty.json", "{}");
  write_literal_seed(dst_dir, "seed_empty_array.json", "[]");
  write_literal_seed(dst_dir, "seed_whitespace.json",
                      "   \t\n\r   {  \"x\" :  1  }  \n");
  write_literal_seed(dst_dir, "seed_bom.json", "\xEF\xBB\xBF{\"a\":1}");
  write_literal_seed(dst_dir, "seed_ndjson.json",
                      "{\"a\":1}\n{\"b\":2}\n{\"c\":3}\n");
  write_literal_seed(dst_dir, "seed_truncated.json", "{\"a\": [1, 2,");
  write_literal_seed(dst_dir, "seed_trailing_comma.json", "[1,2,3,]");
}

}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {

  if (argc == nullptr || argv == nullptr) {
    return 0;
  }
  for (int i = 1; i < *argc; i++) {
    const char *arg = (*argv)[i];
    if (arg == nullptr || arg[0] == '-') {
      continue;
    }
    std::error_code ec;
    fs::path dir(arg);
    bool exists = fs::exists(dir, ec);
    if (ec || !exists) {
      continue;
    }
    bool is_dir = fs::is_directory(dir, ec);
    if (ec || !is_dir) {
      continue;
    }

    size_t budget = 4000;

    static const char *kCandidateSourceDirs[] = {
        "/src/simdjson/jsonexamples",
        "/src/simdjson/build/jsonexamples",
        "/src/simdjson/build/_deps/simdjson-data-src/jsonchecker",
        "/src/simdjson/build/_deps/simdjson-data-src",
        "/src/simdjson/tests",
        "/src/simdjson/fuzz",
    };
    for (const char *src : kCandidateSourceDirs) {
      if (budget == 0) break;
      copy_json_seeds_from(src, dir,  8192, budget);
    }

    seed_handwritten_edge_cases(dir);

    break;
  }
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {

  static simdjson::dom::parser parser;

  static const std::vector<const simdjson::implementation*> impls =
      get_runtime_supported_implementations();

  simdjson::dom::element elem;

  simdjson::simdjson_result<simdjson::dom::element> (simdjson::dom::parser::*parse_fn)(
      const uint8_t *, size_t, bool) & = &simdjson::dom::parser::parse;

  uint8_t ctrl = 0;
  for (size_t i = 0; i < Size; i++) {
    ctrl ^= Data[i];
  }
  const uint8_t *body = Data;
  size_t body_len = Size;

  const bool realloc_if_needed = (ctrl & 1) != 0;

  if ((ctrl & 0x30) == 0x30) {
    parser.set_max_capacity(32 + (ctrl >> 2));
  } else {
    parser.set_max_capacity(simdjson::SIMDJSON_MAXSIZE_BYTES);
  }

  parser.number_as_string((ctrl & 4) != 0);

  if ((ctrl & 0xC0) == 0xC0) {
    size_t small_depth = 1 + ((ctrl >> 4) % 8);
    simdjson::error_code alloc_err =
        parser.allocate(simdjson::dom::MINIMAL_DOCUMENT_CAPACITY, small_depth);
    (void)alloc_err;
  }

  if (!impls.empty()) {
    simdjson::get_active_implementation() = impls[ctrl % impls.size()];
  }

  std::vector<uint8_t> padded_buffer(body_len + simdjson::SIMDJSON_PADDING, 0);
  if (body_len > 0) {
    std::memcpy(padded_buffer.data(), body, body_len);
  }

  auto error = (parser.*parse_fn)(padded_buffer.data(), body_len, realloc_if_needed).get(elem);

  if (error) { return 0; }
  NulOStream os;

  print_json(os,elem);
  return 0;
}