# Coverage report — dataset_v2

Generated 2026-08-29. One row per benchmark case.

## Method

Corpus **replay**, not fuzzing. Each case runs its project's official OSS-Fuzz
global corpus once through the coverage build:

    <target>_cov -merge=1 -timeout=100 /tmp/empty /corpus
    llvm-profdata merge -sparse /tmp/dumps/*.profraw -o cov.profdata
    llvm-cov export -summary-only -instr-profile=cov.profdata <target>_cov

`-merge=1` is the procedure OSS-Fuzz itself uses for its coverage reports, and it
tolerates inputs that crash the target. `-summary-only` omits the per-line segment
arrays: without it the export reached 965 MB on the largest target and was
OOM-killed. The summaries it keeps are identical — re-measuring all 100 cases with
and without the flag gave the same numbers to the reported precision.

Coverage is **scoped to the project's own sources**, using the prefix the build's
own `srcmap.json` names. Without scoping, statically linked dependencies inflate
the denominator: libvips read 1632 files / 415,751 lines (42.83%) unscoped against
396 files / 73,117 lines scoped. The scoped figure matches Google's public report
for the same target (50,173/73,117 = 68.62% here, 50,729/73,117 = 69.38% official).

Seven cases have a source prefix that differs from `/src/<project>/`:
binutils -> `/src/binutils-gdb/`, cmake (x5) -> `/src/CMake/`, simd -> `/src/Simd/`.

Replay is deterministic: the same corpus against the same binary gives the same
execution count and coverage on every run. `-fork=1` is deliberately never used —
fork mode reinterprets `-runs=0` as a short fuzzing session.

## Headline

| | |
|---|---|
| Cases | 100 |
| Seeds replayed | 625,891 |
| Line coverage, median | 18.06% |
| Line coverage, min / max | 0.20% / 87.32% |

### Reading the low end

Absolute percentage is a per-target measurement against the **whole project's**
source, so a deliberately narrow harness reads low without being defective.
`cmake/cmVersionFuzzer` covers 223 lines because parsing a version string is all
it does; the denominator is all of CMake. The evaluation compares systems on the
same target, where this denominator cancels.

Comparisons between harnesses on the same target are unaffected: the denominator is
identical on both sides and cancels.

## Per case

| Case | Lines | Covered / Total | Funcs | Files in scope | Seeds | Edges |
|---|---:|---:|---:|---:|---:|---:|
| `woff2/convert_woff2ttf_fuzzer_new_entry` | 87.32% | 3,477 / 3,982 | 79.35% | 28/28 | 8,057 | 1,856 |
| `libyaml/libyaml_emitter_fuzzer` | 85.84% | 4,257 / 4,959 | 84.80% | 9/11 | 9,882 | 3,171 |
| `libyaml/libyaml_reformatter_alt_fuzzer` | 82.22% | 4,475 / 5,443 | 81.63% | 11/13 | 11,104 | 3,098 |
| `astc-encoder/fuzz_astc_compress` | 79.44% | 9,025 / 11,361 | 84.84% | 26/26 | 2,420 | 3,587 |
| `libyaml/libyaml_loader_fuzzer` | 78.24% | 2,862 / 3,658 | 69.85% | 8/9 | 8,283 | 1,982 |
| `fast_float/from_chars` | 78.21% | 1,407 / 1,799 | 76.47% | 8/8 | 5,462 | 866 |
| `libyaml/libyaml_parser_fuzzer` | 77.28% | 2,605 / 3,371 | 65.57% | 7/8 | 8,104 | 1,907 |
| `hunspell/morphfuzzer` | 73.89% | 7,946 / 10,754 | 66.94% | 27/27 | 19,217 | 11,271 |
| `wabt/wasm_objdump_fuzzer` | 72.41% | 5,548 / 7,662 | 66.59% | 29/30 | 13,172 | 4,859 |
| `cmake/cmListFileLexerFuzzer` | 68.63% | 792 / 1,154 | 62.96% | 2/2 | 1,342 | 225 |
| `libvips/vips_fuzzer` | 68.62% | 50,173 / 73,117 | 75.39% | 396/1632 | 34,723 | 65,211 |
| `libidn2/libidn2_to_ascii_8z_fuzzer` | 68.22% | 4,363 / 6,396 | 66.67% | 66/66 | 2,904 | 789 |
| `wabt/read_binary_ir_fuzzer` | 63.26% | 9,331 / 14,751 | 57.98% | 47/48 | 20,638 | 8,408 |
| `eigen/dense_solver_fuzzer` | 59.19% | 8,483 / 14,332 | 63.21% | 138/139 | 4,335 | 11,880 |
| `zlib/compress_fuzzer` | 56.75% | 1,975 / 3,480 | 54.63% | 15/16 | 3,760 | 721 |
| `zlib/zlib_uncompress_fuzzer` | 55.63% | 1,012 / 1,819 | 37.26% | 11/12 | 1,577 | 308 |
| `zlib/zlib_uncompress2_fuzzer` | 55.41% | 1,008 / 1,819 | 35.29% | 11/12 | 1,553 | 308 |
| `wabt/wasm2wat_fuzzer` | 54.35% | 3,954 / 7,275 | 42.21% | 28/29 | 11,848 | 3,783 |
| `pugixml/fuzz_xpath` | 54.35% | 4,218 / 7,761 | 42.78% | 3/3 | 11,030 | 3,311 |
| `libyaml/libyaml_scanner_fuzzer` | 53.25% | 1,795 / 3,371 | 46.72% | 7/8 | 7,104 | 1,563 |
| `zlib/gzio_fuzzer` | 50.02% | 2,271 / 4,540 | 65.75% | 18/19 | 2,408 | 1,062 |
| `lz4/compress_frame_fuzzer` | 48.92% | 2,363 / 4,830 | 43.77% | 12/12 | 6,725 | 1,243 |
| `pycryptodome/sha384_fuzzer` | 48.12% | 218 / 453 | 40.74% | 5/6 | 304 | 45 |
| `wabt/wasm_interp_fuzzer` | 47.13% | 6,409 / 13,599 | 43.09% | 42/43 | 11,114 | 6,351 |
| `pycryptodome/ripemd160_fuzzer` | 44.05% | 148 / 336 | 39.13% | 4/5 | 265 | 48 |
| `pycryptodome/sha224_fuzzer` | 43.45% | 189 / 435 | 40.74% | 5/6 | 298 | 45 |
| `pycryptodome/sha256_fuzzer` | 43.45% | 189 / 435 | 40.74% | 5/6 | 299 | 45 |
| `zlib/example_small_fuzzer` | 43.22% | 1,479 / 3,422 | 49.04% | 14/15 | 2,487 | 594 |
| `eigen/sparse_fuzzer` | 43.14% | 4,807 / 11,143 | 45.68% | 130/131 | 2,705 | 3,315 |
| `pycryptodome/md4_fuzzer` | 41.11% | 141 / 343 | 31.82% | 4/5 | 262 | 38 |
| `libvips/jpegsave_file_fuzzer` | 38.60% | 28,118 / 72,851 | 45.35% | 396/1632 | 31,468 | 48,778 |
| `libvips/thumbnail_fuzzer` | 38.38% | 27,953 / 72,835 | 44.46% | 396/1632 | 33,553 | 47,197 |
| `libtasn1/libtasn1_pkix_der_fuzzer` | 38.23% | 1,997 / 5,224 | 45.38% | 15/15 | 4,126 | 651 |
| `libvips/smartcrop_fuzzer` | 38.01% | 27,684 / 72,835 | 44.37% | 396/1632 | 29,503 | 45,532 |
| `icu/locale_morph_fuzzer` | 36.77% | 8,349 / 22,703 | 42.57% | 112/112 | 7,456 | 4,356 |
| `libvips/mosaic_fuzzer` | 33.28% | 24,247 / 72,847 | 39.42% | 396/1632 | 27,883 | 38,150 |
| `cmake/cmJSONParserFuzzer` | 29.76% | 1,195 / 4,015 | 28.78% | 11/11 | 3,341 | 910 |
| `boost/boost_regex_replace_fuzzer` | 29.32% | 2,658 / 9,066 | 51.45% | 30/31 | 1,742 | 811 |
| `libtasn1/libtasn1_gnutls_der_fuzzer` | 27.74% | 1,452 / 5,234 | 39.50% | 15/15 | 1,424 | 357 |
| `astc-encoder/fuzz_astc_decompress` | 26.76% | 3,041 / 11,363 | 43.52% | 26/26 | 1,195 | 876 |
| `libsodium/crypto_box_fuzzer` | 25.07% | 2,213 / 8,829 | 27.31% | 97/99 | 390 | 210 |
| `eigen/geometry_fuzzer` | 23.68% | 1,678 / 7,085 | 37.54% | 90/91 | 1,000 | 322 |
| `flatbuffers/scalar_fuzzer` | 23.55% | 2,165 / 9,194 | 20.83% | 30/30 | 5,157 | 3,166 |
| `yara/pe_fuzzer` | 23.39% | 7,034 / 30,074 | 23.47% | 85/85 | 7,936 | 1,800 |
| `binutils/fuzz_disassemble` | 21.93% | 110,755 / 505,110 | 17.13% | 730/730 | 49,128 | 32,397 |
| `simdjson/fuzz_minify` | 21.89% | 3,226 / 14,739 | 21.45% | 99/99 | 12,133 | 1,432 |
| `glaze/cbor_reflection` | 21.11% | 2,162 / 10,241 | 22.16% | 52/52 | 7,793 | 1,096 |
| `glaze/binary_reflection` | 18.96% | 1,932 / 10,191 | 22.42% | 53/53 | 8,857 | 1,115 |
| `flatbuffers/64bit_fuzzer` | 18.60% | 671 / 3,607 | 19.93% | 24/24 | 3,827 | 382 |
| `yara/dotnet_fuzzer` | 18.06% | 5,432 / 30,074 | 24.69% | 85/85 | 7,559 | 1,304 |
| `simdjson/fuzz_parser` | 17.18% | 2,533 / 14,746 | 17.25% | 98/98 | 9,095 | 1,035 |
| `glaze/json_jmespath` | 16.74% | 1,618 / 9,668 | 12.77% | 51/51 | 5,674 | 1,652 |
| `libevent/ws_fuzzer` | 16.25% | 2,430 / 14,953 | 21.70% | 48/49 | 976 | 587 |
| `gnutls/gnutls_server_rawpk_fuzzer` | 16.10% | 12,601 / 78,282 | 24.36% | 345/975 | 3,504 | 4,844 |
| `pugixml/fuzz_parse` | 15.27% | 1,181 / 7,734 | 10.28% | 3/3 | 3,745 | 640 |
| `icu/date_time_pattern_generator_fuzzer` | 15.06% | 12,662 / 84,048 | 18.81% | 345/345 | 7,389 | 5,783 |
| `hwloc/hwloc_fuzzer` | 13.94% | 126 / 904 | 2.70% | 7/8 | 159 | 41 |
| `libplist/jplist_fuzzer` | 13.80% | 1,145 / 8,297 | 22.07% | 26/26 | 2,335 | 443 |
| `flatbuffers/verifier_fuzzer` | 13.31% | 734 / 5,513 | 12.93% | 19/19 | 6,379 | 1,806 |
| `yara/macho_fuzzer` | 13.28% | 3,993 / 30,074 | 21.76% | 85/85 | 4,044 | 750 |
| `icu/normalizer2_fuzzer` | 12.99% | 2,394 / 18,428 | 17.53% | 95/95 | 3,179 | 1,110 |
| `unicorn/fuzz_emu_s390x_be` | 12.67% | 27,054 / 213,603 | 17.44% | 366/366 | 28,223 | 11,329 |
| `libplist/oplist_fuzzer` | 11.91% | 988 / 8,297 | 18.97% | 26/26 | 1,607 | 334 |
| `libevent/http_message_fuzzer` | 11.69% | 1,748 / 14,953 | 11.28% | 48/49 | 3,141 | 728 |
| `mbedtls/fuzz_x509crt` | 11.55% | 4,343 / 37,595 | 14.19% | 148/148 | 7,215 | 1,638 |
| `cmake/cmCMakePathFuzzer` | 11.41% | 143 / 1,253 | 16.67% | 11/11 | 389 | 196 |
| `yara/dex_fuzzer` | 11.22% | 3,373 / 30,074 | 18.21% | 85/85 | 3,776 | 678 |
| `icu/list_format_fuzzer` | 10.47% | 4,333 / 41,370 | 16.49% | 210/210 | 1,099 | 1,695 |
| `libplist/bplist_fuzzer` | 10.28% | 853 / 8,297 | 16.21% | 26/26 | 2,389 | 272 |
| `lz4/decompress_fuzzer` | 10.18% | 491 / 4,822 | 7.91% | 12/12 | 1,008 | 434 |
| `yara/elf_fuzzer` | 9.20% | 2,767 / 30,074 | 18.34% | 85/85 | 7,776 | 895 |
| `icu/unicode_string_codepage_create_fuzzer` | 9.03% | 3,684 / 40,782 | 8.76% | 155/155 | 2,671 | 957 |
| `libevent/utils_fuzzer` | 9.03% | 576 / 6,380 | 5.90% | 30/31 | 1,448 | 211 |
| `mbedtls/fuzz_x509csr` | 8.90% | 3,236 / 36,372 | 12.09% | 148/148 | 3,585 | 1,166 |
| `iperf/auth_fuzzer` | 8.82% | 30 / 340 | 12.50% | 1/2 | 97 | 13 |
| `openssh/privkey_fuzz` | 6.92% | 1,710 / 24,711 | 6.39% | 70/70 | 3,040 | 604 |
| `libtasn1/asn1_decode_simple_ber_fuzzer` | 6.58% | 343 / 5,211 | 7.56% | 15/15 | 1,001 | 117 |
| `libevent/evtag_fuzzer` | 6.23% | 630 / 10,115 | 7.81% | 38/39 | 1,247 | 300 |
| `openssh/pubkey_fuzz` | 5.96% | 1,473 / 24,709 | 5.74% | 70/70 | 2,796 | 505 |
| `lcms/cmsIT8_load_fuzzer` | 5.83% | 1,088 / 18,669 | 6.16% | 25/26 | 2,550 | 556 |
| `hpn-ssh/sig_fuzz` | 5.60% | 650 / 11,613 | 7.76% | 71/71 | 1,216 | 190 |
| `openssh/sshsig_fuzz` | 5.55% | 1,525 / 27,472 | 5.96% | 79/79 | 2,136 | 556 |
| `assimp/assimp_fuzzer_obj` | 5.40% | 5,534 / 102,512 | 6.89% | 410/410 | 10,594 | 6,440 |
| `lcms/cms_md5_fuzzer` | 5.11% | 871 / 17,056 | 6.72% | 25/26 | 1,416 | 204 |
| `mbedtls/fuzz_x509crl` | 4.03% | 1,466 / 36,415 | 3.53% | 148/148 | 2,992 | 658 |
| `glaze/json_minify` | 3.84% | 302 / 7,866 | 4.25% | 48/48 | 1,421 | 153 |
| `glaze/json_prettify` | 3.75% | 298 / 7,940 | 4.04% | 48/48 | 1,325 | 276 |
| `leptonica/graphics_fuzzer` | 3.25% | 3,005 / 92,526 | 6.72% | 124/449 | 1,394 | 918 |
| `libtasn1/asn1_get_object_id_der_fuzzer` | 3.19% | 166 / 5,195 | 5.04% | 15/15 | 731 | 47 |
| `mbedtls/fuzz_pkcs7` | 2.92% | 1,124 / 38,465 | 3.05% | 151/151 | 1,601 | 446 |
| `libtasn1/asn1_decode_simple_der_fuzzer` | 2.69% | 140 / 5,209 | 4.20% | 15/15 | 429 | 41 |
| `openssh/sig_fuzz` | 2.63% | 651 / 24,741 | 2.66% | 70/70 | 1,179 | 190 |
| `simd/simd_load_fuzzer` | 2.60% | 4,815 / 184,989 | 3.49% | 770/771 | 7,397 | 2,385 |
| `libgit2/patch_parse_fuzzer` | 1.77% | 1,732 / 97,769 | 3.55% | 327/327 | 2,735 | 439 |
| `libgit2/config_file_fuzzer` | 1.73% | 1,687 / 97,322 | 3.75% | 326/326 | 1,512 | 363 |
| `eigen/tensor_fuzzer` | 1.72% | 140 / 8,119 | 4.14% | 62/63 | 344 | 68 |
| `libgit2/midx_fuzzer` | 1.68% | 1,627 / 97,039 | 2.87% | 325/325 | 2,066 | 335 |
| `znc/msg_parse_fuzzer` | 1.20% | 271 / 22,565 | 1.33% | 74/75 | 2,142 | 601 |
| `cmake/cmGeneratorExpressionFuzzer` | 0.52% | 1,683 / 321,722 | 1.21% | 1205/1205 | 2,507 | 754 |
| `cmake/cmVersionFuzzer` | 0.20% | 223 / 109,507 | 0.62% | 343/343 | 364 | 113 |
