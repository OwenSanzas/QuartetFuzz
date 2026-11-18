## target: uncompress

### P2.1 Init Sequence
**Claim:** No library-level global init call is required before `uncompress`. The caller only needs: (1) a `source` buffer containing a complete zlib-wrapped deflate stream, (2) a `sourceLen` describing its length, (3) a `dest` buffer large enough for the uncompressed data, and (4) `*destLen` set, on entry, to the capacity of `dest`. Internally `uncompress` → `uncompress2` → `uncompress2_z`, which itself calls `inflateInit(&stream)` (a macro for `inflateInit_`) before the first `inflate()` call — this init is done *inside* `uncompress`, the harness/caller does not call it.
**Evidence:**
> zlib/uncompr.c:97-101: `uncompress(dest, destLen, source, sourceLen)` just forwards to `uncompress2`.
> zlib/uncompr.c:83-91: `uncompress2` forwards to `uncompress2_z`.
> zlib/uncompr.c:51-52: inside `uncompress2_z`: `err = inflateInit(&stream); if (err != Z_OK) return err;` — init happens internally, not something the caller must do.
> zlib.h:1320-1326 (doc comment above `uncompress`/`uncompress_z`): "*destLen is the total size of the destination buffer, which must be large enough to hold the entire uncompressed data... (size... must have been saved previously by the compressor and transmitted... outside the scope of this library)".

### P2.2 Parameter Construction
**Claim:** `dest` is `Bytef*` (writable buffer, caller-allocated, size ≥ `*destLen`); `destLen` is `uLongf*`, non-NULL, pointing to the capacity of `dest` on entry (may be 0 if `dest` is NULL — library supplies an internal dummy output pointer in that case); `source` is `const Bytef*` of length `sourceLen` (`uLong`); `sourceLen` just describes `source`'s length (no pointer, by value, unlike `uncompress2`). Internal check: `uncompress2_z` rejects `(sourceLen>0 && source==NULL)` or `(*destLen>0 && dest==NULL)` with `Z_STREAM_ERROR`.
**Evidence:**
> zlib.h:1315-1316: `int ZEXPORT uncompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen);`
> zlib/uncompr.c:36-38: `if (sourceLen == NULL || (*sourceLen > 0 && source == NULL) || destLen == NULL || (*destLen > 0 && dest == NULL)) return Z_STREAM_ERROR;` (checked in `uncompress2_z`, reached via `uncompress`→`uncompress2`→`uncompress2_z`).
> zlib/uncompr.c:42-43: `if (left == 0 && dest == Z_NULL) dest = (Bytef *)&stream.reserved; /* next_out cannot be NULL */` — internal guard so `inflate` never sees `next_out==NULL`.

### P2.3 Object Lifecycle
**Claim:** `uncompress` is single-shot and fully self-contained: it creates a local `z_stream` on the stack, initializes it (`inflateInit`), drives decompression to completion via one or more `inflate(&stream, Z_NO_FLUSH)` calls in a loop until `err != Z_OK`, and tears it down with `inflateEnd(&stream)` before returning — all internal to the call. The caller never sees or manages the `z_stream`.
**Evidence:**
> zlib/uncompr.c:29-82 (`uncompress2_z`, the real engine behind `uncompress`): declares `z_stream stream;` locally (line 31), calls `inflateInit(&stream)` (line 51), loop `do { ... err = inflate(&stream, Z_NO_FLUSH); } while (err == Z_OK);` (lines 57-67), then `inflateEnd(&stream);` (line 77) right before `return`.

### P2.4 Return Value Handling
**Claim:** Return codes from `uncompress`: `Z_OK` (success, `*destLen` updated to actual decompressed size), `Z_MEM_ERROR` (alloc failure from `inflateInit`/`inflate`), `Z_BUF_ERROR` (destination buffer too small, i.e. inflate ran out of output space with more input/need-more-output remaining), `Z_DATA_ERROR` (corrupted/incomplete input, or a stream that needed a preset dictionary — `Z_NEED_DICT` is translated to `Z_DATA_ERROR` since `uncompress` has no dictionary-setting path), `Z_STREAM_ERROR` (bad parameters, e.g. NULL pointers with nonzero length). Callers in the project check `err != Z_OK` and abort/report.
**Evidence:**
> zlib/uncompr.c:78-81: `return err == Z_STREAM_END ? Z_OK : err == Z_NEED_DICT ? Z_DATA_ERROR : err == Z_BUF_ERROR && len == 0 ? Z_DATA_ERROR : err;`
> zlib.h:1319-1327 doc comment: "uncompress returns Z_OK if success, Z_M[EM_ERROR...]" (truncated in header but matches uncompr.c behavior).
> test/example.c:28-33, 76-77: `#define CHECK_ERR(err,msg) { if (err != Z_OK) { fprintf(stderr,...); exit(1);} }` then `err = uncompress(uncompr,&uncomprLen,compr,comprLen); CHECK_ERR(err,"uncompress");`
> ossfuzz_view/zlib-c5/zlib_uncompress_fuzzer.cc:14-15: fuzz harness intentionally ignores the return code — "// Ignore return code." — since fuzzing feeds arbitrary/corrupted data and all error codes are acceptable outcomes.

### P2.5 Cleanup Sequence
**Claim:** No cleanup is required by the caller of `uncompress` itself — all internal `z_stream` resources (`inflateEnd`) are freed before the function returns, on both the success and error paths (the do/while loop breaks on any `err != Z_OK`, including fatal errors, and `inflateEnd` is called unconditionally right after). The caller is only responsible for freeing the `dest`/`source` buffers it allocated itself.
**Evidence:**
> zlib/uncompr.c:66-77: the loop ends on first non-`Z_OK` result (including `Z_STREAM_END`, `Z_DATA_ERROR`-producing `Z_NEED_DICT`, `Z_BUF_ERROR`, `Z_MEM_ERROR`), and `inflateEnd(&stream);` (line 77) is reached unconditionally afterward, before `return` on line 78.
> ossfuzz_view/zlib-c5/compress_fuzzer.c:93-95 (project-level harness cleanup of caller-owned buffers): `err: free(compr); free(uncompr);`

### P2.6 API Existence
**Claim:** All functions mentioned exist in this zlib source tree.
**Evidence:**
> zlib.h:1271 `compress`, 1288 `compress2`, 1307 `compressBound`, 1315 `uncompress`, 1317 `uncompress_z` declared.
> zlib/uncompr.c:29 `uncompress2_z`, 83 `uncompress2`, 92 `uncompress_z`, 97 `uncompress` all defined.
> zlib/compress.c:24 `compress2_z`, 67 `compress2`, 77 `compress_z`, 82 `compress`, 96 `compressBound` (via `compressBound_z` at line 91) all defined.
> zlib.h:405 `inflate`, 525 `inflateEnd`, 986 `inflateReset`, 1905 `inflateInit_`, 1911 `inflateInit2_`, 1934 `#define inflateInit(strm) inflateInit_(...)` macro.

### P2.7 Co-call Constraints
**Claim:** Inside `uncompress`'s implementation, `inflateInit`/`inflateEnd` is a strict create/destroy pair always both called in `uncompress2_z` — never one without the other. At the harness level, `compress`/`compress2` (producer) and `uncompress`/`uncompress2` (consumer) are the natural paired entries for round-trip testing, and `compressBound` must be called before `compress2` to correctly size the destination buffer.
**Evidence:**
> zlib/uncompr.c:51-52,77: `inflateInit(&stream)` ... `inflateEnd(&stream)` both present, unconditionally paired within the same function body.
> zlib.h:1309-1312 (`compressBound` doc): "It would be used before a compress() or compress2() call to allocate the destination buffer."
> ossfuzz_view/zlib-c5/compress_fuzzer.c:77,83,90: `comprLen = compressBound(size); ... compr = calloc(1, comprLen); ... check_compress_level(...)` which calls `compress2` then `uncompress` (lines 28-29).

### P2.8 Prerequisite State
**Claim:** No external context/config/connection object must exist before calling `uncompress`; it is a pure, stateless, single-call utility function operating purely on caller-supplied memory buffers. The only "state" that must exist is valid, allocated `dest` and `source` memory and a correctly initialized `*destLen`.
**Evidence:**
> zlib/uncompr.c:29-101: no global/static state referenced besides the local `z_stream`; no prior zlib API call is required (verified via `get_callers(uncompress)` returning 0 callers in this tree, i.e. it's a leaf public entry, and its only internal dependency `inflateInit`/`inflate`/`inflateEnd` is self-contained).

### P2.9 Result Usage
**Claim:** Callers use the filled `dest` buffer (now holding `*destLen` bytes of decompressed data) directly for comparison/printing/further processing; no other zlib API is required to interpret or free this plain memory buffer (it is not a handle/object). Typical consumption: string compare against expected plaintext, or `memcmp` in round-trip tests; free the buffer with normal `free()`/stack deallocation if heap-allocated by the caller.
**Evidence:**
> test/example.c:76,79-83: `err = uncompress(uncompr,&uncomprLen,compr,comprLen); ... if (strcmp((char*)uncompr, hello)) {...} else { printf("uncompress(): %s\n",(char*)uncompr); }` — result buffer read as a C string.
> ossfuzz_view/zlib-c5/compress_fuzzer.c:28-33: `compress2(...); uncompress(uncompr,&uncomprLen,compr,comprLen); assert(dataLen==uncomprLen); assert(0==memcmp(data,uncompr,dataLen));` — result buffer and `*destLen` (renamed `uncomprLen`) checked against the original plaintext.
> ossfuzz_view/zlib-c5/compress_fuzzer.c:93-95: result buffer (`uncompr`) freed via `free(uncompr);` after use.
> ossfuzz_view/zlib_uncompress_fuzzer.cc, zlib_uncompress2_fuzzer.cc, zlib_uncompress3_fuzzer.cc: result buffer is a fixed static array `buffer[256*1024]`, not further processed beyond ignoring the return code — demonstrating that a harness may legitimately just call `uncompress`/`uncompress2` and discard results, since the goal is exercising the inflate state machine, not validating output content.

### P2.10 Behaviour Controls
**Claim:** `uncompress` itself has no explicit mode flags — the only inputs that steer behavior are the *contents* of `source` (zlib header CMF/FLG bytes selecting compression method/window size, FDICT bit selecting the `Z_NEED_DICT` path — which `uncompress` cannot satisfy and always turns into `Z_DATA_ERROR`, and the deflate block type bits inside the stream selecting stored/fixed/dynamic Huffman decoding inside `inflate`), `sourceLen` (how much of `source` is consumed/available), and `*destLen` on entry (capacity, controlling whether `Z_BUF_ERROR` is hit). For `compress`/`compress2`, the `level` parameter (`Z_NO_COMPRESSION`..`Z_BEST_COMPRESSION`, or `Z_DEFAULT_COMPRESSION`) selects the deflate strategy/effort, feeding `deflateInit`.
**Evidence:**
> zlib/uncompr.c:36-43: branches on `*sourceLen`, `*destLen`, NULL-ness of `source`/`dest` (`Z_STREAM_ERROR` branch vs. normal path).
> zlib/uncompr.c:78-80: `err == Z_NEED_DICT ? Z_DATA_ERROR` shows the FDICT-header path is deliberately turned into a data error since `uncompress` never calls `inflateSetDictionary`.
> zlib/inflate.c:703: `return Z_NEED_DICT;` inside `inflate()`'s header-parsing state, reached when the zlib header's FDICT bit is set — this is the "missing Z_NEED_DICT handling" branch mentioned in the task description.
> zlib/compress.c:67-68,82-84: `compress2(dest,destLen,source,sourceLen,level)` vs `compress(...)` → `compress2(..., Z_DEFAULT_COMPRESSION)`; `level` forwarded to `deflateInit(&stream, level)` (compress.c:42).
> ossfuzz_view/zlib-c5/compress_fuzzer.c:66,70,74,90-91: harness derives `level = d[0] % 10`, `compression_method`, `flags` from fuzz bytes and exercises both the compress→uncompress round trip (`check_compress_level`) and direct malformed-header decompression (`check_decompress`) with a hand-crafted zlib header via `write_zlib_header`.

### P2.11 Input Expectations
**Claim:** `source` must be (or be treated as) a complete, in-memory zlib-formatted stream: 2-byte zlib header (CMF/FLG, with a checksum constraint `header % 31 == 0`), followed by one or more deflate blocks (stored/fixed/dynamic), followed by a 4-byte Adler-32 trailer checked by `inflate`. `sourceLen` is the exact byte length of `source`; data beyond `*sourceLen`/the stream end is not read. No alignment or termination byte beyond the zlib format itself is required. Truncated or garbage input yields `Z_DATA_ERROR` (or `Z_BUF_ERROR` translated to `Z_DATA_ERROR` when no bytes were consumed). `dest`'s size (`*destLen` on entry) is the single external limit; if it is too small, decompression stops early with `Z_BUF_ERROR` without buffer overflow (bounded by `stream.avail_out`).
**Evidence:**
> zlib.h:1320-1326: "*destLen is the total size of the destination buffer, which must be large enough to hold the entire uncompressed data...size of the uncompressed data must have been saved previously by the compressor".
> ossfuzz_view/zlib-c5/compress_fuzzer.c:38-47 (`write_zlib_header`): constructs a conformant 2-byte header: `header = (Z_DEFLATED + (flags<<4))<<8; header |= (compression_method<<6); header += 31 - (header % 31);` demonstrating the header-checksum constraint `inflate` enforces.
> zlib/uncompr.c:57-67: the main `do{...}while(err==Z_OK)` loop feeds input/output in `max=(uInt)-1`-sized chunks derived strictly from `*sourceLen`/`*destLen`, so no out-of-bounds read/write beyond the supplied lengths.

## Other Logic-Group Entries: call protocol and combination with the target

### uncompress2
**Protocol:** `int uncompress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong *sourceLen)`. Unlike `uncompress`, `sourceLen` is passed **by pointer**: on entry `*sourceLen` is the available input length; on exit it is updated to the number of input bytes actually consumed (so `source + *sourceLen` points to the first unused byte — supports concatenated/multi-stream or trailing-garbage input). Same init/use/cleanup internally (`inflateInit`→`inflate` loop→`inflateEnd`) via shared `uncompress2_z` core. Same return codes as `uncompress`. No separate alloc/free needed beyond `dest`/`source` buffers.
**Combination with target:** `uncompress` is implemented literally as a thin wrapper that stashes `sourceLen` into a local `uLong used` and calls `uncompress2` (zlib/uncompr.c:97-100). A harness can call either one interchangeably as the main decompression entry; `uncompress2` additionally lets the harness inspect how much input was consumed (useful to test truncated/garbage-trailing streams), whereas `uncompress` only reports how much output was produced.
**Evidence:**
> zlib/uncompr.c:83-91: definition of `uncompress2`.
> zlib/uncompr.c:97-100: `uncompress` calls `uncompress2(dest, &used_as_sourceLen_wrapper...)`.
> ossfuzz_view/zlib-c5/zlib_uncompress2_fuzzer.cc:9-15 and zlib_uncompress3_fuzzer.cc:26-40: both existing OSS-Fuzz harnesses for this project call `uncompress2` directly (not `uncompress`) with a fixed/derived output buffer and ignore the return code, confirming `uncompress2` is an accepted top-level fuzz entry equivalent to the target.

### uncompress_z / uncompress2_z
**Protocol:** size_t-width variants: `uncompress_z(Bytef*, z_size_t *destLen, const Bytef*, z_size_t sourceLen)` and `uncompress2_z(Bytef*, z_size_t *destLen, const Bytef*, z_size_t *sourceLen)`. `uncompress2_z` is the actual shared core implementing all four `uncompress*` entry points (does the NULL/size checks, `inflateInit`, inflate-loop chunked in `max=(uInt)-1`-sized pieces to cope with `z_size_t` potentially being wider than `uInt`, `inflateEnd`). Selected only by buffer-size-type preference (size_t vs uLong); behavior otherwise identical to `uncompress`/`uncompress2`.
**Combination with target:** These are alternate-width wrappers around the exact same code path as `uncompress`; a harness normally only needs one width family (`uncompress`/`uncompress2`) since `uncompress2_z` is reached by the target through `uncompress`→`uncompress2`→`uncompress2_z` already (zlib/uncompr.c:97→83→(calls)29).
**Evidence:**
> zlib/uncompr.c:29-82: `uncompress2_z` full core definition, called by `uncompress2` (line 87) which is called by `uncompress` (line 100) — i.e. the target already exercises this code.
> zlib/uncompr.c:92-96: `uncompress_z` forwards to `uncompress2_z`.

### compress / compress2
**Protocol:** `compress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen)` ≡ `compress2(dest, destLen, source, sourceLen, Z_DEFAULT_COMPRESSION)`. `compress2` additionally takes `level` (`Z_NO_COMPRESSION`(0) .. `Z_BEST_COMPRESSION`(9), or `Z_DEFAULT_COMPRESSION`(-1)); invalid level → `Z_STREAM_ERROR` from `deflateInit`. Pre-condition: `*destLen` on entry must be ≥ `compressBound(sourceLen)` or the call may return `Z_BUF_ERROR`. Internally creates a local `z_stream`, calls `deflateInit(&stream, level)`, loops `deflate(&stream, sourceLen_remaining ? Z_NO_FLUSH : Z_FINISH)` until `err != Z_OK`, then `deflateEnd(&stream)`. Returns `Z_OK`/`Z_MEM_ERROR`/`Z_BUF_ERROR`/`Z_STREAM_ERROR`. On exit `*destLen` holds the actual compressed size, to be used as `comprLen`/`sourceLen` fed into `uncompress`.
**Combination with target:** In a round-trip harness, `compress`/`compress2` is called *before* `uncompress`, producing a well-formed zlib stream (`compr`, `comprLen`) from fuzz-derived plaintext; that exact buffer and length are then fed as `source`/`sourceLen` to `uncompress`, and the harness asserts the output matches the original plaintext. Alternatively, as in `check_decompress` below, the harness mutates/overwrites only the 2-byte zlib header of a `compress2`-produced (or raw) buffer before calling `uncompress`, to probe header-validation paths with otherwise well-formed compressed bodies.
**Evidence:**
> zlib/compress.c:67-74 (`compress2`), 82-85 (`compress` forwarding to `compress2` with `Z_DEFAULT_COMPRESSION`).
> zlib/compress.c:42-65: `deflateInit`/loop over `deflate`/`deflateEnd` lifecycle, paralleling `uncompress`'s `inflateInit`/`inflate`/`inflateEnd`.
> ossfuzz_view/zlib-c5/compress_fuzzer.c:25-34 (`check_compress_level`): `compress2(compr,&comprLen,data,dataLen,level); uncompress(uncompr,&uncomprLen,compr,comprLen); assert(dataLen==uncomprLen); assert(0==memcmp(data,uncompr,dataLen));` — canonical compress→uncompress round trip combining the two entries with the target.
> ossfuzz_view/zlib-c5/compress_fuzzer.c:49-60 (`check_decompress`): reuses a `compress2`-sized `compr` buffer as pure scratch, writes a hand-built zlib header via `write_zlib_header`, then calls `uncompress(compr, &comprLen, copy, copyLen)` directly on attacker/fuzzer bytes with only the header forced valid — this is the "feed corrupted/garbage body with a valid header" input-space variant named in the task description.
> test/example.c:66-85 (`test_compress`): canonical doc example pairing `compress` then `uncompress`, checking `CHECK_ERR` after each and comparing the decompressed string to the original.

### compressBound
**Protocol:** `uLong compressBound(uLong sourceLen)` — pure function, no state, no init/cleanup. Returns an upper bound on compressed size for a given `sourceLen`, to be used to size the `dest` buffer passed to `compress`/`compress2` *before* that call. It does not interact with `uncompress` directly; it is only a sizing helper for the compression (producer) side of a round-trip harness.
**Evidence:**
> zlib.h:1307,1309-1312: declaration and doc ("used before a compress() or compress2() call to allocate the destination buffer").
> zlib/compress.c:91-98: `compressBound_z`/`compressBound` implementation: `bound = sourceLen + (sourceLen>>12) + (sourceLen>>14) + (sourceLen>>25) + 13;` with overflow saturation to `(uLong)-1`.
> ossfuzz_view/zlib-c5/compress_fuzzer.c:77,83: `comprLen = compressBound(size); ... compr = (uint8_t*)calloc(1, comprLen);` — called before `compress2`/`compress`, to size the buffer handed later to `uncompress` as `source`/`sourceLen`.

### How the entries combine in one harness (as evidenced by existing OSS-Fuzz drivers for this exact project)
**Claim:** Two valid harness patterns already exist in this project and both are legitimate P2-compliant uses of the target:
1. **Direct-decompress pattern** (`zlib_uncompress_fuzzer.cc`, `zlib_uncompress2_fuzzer.cc`, `zlib_uncompress3_fuzzer.cc`): feed raw fuzz bytes straight into `uncompress`/`uncompress2` as `source`/`sourceLen` against a fixed-size static `dest` buffer, ignore the return value (any zlib error code is an acceptable outcome; the goal is driving `inflate`'s state machine, not validating output).
2. **Round-trip + malformed-header pattern** (`compress_fuzzer.c`): derive `level`/`compression_method`/`flags` from the first few input bytes, call `compressBound` to size a `compr` buffer, `compress2` the remaining fuzz bytes into it at the chosen level, then call `uncompress` on that output and assert equality with the original (`check_compress_level`); separately, copy the remaining fuzz bytes into a buffer with a hand-forged but checksum-valid zlib header and call `uncompress` on that directly (`check_decompress`), to probe header-adjacent/body-corruption paths without needing a full round trip. Both `compr`/`uncompr` buffers are `calloc`'d before use and `free`'d at the end (success or early `goto err` on allocation failure).
**Evidence:**
> ossfuzz_view/zlib-c5/zlib_uncompress_fuzzer.cc:1-16, zlib_uncompress2_fuzzer.cc:1-16, zlib_uncompress3_fuzzer.cc:17-41.
> ossfuzz_view/zlib-c5/compress_fuzzer.c:62-99 (`LLVMFuzzerTestOneInput`): full driver combining `compressBound`, `compress2`, `uncompress` (twice, via `check_compress_level` and `check_decompress`), with `calloc`/`free` for the two heap buffers.

## Build notes

**Claim:** Fuzzer source files for this project are compiled both as C++ (`.cc`) and C (`.c`), always with `-I.` pointing at the zlib source root (where `zlib.h` lives after `./configure`/`make` generates it in-place), and linked statically against `./libz.a` built from source by `make`.
**Evidence:**
> ossfuzz_view/zlib-c5/build.sh:18-24: `./configure`, then `make -j$(nproc) clean`, `make -j$(nproc) all` (and `make check` unless MSan) — builds `libz.a` in the zlib source tree, and `zlib.h` is present at the repo root (`zlib/zlib.h` here) consistent with `#include "zlib.h"` in the fuzzers.
> ossfuzz_view/zlib-c5/build.sh:31-34: `.cc` fuzzers: `for f in $(find $SRC -name '*_fuzzer.cc'); do ... $CXX $CXXFLAGS -std=c++11 -I. $f -o $OUT/$b $LIB_FUZZING_ENGINE ./libz.a; done` — C++11, include path `-I.` (zlib source root), linked against `libz.a`.
> ossfuzz_view/zlib-c5/build.sh:38-43: `.c` fuzzers: `$CC $CFLAGS -I. $f -c -o /tmp/$b.o; $CXX $CXXFLAGS -o $OUT/$b /tmp/$b.o -stdlib=libc++ $LIB_FUZZING_ENGINE ./libz.a` — compiled as C, then linked via `$CXX`/libc++ with the fuzzing engine and `libz.a`.
> ossfuzz_view/zlib-c5/Dockerfile:17-21: base image `gcr.io/oss-fuzz-base/base-builder:ubuntu-24-04`; clones `zlib` (develop branch) as the source tree; copies `build.sh`/`run_tests.sh`/`*_fuzzer.c*` into `$SRC`.
> ossfuzz_view/zlib-c5/zlib_uncompress_fuzzer.cc:5 and compress_fuzzer.c:20: both include `"zlib.h"` directly (no subdirectory prefix), consistent with `-I.` resolving to the zlib root where `zlib.h` lives (confirmed present at `zlib/zlib.h` in this checkout, declaring `uncompress` at line 1315).