#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>

#include "binary-reader-ir.h"
#include "binary-reader.h"
#include "ir.h"
#include "error.h"
#include "feature.h"
#include "common.h"

using namespace wabt;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {

  const char* dummy_filename = "./dummy_file";
  FILE* fp = fopen(dummy_filename, "wb");
  if (fp) {
    if (Size > 0) {
      fwrite(Data, 1, Size, fp);
    }
    fclose(fp);
  }

  ReadBinaryOptions options;
  options.read_debug_names = (Size > 0) ? ((Data[0] & 0x1) != 0) : false;
  if (Size > 0) {

    uint8_t flags = Data[0];
    options.features.enable_simd();
    if (flags & 0x2) options.features.enable_exceptions();
    if (flags & 0x4) options.features.enable_threads();
    if (flags & 0x8) options.features.enable_tail_call();
    if (flags & 0x10) options.features.enable_multi_memory();
    if (flags & 0x20) options.features.enable_memory64();
    if (flags & 0x40) options.features.enable_gc();
    if (flags & 0x80) options.features.enable_function_references();
  }
  options.log_stream = nullptr;

  {
    Errors errors;
    Module module;
    ByteSpan span(Data, Size);
    Result result =
        ReadBinaryIr(dummy_filename, span, options, &errors, &module);

    (void)result;

    if (result == Result::Ok) {
      (void)module.name;
      (void)module.funcs.size();
      (void)module.globals.size();
      (void)module.imports.size();
      (void)module.exports.size();
      (void)module.types.size();
      (void)module.tables.size();
      (void)module.elem_segments.size();
      (void)module.memories.size();
      (void)module.data_segments.size();
      (void)module.tags.size();
      (void)module.customs.size();
      (void)module.features_used.simd;
      (void)module.features_used.exceptions;
      (void)module.features_used.threads;
    }

    for (const auto& err : errors) {
      (void)err.message;
      (void)err.error_level;
    }
  }

  {
    Errors errors2;
    Module module2;
    Result result2 = ReadBinaryIr(dummy_filename, Data, Size, options,
                                   &errors2, &module2);
    (void)result2;

    if (result2 == Result::Ok) {
      (void)module2.name;
      (void)module2.funcs.size();
      (void)module2.globals.size();
      (void)module2.imports.size();
      (void)module2.exports.size();
      (void)module2.types.size();
      (void)module2.tables.size();
      (void)module2.elem_segments.size();
      (void)module2.memories.size();
      (void)module2.data_segments.size();
      (void)module2.tags.size();
      (void)module2.customs.size();
      (void)module2.features_used.simd;
      (void)module2.features_used.exceptions;
      (void)module2.features_used.threads;
    }

    for (const auto& err : errors2) {
      (void)err.message;
      (void)err.error_level;
    }
  }

  {
    Errors errors3;
    Module module3;
    ReadBinaryOptions options3;
    options3.features.enable_simd();
    Result result3 =
        ReadBinaryIr("", Data, Size, options3, &errors3, &module3);
    (void)result3;
  }

  return 0;
}