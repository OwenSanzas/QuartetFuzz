#include <cstddef>
#include <cstdint>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "wabt/apply-names.h"
#include "wabt/binary-reader-ir.h"
#include "wabt/binary-reader.h"
#include "wabt/error.h"
#include "wabt/feature.h"
#include "wabt/generate-names.h"
#include "wabt/ir.h"
#include "wabt/result.h"
#include "wabt/stream.h"
#include "wabt/validator.h"
#include "wabt/wat-writer.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider data_provider(data, size);

  wabt::Features features;
#define WABT_FEATURE(variable, flag, default_, help) \
  if (data_provider.ConsumeBool()) {                 \
    features.enable_##variable();                    \
  }
#include "wabt/feature.def"
#undef WABT_FEATURE

  bool read_debug_names = data_provider.ConsumeBool();
  bool stop_on_first_error = data_provider.ConsumeBool();
  bool fail_on_custom_section_error = data_provider.ConsumeBool();
  bool generate_names = data_provider.ConsumeBool();
  bool alpha_names = data_provider.ConsumeBool();

  std::vector<uint8_t> module_bytes =
      data_provider.ConsumeRemainingBytes<uint8_t>();

  wabt::Errors errors;
  wabt::Module module;
  wabt::ReadBinaryOptions options(features,  nullptr,
                                  read_debug_names, stop_on_first_error,
                                  fail_on_custom_section_error);

  wabt::ByteSpan span(module_bytes.data(), module_bytes.size());
  wabt::Result result =
      wabt::ReadBinaryIr("fuzz-module", span, options, &errors, &module);

  if (wabt::Succeeded(result)) {

    wabt::ValidateOptions validate_options(features);
    result = wabt::ValidateModule(&module, &errors, validate_options);

    if (wabt::Succeeded(result) && generate_names) {
      result = wabt::GenerateNames(
          &module,
          alpha_names ? wabt::NameOpts::AlphaNames : wabt::NameOpts::None);
    }

    if (wabt::Succeeded(result)) {

      wabt::Result apply_result = wabt::ApplyNames(&module);
      (void)apply_result;
    }

    if (wabt::Succeeded(result)) {
      wabt::MemoryStream stream;
      wabt::WriteWatOptions write_wat_options(features);
      wabt::Result write_result =
          wabt::WriteWat(&stream, &module, write_wat_options);
      (void)write_result;
    }
  }

  return 0;
}
