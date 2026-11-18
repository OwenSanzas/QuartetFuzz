#!/bin/bash -eu
# Copyright 2019 Google Inc.
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

export ASAN_OPTIONS="detect_leaks=0"

if [[ $CFLAGS = *sanitize=address* ]]
then
    export CXXFLAGS="$CXXFLAGS -DASAN"
fi

if [[ $CFLAGS = *sanitize=memory* ]]
then
    export CXXFLAGS="$CXXFLAGS -DMSAN"
fi

export CFLAGS="$CFLAGS -D_FORTIFY_SOURCE=0"
export CXXFLAGS="$CXXFLAGS -D_FORTIFY_SOURCE=0 -D_GLIBCXX_DEBUG"

# Force C++17 so that fuzzer/FuzzedDataProvider.h (which uses C++17 features
# such as std::is_integral_v) compiles correctly. The oss-fuzz Makefile.am
# for the FLAC fuzzers hardcodes AM_CXXFLAGS = -std=c++11, but the compile
# rule is "$(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS)", i.e.
# CXXFLAGS (captured from this environment variable at ./configure time)
# is placed *after* AM_CXXFLAGS on the compiler command line, so this
# -std=c++17 flag takes precedence over the hardcoded -std=c++11. This
# must be exported before running ./configure for flac below so that it
# gets frozen into the generated Makefile's CXXFLAGS variable. (Verified:
# without this, the generated Makefile's CXXFLAGS has no -std= override at
# all, so the hardcoded -std=c++11 in AM_CXXFLAGS wins and
# FuzzedDataProvider.h fails to compile due to missing C++17 type traits
# such as std::is_integral_v / std::conditional_t.)
export CXXFLAGS="$CXXFLAGS -std=c++17"

# Build libogg
mkdir $SRC/libogg-install
cd $SRC/ogg
./autogen.sh
./configure --disable-crc --disable-shared --prefix="$SRC/libogg-install"
make -j$(nproc)
make install

# Build libflac
cd $SRC/flac/
./autogen.sh
if [[ $CFLAGS = *sanitize=memory* ]]
then
    LD_LIBRARY_PATH="$SRC/libogg-install/lib" ./configure --with-ogg="$SRC/libogg-install" --enable-static --disable-shared --disable-oggtest --disable-examples --disable-programs --disable-xmms-plugin --disable-asm-optimizations --disable-sse --enable-oss-fuzzers
else
    LD_LIBRARY_PATH="$SRC/libogg-install/lib" ./configure --with-ogg="$SRC/libogg-install" --enable-static --disable-shared --disable-oggtest --disable-examples --disable-programs --disable-xmms-plugin --enable-oss-fuzzers
fi
make -j$(nproc)

# Copy decoder fuzzers
cd $SRC/flac/oss-fuzz
cp fuzzer_* $OUT
cd $SRC

# Build fuzzer_exo
$CXX $CXXFLAGS -I $SRC/flac/oss-fuzz/ -I $SRC/flac/include/ -I $SRC/ExoPlayer/extensions/flac/src/main/jni/ -I /usr/lib/jvm/java-11-openjdk-amd64/include/ -I /usr/lib/jvm/java-11-openjdk-amd64/include/linux/ fuzzer_exo.cpp \
    $SRC/flac/src/libFLAC++/.libs/libFLAC++.a $SRC/flac/src/libFLAC/.libs/libFLAC.a $SRC/libogg-install/lib/libogg.a $LIB_FUZZING_ENGINE -o $OUT/fuzzer_exo

# Build libflac again for encoder fuzzers, but now with additional define
cd $SRC/flac/
echo "#define FUZZING_BUILD_MODE_FLAC_SANITIZE_SIGNED_INTEGER_OVERFLOW" >> config.h

make -j$(nproc)

# Copy encoder fuzzers
cd $SRC/flac/oss-fuzz
cp fuzzer_encoder fuzzer_encoder_v2 $OUT

# Copy seed corpus
cd $SRC/flac/oss-fuzz/seedcorpus || exit 0
for fuzzerName in *; do
    zip -j $OUT/${fuzzerName}_seed_corpus.zip ${fuzzerName}/*
done