#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../src/pugixml.hpp"

namespace {

std::atomic<long long> g_alloc_budget{-1};

void* counting_allocate(size_t size) {
  long long budget = g_alloc_budget.load(std::memory_order_relaxed);
  if (budget == 0) {
    return nullptr;
  }
  if (budget > 0) {
    g_alloc_budget.fetch_sub(1, std::memory_order_relaxed);
  }
  return std::malloc(size);
}

void counting_deallocate(void* ptr) { std::free(ptr); }

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static bool installed = false;
  if (!installed) {
    pugi::set_memory_management_functions(counting_allocate, counting_deallocate);
    installed = true;
  }

  FuzzedDataProvider stream(data, size);

  const unsigned int options = stream.ConsumeIntegral<unsigned int>();

  pugi::xml_encoding encoding;
  if (stream.ConsumeIntegralInRange<int>(0, 9) < 7) {
    encoding = pugi::encoding_auto;
  } else {
    encoding = stream.PickValueInArray<pugi::xml_encoding>(
        {pugi::encoding_utf8, pugi::encoding_utf16_le, pugi::encoding_utf16_be,
         pugi::encoding_utf16, pugi::encoding_utf32_le, pugi::encoding_utf32_be,
         pugi::encoding_utf32, pugi::encoding_wchar, pugi::encoding_latin1});
  }

  static const char* const kPrefixes[] = {
      "",
      "\xEF\xBB\xBF",
      "\xFE\xFF",
      "\xFF\xFE",
      "\x00\x00\xFE\xFF",
      "\xFF\xFE\x00\x00",
      "\x00\x00\x00\x3C",
      "\x3C\x00\x00\x00",
      "\x00\x3C\x00\x3F",
      "\x3C\x00\x3F\x00",
      "<?xml version=\"1.0\" encoding=\"iso-8859-1\"?>",
      "<?xml version=\"1.0\" encoding=\"latin1\"?>",
      "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>",
      "<?xml version=\"1.0\" encoding=\"LATIN1\"?>",
  };
  const size_t prefix_idx = stream.ConsumeIntegralInRange<size_t>(
      0, sizeof(kPrefixes) / sizeof(kPrefixes[0]) - 1);
  const char* prefix = kPrefixes[prefix_idx];
  const size_t prefix_len = std::strlen(prefix);

  const bool build_big_token = stream.ConsumeIntegralInRange<int>(0, 5) == 0;

  const int oom_mode = stream.ConsumeIntegralInRange<int>(0, 9);
  if (oom_mode == 0) {
    g_alloc_budget.store(0);

  } else if (oom_mode <= 2) {

    g_alloc_budget.store(stream.ConsumeIntegralInRange<int>(1, 8));
  } else {
    g_alloc_budget.store(-1);
  }

  std::vector<uint8_t> body = stream.ConsumeRemainingBytes<uint8_t>();

  if (build_big_token) {

    std::vector<uint8_t> unit;
    size_t seed_len = std::min<size_t>(body.size(), static_cast<size_t>(16));
    if (seed_len == 0) {
      unit.push_back('x');
    } else {
      unit.assign(body.begin(), body.begin() + seed_len);
    }
    for (auto& b : unit) {
      if (b == '<' || b == '>' || b == '&' || b == '"' || b == '\'' ||
          b == '-' || b == ']' || b == 0) {
        b = 'x';
      }
    }

    const size_t kBigTokenTarget = 8600;
    std::string big_run;
    big_run.reserve(kBigTokenTarget);
    while (big_run.size() < kBigTokenTarget) {
      size_t take = std::min(unit.size(), kBigTokenTarget - big_run.size());
      big_run.append(reinterpret_cast<const char*>(unit.data()), take);
    }

    std::string wrapper = "<root a=\"" + big_run + "\">";
    std::vector<uint8_t> new_body(wrapper.begin(), wrapper.end());
    new_body.insert(new_body.end(), body.begin(), body.end());
    std::string closer = "</root>";
    new_body.insert(new_body.end(), closer.begin(), closer.end());
    body.swap(new_body);
  }

  std::vector<uint8_t> contents;
  contents.reserve(prefix_len + body.size());
  contents.insert(contents.end(), reinterpret_cast<const uint8_t*>(prefix),
                   reinterpret_cast<const uint8_t*>(prefix) + prefix_len);
  contents.insert(contents.end(), body.begin(), body.end());

  pugi::xml_document doc;
  doc.load_buffer(contents.empty() ? nullptr : contents.data(),
                   contents.size(), options, encoding);

  g_alloc_budget.store(-1);

  return 0;
}