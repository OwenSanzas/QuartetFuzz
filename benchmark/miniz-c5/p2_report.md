## target: mz_uncompress

### Overview
`mz_uncompress` (miniz.h:443, implemented miniz.c:592-595) is a thin wrapper:
```c
int mz_uncompress(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len)
{
    return mz_uncompress2(pDest, pDest_len, pSource, &source_len);
}
```
It takes a local copy of `source_len` and forwards to `mz_uncompress2`. All real logic (stream setup, `mz_inflateInit`, `mz_inflate(..., MZ_FINISH)`, `mz_inflateEnd`) lives in `mz_uncompress2` (miniz.c:561-590).

### P2.1 Init Sequence
**Claim:** No global/library-level init is required before calling `mz_uncompress`. Internally it builds and tears down its own `mz_stream` and internal `inflate_state` on every call; the caller only needs to allocate `pDest`/`pSource` buffers and set `*pDest_len` before the call.
**Evidence:**
> miniz.c:592-595: `mz_uncompress` just forwards to `mz_uncompress2`.
> miniz.c:563-565: `mz_uncompress2` does `mz_stream stream; memset(&stream,0,sizeof(stream));` locally — no external init object needed.
> miniz.c:576: `status = mz_inflateInit(&stream);` is called internally, inside `mz_uncompress2`, not by the caller.

### P2.2 Parameter Construction
**Claim:**
- `pDest`: caller-allocated writable buffer, must be large enough for the decompressed output or you get `MZ_BUF_ERROR`.
- `pDest_len`: in/out `mz_ulong*`; **on input** it is the capacity of `pDest`; **on output** `mz_uncompress2` sets `*pDest_len = stream.total_out` (actual bytes written) only on success (miniz.c:587).
- `pSource`: pointer to compressed bytes (zlib-wrapped by default, see P2.11), may be attacker-controlled/fuzzed data.
- `source_len`: compressed length in bytes, passed by value to `mz_uncompress` (internally promoted to a local `mz_ulong` for `mz_uncompress2`).
- Both `*pDest_len` and `source_len` are truncated to 32 bits with an explicit check: if `(mz_uint64)(*pSource_len | *pDest_len) > 0xFFFFFFFFU` the call returns `MZ_PARAM_ERROR` (miniz.c:568-569) — i.e. values above 4GB-1 are rejected.
**Evidence:**
> miniz.h:443: `int mz_uncompress(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len);`
> miniz.c:568-569: `if ((mz_uint64)(*pSource_len | *pDest_len) > 0xFFFFFFFFU) return MZ_PARAM_ERROR;`
> miniz.c:587: `*pDest_len = stream.total_out;` (only reached on `MZ_STREAM_END`)

### P2.3 Object Lifecycle
**Claim:** The lifecycle of the internal `mz_stream`/`inflate_state` is entirely self-contained inside `mz_uncompress2`: create (zero-init stream) → configure (`mz_inflateInit`) → use (`mz_inflate(&stream, MZ_FINISH)`, single call) → destroy (`mz_inflateEnd`), on *every* return path including errors.
**Evidence:**
> miniz.c:576-589:
> ```
> status = mz_inflateInit(&stream);
> if (status != MZ_OK) return status;
> status = mz_inflate(&stream, MZ_FINISH);
> *pSource_len = *pSource_len - stream.avail_in;
> if (status != MZ_STREAM_END) {
>     mz_inflateEnd(&stream);
>     return ((status == MZ_BUF_ERROR) && (!stream.avail_in)) ? MZ_DATA_ERROR : status;
> }
> *pDest_len = stream.total_out;
> return mz_inflateEnd(&stream);
> ```
From the harness's point of view, `mz_uncompress`/`mz_uncompress2` is a single atomic call: no caller-visible create/destroy pair is needed around it.

### P2.4 Return Value Handling
**Claim:** Return value is one of the `mz_*` status codes (aliased to `Z_*` names). `MZ_OK` (0) = success, decompressed data is in `pDest`/`*pDest_len`. Negative values are errors: `MZ_STREAM_ERROR` (-2, bad stream/args), `MZ_DATA_ERROR` (-3, corrupt deflate stream or exhausted input with BUF_ERROR translated), `MZ_MEM_ERROR` (-4, alloc failure in `mz_inflateInit`), `MZ_BUF_ERROR` (-5, output buffer too small or (translated to MZ_DATA_ERROR when input is also exhausted) — not propagated as MZ_BUF_ERROR), `MZ_PARAM_ERROR` (-10000, bad params / >4GB sizes). On any non-OK return `*pDest_len` is **not** updated with a valid total (left at caller-set capacity).
**Evidence:**
> miniz.h:309-318: enum of return codes `MZ_OK=0, MZ_STREAM_END=1, MZ_STREAM_ERROR=-2, MZ_DATA_ERROR=-3, MZ_MEM_ERROR=-4, MZ_BUF_ERROR=-5, MZ_PARAM_ERROR=-10000`.
> miniz.c:585: `return ((status == MZ_BUF_ERROR) && (!stream.avail_in)) ? MZ_DATA_ERROR : status;` — BUF_ERROR-with-no-more-input is remapped to DATA_ERROR for the single-call API.
> tests/compress_fuzzer.c:22: `uncompress(uncompr, &uncomprLen, compr, comprLen);` then `assert(dataLen == uncomprLen)` — real caller checks resulting length, implying callers should check return code (via the `assert` path it's assumed OK in the round-trip test).

### P2.5 Cleanup Sequence
**Claim:** Harness does not need to free anything related to `mz_uncompress` itself — its internal `mz_stream`/`inflate_state` is allocated and freed inside the call via `mz_inflateInit`/`mz_inflateEnd` using `pStream->zalloc`/`zfree` (default malloc/free). The harness is only responsible for freeing/managing `pDest` and `pSource` buffers it allocated itself.
**Evidence:**
> miniz.c:390 (`mz_inflateInit2`): `pDecomp = (inflate_state *)pStream->zalloc(pStream->opaque, 1, sizeof(inflate_state));`
> miniz.c:550-560 (`mz_inflateEnd`): `if (pStream->state) { pStream->zfree(pStream->opaque, pStream->state); pStream->state = NULL; } return MZ_OK;`
> tests/compress_fuzzer.c:87-88: `free(compr); free(uncompr);` — harness frees only its own buffers, not anything related to the mz_stream.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current source tree (`miniz.h`/`miniz.c`), confirmed by direct reads.
**Evidence:**
> miniz.h:367 `mz_deflateInit`, :374 `mz_deflateInit2`, :389 `mz_deflate`, :395 `mz_deflateEnd`, :402-403 `mz_compress`/`mz_compress2`, :413 `mz_inflateInit`, :417 `mz_inflateInit2`, :436 `mz_inflate`, :439 `mz_inflateEnd`, :443 `mz_uncompress`, :444 `mz_uncompress2`.
> miniz.c:191,196,241,299,318,348,371,407,438,550,561,592 — matching definitions found via grep for each symbol.

### P2.7 Co-call Constraints
**Claim:** For the streaming API (`mz_deflateInit[2]`/`mz_deflate`/`mz_deflateEnd` and `mz_inflateInit[2]`/`mz_inflate`/`mz_inflateEnd`) init/end are a mandatory paired contract: every successful `*Init` must be matched by exactly one `*End` call, even on error paths, to free `pStream->state`. For the single-call `mz_uncompress`/`mz_uncompress2`/`mz_compress`/`mz_compress2` wrappers this pairing is already done internally — the harness does not call Init/End itself for these.
**Evidence:**
> miniz.c:576-589: internal pairing of `mz_inflateInit`→`mz_inflateEnd` on both the success and failure branch inside `mz_uncompress2`.
> miniz.c:333-345: internal pairing of `mz_deflateInit`→`mz_deflateEnd` inside `mz_compress2`, same pattern.
> tests/small_fuzzer.c:39-63 and :80-93: manual streaming usage shows explicit `deflateInit`/…/`deflateEnd` and `inflateInit`/…/`inflateEnd` pairs when the harness drives the stream API directly instead of the single-call wrappers.

### P2.8 Prerequisite State
**Claim:** No external context, config file, or connection is required. The only prerequisite state is: valid `pDest` buffer of adequate (ideally `mz_compressBound`-derived, or generously oversized) size, and `*pDest_len` set to that capacity before the call. For round-trip fuzzing, a prior successful `mz_compress`/`mz_compress2` call producing `pSource`/`source_len` is the natural prerequisite, but `mz_uncompress` can equally be called directly on raw/mutated fuzzer bytes (no compress call needed) since it's the standalone decompression entry.
**Evidence:**
> tests/compress_fuzzer.c:17-27 `check_compress_level`: compress first (`compress2`) producing `compr`/`comprLen`, which is then fed into `uncompress`.
> tests/compress_fuzzer.c:48-60 `check_decompress`: alternatively a hand-built buffer (`copy`) with a forged zlib header is fed directly into `uncompress` without any prior `compress` call — showing the target is also exercised on non-round-trip, directly-crafted input.

### P2.9 Result Usage
**Claim:** The decompressed bytes written to `pDest`/`*pDest_len` by `mz_uncompress` are consumed by: (a) `memcmp`/length-equality checks against the known original data in round-trip tests, and (b) nothing further within miniz's own public API — there is no "parse the uncompressed buffer" API in miniz; downstream usage is caller-defined (e.g., writing to a file in the examples). No miniz API takes the uncompressed buffer as an opaque handle; it is treated as a plain byte buffer.
**Evidence:**
> tests/compress_fuzzer.c:24-26: `assert(dataLen == uncomprLen); assert(0 == memcmp(data, uncompr, dataLen));` — the only post-processing of `mz_uncompress`'s result seen in the project is a straight memory compare against the original input (round-trip validation), confirming `pDest`/`*pDest_len` are the sole outputs consumed, as plain bytes/length, not as a handle passed to any other API.
> examples/example3.c (file-based compress/decompress tool) writes the inflate output directly to a file with `fwrite`, again treating it as a plain buffer (no miniz API call on the result).

### P2.10 Behaviour Controls
**Claim:** Several parameters/flags change the processing path exercised when fuzzing this feature:
- `level` (0..9, or `MZ_DEFAULT_COMPRESSION=-1`, or up to 10 per docs) passed to `mz_compress2`/`mz_deflateInit` selects the compression path/ratio of the data later fed to `mz_uncompress` (miniz.h:278-283, miniz.c:318-346).
- `window_bits` passed to `mz_inflateInit2`/`mz_deflateInit2`: `MZ_DEFAULT_WINDOW_BITS` (15, zlib header+Adler32 wrapper) vs `-MZ_DEFAULT_WINDOW_BITS` (raw deflate, no header/footer) — selects `TINFL_FLAG_PARSE_ZLIB_HEADER` in `mz_inflate` (miniz.c:453-454, miniz.h:416).  `mz_uncompress`/`mz_uncompress2` always use `mz_inflateInit` (plain), i.e. always the zlib-wrapped path (miniz.c:576, :407-409) — raw-deflate-mode decompression can only be reached by calling the streaming API (`mz_inflateInit2` with negative window_bits) directly, not through `mz_uncompress`.
- `flush` passed to `mz_inflate` (`MZ_NO_FLUSH`, `MZ_SYNC_FLUSH`, `MZ_FINISH`) selects single-shot vs. incremental decoding; `mz_uncompress2` always calls with `MZ_FINISH` on the first call (miniz.c:580), taking the fast "whole buffer" path (`TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF`, miniz.c:466-469).
- `*pDest_len` (output capacity) controls whether `MZ_BUF_ERROR`/`MZ_DATA_ERROR` is produced for undersized output buffers — a key fuzzing knob (miniz.c:482-489).
**Evidence:**
> miniz.c:453-454: `if (pState->m_window_bits > 0) decomp_flags |= TINFL_FLAG_PARSE_ZLIB_HEADER;`
> miniz.c:409: `mz_inflateInit(pStream){ return mz_inflateInit2(pStream, MZ_DEFAULT_WINDOW_BITS); }` — confirms `mz_uncompress` path is always zlib-header mode.
> miniz.c:466-489: `MZ_FINISH`+`first_call` branch is the one taken by `mz_uncompress2`.
> tests/compress_fuzzer.c:80-85: exercises levels 1,3,6,7 then calls `uncompress`/`check_decompress`, varying the compression path feeding into the target.

### P2.11 Input Expectations
**Claim:** `mz_uncompress`/`mz_uncompress2` expect `pSource` to be a zlib-wrapped DEFLATE stream (2-byte zlib header + deflate blocks + 4-byte Adler-32 trailer) by default, because `mz_inflateInit` always uses `MZ_DEFAULT_WINDOW_BITS` (positive ⇒ `TINFL_FLAG_PARSE_ZLIB_HEADER`). There is no explicit minimum size check beyond what `tinfl_decompress`'s state machine requires to parse the header/blocks; too-short or malformed input yields `MZ_DATA_ERROR`/`MZ_BUF_ERROR` depending on whether `avail_in` was exhausted. `source_len`/`*pDest_len` are capped to `<= 0xFFFFFFFF` (32-bit) regardless of the actual `mz_ulong` width (miniz.c:568-569). No explicit NUL-termination is required/used — lengths are explicit (`source_len`, `*pDest_len`), consistent with arbitrary binary content.
**Evidence:**
> miniz.c:453-454 and :407-409: default zlib-header parsing mode.
> miniz.c:568-569: 32-bit size cap check, `MZ_PARAM_ERROR` otherwise.
> miniz.c:585: translation of exhausted-input BUF_ERROR to DATA_ERROR, i.e. truncated streams are reported as `MZ_DATA_ERROR`.
> tests/compress_fuzzer.c:34-46,48-60 `write_zlib_header`: shows the project's own fuzzer manually crafting a valid-looking 2-byte zlib header to be accepted by `uncompress`, confirming the header-format expectation.

## Other entries in this Logic Group (harness composition)

### mz_uncompress2 (miniz.h:444; miniz.c:561-590)
**Claim:** Same core logic as `mz_uncompress`, but `pSource_len` is `mz_ulong*` in/out: on input it's the available compressed bytes; after the call it is decremented to reflect bytes actually consumed (`*pSource_len = *pSource_len - stream.avail_in;`, miniz.c:581) — done *before* checking for success, so it's updated even on failure. No additional init required beyond what `mz_uncompress` needs; it has the *same* prerequisites, return codes, and internal `mz_inflateInit`/`mz_inflate`/`mz_inflateEnd` triple.
**Evidence:**
> miniz.c:561,567-589: body quoted above.
> tests/uncompress2_fuzzer.c:13-20 real caller: `uncompress2(buffer, &buffer_length, data, &size)` — note zlib-API alias `uncompress2` maps to `mz_uncompress2` (miniz.h "Compatiblity with zlib API" block, inflate section around line 561-574).
**Combination with target:** Harness can call `mz_uncompress` and `mz_uncompress2` as alternative/complementary entries over the same fuzzed byte buffer — both reach the identical `mz_inflateInit`→`mz_inflate(MZ_FINISH)`→`mz_inflateEnd` core, differing only in whether consumed-length feedback is obtained.

### mz_compress / mz_compress2 (miniz.h:402-403; miniz.c:318-351)
**Claim:** Inverse operation producing valid zlib streams to feed into `mz_uncompress`. `mz_compress2(pDest, pDest_len, pSource, source_len, level)`: `level` must be in `[MZ_NO_COMPRESSION(0), MZ_BEST_COMPRESSION(9)]` or `MZ_DEFAULT_COMPRESSION(-1)` (miniz.h:359-361, :278-283). `mz_compress` calls `mz_compress2` with `MZ_DEFAULT_COMPRESSION`. Internally does `mz_deflateInit`→`mz_deflate(&stream, MZ_FINISH)`→`mz_deflateEnd`, paired on every path (miniz.c:333-345), same pattern as the uncompress side. Returns `MZ_OK` on success, with `*pDest_len` updated to `stream.total_out` (actual compressed size) — must be large enough (`mz_compressBound(source_len)` recommended, miniz.h:406) or you get `MZ_BUF_ERROR` from the internal `mz_deflate` (translated, miniz.c:341: `(status == MZ_OK) ? MZ_BUF_ERROR : status`).
**Evidence:**
> miniz.c:318-346: `mz_compress2` body quoted in earlier section.
> miniz.c:353-356 `mz_compressBound`: `return mz_deflateBound(NULL, source_len);` — sizing helper to allocate `pDest` before calling `mz_compress`/`mz_compress2`.
> tests/compress_fuzzer.c:17-27,62-83: real round-trip caller — `compress2(compr,&comprLen,data,dataLen,level)` then `uncompress(uncompr,&uncomprLen,compr,comprLen)`, run for several levels (1,3,6,7).
**Combination with target:** Harness should call `mz_compress`/`mz_compress2` first on fuzzer-controlled raw bytes (optionally varying `level` across 0..9/`MZ_DEFAULT_COMPRESSION`) to produce a well-formed compressed buffer, then feed that buffer (optionally with further bit-flipping/truncation of the fuzz input) into `mz_uncompress`/`mz_uncompress2`, exactly as `tests/compress_fuzzer.c` does. No cleanup object is shared between the two calls — each is a fully self-contained Init/Use/End sequence.

### mz_inflateInit / mz_inflateInit2 (miniz.h:413,417; miniz.c:371-410)
**Claim:** These are called **internally by `mz_uncompress2`** (`mz_inflateInit` at miniz.c:576); the harness does not need to call them when only exercising `mz_uncompress`/`mz_uncompress2`. They are relevant to this Logic Group only if the harness ALSO drives the lower-level `mz_inflate` streaming API directly (as `tests/small_fuzzer.c` / `tests/flush_fuzzer.c` do for the deflate side) to reach the raw-deflate (`window_bits<0`) path that `mz_uncompress` itself cannot reach. `mz_inflateInit2` validates `window_bits` (`!= MZ_DEFAULT_WINDOW_BITS && -window_bits != MZ_DEFAULT_WINDOW_BITS` ⇒ `MZ_PARAM_ERROR`, miniz.c:376-377), allocates `inflate_state` via `pStream->zalloc` (miniz.c:390-392, `MZ_MEM_ERROR` on failure), and must be paired with exactly one `mz_inflateEnd` (frees via `pStream->zfree`, miniz.c:550-560).
**Evidence:**
> miniz.c:371-405 (`mz_inflateInit2` body), :407-410 (`mz_inflateInit` wrapper calling it with `MZ_DEFAULT_WINDOW_BITS`).
> miniz.c:576 inside `mz_uncompress2`: `status = mz_inflateInit(&stream);` — confirms the single-call API always takes the zlib-wrapped path, never calls `mz_inflateInit2` with a negative window_bits.
**Combination with target:** Not directly chained to `mz_uncompress` by the library itself; if the harness wants to test negative-`window_bits` (raw deflate) decoding, it must call `mz_inflateInit2`+`mz_inflate`+`mz_inflateEnd` directly instead of `mz_uncompress`, as an independent/parallel path in the same harness, not as a pre-step for the target.

### mz_inflate / tinfl_decompress / mz_inflateEnd (core, called internally)
**Claim:** These are the Core functions this Logic Group's target drives; they are **not** meant to be called directly by the harness as separate entries — they are reached exclusively through `mz_uncompress`/`mz_uncompress2` (or through manual `mz_inflateInit`+`mz_inflate`+`mz_inflateEnd` loops if the harness chooses to exercise streaming mode). `mz_inflate`'s `flush` parameter (`MZ_NO_FLUSH=0`, `MZ_SYNC_FLUSH=2`, `MZ_FINISH=4`) and the `m_window_bits`/first-call state selects which `decomp_flags` are passed to `tinfl_decompress` (miniz.c:441-472).
**Evidence:**
> miniz.c:438-548: full `mz_inflate` body with the `MZ_FINISH`-and-`first_call` fast path (quoted above) vs. the generic loop path (miniz.c:491 onward, not fully shown but present per grep).
> miniz.h:436,439: public declarations confirming existence as public API (`MINIZ_EXPORT int mz_inflate(...)`, `MINIZ_EXPORT int mz_inflateEnd(...)`), even though this Logic Group treats them as Core (internal to the uncompress feature) rather than as a harness entry in their own right.

## Build notes

**Claim:** Fuzzers for this project are plain C, compiled against the amalgamated single-header/single-source `miniz.h`/`miniz.c` produced by CMake (`AMALGAMATE_SOURCES=ON`), then linked against the static `libminiz.a` together with `$LIB_FUZZING_ENGINE` using the C++ driver (`$CXX`) for final linking (to satisfy libFuzzer's C++ runtime), while the fuzzer `.c` source itself is compiled with `$CC`.

**Evidence:**
> ossfuzz_view/miniz-c5/build.sh:19: `$SRC/miniz/tests/ossfuzz.sh` is invoked to do the actual fuzz build.
> miniz/tests/ossfuzz.sh:21-25: generates a stub `miniz_export.h` defining `MINIZ_EXPORT` to empty (for static, non-DLL builds).
> miniz/tests/ossfuzz.sh:27-30: `cmake .. -DAMALGAMATE_SOURCES=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_FUZZERS=ON -DBUILD_TESTS=OFF` then `make -j$(nproc)` — builds `build/libminiz.a` and `build/amalgamation/{miniz.h,miniz.c}`.
> miniz/tests/ossfuzz.sh:35-41:
> ```
> for f in $(find $SRC -name '*_fuzzer.c'); do
>     b=$(basename -s .c $f)
>     $CC $CFLAGS -Ibuild/amalgamation $f -c -o /tmp/$b.o
>     $CXX $CXXFLAGS -stdlib=libc++ -Ibuild/amalgamation /tmp/$b.o -o $OUT/$b $LIB_FUZZING_ENGINE ./build/libminiz.a
> done
> ```
> So: fuzzer source is **C** (compiled with `$CC`), `#include "miniz.h"` resolves via `-Ibuild/amalgamation` to the amalgamated header (which itself `#include`s nothing extra — `miniz.h`/`miniz.c` are self-contained after amalgamation, confirmed by miniz/CMakeLists.txt:60-113 showing the amalgamation step concatenating `miniz_common.h`, `miniz_tdef.h`, `miniz_tinfl.h`, `miniz_zip.h` into one `miniz.h`, and `miniz_tdef.c`, `miniz_tinfl.c`, `miniz_zip.c` into one `miniz.c`).
> miniz/CMakeLists.txt:271-309: CMake fuzzer targets (`uncompress_fuzzer`, `uncompress2_fuzzer`, `compress_fuzzer`, etc.) each link only against the `miniz` target — no other external libraries required.
> Existing example fuzzer `miniz/tests/uncompress_fuzzer.c` in this checkout is a stub (`qf_target_0` asm-aliased to `mz_uncompress`, no real call) — real usage pattern for `uncompress`/`uncompress2` is best taken from `tests/uncompress2_fuzzer.c` and `tests/compress_fuzzer.c`, both of which are genuine, non-stubbed fuzzers using the zlib-compatible aliases (`uncompress`, `uncompress2`, `compress2`, `compressBound`) which resolve via static inline wrappers in miniz.h (lines ~526-574) directly to `mz_compress`/`mz_compress2`/`mz_uncompress`/`mz_uncompress2`.