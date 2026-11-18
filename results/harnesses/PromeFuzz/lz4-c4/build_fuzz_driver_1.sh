#! /bin/bash

cd /tmp/promefuzz
clang /pf/out/fuzz_driver/fuzz_driver_1.c -o /pf/out/fuzz_driver/fuzz_driver_1 -fsanitize=fuzzer,address,undefined -g -I/src/lz4/lib -I/src/lz4/lib -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -DLZ4_DEBUG=1 -DXXH_NAMESPACE=LZ4_ -fno-omit-frame-pointer -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=deprecated-declarations -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=unknown-warning-option -Wno-error=vla-cxx-extension -stdlib=libc++ /src/lz4/ossfuzz/lz4_helpers.c /src/lz4/ossfuzz/fuzz_data_producer.c /src/lz4/lib/liblz4.a
