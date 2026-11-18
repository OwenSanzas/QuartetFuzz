#! /bin/bash

cd /tmp/promefuzz
clang /pf/out/fuzz_driver/fuzz_driver_1.c -o /pf/out/fuzz_driver/fuzz_driver_1 -fsanitize=fuzzer,address,undefined -g -I/src/janet/src/include -D_XOPEN_SOURCE=600 -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -DJANET_BOOTSTRAP -I/src/janet/src/include -I/src/janet/src/conf -std=c99 -fno-omit-frame-pointer -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=deprecated-declarations -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=unknown-warning-option -Wno-error=vla-cxx-extension -stdlib=libc++ /src/janet/build/libjanet.a
