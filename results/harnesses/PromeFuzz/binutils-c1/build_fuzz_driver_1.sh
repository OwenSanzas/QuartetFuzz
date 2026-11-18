#! /bin/bash

cd /tmp/promefuzz
clang /pf/out/fuzz_driver/fuzz_driver_1.c -o /pf/out/fuzz_driver/fuzz_driver_1 -fsanitize=fuzzer,address,undefined -g -I/src/binutils-gdb/include -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION -I../include -I../bfd -I../opcodes -fno-omit-frame-pointer -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion -Wno-error=deprecated-declarations -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=unknown-warning-option -Wno-error=vla-cxx-extension -stdlib=libc++ -Wl,--start-group /src/binutils-gdb/opcodes/libopcodes.a /src/binutils-gdb/libctf/.libs/libctf.a /src/binutils-gdb/bfd/.libs/libbfd.a /src/binutils-gdb/zlib/libz.a /src/binutils-gdb/libsframe/.libs/libsframe.a /src/binutils-gdb/libiberty/libiberty.a -Wl,--end-group
