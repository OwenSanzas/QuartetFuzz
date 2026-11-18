#! /bin/bash

cd /tmp/promefuzz
clang /pf/out/fuzz_driver/fuzz_driver_1.c -o /pf/out/fuzz_driver/fuzz_driver_1 -fsanitize=fuzzer,address,undefined -g -I/src/gnutls/lib/includes/gnutls -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -I/src/gnutls/lib/includes -I/src/gnutls -fno-omit-frame-pointer -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=deprecated-declarations -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=unknown-warning-option -Wno-error=vla-cxx-extension -stdlib=libc++ -L/src/deps/lib -L/src/deps/lib -L/src/deps/lib64 /src/gnutls/lib/.libs/libgnutls.a -Wl,-z,muldefs -Wl,-Bstatic -lhogweed -lnettle -ltasn1 -lgmp -lidn2 -lunistring -Wl,-Bdynamic
