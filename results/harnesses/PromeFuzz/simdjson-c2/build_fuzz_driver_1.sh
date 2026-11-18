#! /bin/bash

cd /tmp/promefuzz
clang++ /pf/out/fuzz_driver/fuzz_driver_1.cpp -o /pf/out/fuzz_driver/fuzz_driver_1 -fsanitize=fuzzer,address,undefined -g -I/src/simdjson/include -I/src/simdjson/include -DSIMDJSON_AVX512_ALLOWED=1 -DSIMDJSON_DEVELOPMENT_CHECKS -DSIMDJSON_DISABLE_DEPRECATED_API=1 -DSIMDJSON_THREADS_ENABLED=1 -DSIMDJSON_UTF8VALIDATION=1 -I/src/simdjson/src -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -std=c++17 -fno-omit-frame-pointer -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=deprecated-declarations -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=unknown-warning-option -Wno-error=vla-cxx-extension -stdlib=libc++ /src/simdjson/build/libsimdjson.a
