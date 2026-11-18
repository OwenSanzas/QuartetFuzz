#!/bin/bash -eu
#
# Copyright 2019 Google LLC
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

mkdir -p ${WORK}/double-conversion
cd ${WORK}/double-conversion

fuzzer="string_to_double_fuzzer"

SRC_DIR="${SRC}/double-conversion/double-conversion"

# Only compile and link the translation units that are actually reachable
# from StringToDoubleConverter::StringToDouble. Building/linking the full
# libdouble-conversion.a (as the original build did) pulls in
# double-to-string.cc, fast-dtoa.cc, fixed-dtoa.cc, and bignum-dtoa.cc --
# all part of the unrelated double->string conversion direction, which is
# structurally unreachable from this fuzz target and only inflates the
# coverage denominator.
${CXX} ${CXXFLAGS} -std=c++17 -I"${SRC_DIR}" \
    -c "${SRC_DIR}/string-to-double.cc" -o string-to-double.o
${CXX} ${CXXFLAGS} -std=c++17 -I"${SRC_DIR}" \
    -c "${SRC_DIR}/strtod.cc" -o strtod.o
${CXX} ${CXXFLAGS} -std=c++17 -I"${SRC_DIR}" \
    -c "${SRC_DIR}/bignum.cc" -o bignum.o
${CXX} ${CXXFLAGS} -std=c++17 -I"${SRC_DIR}" \
    -c "${SRC_DIR}/cached-powers.cc" -o cached-powers.o

${CXX} ${CXXFLAGS} -std=c++17 -I"${SRC_DIR}" \
    -c ${SRC}/${fuzzer}.cc \
    -o ${fuzzer}.o

${CXX} ${CXXFLAGS} -std=c++17 ${fuzzer}.o \
    string-to-double.o strtod.o bignum.o cached-powers.o \
    -o ${OUT}/${fuzzer} "${LIB_FUZZING_ENGINE}"

# Build a seed corpus containing numeric literals that are known (from the
# project's own regression tests in test/cctest/test-strtod.cc) to sit
# extremely close to the exact midpoint between two adjacent doubles, as
# well as other structurally-interesting literals (hex, octal, hex-float,
# huge exponent, infinity/nan, octal-with-dot, and exponent-clamp-boundary
# values). Because the harness consumes the flag/value/symbol/separator/
# mode bytes from the input via FuzzedDataProvider, and FuzzedDataProvider's
# Consume* methods return deterministic defaults (false / 0 / min-value)
# once the remaining bytes are exhausted, these seeds still provide a
# useful starting point for the structured-mode generator as well as the
# raw-bytes fallback mode.
seed_corpus_dir="${WORK}/seed_corpus"
rm -rf "${seed_corpus_dir}"
mkdir -p "${seed_corpus_dir}"

cat > "${seed_corpus_dir}/seed_bignum_1" <<'EOF'
72057594037927928
EOF
cat > "${seed_corpus_dir}/seed_bignum_2" <<'EOF'
72057594037927932
EOF
cat > "${seed_corpus_dir}/seed_bignum_3" <<'EOF'
72057594037927936
EOF
cat > "${seed_corpus_dir}/seed_bignum_4" <<'EOF'
9223372036854775296
EOF
cat > "${seed_corpus_dir}/seed_bignum_5" <<'EOF'
9223372036854775808
EOF
cat > "${seed_corpus_dir}/seed_bignum_6" <<'EOF'
10141204801825834649023672221696
EOF
cat > "${seed_corpus_dir}/seed_bignum_7" <<'EOF'
7205759403792793199999
EOF
cat > "${seed_corpus_dir}/seed_bignum_8" <<'EOF'
922337203685477529599999
EOF
cat > "${seed_corpus_dir}/seed_bignum_9" <<'EOF'
7205759403792793200001
EOF
cat > "${seed_corpus_dir}/seed_bignum_10" <<'EOF'
5708990770823839207320493820740630171355185152
EOF
cat > "${seed_corpus_dir}/seed_hex" <<'EOF'
0x1A2B3C
EOF
cat > "${seed_corpus_dir}/seed_hexfloat" <<'EOF'
0x1.8p3
EOF
cat > "${seed_corpus_dir}/seed_octal" <<'EOF'
01234567
EOF
cat > "${seed_corpus_dir}/seed_octal_dot" <<'EOF'
01234567.89
EOF
cat > "${seed_corpus_dir}/seed_octal_dot_only" <<'EOF'
0777.
EOF
cat > "${seed_corpus_dir}/seed_huge_exponent" <<'EOF'
1e999999999
EOF
cat > "${seed_corpus_dir}/seed_tiny_exponent" <<'EOF'
1e-999999999
EOF
cat > "${seed_corpus_dir}/seed_exponent_clamp_no" <<'EOF'
1e1073741823
EOF
cat > "${seed_corpus_dir}/seed_exponent_clamp_yes" <<'EOF'
1e1073741829
EOF
cat > "${seed_corpus_dir}/seed_exponent_clamp_exact_prefix" <<'EOF'
1e107374182
EOF
cat > "${seed_corpus_dir}/seed_infinity" <<'EOF'
infinity
EOF
cat > "${seed_corpus_dir}/seed_neg_infinity" <<'EOF'
-infinity
EOF
cat > "${seed_corpus_dir}/seed_nan" <<'EOF'
nan
EOF
cat > "${seed_corpus_dir}/seed_plain" <<'EOF'
3.14159
EOF
cat > "${seed_corpus_dir}/seed_with_separator" <<'EOF'
1_000_000
EOF
cat > "${seed_corpus_dir}/seed_min_denormal" <<'EOF'
4.9406564584124654e-324
EOF
cat > "${seed_corpus_dir}/seed_max_double" <<'EOF'
1.7976931348623157e308
EOF
cat > "${seed_corpus_dir}/seed_above_max_double" <<'EOF'
17976931348623159e292
EOF

zip -j -r "${OUT}/${fuzzer}_seed_corpus.zip" "${seed_corpus_dir}"