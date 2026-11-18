#! /bin/bash

cd /tmp/promefuzz
clang++ /pf/out/fuzz_driver/fuzz_driver_1.cpp -o /pf/out/fuzz_driver/fuzz_driver_1 -fsanitize=fuzzer,address,undefined -g -I/src/leptonica/src -fno-omit-frame-pointer -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=deprecated-declarations -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=unknown-warning-option -Wno-error=vla-cxx-extension -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -stdlib=libc++ -std=c++11 -I/work/include -I/src/leptonica/src /work/lib/libleptonica.a /work/lib/libtiff.a /work/lib/libwebp.a /work/lib/libpng.a /work/lib/libjpeg.a /work/lib/libjbig.a /work/lib/libzstd.a /work/lib/libz.a /work/lib/libsharpyuv.a
