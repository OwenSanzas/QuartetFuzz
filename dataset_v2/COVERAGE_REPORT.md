# Coverage report — dataset_v2

Generated 2026-09-01. One row per benchmark case.

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
| Seeds replayed | 764,396 |
| Line coverage, median | 23.59% |
| Line coverage, min / max | 1.20% / 87.32% |

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
| `capstone/fuzz_disasmnext` | 87.05% | 248,394 / 285,341 | 71.94% | 194/194 | 59,623 | 22,517 |
| `libyaml/libyaml_emitter_fuzzer` | 85.84% | 4,257 / 4,959 | 84.80% | 9/11 | 9,882 | 3,171 |
| `libcbor/cbor_load_fuzzer` | 83.67% | 2,547 / 3,044 | 91.19% | 22/23 | 3,743 | 1,133 |
| `libyaml/libyaml_reformatter_alt_fuzzer` | 82.22% | 4,475 / 5,443 | 81.63% | 11/13 | 11,104 | 3,098 |
| `libyaml/libyaml_loader_fuzzer` | 78.24% | 2,862 / 3,658 | 69.85% | 8/9 | 8,283 | 1,982 |
| `fast_float/from_chars` | 78.21% | 1,407 / 1,799 | 76.47% | 8/8 | 5,462 | 866 |
| `libyaml/libyaml_parser_fuzzer` | 77.28% | 2,605 / 3,371 | 65.57% | 7/8 | 8,104 | 1,907 |
| `llhttp/fuzz_parser` | 74.52% | 7,027 / 9,430 | 75.97% | 5/5 | 4,591 | 1,118 |
| `hunspell/morphfuzzer` | 73.89% | 7,946 / 10,754 | 66.94% | 27/27 | 19,217 | 11,271 |
| `wabt/wasm_objdump_fuzzer` | 72.41% | 5,548 / 7,662 | 66.59% | 29/30 | 13,172 | 4,859 |
| `flac/fuzzer_encoder_v2` | 68.80% | 10,720 / 15,581 | 63.78% | 48/50 | 12,285 | 3,985 |
| `libvips/vips_fuzzer` | 68.62% | 50,173 / 73,117 | 75.39% | 396/1632 | 34,723 | 65,211 |
| `libidn2/libidn2_to_ascii_8z_fuzzer` | 68.22% | 4,363 / 6,396 | 66.67% | 66/66 | 2,904 | 789 |
| `boost/boost_graph_graphml_fuzzer` | 63.42% | 2,698 / 4,254 | 60.81% | 134/135 | 7,336 | 2,474 |
| `wabt/read_binary_ir_fuzzer` | 63.26% | 9,331 / 14,751 | 57.98% | 47/48 | 20,638 | 8,408 |
| `uriparser/uri_free_fuzzer` | 58.95% | 1,321 / 2,241 | 68.08% | 13/13 | 6,929 | 1,704 |
| `flac/fuzzer_encoder` | 56.90% | 8,359 / 14,691 | 50.30% | 55/57 | 11,810 | 3,300 |
| `zlib/compress_fuzzer` | 56.75% | 1,975 / 3,480 | 54.63% | 15/16 | 3,760 | 721 |
| `zlib/zlib_uncompress_fuzzer` | 55.63% | 1,012 / 1,819 | 37.26% | 11/12 | 1,577 | 308 |
| `zlib/zlib_uncompress2_fuzzer` | 55.41% | 1,008 / 1,819 | 35.29% | 11/12 | 1,553 | 308 |
| `wabt/wasm2wat_fuzzer` | 54.35% | 3,954 / 7,275 | 42.21% | 28/29 | 11,848 | 3,783 |
| `pugixml/fuzz_xpath` | 54.35% | 4,218 / 7,761 | 42.78% | 3/3 | 11,030 | 3,311 |
| `libyaml/libyaml_scanner_fuzzer` | 53.25% | 1,795 / 3,371 | 46.72% | 7/8 | 7,104 | 1,563 |
| `muparser/set_eval_fuzzer` | 51.26% | 1,565 / 3,053 | 52.29% | 13/14 | 3,796 | 1,789 |
| `zlib/gzio_fuzzer` | 50.02% | 2,271 / 4,540 | 65.75% | 18/19 | 2,408 | 1,062 |
| `lz4/compress_frame_fuzzer` | 48.92% | 2,363 / 4,830 | 43.77% | 12/12 | 6,725 | 1,243 |
| `avahi/fuzz-strlst` | 48.75% | 293 / 601 | 43.18% | 6/6 | 417 | 119 |
| `nghttp2/nghttp2_fuzzer_fdp` | 48.29% | 5,610 / 11,618 | 51.53% | 34/34 | 12,290 | 1,590 |
| `wabt/wasm_interp_fuzzer` | 47.13% | 6,409 / 13,599 | 43.09% | 42/43 | 11,114 | 6,351 |
| `cjson/cjson_read_fuzzer` | 44.03% | 1,022 / 2,321 | 28.07% | 3/3 | 2,092 | 330 |
| `zlib/example_small_fuzzer` | 43.22% | 1,479 / 3,422 | 49.04% | 14/15 | 2,487 | 594 |
| `eigen/sparse_fuzzer` | 43.14% | 4,807 / 11,143 | 45.68% | 130/131 | 2,705 | 3,315 |
| `jq/jq_fuzz_fixed` | 42.53% | 17,237 / 40,528 | 51.41% | 52/52 | 8,319 | 4,441 |
| `libvips/jpegsave_file_fuzzer` | 38.60% | 28,118 / 72,851 | 45.35% | 396/1632 | 31,468 | 48,778 |
| `libvips/thumbnail_fuzzer` | 38.38% | 27,953 / 72,835 | 44.46% | 396/1632 | 33,553 | 47,197 |
| `libtasn1/libtasn1_pkix_der_fuzzer` | 38.23% | 1,997 / 5,224 | 45.38% | 15/15 | 4,126 | 651 |
| `libvips/smartcrop_fuzzer` | 38.01% | 27,684 / 72,835 | 44.37% | 396/1632 | 29,503 | 45,532 |
| `icu/locale_morph_fuzzer` | 36.77% | 8,349 / 22,703 | 42.57% | 112/112 | 7,456 | 4,356 |
| `uriparser/uri_dissect_query_malloc_fuzzer` | 33.87% | 443 / 1,308 | 37.74% | 11/11 | 1,547 | 242 |
| `libvips/mosaic_fuzzer` | 33.28% | 24,247 / 72,847 | 39.42% | 396/1632 | 27,883 | 38,150 |
| `boost/boost_programoptions_fuzzer` | 30.18% | 1,023 / 3,390 | 38.46% | 71/72 | 2,325 | 1,538 |
| `boost/boost_regex_replace_fuzzer` | 29.32% | 2,658 / 9,066 | 51.45% | 30/31 | 1,742 | 811 |
| `msgpack-c/unpack_pack_fuzzer` | 28.60% | 1,274 / 4,455 | 22.87% | 48/48 | 2,183 | 504 |
| `libtasn1/libtasn1_gnutls_der_fuzzer` | 27.74% | 1,452 / 5,234 | 39.50% | 15/15 | 1,424 | 357 |
| `lz4/round_trip_hc_fuzzer` | 27.05% | 1,298 / 4,799 | 21.94% | 12/12 | 9,543 | 986 |
| `miniz/compress_fuzzer` | 25.91% | 1,367 / 5,275 | 21.61% | 3/3 | 3,064 | 676 |
| `libsodium/crypto_box_fuzzer` | 25.07% | 2,213 / 8,829 | 27.31% | 97/99 | 390 | 210 |
| `lz4/decompress_frame_fuzzer` | 24.23% | 1,175 / 4,849 | 18.44% | 12/12 | 2,628 | 514 |
| `miniz/small_fuzzer` | 23.59% | 1,251 / 5,303 | 21.72% | 3/3 | 2,381 | 652 |
| `flatbuffers/scalar_fuzzer` | 23.55% | 2,165 / 9,194 | 20.83% | 30/30 | 5,157 | 3,166 |
| `yara/pe_fuzzer` | 23.39% | 7,034 / 30,074 | 23.47% | 85/85 | 7,936 | 1,800 |
| `binutils/fuzz_disassemble` | 21.93% | 110,755 / 505,110 | 17.13% | 730/730 | 49,128 | 32,397 |
| `simdjson/fuzz_minify` | 21.89% | 3,226 / 14,739 | 21.45% | 99/99 | 12,133 | 1,432 |
| `glaze/cbor_reflection` | 21.11% | 2,162 / 10,241 | 22.16% | 52/52 | 7,793 | 1,096 |
| `glaze/binary_reflection` | 18.96% | 1,932 / 10,191 | 22.42% | 53/53 | 8,857 | 1,115 |
| `duckdb/parse_fuzz_test` | 18.64% | 98,992 / 530,986 | 24.99% | 3130/3130 | 4,909 | 75,650 |
| `yara/dotnet_fuzzer` | 18.06% | 5,432 / 30,074 | 24.69% | 85/85 | 7,559 | 1,304 |
| `simdjson/fuzz_dump_raw_tape` | 18.06% | 2,663 / 14,748 | 17.56% | 99/99 | 8,998 | 1,114 |
| `simdjson/fuzz_parser` | 17.18% | 2,533 / 14,746 | 17.25% | 98/98 | 9,095 | 1,035 |
| `glaze/json_jmespath` | 16.74% | 1,618 / 9,668 | 12.77% | 51/51 | 5,674 | 1,652 |
| `libevent/ws_fuzzer` | 16.25% | 2,430 / 14,953 | 21.70% | 48/49 | 976 | 587 |
| `gnutls/gnutls_server_rawpk_fuzzer` | 16.10% | 12,601 / 78,282 | 24.36% | 345/975 | 3,504 | 4,844 |
| `pugixml/fuzz_parse` | 15.27% | 1,181 / 7,734 | 10.28% | 3/3 | 3,745 | 640 |
| `icu/date_time_pattern_generator_fuzzer` | 15.06% | 12,662 / 84,048 | 18.81% | 345/345 | 7,389 | 5,783 |
| `libiec61850/fuzz_mms_decode` | 13.85% | 555 / 4,006 | 16.10% | 15/15 | 945 | 181 |
| `libplist/jplist_fuzzer` | 13.80% | 1,145 / 8,297 | 22.07% | 26/26 | 2,335 | 443 |
| `yara/macho_fuzzer` | 13.28% | 3,993 / 30,074 | 21.76% | 85/85 | 4,044 | 750 |
| `icu/normalizer2_fuzzer` | 12.99% | 2,394 / 18,428 | 17.53% | 95/95 | 3,179 | 1,110 |
| `unicorn/fuzz_emu_s390x_be` | 12.67% | 27,054 / 213,603 | 17.44% | 366/366 | 28,223 | 11,329 |
| `libplist/oplist_fuzzer` | 11.91% | 988 / 8,297 | 18.97% | 26/26 | 1,607 | 334 |
| `mbedtls/fuzz_x509crt` | 11.55% | 4,343 / 37,595 | 14.19% | 148/148 | 7,215 | 1,638 |
| `yara/dex_fuzzer` | 11.22% | 3,373 / 30,074 | 18.21% | 85/85 | 3,776 | 678 |
| `miniz/uncompress_fuzzer` | 10.54% | 553 / 5,247 | 6.12% | 3/3 | 1,193 | 237 |
| `icu/list_format_fuzzer` | 10.47% | 4,333 / 41,370 | 16.49% | 210/210 | 1,099 | 1,695 |
| `miniz/uncompress2_fuzzer` | 10.33% | 541 / 5,238 | 5.61% | 3/3 | 1,217 | 233 |
| `libplist/bplist_fuzzer` | 10.28% | 853 / 8,297 | 16.21% | 26/26 | 2,389 | 272 |
| `lz4/decompress_fuzzer` | 10.18% | 491 / 4,822 | 7.91% | 12/12 | 1,008 | 434 |
| `usrsctp/fuzzer_listen` | 10.00% | 4,522 / 45,206 | 18.83% | 50/50 | 3,217 | 659 |
| `yara/elf_fuzzer` | 9.20% | 2,767 / 30,074 | 18.34% | 85/85 | 7,776 | 895 |
| `icu/unicode_string_codepage_create_fuzzer` | 9.03% | 3,684 / 40,782 | 8.76% | 155/155 | 2,671 | 957 |
| `libevent/utils_fuzzer` | 9.03% | 576 / 6,380 | 5.90% | 30/31 | 1,448 | 211 |
| `mbedtls/fuzz_x509csr` | 8.90% | 3,236 / 36,372 | 12.09% | 148/148 | 3,585 | 1,166 |
| `janet/fuzz_dostring` | 8.88% | 2,059 / 23,197 | 9.49% | 4/4 | 6,257 | 869 |
| `libtasn1/asn1_decode_simple_ber_fuzzer` | 6.58% | 343 / 5,211 | 7.56% | 15/15 | 1,001 | 117 |
| `libevent/evtag_fuzzer` | 6.23% | 630 / 10,115 | 7.81% | 38/39 | 1,247 | 300 |
| `wxwidgets/zip` | 5.67% | 2,189 / 38,577 | 6.99% | 209/209 | 2,897 | 677 |
| `assimp/assimp_fuzzer_obj` | 5.40% | 5,534 / 102,512 | 6.89% | 410/410 | 10,594 | 6,440 |
| `mbedtls/fuzz_x509crl` | 4.03% | 1,466 / 36,415 | 3.53% | 148/148 | 2,992 | 658 |
| `glaze/json_minify` | 3.84% | 302 / 7,866 | 4.25% | 48/48 | 1,421 | 153 |
| `glaze/json_prettify` | 3.75% | 298 / 7,940 | 4.04% | 48/48 | 1,325 | 276 |
| `jq/jq_fuzz_parse_stream` | 3.72% | 1,508 / 40,515 | 6.92% | 52/52 | 3,028 | 567 |
| `leptonica/graphics_fuzzer` | 3.25% | 3,005 / 92,526 | 6.72% | 124/449 | 1,394 | 918 |
| `libtasn1/asn1_get_object_id_der_fuzzer` | 3.19% | 166 / 5,195 | 5.04% | 15/15 | 731 | 47 |
| `mbedtls/fuzz_pkcs7` | 2.92% | 1,124 / 38,465 | 3.05% | 151/151 | 1,601 | 446 |
| `libtasn1/asn1_decode_simple_der_fuzzer` | 2.69% | 140 / 5,209 | 4.20% | 15/15 | 429 | 41 |
| `simd/simd_load_fuzzer` | 2.60% | 4,815 / 184,989 | 3.49% | 770/771 | 7,397 | 2,385 |
| `libgit2/config_file_fuzzer` | 1.73% | 1,687 / 97,322 | 3.75% | 326/326 | 1,512 | 363 |
| `eigen/tensor_fuzzer` | 1.72% | 140 / 8,119 | 4.14% | 62/63 | 344 | 68 |
| `znc/msg_parse_fuzzer` | 1.20% | 271 / 22,565 | 1.33% | 74/75 | 2,142 | 601 |
