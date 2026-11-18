#! /bin/bash

cd /tmp/promefuzz
clang++ /pf/out/fuzz_driver/fuzz_driver_1.cpp -o /pf/out/fuzz_driver/fuzz_driver_1 -fsanitize=fuzzer,address,undefined -g -I/src/znc/include/znc -I/src/znc/include -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -DGTEST_HAS_POSIX_RE=0 -I/src/znc/build/include -include znc/zncconfig.h -std=c++11 -fno-omit-frame-pointer -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=deprecated-declarations -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=unknown-warning-option -Wno-error=vla-cxx-extension -stdlib=libc++ /src/znc/build/src/libznc.a
