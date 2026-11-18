## target: uncompress2

### P2.1 Init Sequence
**Claim:** `uncompress2` is a self-contained, one-shot API. The caller does NOT need to call `inflateInit`/`inflate`/`inflateEnd` themselves — `uncompress2` does this internally via `uncompress2_z`. No library-wide global init is required (zlib has no global init function). The only thing the caller must do before calling it is allocate the `dest` buffer and set `*destLen` to its capacity, and set `*sourceLen` to the number of valid bytes in `source`.
**Evidence:**
> zlib/uncompr.c:83-91: `uncompress2` just forwards to `uncompress2_z`
> zlib/uncompr.c:51: `err = inflateInit(&stream); if (err != Z_OK) return err;` — init happens inside `uncompress2_z`, not something the caller does.
> zlib/zlib.h:1339-1343: doc comment "Same as uncompress, except that sourceLen is a pointer ... On return, *sourceLen is the number of source bytes consumed."

### P2.2 Parameter Construction
**Claim:** `dest` is a writable `Bytef*` buffer of at least `*destLen` bytes (may be NULL only if `*destLen==0`); `destLen` is a non-NULL `uLongf*` whose pointee is the destination buffer capacity on entry and the actual decompressed size on exit; `source` is a `const Bytef*` of at least `*sourceLen` bytes (may be NULL only if `*sourceLen==0`); `sourceLen` is a non-NULL `uLong*` whose pointee is the number of valid input bytes on entry and the number of bytes actually consumed on exit.
**Evidence:**
> zlib/uncompr.c:36-38 (in `uncompress2_z`, which `uncompress2` wraps byte-for-byte): `if (sourceLen == NULL || (*sourceLen > 0 && source == NULL) || destLen == NULL || (*destLen > 0 && dest == NULL)) return Z_STREAM_ERROR;`
> zlib/uncompr.c:86-90: `uncompress2` copies `*destLen`/`*sourceLen` (uLong) into local `z_size_t` variables `got`/`used` before calling `uncompress2_z`, then writes results back truncated to `uLong`.
> zlib.h:1335-1336: declaration `uncompress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong *sourceLen)`.
> zlib.h:1312-1326 (doc comment above uncompress, same semantics apply to uncompress2 per zlib.h:1339): "*destLen is the total size of the destination buffer, which must be large enough to hold the entire uncompressed data."

### P2.3 Object Lifecycle
**Claim:** There is no persistent object exposed to the caller — `uncompress2` creates an internal `z_stream`, calls `inflateInit`, loops `inflate(&stream, Z_NO_FLUSH)` until not `Z_OK`, then always calls `inflateEnd` before returning (even on error paths after `inflateInit` succeeded). The caller's lifecycle is simply: allocate buffers → call `uncompress2` → use/inspect `dest`/`*destLen`/`*sourceLen` → free the caller-owned buffers.
**Evidence:**
> zlib/uncompr.c:51-52: `err = inflateInit(&stream); if (err != Z_OK) return err;` (early-return if init fails — no `inflateEnd` call needed in that case because no state was allocated)
> zlib/uncompr.c:57-67: `do { ... err = inflate(&stream, Z_NO_FLUSH); } while (err == Z_OK);`
> zlib/uncompr.c:77: `inflateEnd(&stream);` called unconditionally once the loop exits, regardless of error code, before the final `return`.

### P2.4 Return Value Handling
**Claim:** Return value is `int`: `Z_OK` (0) on success, `Z_MEM_ERROR` if `inflateInit`/`inflate` failed to allocate, `Z_BUF_ERROR` if `dest` was too small (unless no input bytes remain, in which case this is remapped to `Z_DATA_ERROR`), `Z_DATA_ERROR` for corrupted/incomplete/truncated streams or bad dictionary, `Z_STREAM_ERROR` if parameters are structurally invalid (e.g. NULL `destLen`/`sourceLen` or NULL buffer with nonzero length). Callers must check for `Z_OK` before trusting `dest`/`*destLen`; on `Z_BUF_ERROR` zlib still fills `dest` up to that point (per doc) but data is not complete.
**Evidence:**
> zlib/uncompr.c:78-81: `return err == Z_STREAM_END ? Z_OK : err == Z_NEED_DICT ? Z_DATA_ERROR : err == Z_BUF_ERROR && len == 0 ? Z_DATA_ERROR : err;`
> zlib/uncompr.c:36-38: `Z_STREAM_ERROR` on bad args.
> zlib.h:1328-1332 (doc, applies analogously to uncompress2 per zlib.h:1339): "uncompress returns Z_OK if success, Z_MEM_ERROR ... Z_BUF_ERROR ... or Z_DATA_ERROR ... In the case where there is not enough room, uncompress() will fill the output buffer with the uncompressed data up to that point."
> test/example.c:76-77: `err = uncompress(...); CHECK_ERR(err, "uncompress");` — real caller checks return value immediately and aborts on non-`Z_OK`.

### P2.5 Cleanup Sequence
**Claim:** `uncompress2` cleans up its own internal `z_stream` (via `inflateEnd`) in every reachable path after `inflateInit` succeeds — the caller never calls `inflateEnd` itself and never passes a `z_stream` to `uncompress2`. The only cleanup the harness/caller is responsible for is freeing the `dest`/`source` buffers it allocated (e.g. with `malloc`/`calloc`), on both success and failure, since `uncompress2` never takes ownership of them.
**Evidence:**
> zlib/uncompr.c:77: `inflateEnd(&stream);` unconditional cleanup inside the function before return.
> ossfuzz_view/zlib-c4/compress_fuzzer.c:93-95: `err: free(compr); free(uncompr);` — real harness frees both buffers via a single error-and-success-shared cleanup label regardless of `uncompress`/`compress2` return codes.

### P2.6 API Existence
**Claim:** All functions referenced below exist in this codebase's current zlib.h / .c sources.
**Evidence:**
> zlib.h:1335-1336 `uncompress2`; zlib.h:1337-1338 `uncompress2_z`; zlib.h:1315-1318 `uncompress`/`uncompress_z`; zlib.h:1288-1293 `compress2`/`compress2_z`; zlib.h:1307-1308 `compressBound`/`compressBound_z`; zlib.h:382 `inflateInit` (macro, see below); zlib.h:525 `inflateEnd`.
> zlib/uncompr.c:29,83,92,97 define `uncompress2_z`, `uncompress2`, `uncompress_z`, `uncompress`.
> zlib/compress.c:24,67,77,82,91,96 define `compress2_z`, `compress2`, `compress_z`, `compress`, `compressBound_z`, `compressBound`.

### P2.7 Co-call Constraints
**Claim:** Inside `uncompress2`/`uncompress2_z`, `inflateInit` and `inflateEnd` are a mandatory paired sequence — `inflateEnd` is only called if `inflateInit` returned `Z_OK` (otherwise there's nothing to free). From the caller's perspective, the natural pairing for a round-trip fuzz harness is `compress2`/`compressBound` (producer) with `uncompress2` (consumer) — `compressBound(sourceLen)` must be used to size the buffer passed to `compress2` before compression, and the compressed output + its length become the `source`/`*sourceLen` input to `uncompress2`.
**Evidence:**
> zlib/uncompr.c:51-52,77: `inflateInit` success gate before `inflateEnd`.
> zlib.h:1297-1299 (compress2 doc): "destLen ... must be at least the value returned by compressBound(sourceLen)."
> ossfuzz_view/zlib-c4/compress_fuzzer.c:77,83,90: `comprLen = compressBound(size); compr = calloc(1, comprLen); check_compress_level(...)` then feeding `compr`/`comprLen` into `uncompress(...)` at line 29/58.

### P2.8 Prerequisite State
**Claim:** No external context/connection/global config is required before calling `uncompress2` — it is a pure, stateless, reentrant function operating solely on the four parameters passed in (it allocates its own internal `z_stream` and default `zalloc`/`zfree`/`opaque` of 0, i.e. standard malloc/free). The only "state" that must exist is the caller's already-allocated `dest` buffer and a `source` buffer containing (possibly corrupted) zlib-formatted bytes.
**Evidence:**
> zlib/uncompr.c:47-49: `stream.zalloc = (alloc_func)0; stream.zfree = (free_func)0; stream.opaque = (voidpf)0;` — set fresh for every call, no reliance on prior global state.
> zlib/uncompr.c:42-43: `if (left == 0 && dest == Z_NULL) dest = (Bytef *)&stream.reserved;` — internally handles the zero-length-dest edge case itself.

### P2.9 Result Usage
**Claim:** After `uncompress2` returns `Z_OK`, real callers read back `*destLen` bytes from `dest` as the decompressed payload (e.g. compare against original data, print as a string, pass to subsequent parsers) and may read `*sourceLen` to know how many input bytes were consumed (for chaining in a stream protocol). No other zlib API takes the `dest` buffer or `*destLen` as a parameter to continue zlib-specific processing — downstream usage is purely consumer-defined (string compare, memcmp, further non-zlib parsing).
**Evidence:**
> test/example.c:76,79-84: `uncompress(uncompr, &uncomprLen, compr, comprLen); ... if (strcmp((char*)uncompr, hello)) {... exit(1);} else printf("uncompress(): %s\n", (char*)uncompr);` — decompressed buffer treated as a C string and compared to known plaintext.
> ossfuzz_view/zlib-c4/compress_fuzzer.c:28-33: `compress2(...); uncompress(compr, &comprLen, ...)... assert(dataLen == uncomprLen); assert(0 == memcmp(data, uncompr, dataLen));` — round-trip validated via memcmp against the original fuzz input.
> ossfuzz_view/zlib-c4/zlib_uncompress3_fuzzer.cc:39: `uncompress2(buffer, &buffer_length, data, &buf_size); return 0;` — result ignored except for the return code (harness only cares that it doesn't crash/leak/overflow), ignoring return value is acceptable for a fuzz-only harness provided buffers are sized safely beforehand.

### P2.10 Behaviour Controls
**Claim:** `uncompress2` itself exposes no explicit flag parameter; its only "mode selector" is the relationship between `*destLen` (controls whether `Z_BUF_ERROR`/`Z_DATA_ERROR` is hit), `*sourceLen` (controls how much of `source` is fed, and whether truncation triggers `Z_DATA_ERROR` via `Z_BUF_ERROR+len==0`), and the content of `source` itself (zlib header CMF/FLG bytes, FDICT bit triggering `Z_NEED_DICT`→remapped to `Z_DATA_ERROR` since no preset dictionary is ever set, corrupted Huffman tables, truncated final block). `compress2`'s `level` parameter (`Z_NO_COMPRESSION`(0)..`Z_BEST_COMPRESSION`(9), or `Z_DEFAULT_COMPRESSION`(-1)) changes the structure of the stream later fed to `uncompress2`, and an invalid `level` yields `Z_STREAM_ERROR` from `deflateInit` (propagated out of `compress2`).
**Evidence:**
> zlib/uncompr.c:62-65: chunking loop splits `*sourceLen`/`*destLen` into `uInt`-sized (`max = (uInt)-1`) pieces fed one `inflate()` call at a time — relevant when `*sourceLen`/`*destLen` exceed `UINT_MAX`.
> zlib/uncompr.c:78-81: `Z_NEED_DICT` (from a stream requiring a preset dictionary, since `uncompress2` never calls `inflateSetDictionary`) is remapped to `Z_DATA_ERROR`; `Z_BUF_ERROR` with no input left (`len==0`) also becomes `Z_DATA_ERROR`.
> zlib/compress.c:42-43: `err = deflateInit(&stream, level); if (err != Z_OK) return err;` — invalid `level` surfaces as whatever `deflateInit` returns (typically `Z_STREAM_ERROR`).
> ossfuzz_view/zlib-c4/compress_fuzzer.c:66,28: `const int level = d[0] % 10; ... check_compress_level(..., level);` — real harness varies `level` 0-9.
> ossfuzz_view/zlib-c4/compress_fuzzer.c:38-47,70-74: harness also mutates the raw zlib header bits (`compression_method`, `flags`/FDICT-ish bits) directly in the stream handed to `uncompress`, exercising header-validation paths inside `inflate`.

### P2.11 Input Expectations
**Claim:** `source[0..*sourceLen)` must be a byte stream that `inflate()` can parse as RFC1950 zlib-format (2-byte CMF/FLG header with correct check bits, optional preset-dictionary flag, DEFLATE-compressed blocks, trailing 4-byte Adler-32). There is no required NUL-termination (lengths are explicit via `*sourceLen`); zero-length source is legal only if `*sourceLen==0` (and then `source` may be NULL). `dest`'s capacity (`*destLen`) must be large enough to hold the entire decompressed output or the call fails with `Z_BUF_ERROR`/`Z_DATA_ERROR`; zero-length `dest` is legal if `*destLen==0` and is handled by substituting `&stream.reserved` as a non-NULL dummy output pointer.
**Evidence:**
> zlib/uncompr.c:36-38: NULL/length consistency checks (`Z_STREAM_ERROR` otherwise).
> zlib/uncompr.c:42-43: dest==NULL && *destLen==0 handled specially: `dest = (Bytef *)&stream.reserved;`
> zlib.h:1320-1326 (doc for uncompress, applies to uncompress2 per zlib.h:1339-1343): "*destLen is the total size of the destination buffer, which must be large enough to hold the entire uncompressed data. (The size of the uncompressed data must have been saved previously by the compressor...)" — i.e. the real uncompressed size is NOT self-describing in the zlib stream; the caller must know/guess it ahead of time.
> zlib/uncompr.c:66: `inflate(&stream, Z_NO_FLUSH)` — actual header/Huffman/adler32 validation happens inside `inflate()`'s core (not part of this target), which this function simply drives to completion or first error.

## Other entries: call protocol & combination with the target

### uncompress (zlib/uncompr.c:97-100, zlib.h:1315-1316)
**Claim:** Thin wrapper over `uncompress2`: takes `sourceLen` by value instead of by pointer (caller cannot learn how many bytes were actually consumed). No init needed beyond allocating `dest`/`source`. Parameters: `dest` (out buffer, `*destLen`=capacity on entry/actual size on exit), `source`/`sourceLen` (in buffer and its exact byte length, by value). Return: same codes as `uncompress2` (`Z_OK`/`Z_MEM_ERROR`/`Z_BUF_ERROR`/`Z_DATA_ERROR`/`Z_STREAM_ERROR`). Nothing to free beyond caller's own `dest`/`source` buffers. Used directly in place of `uncompress2` when the harness does not need the "bytes consumed" output — real callers (test/example.c:76, ossfuzz compress_fuzzer.c:29,58, zlib_uncompress_fuzzer.cc:16) all use this simpler variant; combining with the target in one harness means picking either `uncompress` or `uncompress2` per call (they are equivalent apart from the extra out-param), not both on the same buffer.
**Evidence:**
> zlib/uncompr.c:97-100: `int ZEXPORT uncompress(...) { uLong used = sourceLen; return uncompress2(dest, destLen, source, &used); }`
> zlib.h:1328-1332: documented return codes.

### uncompress_z / uncompress2_z (zlib/uncompr.c:29-82,92-96, zlib.h:1317,1337-1338)
**Claim:** Size_t-length siblings. `uncompress2_z` (zlib/uncompr.c:29) is the actual implementation containing the inflate loop that `uncompress2` wraps; `uncompress_z` (zlib/uncompr.c:92-96) wraps `uncompress2_z` the same way `uncompress` wraps `uncompress2`. Same parameter rules as the target but with `z_size_t*`/`z_size_t` instead of `uLongf*`/`uLong`, allowing lengths beyond 4GB on platforms where `uLong` is 32-bit while `z_size_t`/`size_t` is 64-bit. A harness exercising `uncompress2` can additionally call `uncompress2_z` directly on the same `source` buffer with `z_size_t` lengths (e.g. cast from `size_t`) to probe the uLong-vs-size_t truncation boundary described in the feature background, since `uncompress2` itself truncates its size_t results down to `uLong` at zlib/uncompr.c:88-89 (`*sourceLen = (uLong)used; *destLen = (uLong)got;`).
**Evidence:**
> zlib/uncompr.c:29-82: full implementation of `uncompress2_z` (identical structure/logic to what was shown for the target).
> zlib/uncompr.c:86-90: `uncompress2`'s truncation of `z_size_t got/used` to `uLong` on the way out — potential silent truncation if decompressed size or consumed bytes exceed `UINT32_MAX` on LP64 platforms.
> zlib.h:1337-1338: declaration of `uncompress2_z` with `z_size_t *destLen`/`z_size_t *sourceLen`.

### compress2 / compress2_z (zlib/compress.c:24-74, zlib.h:1288-1293)
**Claim:** Producer counterpart. Before calling, caller must allocate `dest` sized via `compressBound(sourceLen)` (not arbitrary) and set `*destLen` to that capacity; `source`/`sourceLen` describe the plaintext to compress (by value, not pointer — caller doesn't need to pre-know compressed size). `level` selects compression path: `Z_NO_COMPRESSION`(0) through `Z_BEST_COMPRESSION`(9), or `Z_DEFAULT_COMPRESSION`(-1); any other value causes `deflateInit` to fail and `compress2` to return `Z_STREAM_ERROR`. On success (`Z_OK`), `*destLen` is updated to the actual compressed size, and this `dest` buffer (now holding a valid, or fuzzer-mutated, zlib stream) together with the updated `*destLen` is exactly the `source`/`*sourceLen` pair to feed into `uncompress2`. Internally pairs `deflateInit`/`deflate`-loop/`deflateEnd` (its own self-contained lifecycle, mirroring `uncompress2`'s `inflateInit`/`inflate`/`inflateEnd`); caller never touches `deflateInit`/`deflateEnd` directly. Nothing to free beyond the caller's own `dest`/`source` buffers.
**Evidence:**
> zlib/compress.c:42-43: `err = deflateInit(&stream, level); if (err != Z_OK) return err;`
> zlib/compress.c:64: `deflateEnd(&stream);` unconditional before return.
> zlib/compress.c:60: `err = deflate(&stream, sourceLen ? Z_NO_FLUSH : Z_FINISH);` — flush mode chosen automatically based on remaining input, not caller-controlled.
> zlib.h:1295-1304: doc — `destLen` "must be at least the value returned by compressBound(sourceLen)"; return codes `Z_OK`/`Z_MEM_ERROR`/`Z_BUF_ERROR`/`Z_STREAM_ERROR` (invalid level).
> ossfuzz_view/zlib-c4/compress_fuzzer.c:28-29,77,83,90: real harness pattern `comprLen = compressBound(size); compr = calloc(...); compress2(compr,&comprLen,data,dataLen,level); uncompress(uncompr,&uncomprLen,compr,comprLen);` — direct round-trip producer→target chaining.

### compressBound / compressBound_z (zlib/compress.c:91-98, zlib.h:1307-1308)
**Claim:** Pure helper/configuration function, no lifecycle (stateless, takes a `uLong`/`z_size_t` input size and returns an upper bound `uLong`/`z_size_t`; no allocation, no error code — cannot fail other than saturating to `(uLong)-1`/`(z_size_t)-1` on overflow). Must be called before `compress2` to size its `dest` buffer; it is not used with `uncompress2` directly, only indirectly through sizing the compressed buffer that becomes `uncompress2`'s `source`.
**Evidence:**
> zlib/compress.c:91-95: `z_size_t ZEXPORT compressBound_z(z_size_t sourceLen) { ... return bound < sourceLen ? (z_size_t)-1 : bound; }`
> zlib/compress.c:96-98: `uLong ZEXPORT compressBound(uLong sourceLen) { z_size_t bound = compressBound_z(sourceLen); return (uLong)bound != bound ? (uLong)-1 : (uLong)bound; }`
> zlib.h:1309-1313: doc "would be used before a compress() or compress2() call to allocate the destination buffer."
> ossfuzz_view/zlib-c4/compress_fuzzer.c:77: `size_t comprLen = compressBound(size);` immediately before `calloc`.

## Build notes
**Claim:** Fuzzers for this project are compiled as free-standing source files (C or C++) against the static library `libz.a`, with the zlib headers found via `-I.` (the zlib source/build root, where `zlib.h`/`zconf.h` live after `./configure`); `.cc` fuzzers are compiled with `$CXX`/`-std=c++11`, `.c` fuzzers with `$CC` then linked with `$CXX -stdlib=libc++`. Both link `$LIB_FUZZING_ENGINE` and `./libz.a`.
**Evidence:**
> ossfuzz_view/zlib-c4/build.sh:18-24: `./configure; make all` builds `libz.a` and generates `zconf.h`/sets up `zlib.h` in the repo root.
> ossfuzz_view/zlib-c4/build.sh:31-34: `for f in $(find $SRC -name '*_fuzzer.cc'); do ... $CXX $CXXFLAGS -std=c++11 -I. $f -o $OUT/$b $LIB_FUZZING_ENGINE ./libz.a; done`
> ossfuzz_view/zlib-c4/build.sh:38-43: `for f in $(find $SRC -name '*_fuzzer.c'); do $CC $CFLAGS -I. $f -c -o /tmp/$b.o; $CXX $CXXFLAGS -o $OUT/$b /tmp/$b.o -stdlib=libc++ $LIB_FUZZING_ENGINE ./libz.a; done`
> ossfuzz_view/zlib-c4/Dockerfile:19-21: `git clone ... zlib.git; WORKDIR zlib; COPY run_tests.sh build.sh *_fuzzer.c* $SRC/` — fuzzer sources are copied into the zlib source tree root, so `#include "zlib.h"` (as seen in zlib_uncompress_fuzzer.cc:9, compress_fuzzer.c:20, zlib_uncompress3_fuzzer.cc:21) resolves via `-I.` to the project root's generated header.
> ossfuzz_view/zlib-c4/zlib_uncompress2_fuzzer.cc:1-11: NOTE — the existing `zlib_uncompress2_fuzzer.cc` in this checkout is a stub ("Gold harness replaced by an empty stub for baseline generation") that does nothing; the real reference implementation for an `uncompress2`-driving harness is `zlib_uncompress3_fuzzer.cc` (full content read, uses `uncompress2` directly with a fixed `buffer[256*1024]`, derives `buffer_length` from input bytes and clamps it to `sizeof(buffer)`, and passes `size`/`data` as `sourceLen`/`source`).