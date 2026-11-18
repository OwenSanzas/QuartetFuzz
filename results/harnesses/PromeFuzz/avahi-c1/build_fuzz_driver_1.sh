#! /bin/bash

cd /tmp/promefuzz
clang /pf/out/fuzz_driver/fuzz_driver_1.c -o /pf/out/fuzz_driver/fuzz_driver_1 -fsanitize=fuzzer,address,undefined -g -I/src/avahi/avahi-common -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -I/src/avahi -fno-omit-frame-pointer -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=deprecated-declarations -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=unknown-warning-option -Wno-error=vla-cxx-extension -stdlib=libc++ /src/avahi/avahi-core/.libs/libavahi-core.a /src/avahi/avahi-common/.libs/libavahi-common.a
