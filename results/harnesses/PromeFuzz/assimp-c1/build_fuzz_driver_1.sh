#! /bin/bash

cd /tmp/promefuzz
clang++ /pf/out/fuzz_driver/fuzz_driver_1.cpp -o /pf/out/fuzz_driver/fuzz_driver_1 -fsanitize=fuzzer,address,undefined -g -I/src/assimp/include/assimp -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -I/src/assimp/include -I/src/assimp/build/include -fno-omit-frame-pointer -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=deprecated-declarations -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=unknown-warning-option -Wno-error=vla-cxx-extension -stdlib=libc++ /src/assimp/build/lib/libassimp.a /src/assimp/build/contrib/zlib/libzlibstatic.a -lpthread -ldl
