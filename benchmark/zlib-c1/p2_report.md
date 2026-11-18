## target: compress2

### P2.1 Init Sequence
**Claim:** No external/global zlib init is required before calling `compress2`. `compress2` is a self-contained one-shot wrapper: it internally declares a local `z_stream`, zeroes the allocator fields, and calls `deflateInit(&stream, level)` itself. The caller does *not* call `deflateInit`/`deflateInit2` beforehand — doing so is unnecessary and would leak the caller's stream.
**Evidence:**
> zlib/compress.c:67-74 (`compress2`) delegates straight to `compress2_z`.
> zlib/compress.c:24-43 (`compress2_z`): `stream.zalloc=0; stream.zfree=0; stream.opaque=0; err = deflateInit(&stream, level); if (err != Z_OK) return err;` — init happens inside, not by the caller.
> zlib.h:1294-1296: "The level parameter has the same meaning as in deflateInit." (doc confirms deflateInit semantics are reused, not that caller calls it.)

The only thing a correct caller must do *before* calling `compress2` is size the destination buffer, normally via `compressBound(sourceLen)` (see P2.8/P2.9).

### P2.2 Parameter Construction
**Claim:** 
- `dest` (Bytef*): caller-allocated buffer of at least `*destLen` bytes; must be non-NULL if `*destLen > 0`.
- `destLen` (uLongf*): in/out. On entry: total capacity of `dest`. On exit: actual compressed size. Must not be NULL.
- `source` (const Bytef*): caller buffer of `sourceLen` bytes; may be NULL only if `sourceLen==0`.
- `sourceLen` (uLong): byte length of `source`; no stated upper bound other than internal UINT_MAX chunking (handled automatically).
- `level` (int): must be `Z_DEFAULT_COMPRESSION` (-1) or in `[0,9]` inclusive; anything else is rejected by `deflateInit`→`deflateInit2_` with `Z_STREAM_ERROR`.
**Evidence:**
> zlib/compress.c:31-33 (`compress2_z`): `if ((sourceLen > 0 && source == NULL) || destLen == NULL || (*destLen > 0 && dest == NULL)) return Z_STREAM_ERROR;`
> zlib/compress.c:67-74 (`compress2`): wraps `*destLen` into a local `z_size_t got` then calls `compress2_z`.
> zlib/deflate.c:435: `windowBits < 8 || windowBits > 15 || level < 0 || level > 9 ||` → returns `Z_STREAM_ERROR` for bad level (inside `deflateInit2_`, reached via `deflateInit`).
> zlib/deflate.c:419: `if (level == Z_DEFAULT_COMPRESSION) level = 6;` — -1 is special-cased and valid.
> zlib.h:1288-1305 (declaration + doc comment): "destLen ... must be at least the value returned by compressBound(sourceLen)."

### P2.3 Object Lifecycle
**Claim:** `compress2` manages its own `z_stream` lifecycle entirely internally: create (stack-allocate + zero alloc hooks) → configure (`deflateInit`) → use (loop of `deflate(Z_NO_FLUSH/Z_FINISH)`) → destroy (`deflateEnd`), all inside `compress2_z`. There is no handle returned to the caller to manage — the caller's lifecycle is just: allocate `dest`/`source` buffers, call `compress2`, then free the buffers itself.
**Evidence:**
> zlib/compress.c:26,38-40,42-43,50-61,64 — the full create/init/use/destroy sequence inside `compress2_z`:
> ```
> z_stream stream;                          // create
> stream.zalloc=0; stream.zfree=0; stream.opaque=0;
> err = deflateInit(&stream, level);        // configure
> ...
> do { ... err = deflate(&stream, ...); } while (err == Z_OK);  // use
> deflateEnd(&stream);                      // destroy
> ```

### P2.4 Return Value Handling
**Claim:** `compress2` returns one of `Z_OK` (0, success), `Z_MEM_ERROR` (-4, allocation failure inside deflateInit/deflate), `Z_BUF_ERROR` (-5, destLen too small — the deflate loop exits before `Z_STREAM_END`), or `Z_STREAM_ERROR` (-2, bad level or NULL/len-mismatched pointers). Caller must check for `Z_OK`; on any other value `*destLen` may be 0 or a partial size and the data in `dest` should not be trusted as complete. The internal loop maps `Z_STREAM_END` (success condition of `deflate`) back to `Z_OK`.
**Evidence:**
> zlib/compress.c:65: `return err == Z_STREAM_END ? Z_OK : err;`
> zlib.h:1302-1304 (doc): "compress2 returns Z_OK if success, Z_MEM_ERROR if there was not enough memory, Z_BUF_ERROR if there was not enough room in the output buffer, Z_STREAM_ERROR if the level parameter is invalid."
> test/example.c:28-33: `CHECK_ERR` macro used by real callers: `if (err != Z_OK) { fprintf(stderr, ...); exit(1); }` applied right after `compress()` (compress2's sibling) at test/example.c:71-72.

### P2.5 Cleanup Sequence
**Claim:** No library-side handle needs freeing by the caller — `compress2` already calls `deflateEnd(&stream)` on every exit path (both success and error, since the `do/while` only exits the loop on `err != Z_OK`, and `deflateEnd` is called unconditionally afterward). The *caller's* only cleanup responsibility is freeing/releasing the `dest` and `source` buffers it allocated itself, in any order, whenever done.
**Evidence:**
> zlib/compress.c:60-65: the `do { ... err = deflate(...) ...} while (err == Z_OK); *destLen = ...; deflateEnd(&stream); return ...;` — `deflateEnd` runs regardless of whether the loop ended due to `Z_STREAM_END` or an error code.
> test/example.c: buffers `compr`/`uncompr` are allocated with `calloc` (test/example.c:515-516, per earlier grep) and freed by the caller/test harness at teardown — consistent with "caller owns buffers, library owns internal stream."

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current zlib-c1 source tree.
**Evidence:**
> zlib.h:1288-1293,1307-1308,1335-1338,1271-1272 declare `compress2`, `compress2_z`, `compressBound`, `compressBound_z`, `uncompress2`, `uncompress2_z`, `compress`, `compress_z`, `uncompress`, `uncompress_z`.
> zlib/compress.c:24,67,77,82,91,96 define `compress2_z`, `compress2`, `compress_z`, `compress`, `compressBound_z`, `compressBound`.
> zlib/uncompr.c:29,83,(93),97 define `uncompress2_z`, `uncompress2`, `uncompress_z`, `uncompress`.
> `deflateInit`/`deflateInit_`/`deflate`/`deflateEnd` declared zlib.h:1903,1931-1934 and implemented in zlib/deflate.c (confirmed via deflate.c:419,435,784,786 level-check lines).
> `inflateInit`/`inflate`/`inflateEnd` used analogously inside uncompr.c:51,66,77.

### P2.7 Co-call Constraints
**Claim:** Internally, `compress2`'s pairing is `deflateInit` ↔ `deflateEnd` (always both called, matched 1:1, regardless of success/failure) — this is self-contained and invisible to the harness. At the harness/API level, the natural pairing to exercise the *feature* is `compressBound` (pre-call sizing) paired with `compress2`/`compress` (production), and `compress2`'s output paired with `uncompress2`/`uncompress` (consumption) to round-trip. There is no explicit "close"/"destroy" call exposed to callers of `compress2` itself.
**Evidence:**
> zlib/compress.c:42-43 (`deflateInit`) and line 64 (`deflateEnd`) — always paired in `compress2_z`.
> zlib.h:1310-1312: "compressBound() ... would be used before a compress() or compress2() call to allocate the destination buffer." — documents the compressBound→compress2 pairing.
> zlib/uncompr.c:51 (`inflateInit`) / 77 (`inflateEnd`) analogous pairing inside `uncompress2_z`, the consumer of compress2's output.

### P2.8 Prerequisite State
**Claim:** No persistent context, connection, or global config is required. The only prerequisite state is: (1) a `source` buffer with `sourceLen` valid bytes (any byte content — it is explicitly meant for arbitrary/untrusted data), and (2) a `dest` buffer sized to at least `compressBound(sourceLen)` bytes with `*destLen` set to that capacity before the call. `level` must already be chosen (fixed int, no other prerequisite).
**Evidence:**
> zlib.h:1297-1299 (doc for compress2): "destLen is the total size of the destination buffer, which must be at least the value returned by compressBound(sourceLen)."
> zlib/compress.c:91-99 (`compressBound_z`/`compressBound`) is the function that computes this prerequisite size: `bound = sourceLen + (sourceLen>>12) + (sourceLen>>14) + (sourceLen>>25) + 13;`.
> zlib/compress.c:31-33: NULL-pointer / zero-length checks enforce that `source` can be NULL only when `sourceLen==0`, and `dest` can be NULL only when `*destLen==0`.

### P2.9 Result Usage
**Claim:** The result of `compress2` is the pair `(dest buffer filled with compressed bytes, *destLen = actual compressed size)`. Real callers pass this exact pair into the decompression entry points `uncompress`/`uncompress2` (and their `_z` variants) as the `source`/`sourceLen` arguments, to validate round-tripping. No other project API consumes `compress2`'s output directly in this source tree (no gz* function takes a pre-compressed buffer from compress2).
**Evidence:**
> test/example.c:66-84 (`test_compress`): 
> ```
> err = compress(compr, &comprLen, (const Bytef*)hello, len);      // produce
> CHECK_ERR(err, "compress");
> ...
> err = uncompress(uncompr, &uncomprLen, compr, comprLen);         // consume: compr/comprLen from compress() feed directly into uncompress()
> CHECK_ERR(err, "uncompress");
> if (strcmp((char*)uncompr, hello)) { ... }                       // further validates the round-tripped content
> ```
> (`compress` is `compress2` with `level=Z_DEFAULT_COMPRESSION` fixed — zlib/compress.c:82-85 — so this is the canonical "compress2-output consumed by uncompress" pattern.)
> zlib/uncompr.c:83-91 (`uncompress2`) and zlib/uncompr.c:97-100 (`uncompress`) are the functions that take a compressed buffer+length (as produced by compress2) as `source`/`sourceLen`.
> ossfuzz_view/zlib-c1/zlib_uncompress2_fuzzer.cc:10-15 shows `uncompress2` being fed arbitrary fuzz `data`/`size` directly as the compressed-source side — i.e. the harness pattern already used in this project for the consumer half of the round trip, confirming `uncompress2(dest,&destLen,source,&sourceLen)` is the correct signature/usage to pair with compress2's output in a harness.

### P2.10 Behaviour Controls
**Claim:** 
- `level` (compress2's own parameter): `Z_DEFAULT_COMPRESSION`(-1) or `0..9`. Selects `configuration_table` row in deflate.c, hence `deflate_stored`/`deflate_fast`/`deflate_slow` internal strategy — not settable to RLE/HUFFMAN/FILTERED/FIXED strategies through `compress2` (those require `deflateInit2`+`deflateParams`, which `compress2` never calls).
- `sourceLen`/content: drives which internal deflate_* function runs for a given level (redundant vs random bytes), but this is not an explicit "flag" of compress2 — content-dependent only.
- `destLen` (in): if smaller than needed, the deflate loop breaks on `Z_BUF_ERROR` once `avail_out` can't be refilled and `deflate` can't make progress — this is the main caller-controlled failure-path trigger.
- Internal chunking constant `max = (uInt)-1` (compress.c:28) splits `sourceLen`/`left` into ≤ UINT_MAX chunks per `deflate` call — relevant when `sourceLen`/`destLen` exceed 32-bit range (only reachable via `compress2_z`/64-bit `z_size_t`, since `compress2`'s `uLong`/`uLongf` are effectively capped by `uLong` width on most platforms).
**Evidence:**
> zlib/compress.c:28,50-61 (`compress2_z`): `const uInt max = (uInt)-1;` then `stream.avail_out = left > (z_size_t)max ? max : (uInt)left;` / same for `avail_in` — this is the chunking logic selecting how many deflate() calls occur for large buffers.
> zlib/compress.c:60: `err = deflate(&stream, sourceLen ? Z_NO_FLUSH : Z_FINISH);` — flush mode is auto-selected by compress2 based on remaining input, not caller-controllable.
> zlib/deflate.c:419,435 — level clamped/validated, drives `configuration_table` indexing (not shown here but referenced by `deflateInit2_`'s level handling).
> zlib.h:1295-1296: "The level parameter has the same meaning as in deflateInit." confirms level is the only compress2-facing knob; strategy (Z_FILTERED/Z_HUFFMAN_ONLY/Z_RLE/Z_FIXED) is NOT reachable through compress2 since it never calls `deflateInit2`/`deflateParams` (only `deflateInit`, compress.c:42).

### P2.11 Input Expectations
**Claim:** `source` is treated as an opaque raw byte buffer of exactly `sourceLen` bytes — no required header, magic number, or NUL-termination; `compress2`/`deflate` make no assumptions about content (arbitrary/adversarial bytes are explicitly the intended fuzzing target per the feature description). The only size expectation is consistency between the pointer and length (`source==NULL` only valid when `sourceLen==0`), and that `dest`'s declared capacity (`*destLen`) actually matches the real buffer size the caller allocated (compress2 trusts `*destLen` and will write up to that many bytes into `dest`).
**Evidence:**
> zlib/compress.c:31-33: `if ((sourceLen > 0 && source == NULL) || destLen == NULL || (*destLen > 0 && dest == NULL)) return Z_STREAM_ERROR;` — these are the only input-shape checks; no content/format checks.
> zlib.h:1296-1297 (doc): "sourceLen is the byte length of the source buffer" — no format requirement stated.
> zlib/compress.c:45,47 (`stream.next_out = dest; ... stream.next_in = (z_const Bytef*)source;`) — raw byte pointers passed straight into deflate, confirming no header parsing.

## Other entries of the Logic Group

### compress2_z (size_t variant — same code path as compress2)
**Claim:** `compress2_z(Bytef *dest, z_size_t *destLen, const Bytef *source, z_size_t sourceLen, int level)` is the actual implementation; `compress2` is a thin wrapper around it (converts `uLongf*`↔`z_size_t`). Needs nothing extra before it beyond what P2.1/P2.8 state for `compress2`. Return codes and cleanup identical to `compress2` (P2.4/P2.5). In a harness, calling `compress2_z` directly lets 64-bit lengths/edge sizes near `UINT_MAX`/`SIZE_MAX` be exercised that `uLong`/`uLongf` (32-bit on some platforms) can't represent.
**Evidence:**
> zlib/compress.c:24-66 (full body, already quoted above); zlib/compress.c:67-74 shows `compress2` is literally `got = *destLen; compress2_z(dest,&got,source,sourceLen,level); *destLen=(uLong)got;`.
> zlib.h:1291-1293,1305: declared and documented alongside compress2 ("The _z versions of the functions take size_t length arguments.").

### compress (fixed-level wrapper)
**Claim:** `compress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen)` == `compress2(dest, destLen, source, sourceLen, Z_DEFAULT_COMPRESSION)`. Same pre/post conditions as compress2 minus the `level` parameter (fixed at -1→6 internally). Used identically to compress2 in a harness except level is not a variable — use it when you want to isolate sourceLen/destLen-edge testing without also varying level.
**Evidence:**
> zlib/compress.c:82-85: `int ZEXPORT compress(...) { return compress2(dest, destLen, source, sourceLen, Z_DEFAULT_COMPRESSION); }`
> test/example.c:71 real call site: `err = compress(compr, &comprLen, (const Bytef*)hello, len); CHECK_ERR(err, "compress");`

### compressBound / compressBound_z (sizing helper, call BEFORE compress2)
**Claim:** `uLong compressBound(uLong sourceLen)` takes only `sourceLen` (no z_stream, no init) and returns the minimum safe `destLen` to pass into `compress`/`compress2` for that `sourceLen`. No return-value error case other than saturation to `(uLong)-1`/`(z_size_t)-1` on overflow — treat that saturated max as "cannot safely compress this large a buffer with this API", and the harness should check for it before allocating. It is pure/stateless: no lifecycle, no cleanup.
**Evidence:**
> zlib/compress.c:91-99:
> ```
> z_size_t ZEXPORT compressBound_z(z_size_t sourceLen) {
>     z_size_t bound = sourceLen + (sourceLen >> 12) + (sourceLen >> 14) + (sourceLen >> 25) + 13;
>     return bound < sourceLen ? (z_size_t)-1 : bound;   // overflow saturation
> }
> uLong ZEXPORT compressBound(uLong sourceLen) {
>     z_size_t bound = compressBound_z(sourceLen);
>     return (uLong)bound != bound ? (uLong)-1 : (uLong)bound;  // truncation saturation
> }
> ```
> zlib.h:1307-1313 (doc): "compressBound() returns an upper bound on the compressed size ... It would be used before a compress() or compress2() call to allocate the destination buffer."
> ossfuzz_view/zlib-c1/example_small_fuzzer.c:97: `size_t comprLen = compressBound(size);` — real usage pattern: call compressBound first, then allocate, exactly as the harness for compress2 should do.

### uncompress2 / uncompress2_z (consumer of compress2's output)
**Claim:** `uncompress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong *sourceLen)` must be called AFTER compress2 produced `(dest_c, destLen_c)`, using `source = dest_c`, `*sourceLen = destLen_c` (the compressed buffer and its exact produced length — note it's a **pointer** to sourceLen unlike compress2 since inflate may not consume all input). Needs its own separate `dest`/`*destLen` out-buffer (sized to the expected/trusted uncompressed length — zlib docs state this size must be known out-of-band). Internally self-contained like compress2: calls `inflateInit`→loop of `inflate(Z_NO_FLUSH)`→`inflateEnd`, so no extra init/cleanup needed by the harness. Returns `Z_OK`/`Z_MEM_ERROR`/`Z_BUF_ERROR`/`Z_DATA_ERROR`; on `Z_DATA_ERROR` the compressed data was corrupt/incomplete — expected on arbitrary/mutated input in a round-trip harness only if the compressed buffer itself is tampered with between the two calls (should not happen if fed compress2's exact output untouched).
**Evidence:**
> zlib/uncompr.c:29-82 (`uncompress2_z`), full self-contained init/use/destroy sequence (`inflateInit` line 51, loop lines 57-67, `inflateEnd` line 77), mirroring compress2_z's structure.
> zlib/uncompr.c:83-91 (`uncompress2`): wraps `*destLen`/`*sourceLen` into `z_size_t got/used` and calls `uncompress2_z`.
> zlib.h:1335-1343 (declaration + doc): "Same as uncompress, except that sourceLen is a pointer ... On return, *sourceLen is the number of source bytes consumed."
> test/example.c:76-84 real round-trip call site immediately following a `compress()` call (sibling of compress2): `err = uncompress(uncompr, &uncomprLen, compr, comprLen); CHECK_ERR(...); if (strcmp(...)) {...}` — `uncompress` is uncompress2's fixed-length-arg sibling (zlib/uncompr.c:97-100: `return uncompress2(dest, destLen, source, &len);`), demonstrating the canonical compress2(or compress)-then-uncompress2(or uncompress) pairing a harness should replicate, substituting `uncompress2` to also inspect consumed-length behavior.
> ossfuzz_view/zlib-c1/zlib_uncompress2_fuzzer.cc:7-15: existing OSS-Fuzz harness calling `uncompress2(buffer, &buffer_length, data, &buf_size)` directly on fuzz bytes — shows the exact call shape (`Bytef*, uLongf*, const Bytef*, uLong*`) to use when uncompress2 consumes compress2's output in the new harness.

## Build notes
**Claim:** Fuzzers for this project are plain C (`.c`, compiled with `$CC`) or C++ (`.cc`, compiled with `$CXX -std=c++11`), include zlib's header via `-I.` (the zlib source root, where `zlib.h` lives after `./configure`/`make`), and link statically against `./libz.a` (built by `make` from the zlib sources, which is where `compress2`'s object file, `compress.o`, is produced and linked in). No other external libraries are required.
**Evidence:**
> ossfuzz_view/zlib-c1/build.sh:18-24: `./configure; make -j$(nproc) clean; make -j$(nproc) all` — builds `libz.a` from zlib source (including compress.c → compress.o → libz.a).
> ossfuzz_view/zlib-c1/build.sh:31-34 (C++ fuzzers): `$CXX $CXXFLAGS -std=c++11 -I. $f -o $OUT/$b $LIB_FUZZING_ENGINE ./libz.a`
> ossfuzz_view/zlib-c1/build.sh:38-43 (C fuzzers): `$CC $CFLAGS -I. $f -c -o /tmp/$b.o; $CXX $CXXFLAGS -o $OUT/$b /tmp/$b.o -stdlib=libc++ $LIB_FUZZING_ENGINE ./libz.a`
> ossfuzz_view/zlib-c1/zlib_uncompress2_fuzzer.cc:5: `#include "zlib.h"` — single header include, no extra include-path tricks beyond `-I.`.
> zlib/zconf.h:35-39 shows `compress2`/`compress2_z`/`compressBound`/`compressBound_z` are subject to `Z_PREFIX` renaming (`z_compress2` etc.) only if `Z_PREFIX` is defined by the build — not set in build.sh, so the plain names (`compress2`, `compressBound`, `uncompress2`, ...) are the linkable symbols for a harness built by this build.sh.