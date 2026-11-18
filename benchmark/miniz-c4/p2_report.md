## target: mz_uncompress2

### Summary of target body
`miniz/miniz.c:561-590`:
```
int mz_uncompress2(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong *pSource_len)
{
    mz_stream stream;
    int status;
    memset(&stream, 0, sizeof(stream));
    if ((mz_uint64)(*pSource_len | *pDest_len) > 0xFFFFFFFFU)
        return MZ_PARAM_ERROR;
    stream.next_in = pSource;
    stream.avail_in = (mz_uint32)*pSource_len;
    stream.next_out = pDest;
    stream.avail_out = (mz_uint32)*pDest_len;
    status = mz_inflateInit(&stream);
    if (status != MZ_OK) return status;
    status = mz_inflate(&stream, MZ_FINISH);
    *pSource_len = *pSource_len - stream.avail_in;
    if (status != MZ_STREAM_END) {
        mz_inflateEnd(&stream);
        return ((status == MZ_BUF_ERROR) && (!stream.avail_in)) ? MZ_DATA_ERROR : status;
    }
    *pDest_len = stream.total_out;
    return mz_inflateEnd(&stream);
}
```
It internally zero-inits an `mz_stream`, wires `pSource`/`*pSource_len` and `pDest`/`*pDest_len` into it, calls `mz_inflateInit` (== `mz_inflateInit2(&stream, MZ_DEFAULT_WINDOW_BITS)`, miniz.c:407-410), then a single `mz_inflate(&stream, MZ_FINISH)`, and always calls `mz_inflateEnd` before returning — it is itself a correct, fully self-contained use of the init/use/destroy protocol around `mz_inflate*`. A harness therefore does NOT need to call `mz_inflateInit`/`mz_inflate`/`mz_inflateEnd` itself when it calls `mz_uncompress2` — those are internal. The harness only supplies `pDest`, `*pDest_len` (capacity, updated to actual size on success), `pSource`, `*pSource_len` (in: available bytes, out: bytes consumed).

### P2.1 Init Sequence
**Claim:** No external init is required before calling `mz_uncompress2` itself; it performs its own `mz_inflateInit` internally. The only "init" a caller must do is allocate/prepare the `pDest` buffer and set `*pDest_len` to its capacity and `*pSource_len` to the number of valid bytes in `pSource` before the call.
**Evidence:**
> miniz/miniz.c:576: `status = mz_inflateInit(&stream);` — called inside mz_uncompress2, not by the caller.
> miniz/miniz.c:572,574: `stream.avail_in = (mz_uint32)*pSource_len; ... stream.avail_out = (mz_uint32)*pDest_len;` — these are read directly from the caller-supplied `*pSource_len`/`*pDest_len` before any init.
> miniz/tests/uncompress_fuzzer.c:13-24: caller only mallocs `buffer`, sets `buffer_length`, then calls `uncompress(buffer, &buffer_length, data, size)` with no other setup.

### P2.2 Parameter Construction
**Claim:** `pDest` must be a writable buffer of at least `*pDest_len` bytes (allocated by caller, e.g. malloc/calloc); `*pDest_len` is in/out: input = capacity, output = decompressed size on success. `pSource` must point to >= `*pSource_len` readable bytes; `*pSource_len` is in/out: input = compressed length, output = bytes actually consumed. Both `*pSource_len` and `*pDest_len` must individually fit in 32 bits even if `mz_ulong` is 64-bit, or `MZ_PARAM_ERROR` is returned.
**Evidence:**
> miniz/miniz.c:568-569: `if ((mz_uint64)(*pSource_len | *pDest_len) > 0xFFFFFFFFU) return MZ_PARAM_ERROR;`
> miniz/miniz.c:572,574: `stream.avail_in = (mz_uint32)*pSource_len; ... stream.avail_out = (mz_uint32)*pDest_len;` (both narrowed to 32-bit `mz_uint32`)
> miniz/tests/large_fuzzer.c:13-24: `buffer_length` computed from input bytes, `buffer = malloc(buffer_length)`, then `uncompress(buffer, &buffer_length, data, size)` — shows caller must pre-size the dest buffer and pass size by pointer.

### P2.3 Object Lifecycle
**Claim:** `mz_uncompress2` is a one-shot call with no exposed object for the caller to manage; the `mz_stream` it builds is entirely local/stack-allocated and fully torn down (`mz_inflateEnd`) before it returns on every path (success or failure). There is nothing for the harness to create/destroy around the call itself beyond the `pDest` buffer.
**Evidence:**
> miniz/miniz.c:563: `mz_stream stream;` (local, stack)
> miniz/miniz.c:576-589: every return path — success (`mz_inflateEnd` at line 589) and failure (`mz_inflateEnd` at line 584) — closes the stream before returning.

### P2.4 Return Value Handling
**Claim:** Return `MZ_OK` (0) on success is NOT what signals successful decompression — the function returns the final `mz_inflateEnd` status, which is `MZ_OK` on success; a non-`MZ_STREAM_END` result from the internal `mz_inflate` call is translated to `MZ_DATA_ERROR` (if truncated/out of input) or passed through (`MZ_BUF_ERROR`, `MZ_PARAM_ERROR`, `MZ_STREAM_ERROR`). Callers must check for `MZ_OK` (0); any negative or nonzero value is an error and `*pDest_len`/`*pSource_len` should not be trusted for the full decompressed size in that case (`*pDest_len` is only updated on the `MZ_STREAM_END` branch).
**Evidence:**
> miniz/miniz.c:580-589: `status = mz_inflate(&stream, MZ_FINISH); ... if (status != MZ_STREAM_END) { mz_inflateEnd(&stream); return ((status == MZ_BUF_ERROR) && (!stream.avail_in)) ? MZ_DATA_ERROR : status; } *pDest_len = stream.total_out; return mz_inflateEnd(&stream);`
> miniz.h:309-318: enum `MZ_OK=0, MZ_STREAM_END=1, MZ_DATA_ERROR=-3, MZ_BUF_ERROR=-5, MZ_PARAM_ERROR=-10000`.
> miniz/tests/large_fuzzer.c:24-28: `z_status = uncompress(buffer, &buffer_length, data, size); free(buffer); if (Z_OK != z_status) return 0;` — real caller checks for `Z_OK`/`MZ_OK` and otherwise discards result.

### P2.5 Cleanup Sequence
**Claim:** The harness must free the `pDest` buffer it allocated (and `pSource` buffer if heap-allocated) after inspecting the result; `mz_uncompress2` itself frees its internal `mz_stream.state` via `mz_inflateEnd` on all paths, so no additional miniz-level cleanup is required for the stream.
**Evidence:**
> miniz/miniz.c:550-559 (`mz_inflateEnd`): `if (pStream->state) { pStream->zfree(pStream->opaque, pStream->state); pStream->state = NULL; } return MZ_OK;` called from both exit paths of `mz_uncompress2` (lines 584, 589).
> miniz/tests/large_fuzzer.c:22-25: `buffer = (unsigned char *)malloc(buffer_length); z_status = uncompress(buffer, &buffer_length, data, size); free(buffer);` — caller frees its own destination buffer after the call.
> miniz/tests/miniz_zip_validate_fuzzer.cc:40-63: `compressed = malloc(...); ... free(compressed);` and `uncompressed = malloc(...); mz_uncompress(...); free(uncompressed);` — same pattern for both source and dest buffers.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase (`miniz/miniz.c` / `miniz/miniz.h`), with these exact signatures.
**Evidence:**
> miniz.h:443: `MINIZ_EXPORT int mz_uncompress(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len);`
> miniz.h:444: `MINIZ_EXPORT int mz_uncompress2(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong *pSource_len);`
> miniz.h:413,417,420,436,439: `mz_inflateInit`, `mz_inflateInit2`, `mz_inflateReset`, `mz_inflate`, `mz_inflateEnd` declared.
> miniz.h:402-403: `mz_compress`, `mz_compress2` declared.
> miniz.c:561, 592, 371, 407, 412, 438, 550, 318, 348: definitions of each of the above found at these lines.

### P2.7 Co-call Constraints
**Claim:** Inside `mz_uncompress2` the pairing is `mz_inflateInit`/`mz_inflateInit2` (begin) must always be matched by `mz_inflateEnd` (end) — this is enforced internally and both paths call it. If the harness instead drives `mz_inflateInit2`/`mz_inflate`/`mz_inflateEnd` manually (to reach streaming paths) it must preserve this pairing itself: one `mz_inflateInit`/`mz_inflateInit2` (or `mz_inflateReset` to reuse state) must precede each sequence of `mz_inflate` calls, and exactly one `mz_inflateEnd` must follow to free `pStream->state`, mirroring `mz_deflateInit`/`mz_deflate`*/`mz_deflateEnd` on the compress side.
**Evidence:**
> miniz.c:576-589: init always paired with end in `mz_uncompress2`.
> miniz.c:419: "/* Quickly resets a compressor without having to reallocate anything. Same as calling mz_inflateEnd() followed by mz_inflateInit()/mz_inflateInit2(). */" (mz_inflateReset doc, miniz.h:419) — documents the init/end pairing it replaces.
> miniz/tests/large_fuzzer.c:91-106: `inflateInit(&d_stream); for(;;) { ... inflate(&d_stream, Z_NO_FLUSH); if (err==Z_STREAM_END) break; } inflateEnd(&d_stream);` — classic init…loop…end pairing for the streaming use of the same core.

### P2.8 Prerequisite State
**Claim:** No global/library-wide state must exist before calling `mz_uncompress2`; it is a pure, reentrant one-shot function. The only "state" needed is the caller-owned buffers (`pDest` sized to the expected/maximum decompressed size, `pSource` holding valid compressed bytes) and the two length variables.
**Evidence:**
> miniz.c:561-590: function uses only its parameters and a local stack `mz_stream`; no reference to any global miniz state.
> miniz.c:385-388 (`mz_inflateInit2`): `if (!pStream->zalloc) pStream->zalloc = miniz_def_alloc_func; if (!pStream->zfree) pStream->zfree = miniz_def_free_func;` — default allocators are set up lazily inside init, no external allocator setup required.

### P2.9 Result Usage
**Claim:** Real callers use `*pDest_len` (actual decompressed size) to bound further processing of `pDest` (e.g., `memcmp` against the original plaintext in round-trip tests, or simply `free()`); `*pSource_len`/consumed byte count is not reused for streaming by any example found (it's informational, "bytes consumed"). The project's own fuzzers pass the decompressed buffer only to `free()`/`memcmp`, never into another miniz API — `mz_uncompress2`'s output is a plain terminal buffer, not a handle.
**Evidence:**
> miniz/tests/compress_fuzzer.c:17-27 (`check_compress_level`): `compress2(...); uncompress(comp,&comprLen,...); assert(dataLen==uncomprLen); assert(0==memcmp(data,uncompr,dataLen));` — decompressed buffer and its length are compared against the original input, not passed to another miniz call.
> miniz/tests/miniz_zip_validate_fuzzer.cc:52-59: `if (compress_result == MZ_OK && compressed_size > 0) { ... mz_uncompress(uncompressed, &uncompressed_size, compressed, compressed_size); free(uncompressed); }` — result buffer only freed, not further consumed by another API.
> miniz/tests/large_fuzzer.c:24-28: `z_status = uncompress(buffer, &buffer_length, data, size); free(buffer);`

### P2.10 Behaviour Controls
**Claim:** `mz_uncompress2` itself exposes no flags — it fixes `window_bits=MZ_DEFAULT_WINDOW_BITS` (zlib-wrapped, via `mz_inflateInit`) and `flush=MZ_FINISH` internally (miniz.c:576,580). Behavioural variation for the surrounding entries: `mz_inflateInit2`'s `window_bits` (`MZ_DEFAULT_WINDOW_BITS`=15 vs `-MZ_DEFAULT_WINDOW_BITS`=-15) selects zlib-header parsing vs raw deflate (`TINFL_FLAG_PARSE_ZLIB_HEADER` set only if `m_window_bits>0`); `mz_inflate`'s `flush` argument (`MZ_NO_FLUSH`=0, `MZ_SYNC_FLUSH`=2, `MZ_FINISH`) selects the single-shot "non-wrapping output buffer" fast path (`first_call && flush==MZ_FINISH`) vs the streaming dictionary-based loop; `mz_compress2`'s `level` (0-10 or `MZ_DEFAULT_COMPRESSION=-1`) changes which deflate block types are generated as input to decompression round-trips.
**Evidence:**
> miniz.c:376-377 (`mz_inflateInit2`): `if ((window_bits != MZ_DEFAULT_WINDOW_BITS) && (-window_bits != MZ_DEFAULT_WINDOW_BITS)) return MZ_PARAM_ERROR;` — only these two values are legal.
> miniz.c:453-454 (`mz_inflate`): `if (pState->m_window_bits > 0) decomp_flags |= TINFL_FLAG_PARSE_ZLIB_HEADER;`
> miniz.c:447-450: `if (flush == MZ_PARTIAL_FLUSH) flush = MZ_SYNC_FLUSH; if ((flush) && (flush != MZ_SYNC_FLUSH) && (flush != MZ_FINISH)) return MZ_STREAM_ERROR;`
> miniz.c:466-490 vs 507-547: the `first_call && flush==MZ_FINISH` branch (single-shot, `TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF`) vs the `for(;;)` streaming/dictionary loop used for `MZ_NO_FLUSH`/`MZ_SYNC_FLUSH` or subsequent calls.
> miniz.c:318-345 (`mz_compress2`): `level` parameter passed straight to `mz_deflateInit(&stream, level)`.
> miniz/tests/compress_fuzzer.c:80-83: harness calls `check_compress_level(..., 1/3/6/7)` to vary level before decompressing — demonstrates level as a path-selecting parameter feeding into decompression fuzzing.

### P2.11 Input Expectations
**Claim:** `pSource` is expected to be a zlib-wrapped DEFLATE stream (2-byte CMF/FLG header + deflate blocks + 4-byte Adler-32 trailer) because `mz_uncompress2` always uses `mz_inflateInit`→`MZ_DEFAULT_WINDOW_BITS` (positive), which turns on `TINFL_FLAG_PARSE_ZLIB_HEADER`. There is no explicit minimum size check in `mz_uncompress2`/`mz_inflate`; malformed/too-short input simply drives `tinfl_decompress` into `TINFL_STATUS_FAILED`/`NEEDS_MORE_INPUT`, surfaced as `MZ_DATA_ERROR` or `MZ_BUF_ERROR`. `*pSource_len` and `*pDest_len` must each fit in 32 bits (checked explicitly). No null termination is required; lengths are explicit.
**Evidence:**
> miniz.c:453-454: `TINFL_FLAG_PARSE_ZLIB_HEADER` is set because `mz_inflateInit`→`mz_inflateInit2(&stream, MZ_DEFAULT_WINDOW_BITS)` (miniz.c:409) uses the positive window_bits (15).
> miniz.h:416: doc comment "window_bits must be MZ_DEFAULT_WINDOW_BITS (to parse zlib header/footer) or -MZ_DEFAULT_WINDOW_BITS (raw deflate)."
> miniz.c:568-569: explicit 32-bit size check, else `MZ_PARAM_ERROR`.
> miniz.c:482-489: `if (status < 0) return MZ_DATA_ERROR; else if (status != TINFL_STATUS_DONE) { ...; return MZ_BUF_ERROR; }` — malformed/incomplete streams are rejected via these two codes, no separate minimum-length precheck.
> miniz/tests/compress_fuzzer.c:34-46,48-60 (`write_zlib_header`, `check_decompress`): demonstrates callers must prepend a valid 2-byte zlib header to arbitrary fuzzer data before calling `uncompress`/`mz_uncompress2` for the zlib-header path to be meaningfully exercised (otherwise header validation just rejects garbage bytes).

## Other Logic-Group entries: call protocol and combination with the target

### mz_uncompress (entry, variant of target)
**Claim:** Thin wrapper: takes `source_len` by value instead of by pointer, then calls `mz_uncompress2(pDest, pDest_len, pSource, &source_len)` with a local copy. Same init/use/destroy, same return codes, same parameter rules as the target, but the harness loses visibility into "bytes consumed" since the local copy is discarded after the call. Combine with target: calling both in one harness on the same input gives identical decompression behavior/coverage (no extra path) — only useful if the harness wants to also check `mz_uncompress`'s own wrapper code (miniz.c:592-595) for existence/linkage, not extra decompression coverage.
**Evidence:**
> miniz.c:592-595: `int mz_uncompress(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len) { return mz_uncompress2(pDest, pDest_len, pSource, &source_len); }`
> miniz.h:443: declaration, same return-code contract as `mz_uncompress2` (miniz.h:442 comment: "Returns MZ_OK on success, or one of the error codes from mz_inflate() on failure.")

### mz_inflateInit2 (entry, configuration)
**Claim:** Must be called before any `mz_inflate` call when NOT going through `mz_uncompress2`/`mz_uncompress` (those call `mz_inflateInit` internally). Needs a zero/caller-initialized `mz_stream` (zalloc/zfree/opaque may be left 0 — they're defaulted inside). Parameter `window_bits` must be exactly `MZ_DEFAULT_WINDOW_BITS` (15, zlib-wrapped) or `-MZ_DEFAULT_WINDOW_BITS` (-15, raw deflate); any other value returns `MZ_PARAM_ERROR`. Allocates `inflate_state` via `pStream->zalloc`; stores it in `pStream->state`. Returns `MZ_OK` or `MZ_MEM_ERROR`/`MZ_PARAM_ERROR`/`MZ_STREAM_ERROR`. Must be matched by exactly one `mz_inflateEnd` to free `pStream->state`. To use with the target in one harness: call `mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS)` directly (bypassing `mz_uncompress2`) to exercise the raw-deflate branch that `mz_uncompress2` can never reach (it hardcodes zlib-wrapped via `mz_inflateInit`).
**Evidence:**
> miniz.c:371-405: body (allocation, field resets, `tinfl_init`, `pDecomp->m_window_bits = window_bits`).
> miniz.c:376-377: window_bits validation → `MZ_PARAM_ERROR`.
> miniz.c:390-392: `pDecomp = (inflate_state *)pStream->zalloc(...); if (!pDecomp) return MZ_MEM_ERROR;`
> miniz.h:415-417: doc comment on window_bits semantics.

### mz_inflate (entry, core streaming call used internally by the target)
**Claim:** Requires a stream previously initialized by `mz_inflateInit`/`mz_inflateInit2` (checks `pStream->state` non-null, else `MZ_STREAM_ERROR`, miniz.c:445-446). `flush` must be `MZ_NO_FLUSH`(0), `MZ_SYNC_FLUSH`(2, `MZ_PARTIAL_FLUSH` is coerced to this), or `MZ_FINISH`; anything else → `MZ_STREAM_ERROR` (miniz.c:447-450). On the very first call with `flush==MZ_FINISH` it assumes both buffers are sized for the whole stream (same path `mz_uncompress2` uses). Calling it directly with `MZ_NO_FLUSH`/`MZ_SYNC_FLUSH` and smaller buffers drives the internal dictionary-buffered loop (miniz.c:507-547) — this is how the harness reaches partial-input/partial-output and dictionary-wraparound logic that `mz_uncompress2` skips. Returns `MZ_OK` (more work needed), `MZ_STREAM_END` (done, adler32 checked), `MZ_DATA_ERROR` (corrupt stream), `MZ_BUF_ERROR` (no forward progress / buffer too small), or `MZ_STREAM_ERROR`. After `MZ_STREAM_END` or an error, the harness must still call `mz_inflateEnd` once to free state (the function itself does not free anything).
**Evidence:**
> miniz.c:438-450: parameter checks (`pStream->state`, flush values).
> miniz.c:459-460: `if (pState->m_last_status < 0) return MZ_DATA_ERROR;` — cannot call again after a failed call without `mz_inflateReset`.
> miniz.c:462-464: `if (pState->m_has_flushed && (flush != MZ_FINISH)) return MZ_STREAM_ERROR; pState->m_has_flushed |= (flush == MZ_FINISH);` — co-call constraint: once `MZ_FINISH` has been used, further calls with other flush values are rejected.
> miniz/tests/large_fuzzer.c:94-106: `for (;;) { d_stream.next_out=...; d_stream.avail_out=...; err = inflate(&d_stream, Z_NO_FLUSH); if (err==Z_STREAM_END) break; CHECK_ERR(err,"large inflate"); } err = inflateEnd(&d_stream);` — real streaming usage pattern (Z_NO_FLUSH + loop) for the harness to mirror when exercising `mz_inflate` directly.

### mz_inflateReset (entry, lifecycle)
**Claim:** Resets an already-initialized stream's internal `inflate_state` (requires `pStream->state` to already exist — it dereferences it unconditionally at miniz.c:425 without a null check beyond `pStream` itself) so the same `mz_stream`/`inflate_state` allocation can be reused for a new decompression without calling `mz_inflateEnd`+`mz_inflateInit2` again. Does NOT reset `m_window_bits` (explicitly commented out), so the header-parsing mode from the original `mz_inflateInit2` call persists. Returns `MZ_OK` or `MZ_STREAM_ERROR` if `pStream` is null. Combine with target: use between repeated `mz_inflate` sequences on the same `mz_stream` object in a loop-based harness variant that doesn't call `mz_uncompress2` (since `mz_uncompress2` builds a fresh local stream every call and never reuses state).
**Evidence:**
> miniz.c:412-436: body; line 425 `pDecomp = (inflate_state *)pStream->state;` then uses it directly — must be called only after `mz_inflateInit`/`mz_inflateInit2`.
> miniz.c:433: `/* pDecomp->m_window_bits = window_bits */;` — comment showing window_bits is intentionally preserved.
> miniz.h:419: "Quickly resets ... Same as calling mz_inflateEnd() followed by mz_inflateInit()/mz_inflateInit2()."

### mz_inflateEnd (entry, cleanup)
**Claim:** Frees `pStream->state` via `pStream->zfree` if non-null, sets it to NULL, returns `MZ_OK`; safe to call even if `state` is already NULL. Must be called exactly once per successful `mz_inflateInit`/`mz_inflateInit2` to avoid a memory leak (the `inflate_state` struct, which embeds a `TINFL_LZ_DICT_SIZE`-byte dictionary, is heap-allocated). `mz_uncompress2` already calls this internally on every path (see target body above); a harness only needs to call it itself when it drives `mz_inflateInit2`/`mz_inflate` directly instead of going through `mz_uncompress2`/`mz_uncompress`.
**Evidence:**
> miniz.c:550-559: full body.
> miniz.c:367: `mz_uint8 m_dict[TINFL_LZ_DICT_SIZE];` inside `inflate_state` — confirms a nontrivial heap allocation that must be freed.

### mz_compress2 (entry, inverse operation / input generator)
**Claim:** Not itself part of the decompression lifecycle, but useful in the harness as a producer of well-formed zlib streams to feed into `mz_uncompress2` for round-trip testing. Needs caller to zero/prepare an `mz_stream` is NOT required by the caller — like `mz_uncompress2`, `mz_compress2` builds and owns its own local `mz_stream`, calls `mz_deflateInit(&stream, level)`, then `mz_deflate(&stream, MZ_FINISH)`, and always calls `mz_deflateEnd` before returning. `level` ranges 0-10 or `MZ_DEFAULT_COMPRESSION`(-1); invalid `source_len`/`*pDest_len` combos (>32-bit) → `MZ_PARAM_ERROR`. Returns `MZ_OK` on success with `*pDest_len` updated to the compressed size, or an error code from `mz_deflate`/`mz_deflateEnd`. Combine with target: `mz_compress2(comp, &compLen, plain, plainLen, level)` → check `MZ_OK` → feed `comp`/`compLen` as `pSource`/`*pSource_len` into `mz_uncompress2`, then optionally compare decompressed output back to `plain` (as done in `compress_fuzzer.c`).
**Evidence:**
> miniz.c:318-346: full body, mirrors `mz_uncompress2`'s self-contained init/use/destroy pattern on the encode side.
> miniz.h:402-403: declarations; miniz.h:401 "Returns MZ_OK on success, or one of the error codes from mz_deflate() on failure."
> miniz/tests/compress_fuzzer.c:17-27: `compress2(compr,&comprLen,data,dataLen,level); uncompress(uncompr,&uncomprLen,compr,comprLen); assert(dataLen==uncomprLen); assert(0==memcmp(data,uncompr,dataLen));` — exact round-trip combination pattern already used by a real miniz fuzzer, varying `level` in {1,3,6,7} (compress_fuzzer.c:80-83).

## Build notes

**Claim:** Fuzzer source files for miniz are compiled as plain C (`$CC $CFLAGS`), then linked with the C++ driver (`$CXX ... $LIB_FUZZING_ENGINE`) against a static `libminiz.a` built via CMake with `AMALGAMATE_SOURCES=ON`; the target's header is included via the amalgamated build directory (`-Ibuild/amalgamation`), i.e. harnesses should `#include "miniz.h"` and rely on `-Ibuild/amalgamation` (or equivalently the project root where `miniz.h`/`miniz.c` live) for the include path. No special macros are required at the fuzzer level beyond what CMake's `AMALGAMATE_SOURCES` build already defines (`MINIZ_EXPORT` is stubbed to empty via a generated `miniz_export.h`).
**Evidence:**
> miniz/tests/ossfuzz.sh:21-25: generates `miniz_export.h` defining empty `MINIZ_EXPORT`.
> miniz/tests/ossfuzz.sh:27-30: `mkdir build; cd build; cmake .. -DAMALGAMATE_SOURCES=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_FUZZERS=ON -DBUILD_TESTS=OFF; make -j$(nproc)`
> miniz/tests/ossfuzz.sh:35-41: `for f in $(find $SRC -name '*_fuzzer.c'); do b=$(basename -s .c $f); $CC $CFLAGS -Ibuild/amalgamation $f -c -o /tmp/$b.o; $CXX $CXXFLAGS -stdlib=libc++ -Ibuild/amalgamation /tmp/$b.o -o $OUT/$b $LIB_FUZZING_ENGINE ./build/libminiz.a; ...; done` — shows C compilation of the fuzzer, C++ link step, static lib `libminiz.a`, include path `build/amalgamation` (where the amalgamated `miniz.h`/`miniz.c` are generated).
> ossfuzz_view/miniz-c4/build.sh:18-19: `$SRC/miniz/tests/ossfuzz.sh` is the actual OSS-Fuzz build entry point used by this project's Dockerfile-based build.
> ossfuzz_view/miniz-c4/Dockerfile:22,24: `git clone --depth 1 https://github.com/richgel999/miniz.git; WORKDIR miniz` — confirms project layout matches `${QF_WORK}/src/miniz-c4/miniz`.
> miniz/tests/uncompress2_fuzzer.c:1-16: current stub harness for this target only `#include`s nothing from miniz but force-links the symbol via `__asm__("mz_uncompress2")`; existing fuzzer examples (`compress_fuzzer.c:12`, `large_fuzzer.c:9`, `uncompress_fuzzer.c:9`) all simply `#include "miniz.h"` with no other project header, confirming `miniz.h` (single header, declares `mz_uncompress2` at line 444) is the only include needed.