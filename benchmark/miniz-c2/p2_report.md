## target: mz_compress2

### P2.1 Init Sequence
**Claim:** `mz_compress2` is a self-contained single-call API: it builds its own local `mz_stream` (zero-initialized via `memset`), so the caller does NOT need to call `mz_deflateInit`/`mz_deflateInit2` beforehand — `mz_compress2` does that internally. The only prerequisite is that `pDest`, `pDest_len`, `pSource` are valid and `*pDest_len` is set by the caller to the capacity of `pDest` before the call.
**Evidence:**
> miniz/miniz.c:318-333:
> ```
> int mz_compress2(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len, int level)
> {
>     int status;
>     mz_stream stream;
>     memset(&stream, 0, sizeof(stream));
>     if ((mz_uint64)(source_len | *pDest_len) > 0xFFFFFFFFU)
>         return MZ_PARAM_ERROR;
>     stream.next_in = pSource;
>     stream.avail_in = (mz_uint32)source_len;
>     stream.next_out = pDest;
>     stream.avail_out = (mz_uint32)*pDest_len;
>     status = mz_deflateInit(&stream, level);
> ```
> examples/example1.c:23,34,43: caller first computes `cmp_len = compressBound(src_len)`, allocates `pCmp = malloc(cmp_len)`, then calls `compress(pCmp, &cmp_len, ..., src_len)` (which is `mz_compress2` with `MZ_DEFAULT_COMPRESSION`) — no explicit deflate-stream init done by the caller.

### P2.2 Parameter Construction
**Claim:** `pDest` must point to a writable buffer of at least `*pDest_len` bytes (caller-supplied capacity, ideally computed via `mz_compressBound`/`compressBound`). `pDest_len` is in/out: in = buffer capacity, out = bytes actually written. `pSource`/`source_len` describe the input buffer (may be any bytes, any length, including 0). `level` ranges over `[MZ_NO_COMPRESSION(0) .. MZ_BEST_COMPRESSION(9)]`, plus a hidden level `10`, and `MZ_DEFAULT_COMPRESSION(-1)`; values are clamped internally (`MZ_MIN(10, level)`), so levels >10 are silently clamped to 10 and any negative value other than exactly what's `>=0` falls back to `MZ_DEFAULT_LEVEL` (no explicit MZ_PARAM_ERROR for out-of-range levels inside `tdefl_create_comp_flags_from_zip_params`). 64-bit `mz_ulong` values for `source_len`/`*pDest_len` greater than `0xFFFFFFFF` are rejected with `MZ_PARAM_ERROR`.
**Evidence:**
> miniz.h:402-403 (declaration), miniz.h:400-401 comment: "Single-call compression functions mz_compress() and mz_compress2()"
> miniz.h:275-283: `MZ_NO_COMPRESSION=0, MZ_BEST_SPEED=1, ... MZ_BEST_COMPRESSION=9, ... MZ_DEFAULT_COMPRESSION=-1`
> miniz_tdef.c:1461-1464: "/* level may actually range from [0,10] (10 is a 'hidden' max level...) */ mz_uint tdefl_create_comp_flags_from_zip_params(int level, int window_bits, int strategy) { mz_uint comp_flags = s_tdefl_num_probes[(level >= 0) ? MZ_MIN(10, level) : MZ_DEFAULT_LEVEL] ..."
> miniz.c:325-326: `if ((mz_uint64)(source_len | *pDest_len) > 0xFFFFFFFFU) return MZ_PARAM_ERROR;`

### P2.3 Object Lifecycle
**Claim:** Internally: stack-allocate `mz_stream stream` → zero it → set `next_in/avail_in/next_out/avail_out` → `mz_deflateInit(&stream, level)` (create) → `mz_deflate(&stream, MZ_FINISH)` (use, single shot) → `mz_deflateEnd(&stream)` (destroy), on both the success and failure branches. The caller of `mz_compress2` sees none of this — it is fully encapsulated; the caller's lifecycle is just: allocate `pDest`/`pDest_len` → call `mz_compress2` → use `*pDest_len`/`pDest` → free `pDest` when done.
**Evidence:**
> miniz.c:333-345:
> ```
> status = mz_deflateInit(&stream, level);
> if (status != MZ_OK) return status;
> status = mz_deflate(&stream, MZ_FINISH);
> if (status != MZ_STREAM_END) { mz_deflateEnd(&stream); return (status == MZ_OK) ? MZ_BUF_ERROR : status; }
> *pDest_len = stream.total_out;
> return mz_deflateEnd(&stream);
> ```
> Note: `mz_deflateEnd` is called on EVERY path out of `mz_compress2` (both error and success) — no leak of the internal `tdefl_compressor` allocated by `mz_deflateInit2` (miniz.c:217-221).

### P2.4 Return Value Handling
**Claim:** `mz_compress2` returns `MZ_OK` (0) on success; otherwise one of the `mz_deflate`/`mz_deflateInit` error codes: `MZ_PARAM_ERROR` (bad sizes or bad level/window/method), `MZ_STREAM_ERROR` (bogus stream — not reachable from `mz_compress2`'s normal use since it builds its own valid stream), `MZ_MEM_ERROR` (zalloc failure), or `MZ_BUF_ERROR` (output buffer too small — derived when `mz_deflate` returns `MZ_OK` instead of `MZ_STREAM_END`, meaning compression did not finish). Callers must check for `MZ_OK`/`Z_OK` before trusting `*pDest_len`.
**Evidence:**
> miniz.c:334-335: `status = mz_deflateInit(&stream, level); if (status != MZ_OK) return status;`
> miniz.c:338-342: `if (status != MZ_STREAM_END) { mz_deflateEnd(&stream); return (status == MZ_OK) ? MZ_BUF_ERROR : status; }`
> miniz.h:441-442 (doc for mz_uncompress, same pattern): "Returns MZ_OK on success, or one of the error codes from mz_inflate() on failure."
> examples/example1.c:43-50: `cmp_status = compress(...); if (cmp_status != Z_OK) { printf("compress() failed!\n"); free(pCmp); free(pUncomp); return EXIT_FAILURE; }`

### P2.5 Cleanup Sequence
**Claim:** Inside `mz_compress2` all internal state (`tdefl_compressor` allocated by `zalloc` in `mz_deflateInit2`) is freed via `mz_deflateEnd` on every return path — no caller action needed for the internal deflate stream. The caller's own responsibility is only to `free()` the `pDest`/`pCmp` buffer it allocated (and the `pSource` buffer, if heap-allocated) once done using the compressed output — independent of `mz_compress2` success/failure.
**Evidence:**
> miniz.c:299-309 `mz_deflateEnd`: frees `pStream->state` via `pStream->zfree(pStream->opaque, pStream->state)`, sets to NULL.
> miniz.c:340,345: both failure and success paths of `mz_compress2` call `mz_deflateEnd(&stream)`.
> examples/example1.c:47-49,78-80: caller frees `pCmp`/`pUncomp` on every exit path.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current source tree (`miniz/miniz.c`, `miniz/miniz.h`, `miniz/miniz_tdef.c`).
**Evidence:**
> miniz.h:367 `mz_deflateInit`, :374 `mz_deflateInit2`, :398 `mz_deflateBound`, :402 `mz_compress`, :403 `mz_compress2`, :406 `mz_compressBound`, :436 `mz_inflate`, :439 `mz_inflateEnd`, :443 `mz_uncompress`, :444 `mz_uncompress2`.
> miniz.c:191 `mz_deflateInit`, :196 `mz_deflateInit2`, :241 `mz_deflate`, :299 `mz_deflateEnd`, :311 `mz_deflateBound`, :318 `mz_compress2`, :348 `mz_compress`, :353 `mz_compressBound`, :371 `mz_inflateInit2`, :407 `mz_inflateInit`, :438 `mz_inflate`, :550 `mz_inflateEnd`, :561 `mz_uncompress2`, :592 `mz_uncompress`.
> miniz_tdef.c:1462 `tdefl_create_comp_flags_from_zip_params`.

### P2.7 Co-call Constraints
**Claim:** Inside `mz_compress2`, `mz_deflateInit`/`mz_deflateInit2` is always paired with `mz_deflateEnd` (create/destroy pair) — never skipped. There is no "begin/commit" requirement visible to the caller of `mz_compress2` itself. For the lower-level streaming API (`mz_deflateInit`→`mz_deflate`(*)→`mz_deflateEnd`) used internally, `mz_deflate` must only be called after `mz_deflateInit`/`Init2` succeeded and before `mz_deflateEnd`.
**Evidence:**
> miniz.c:333-345 (shown above): Init always followed by End regardless of the `mz_deflate` outcome.
> miniz.h:376: "Quickly resets a compressor without having to reallocate anything. Same as calling mz_deflateEnd() followed by mz_deflateInit()/mz_deflateInit2()." (documents the pairing).

### P2.8 Prerequisite State
**Claim:** No global/library-wide init is required before calling `mz_compress2` (no `mz_init()` style call exists in the header). The only state needed is valid, non-NULL `pDest`/`pDest_len`/`pSource` pointers (unless `source_len==0`, in which case `pSource` may be unused) and `*pDest_len` set to the destination buffer's capacity by the caller.
**Evidence:**
> miniz.h: no "library init" function declared other than `mz_version()` (miniz.h:352) which is informational only.
> miniz.c:321-331: `mz_compress2` zero-initializes its own local `mz_stream`; it does not rely on any external/global state.

### P2.9 Result Usage
**Claim:** The filled buffer `pDest` and output length `*pDest_len` from `mz_compress2`/`mz_compress` are consumed by `mz_uncompress`/`mz_uncompress2` (and by `mz_deflateBound`/`mz_compressBound`, used beforehand to size the buffer, not after). Real callers also compare `compressBound(src_len)` against the actual `cmp_len` returned, and verify round-trip equality after decompression.
**Evidence:**
> examples/example1.c:66: `cmp_status = uncompress(pUncomp, &uncomp_len, pCmp, cmp_len);` — `pCmp`/`cmp_len` are exactly the outputs of the earlier `compress()` call (line 43).
> examples/example1.c:85-87: after uncompress, caller does `if ((uncomp_len != src_len) || (memcmp(pUncomp, s_pStr, (size_t)src_len))) { ... "Decompression failed!" ... }` — validates round trip.
> miniz_tester.cpp:306-317: `printf("Uncompressed size: %u\nCompressed size: %u\n", ...); if (cmp_len > compressBound(uncomp_len)) { print_error("compressBound() returned bogus result\n"); } ... status = uncompress(decomp_buf, &decomp_len, cmp_buf, cmp_len); if ((status != Z_OK) || (decomp_len != uncomp_len) || (memcmp(decomp_buf, p, uncomp_len))) ...` — same pattern: compress output feeds uncompress, bound-checked by `compressBound`.
> tests/uncompress_fuzzer.c:11-29 and tests/uncompress2_fuzzer.c:11-21 show `uncompress`/`uncompress2` used directly on fuzzer bytes (not necessarily `mz_compress2` output) — demonstrating these are also independently fuzzable entry points, as called out in the feature description.

### P2.10 Behaviour Controls
**Claim:** `level` (0-10, or -1 default) selects match-finder depth and parsing strategy inside `tdefl_create_comp_flags_from_zip_params`: `level<=3` sets `TDEFL_GREEDY_PARSING_FLAG`; `level==0` sets `TDEFL_FORCE_ALL_RAW_BLOCKS`; `window_bits` (fixed to `MZ_DEFAULT_WINDOW_BITS` by `mz_deflateInit`, selectable via `mz_deflateInit2`) controls zlib header emission (`window_bits>0` → `TDEFL_WRITE_ZLIB_HEADER`) — `mz_compress2` always uses `mz_deflateInit` (not Init2), so it always emits the zlib header/footer and cannot select raw-deflate mode. On the decompression side, `mz_uncompress`/`mz_uncompress2` always call `mz_inflateInit` → `MZ_DEFAULT_WINDOW_BITS`, so only zlib-wrapped streams are directly supported by the single-call API; `flush` passed to `mz_deflate`/`mz_inflate` is always `MZ_FINISH` for the single-call wrappers (not user-selectable through `mz_compress2`).
**Evidence:**
> miniz_tdef.c:1464-1469: `mz_uint comp_flags = s_tdefl_num_probes[(level >= 0) ? MZ_MIN(10, level) : MZ_DEFAULT_LEVEL] | ((level <= 3) ? TDEFL_GREEDY_PARSING_FLAG : 0); if (window_bits > 0) comp_flags |= TDEFL_WRITE_ZLIB_HEADER; if (!level) comp_flags |= TDEFL_FORCE_ALL_RAW_BLOCKS;`
> miniz.c:193: `mz_deflateInit` calls `mz_deflateInit2(pStream, level, MZ_DEFLATED, MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY)` — hardcodes zlib-wrapped window bits and default strategy.
> miniz.c:337: `mz_deflate(&stream, MZ_FINISH)` — flush is hardcoded to `MZ_FINISH` in `mz_compress2`.
> miniz.c:409: `mz_inflateInit(pStream)` calls `mz_inflateInit2(pStream, MZ_DEFAULT_WINDOW_BITS)` — same hardcoding for decompression.

### P2.11 Input Expectations
**Claim:** `pSource`/`source_len` is treated as an opaque raw byte buffer — no format, header, or terminator is required or checked by `mz_compress2` itself (it is the *producer* of the zlib format, not a consumer of one). Size limit: `source_len` and `*pDest_len` are truncated to 32 bits internally (`mz_uint32`), and the function explicitly rejects (`MZ_PARAM_ERROR`) any `source_len | *pDest_len` greater than `0xFFFFFFFF` when `mz_ulong` is 64-bit. `source_len == 0` is accepted (produces a minimal valid zlib stream). The destination buffer must be large enough or `MZ_BUF_ERROR` is returned (no partial output is "valid" from this single-call API — caller must size via `mz_compressBound`).
**Evidence:**
> miniz.c:325-326: `if ((mz_uint64)(source_len | *pDest_len) > 0xFFFFFFFFU) return MZ_PARAM_ERROR;`
> miniz.c:329,331: `stream.avail_in = (mz_uint32)source_len; stream.avail_out = (mz_uint32)*pDest_len;` — truncation to 32-bit for the internal stream fields.
> miniz.c:341: `return (status == MZ_OK) ? MZ_BUF_ERROR : status;` — undersized destination buffer surfaces as `MZ_BUF_ERROR`.
> miniz.h:405-406: "mz_compressBound() returns a (very) conservative upper bound on the amount of data that could be generated by calling mz_compress()." — documents the expected sizing contract for `pDest`.

## Other entries in the Logic Group

### Entry: mz_compress
**Declaration:** `MINIZ_EXPORT int mz_compress(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len);` (miniz.h:402)
**Call protocol:** Pure wrapper — `return mz_compress2(pDest, pDest_len, pSource, source_len, MZ_DEFAULT_COMPRESSION);` (miniz.c:348-350). Same pre/post conditions as `mz_compress2` (see target section), with `level` fixed to `MZ_DEFAULT_COMPRESSION` (-1). No extra init, no extra cleanup. Returns the same codes as `mz_compress2`.
**Combination with target:** This is literally an alternate call path to the same target; a harness can call either `mz_compress2(pDest,&len,src,n,level)` with a fuzzed `level`, or `mz_compress(pDest,&len,src,n)` to exercise the fixed-default-level path. Both require `pDest_len` set to capacity beforehand (ideally from `mz_compressBound`) and the same error handling.
**Evidence:** miniz.c:348-350; miniz.h:400-402 (doc comment covers both).

### Entry: mz_compressBound
**Declaration:** `MINIZ_EXPORT mz_ulong mz_compressBound(mz_ulong source_len);` (miniz.h:406)
**Call protocol:** No init needed; pure function. `return mz_deflateBound(NULL, source_len);` (miniz.c:353-356), which computes `MZ_MAX(128 + (source_len*110)/100, 128 + source_len + ((source_len/(31*1024))+1)*5)` (miniz.c:311-316) — note it accepts `pStream == NULL` (unused, just `(void)pStream;`). No return-value error code — always succeeds, returns an `mz_ulong` size bound. Nothing to free (no allocation).
**Combination with target:** Called BEFORE `mz_compress2`/`mz_compress`, to size `pDest` (`cmp_len = compressBound(src_len)`, then `pCmp = malloc(cmp_len)`), guaranteeing (per the library's own documentation) that `mz_compress2` cannot fail with `MZ_BUF_ERROR` due to undersized output. A harness can also deliberately under/over-allocate relative to this bound to exercise `MZ_BUF_ERROR` vs. success paths.
**Evidence:** miniz.c:353-356 (mz_compressBound body), miniz.c:311-316 (mz_deflateBound body); examples/example1.c:23 `uLong cmp_len = compressBound(src_len);` then line 34 `pCmp = (mz_uint8 *)malloc((size_t)cmp_len);`; miniz_tester.cpp:308-311 checks `if (cmp_len > compressBound(uncomp_len)) { print_error("compressBound() returned bogus result\n"); }`.

### Entry: mz_uncompress
**Declaration:** `MINIZ_EXPORT int mz_uncompress(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len);` (miniz.h:443)
**Call protocol:** Self-contained, like `mz_compress2`. Wrapper: `return mz_uncompress2(pDest, pDest_len, pSource, &source_len);` (miniz.c:592-595) — takes a local copy of `source_len` by address since `mz_uncompress2` wants a pointer. Internally builds its own `mz_stream`, calls `mz_inflateInit` → `mz_inflate(&stream, MZ_FINISH)` → `mz_inflateEnd`. No external init required by caller; `pDest_len` must be set to destination buffer capacity in. On return, `*pDest_len` holds actual decompressed size (only valid if return is `MZ_OK`/`Z_OK`/`MZ_STREAM_END`-derived `MZ_OK`). Returns `MZ_OK` on success, or `MZ_DATA_ERROR`/`MZ_BUF_ERROR`/`MZ_PARAM_ERROR`/`MZ_MEM_ERROR`/other `mz_inflate` codes on failure (see mz_uncompress2 below — same core). Nothing extra for caller to free beyond its own `pDest` buffer.
**Combination with target:** This is the natural "consumer" of `mz_compress2`'s output in a round-trip harness: call `mz_compress2` (or `mz_compress`) first to produce a zlib stream in `pCmp`/`cmp_len`, allocate an uncompressed buffer (e.g. sized to the original `src_len`, or to an attacker-chosen size to probe `MZ_BUF_ERROR`), then call `mz_uncompress(pUncomp, &uncomp_len, pCmp, cmp_len)`, check `== MZ_OK`/`Z_OK`, and (for a round-trip harness) `memcmp` against the original source.
**Evidence:** miniz.c:592-595; examples/example1.c:66 `cmp_status = uncompress(pUncomp, &uncomp_len, pCmp, cmp_len);`; examples/example1.c:85-87 round-trip check; tests/uncompress_fuzzer.c:24 `z_status = uncompress(buffer, &buffer_length, data, size);` (fuzzer-only input, not from mz_compress2, showing it's also independently robust against arbitrary bytes).

### Entry: mz_uncompress2
**Declaration:** `MINIZ_EXPORT int mz_uncompress2(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong *pSource_len);` (miniz.h:444)
**Call protocol:** Core of `mz_uncompress`. Differs by taking `pSource_len` as `mz_ulong *` (in/out): in = bytes available at `pSource`, out = bytes of `pSource` actually consumed (`*pSource_len = *pSource_len - stream.avail_in;`, miniz.c:581) — this is the "partial-consumption" path mentioned in the task description. Builds local `mz_stream`, zero-initialized; rejects (`MZ_PARAM_ERROR`) if `*pSource_len | *pDest_len` exceeds 32 bits (miniz.c:568-569); calls `mz_inflateInit` (always `MZ_DEFAULT_WINDOW_BITS`, i.e. zlib-wrapped input expected) then `mz_inflate(&stream, MZ_FINISH)`. On `status != MZ_STREAM_END`: calls `mz_inflateEnd` and remaps `MZ_BUF_ERROR` with no more input available to `MZ_DATA_ERROR` (miniz.c:582-586); on success sets `*pDest_len = stream.total_out` and returns `mz_inflateEnd(&stream)`. `mz_inflateEnd` is called on every exit path (miniz.c:584,589), so no leak of the internal `inflate_state` allocated in `mz_inflateInit2` (miniz.c:390-394).
**Combination with target:** Alternate inverse-path entry: a harness can feed it either fuzzer bytes directly (testing tinfl robustness, per tests/uncompress2_fuzzer.c) or the output of `mz_compress2`, and additionally inspect/compare `*pSource_len` after the call to validate the partial-consumption bookkeeping (particularly interesting if `pSource` contains trailing bytes beyond a complete zlib stream).
**Evidence:** miniz.c:561-595 (full body quoted above); miniz.h:444 declaration; tests/uncompress2_fuzzer.c:17 `if (Z_OK != uncompress2(buffer, &buffer_length, data, &size)) return 0;` with `buffer` a fixed 256KB static array (miniz/tests/uncompress2_fuzzer.c:11,15).

## Build notes
**Claim:** Fuzzers for this project are plain C, compiled directly against the single-header/single-source amalgamated `miniz.h`/`miniz.c` produced by the CMake build (`-DAMALGAMATE_SOURCES=ON`), then linked against the static lib and libFuzzer's engine via the C++ compiler driver. The amalgamated header is placed at `build/amalgamation/miniz.h` and is the include path used for `*_fuzzer.c` files.
**Evidence:**
> miniz/tests/ossfuzz.sh:21-25: writes a stub `miniz_export.h` defining `MINIZ_EXPORT` to nothing.
> miniz/tests/ossfuzz.sh:27-30: `mkdir build; cd build; cmake .. -DAMALGAMATE_SOURCES=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_FUZZERS=ON -DBUILD_TESTS=OFF; make -j$(nproc)`.
> miniz/tests/ossfuzz.sh:35-41: `for f in $(find $SRC -name '*_fuzzer.c'); do b=$(basename -s .c $f); $CC $CFLAGS -Ibuild/amalgamation $f -c -o /tmp/$b.o; $CXX $CXXFLAGS -stdlib=libc++ -Ibuild/amalgamation /tmp/$b.o -o $OUT/$b $LIB_FUZZING_ENGINE ./build/libminiz.a; done` — i.e. harness source is C (`$CC`), compiled with `-Ibuild/amalgamation` so `#include "miniz.h"` picks up the amalgamated header; final link done with `$CXX` (C++ driver, needed for libFuzzer runtime) against `./build/libminiz.a` and `$LIB_FUZZING_ENGINE`.
> miniz/CMakeLists.txt:60-113: the `AMALGAMATE_SOURCES` logic that concatenates `miniz.h/.c` with `miniz_tdef.*`, `miniz_tinfl.*`, `miniz_zip.*` into `${CMAKE_CURRENT_BINARY_DIR}/amalgamation/miniz.h` and `.../miniz.c`, builds `libminiz.a` (static, since `BUILD_SHARED_LIBS=OFF`) from them.
> ossfuzz_view/miniz-c2/Dockerfile:17-25: `FROM gcr.io/oss-fuzz-base/base-builder`; clones richgel999/miniz, copies `run_tests.sh build.sh` to `$SRC/`.
> ossfuzz_view/miniz-c2/build.sh:19: `$SRC/miniz/tests/ossfuzz.sh` is the actual OSS-Fuzz entry point that performs the build described above.
> Note: the existing `tests/compress_fuzzer.c` in this checkout is currently a stub that keeps `mz_compress2` linked via `__asm__` alias but does not call it (miniz/tests/compress_fuzzer.c:1-16) — a new/real harness replacing it should follow the include/link pattern above and the call protocol documented in this report (compressBound → malloc → mz_compress2/mz_compress → mz_uncompress/mz_uncompress2 → free).