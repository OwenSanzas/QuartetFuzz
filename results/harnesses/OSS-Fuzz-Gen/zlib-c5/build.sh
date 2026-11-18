#!/bin/bash -eu
# Copyright 2016 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################

if ! ./configure; then
    cat configure.log
    exit 1
fi

make -j$(nproc) clean
make -j$(nproc) all

# Skip make check for memory sanitizer - zlib's tests have uninitialized memory issues
if [ "$SANITIZER" != "memory" ]; then
    make -j$(nproc) check
fi

# NOTE: -std=c++17 is explicitly appended AFTER $CXXFLAGS so that it takes
# precedence over any `-std=c++11` that may be present in $CXXFLAGS. The
# installed fuzzer/FuzzedDataProvider.h relies on C++17 standard library
# type traits (std::is_integral_v, std::is_floating_point_v,
# std::conditional_t, std::is_enum_v), which fail to compile under C++11.
for f in $(find $SRC -name '*_fuzzer.cc'); do
    b=$(basename -s .cc $f)
    $CXX $CXXFLAGS -std=c++17 -I. $f -o $OUT/$b $LIB_FUZZING_ENGINE ./libz.a
done

zip $OUT/seed_corpus.zip *.*

for f in $(find $SRC -name '*_fuzzer.c'); do
    b=$(basename -s .c $f)
    $CC $CFLAGS -I. $f -c -o /tmp/$b.o
    $CXX $CXXFLAGS -std=c++17 -o $OUT/$b /tmp/$b.o -stdlib=libc++ $LIB_FUZZING_ENGINE ./libz.a
    rm -f /tmp/$b.o
    ln -sf $OUT/seed_corpus.zip $OUT/${b}_seed_corpus.zip
done