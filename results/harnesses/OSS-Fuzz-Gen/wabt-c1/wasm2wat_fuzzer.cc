#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "wabt/binary-reader-ir.h"
#include "wabt/binary-reader.h"
#include "wabt/common.h"
#include "wabt/ir.h"

namespace {

void AppendULEB128(std::vector<uint8_t>& out, uint32_t value) {
  do {
    uint8_t byte = value & 0x7f;
    value >>= 7;
    if (value != 0) {
      byte |= 0x80;
    }
    out.push_back(byte);
  } while (value != 0);
}

std::vector<uint8_t> MakeCustomSection(const char* name,
                                        const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> content;
  size_t name_len = strlen(name);
  AppendULEB128(content, static_cast<uint32_t>(name_len));
  content.insert(content.end(), name, name + name_len);
  content.insert(content.end(), payload.begin(), payload.end());

  std::vector<uint8_t> section;
  section.push_back(0x00);
  AppendULEB128(section, static_cast<uint32_t>(content.size()));
  section.insert(section.end(), content.begin(), content.end());
  return section;
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider data_provider(data, size);

  uint32_t feature_bits = data_provider.ConsumeIntegral<uint32_t>();

  wabt::Features features;
  int feature_index = 0;
#define WABT_FEATURE(variable, flag, default_, help)        \
  if ((feature_bits >> feature_index) & 1u) {                \
    features.enable_##variable();                            \
  }                                                           \
  ++feature_index;
#include "wabt/feature.def"
#undef WABT_FEATURE

  uint8_t option_bits = data_provider.ConsumeIntegral<uint8_t>();
  bool read_debug_names = (option_bits & 0x1) != 0;
  bool stop_on_first_error = (option_bits & 0x2) != 0;
  bool fail_on_custom_section_error = (option_bits & 0x4) != 0;

  wabt::ReadBinaryOptions options(features,  nullptr,
                                   read_debug_names, stop_on_first_error,
                                   fail_on_custom_section_error);

  wabt::Errors errors;
  wabt::Module module;

  uint8_t header_ctrl = data_provider.ConsumeIntegral<uint8_t>();
  uint8_t version_lo = 0x01, version_hi = 0x00;
  uint8_t layer_lo = 0x00, layer_hi = 0x00;
  if ((header_ctrl & 0x7) == 0) {
    version_lo = data_provider.ConsumeIntegral<uint8_t>();
    version_hi = data_provider.ConsumeIntegral<uint8_t>();
    layer_lo = data_provider.ConsumeIntegral<uint8_t>();
    layer_hi = data_provider.ConsumeIntegral<uint8_t>();
  }
  const uint8_t kWasmHeader[8] = {
      0x00, 0x61, 0x73, 0x6d,
      version_lo, version_hi, layer_lo, layer_hi};

  std::vector<uint8_t> body = data_provider.ConsumeRemainingBytes<uint8_t>();

  static const char* const kMagicNames[] = {
      "name",          "dylink.0", "dylink",         "reloc.fuzz",
      "target_features", "linking", "metadata.code.fuzz"};
  constexpr size_t kNumMagicNames =
      sizeof(kMagicNames) / sizeof(kMagicNames[0]);

  std::vector<uint8_t> wasm_bytes;
  wasm_bytes.reserve(sizeof(kWasmHeader) + body.size() + 64);
  wasm_bytes.insert(wasm_bytes.end(), kWasmHeader,
                     kWasmHeader + sizeof(kWasmHeader));

  if (!body.empty() && data_provider.ConsumeBool()) {
    size_t name_idx =
        data_provider.ConsumeIntegralInRange<size_t>(0, kNumMagicNames - 1);

    size_t payload_len =
        data_provider.ConsumeIntegralInRange<size_t>(0, body.size());
    std::vector<uint8_t> payload(body.begin(), body.begin() + payload_len);
    std::vector<uint8_t> custom_section =
        MakeCustomSection(kMagicNames[name_idx], payload);
    wasm_bytes.insert(wasm_bytes.end(), custom_section.begin(),
                       custom_section.end());

    body.erase(body.begin(), body.begin() + payload_len);
  }

  wasm_bytes.insert(wasm_bytes.end(), body.begin(), body.end());

  wabt::ReadBinaryIr("dummy filename", {wasm_bytes.data(), wasm_bytes.size()},
                      options, &errors, &module);
  return 0;
}