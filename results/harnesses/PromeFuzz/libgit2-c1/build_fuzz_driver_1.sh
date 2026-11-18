#! /bin/bash

cd /tmp/promefuzz
clang /pf/out/fuzz_driver/fuzz_driver_1.c -o /pf/out/fuzz_driver/fuzz_driver_1 -fsanitize=fuzzer,address,undefined -g -I/src/libgit2/include/git2/sys -I/src/libgit2/include -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -I/src/libgit2/build/src -I/src/libgit2/build/src/util -I/src/libgit2/build/include -I/src/libgit2/build/gen_headers -I/src/libgit2/src/libgit2 -I/src/libgit2/src/util -I/src/libgit2/fuzzers -fno-omit-frame-pointer -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=deprecated-declarations -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=unknown-warning-option -Wno-error=vla-cxx-extension -stdlib=libc++ /work/fuzzer_utils.o /work/lib/libgit2.a
