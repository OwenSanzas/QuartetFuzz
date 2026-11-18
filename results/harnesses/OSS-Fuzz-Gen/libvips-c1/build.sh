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

export PKG_CONFIG="pkg-config --static"
export PKG_CONFIG_PATH="$WORK/lib/pkgconfig"
export CPPFLAGS="-I$WORK/include"
export LDFLAGS="-L$WORK/lib"

# `-fuse-ld=gold` can't be passed via `CFLAGS` and `CXXFLAGS` as Meson
# injects `-Werror=ignored-optimization-argument` during compile tests.
if [[ "${CFLAGS:-}" == *"-fuse-ld=gold"* ]]; then
  export CFLAGS="${CFLAGS//-fuse-ld=gold/}"
  export CC_LD=gold
fi
if [[ "${CXXFLAGS:-}" == *"-fuse-ld=gold"* ]]; then
  export CXXFLAGS="${CXXFLAGS//-fuse-ld=gold/}"
  export CXX_LD=gold
fi

export MAKEFLAGS="-j$(nproc)"

cd "$SRC/libvips"

# Helper: run a command quietly, logging full output to a file, and only
# print the tail of that log (to stderr, which is reliably captured) if the
# command fails. This keeps the overall script output small (avoiding
# log-size truncation issues) while still surfacing the relevant error
# output on failure.
run_quiet() {
  local log="$1"
  shift
  if ! "$@" > "$log" 2>&1; then
    {
      echo "===== Command failed: $* ====="
      echo "----- tail of $log -----"
      tail -n 300 "$log"
      echo "----- end of log -----"
    } >&2
    exit 1
  fi
}

# ----------------------------------------------------------------------------
# Minimal dependency build.
#
# The function under test, vips_jpegsave_buffer(), only requires JPEG
# support. Building the full set of third-party image format libraries
# (aom, libheif, pdfium, highway, libjxl, libultrahdr, libwebp, libtiff,
# libpng, lcms, cgif, libimagequant, libexif, zlib, ...) from source is slow
# and unreliable in this environment. Instead, we only build libjpeg-turbo
# from source (required, statically linked and sanitizer-instrumented for
# effective fuzzing) and explicitly disable all other optional external
# image format libraries when configuring libvips. Base dependencies
# (glib-2.0, gobject-2.0, gio-2.0, expat, fftw3) are already available as
# system packages and are picked up automatically by Meson via pkg-config.
# ----------------------------------------------------------------------------

# libjpeg-turbo
pushd "$SRC/libjpeg-turbo" > /dev/null
run_quiet /tmp/libjpeg_cmake.log \
  cmake \
  -GNinja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$WORK" \
  -DENABLE_STATIC=TRUE \
  -DENABLE_SHARED=FALSE \
  -DWITH_TURBOJPEG=FALSE \
  -DWITH_TOOLS=FALSE \
  -DWITH_TESTS=FALSE \
  .
run_quiet /tmp/libjpeg_build.log \
  cmake --build . --target install
popd > /dev/null

TSAN_ARGS=""
if [ "${SANITIZER:-}" = "undefined" ]; then
  export CFLAGS+=" -fsanitize-ignorelist=$PWD/suppressions/ubsan_ignorelist.txt"
  export CXXFLAGS+=" -fsanitize-ignorelist=$PWD/suppressions/ubsan_ignorelist.txt"
elif [ "${SANITIZER:-}" = "thread" ]; then
  TSAN_ARGS="--force-fallback-for=glib -Dglib:glib_debug=disabled -Dglib:nls=disabled -Dglib:sysprof=disabled -Dglib:tests=false"
fi

# libvips, with all optional external image format libraries disabled
# except JPEG (which is all that's needed to exercise
# vips_jpegsave_buffer()). Note: cpp_std is explicitly bumped to c++17
# (overriding the project default of c++14) because our fuzz target
# includes <fuzzer/FuzzedDataProvider.h>, which requires C++17 variable
# templates such as std::is_integral_v.
run_quiet /tmp/meson_setup.log \
  meson setup build --prefix="$WORK" --libdir=lib --prefer-static --default-library=static --buildtype=debug $TSAN_ARGS \
  -Dcpp_std=c++17 \
  -Dbackend_max_links=4 -Dexamples=false -Dman=false -Dpo=false \
  -Dtests=false -Dtools=false -Dcplusplus=false -Dmodules=disabled -Dfuzz=true \
  -Dfuzzing_engine=oss-fuzz -Dfuzzer_ldflags="$LIB_FUZZING_ENGINE" \
  -Dcpp_link_args="$LDFLAGS -Wl,-rpath=\$ORIGIN/lib" \
  -Djpeg=enabled \
  -Dheif=disabled -Dheif-module=disabled \
  -Duhdr=disabled \
  -Djpeg-xl=disabled -Djpeg-xl-module=disabled \
  -Dpdfium=disabled \
  -Dhighway=disabled \
  -Dwebp=disabled \
  -Dtiff=disabled \
  -Dpng=disabled \
  -Dlcms=disabled \
  -Dcgif=disabled \
  -Dimagequant=disabled \
  -Dexif=disabled \
  -Dzlib=disabled \
  -Dfftw=disabled \
  -Dfontconfig=disabled \
  -Darchive=disabled \
  -Dcfitsio=disabled \
  -Dmagick=disabled -Dmagick-module=disabled \
  -Dmatio=disabled \
  -Dnifti=disabled \
  -Dopenexr=disabled \
  -Dopenjpeg=disabled \
  -Dopenslide=disabled -Dopenslide-module=disabled \
  -Dpangocairo=disabled \
  -Dpoppler=disabled -Dpoppler-module=disabled \
  -Dquantizr=disabled \
  -Draw=disabled \
  -Drsvg=disabled \
  -Dspng=disabled \
  -Dorc=disabled \
  -Dintrospection=disabled

# Build first (separately from install) so build errors are clearly
# isolated and their output captured/shown on failure.
run_quiet /tmp/ninja_build.log \
  ninja -C build

run_quiet /tmp/meson_install.log \
  meson install -C build --tag devel --no-rebuild

# Copy fuzz executables to $OUT
find build/fuzz -maxdepth 1 -executable -type f -exec cp -v '{}' "$OUT" \;

# All shared libraries needed during fuzz target execution should be inside
# the $OUT/lib directory
mkdir -p "$OUT/lib"
cp "$WORK"/lib/*.so "$OUT/lib" 2>/dev/null || true

if [ -d "$SRC/seed-corpora" ]; then
  pushd "$SRC/seed-corpora" > /dev/null
  zip -rq "$OUT/seed_corpus.zip" \
    afl-testcases/{gif*,jpeg*,png,tiff,webp} \
    common \
    vips || true
  popd > /dev/null
fi

# Merge the test images in the seed corpus, exclude files larger than 4k
find test/test-suite/images -type f -size -4k | \
  zip -r -@ "$OUT/seed_corpus.zip" || true

# Link corpus
for fuzzer in "$OUT"/*_fuzzer; do
  target=$(basename "$fuzzer")
  ln -sf "seed_corpus.zip" "$OUT/${target}_seed_corpus.zip"
done

# Generate dictionary for vips_fuzzer (best-effort; don't fail the build if
# this step has issues)
if [ -x "$OUT/generate_vips_dict" ]; then
  LD_LIBRARY_PATH="$OUT/lib" "$OUT/generate_vips_dict" > "$OUT/vips_fuzzer.dict" 2>/dev/null || true
  rm -f "$OUT/generate_vips_dict"
fi

# Copy options and remaining dictionary files to $OUT
find fuzz -name '*_fuzzer.dict' -exec cp -v '{}' "$OUT" \;
find fuzz -name '*_fuzzer.options' -exec cp -v '{}' "$OUT" \;