## target: LZ4_decompress_safe_usingDict

### P2.1 Init Sequence
**Claim:** No library-level init object is required. The only prerequisites are: (a) a syntactically valid LZ4 compressed block in `src` (normally produced by `LZ4_compress_fast`/`LZ4_compress_default`, optionally `*_continue` variants using a dictionary), and (b) the dictionary bytes (if any) already resident at `dictStart` for `dictSize` bytes, independent of any stream-state object. There is no `LZ4_streamDecode_t` needed for this entry (that's the point of the "stateless" `_usingDict` variant, see doc at lz4.h:539-546: "Works the same as a combination of LZ4_setStreamDecode() followed by LZ4_decompress_safe_continue(). However, it's stateless").
**Evidence:**
> lib/lz4.h:539-546: "LZ4_decompress_safe_usingDict() : Works the same as a combination of LZ4_setStreamDecode() followed by LZ4_decompress_safe_continue(). However, it's stateless: it doesn't need any LZ4_streamDecode_t state. Dictionary is presumed stable..."
> tests/fuzzer.c:761-782: real caller first calls `LZ4_compress_fast_continue` (with `LZ4_stream_t` initialized via `LZ4_initStream`) to build a syntactically valid dict-referencing block, then calls `LZ4_decompress_safe_usingDict` directly — no decode-side init object.
> lib/lz4frame.c:1932-1935: lz4frame's block decoder calls `LZ4_decompress_safe_usingDict` directly with `dctx->dict`/`dctx->dictSize` that were populated by prior frame-header parsing / `LZ4F_updateDict`, again with no decode state object.

### P2.2 Parameter Construction
**Claim:** `src`/`dst` are plain buffers (no header required in `dst`, must be pre-allocated by caller, no NULL requirement for `src` except the internal generic check). `srcSize` is the *exact* compressed block size (not a buffer capacity). `dstCapacity` is the destination buffer capacity, an *upper bound* of decompressed size (not required to be exact). `dictStart`/`dictSize`: if `dictSize==0`, `dictStart` is unused (falls to no-dict path); otherwise `dictStart` must point to `dictSize` valid bytes that logically precede the data being decompressed. Internally `LZ4_decompress_generic` rejects `src==NULL` or negative `outputSize` by returning -1 (lib/lz4.c:2035), but does not itself validate `srcSize>0` and `dictSize` beyond an `assert(dictSize>=0)` in the wrapper (debug-only).
**Evidence:**
> lib/lz4.c:2738-2751: `LZ4_decompress_safe_usingDict(source, dest, compressedSize, maxOutputSize, dictStart, dictSize)` — dispatch logic, `assert(dictSize >= 0)` (debug build only, not enforced in release).
> lib/lz4.c:2035: `if ((src == NULL) || (outputSize < 0)) { return -1; }` inside `LZ4_decompress_generic`.
> lib/lz4.c:2068: `if (unlikely(srcSize==0)) { return -1; }`.
> lib/lz4.h:505-508 (LZ4_decompress_safe_continue doc, same contract): "`@dstCapacity` : size of destination buffer ... must be an upper bound of decompressed size."

### P2.3 Object Lifecycle
**Claim:** The target is pure/stateless: no create→configure→use→destroy sequence for the function itself. The lifecycle instead concerns buffer ownership: `dst` buffer must be allocated by caller before the call and remains owned/freed by caller after; `dictStart` buffer must be allocated/populated before the call and must stay "accessible and unmodified during decompression" (lz4.h:543) but is never freed by the target. No LZ4-owned heap object is created or destroyed inside `LZ4_decompress_safe_usingDict` itself.
**Evidence:**
> lib/lz4.h:542-543: "it's stateless: it doesn't need any LZ4_streamDecode_t state. Dictionary is presumed stable: it must remain accessible and unmodified during decompression."
> ossfuzz/round_trip_fuzzer.c:25-33: caller mallocs `dstPlusLargePrefix`, `largeDict`, `rt` before any decompress call, and frees them (lines 111-113) only after all decompress calls finish.

### P2.4 Return Value Handling
**Claim:** Return value is `int`: positive/zero = number of bytes written to `dst` (== actual decompressed size, "necessarily <= dstCapacity"); negative = error (malformed source, or dst too small). Callers must check `ret < 0` (error) before trusting the decoded bytes, and separately compare `ret` to an expected exact size when round-tripping.
**Evidence:**
> lib/lz4.h:507-509 (same contract as _continue, referenced by target's doc): "@return : number of bytes decompressed into destination buffer (necessarily <= dstCapacity). If destination buffer is not large enough, decoding will stop and output an error code (negative value). If the source stream is detected malformed, the function will stop decoding and return a negative result."
> tests/fuzzer.c:781-782: `ret = LZ4_decompress_safe_usingDict(...); FUZ_CHECKTEST(ret!=blockSize, "...did not regenerate original data");`
> tests/fuzzer.c:841-843: negative-size test: `ret = LZ4_decompress_safe_usingDict(..., blockSize-1, ...); FUZ_CHECKTEST(ret>=0, "...should have failed : not enough output size (-1 byte)");` — confirms `ret>=0` is treated as unexpected/failure in the "too small" scenario.
> lib/lz4frame.c:1936: `RETURN_ERROR_IF(decodedSize < 0, decompressionFailed);` immediately after calling the target — canonical error-check pattern.

### P2.5 Cleanup Sequence
**Claim:** The target allocates nothing, so there is nothing of its own to free. Cleanup is only of caller-owned buffers: `dst`, `src` (compressed buffer), and the dictionary buffer, freed by the harness/test in any order after the last use, and (if a `LZ4_stream_t` was used upstream to produce the compressed block) that encoder stream via `LZ4_freeStream`.
**Evidence:**
> ossfuzz/round_trip_fuzzer.c:111-114: `free(dstPlusLargePrefix); free(largeDict); free(rt); FUZZ_dataProducer_free(producer);` — all cleanup happens after all `LZ4_decompress_safe_usingDict`/`_partial_usingDict` calls.
> ossfuzz/round_trip_stream_fuzzer.c:82-89 `state_free()`: `LZ4_freeStream(state.cstream); LZ4_freeStreamHC(state.cstreamHC); LZ4_freeStreamDecode(state.dstream);` (shows companion streaming objects' cleanup pattern, though `_usingDict` itself needs none of these).

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited locations.
**Evidence:**
> lib/lz4.c:2738 `int LZ4_decompress_safe_usingDict(...)` — target, exists.
> lib/lz4.c:2753 `int LZ4_decompress_safe_partial_usingDict(...)` — exists.
> lib/lz4.c:2650 `int LZ4_decompress_safe_continue(...)` — exists.
> lib/lz4.c:2608 `int LZ4_setStreamDecode(...)` — exists.
> lib/lz4.c:1462 `int LZ4_compress_fast(...)` — exists.
> lib/lz4.c:1481 `int LZ4_compress_default(...)` — exists.
> lib/lz4.c:2588 `LZ4_streamDecode_t* LZ4_createStreamDecode(void)` — exists.
> lib/lz4.c:2594 `int LZ4_freeStreamDecode(LZ4_streamDecode_t*)` — exists.
> lib/lz4.c:2498,2523,2542,2577 `LZ4_decompress_safe_withPrefix64k`, `LZ4_decompress_safe_withSmallPrefix` (static), `LZ4_decompress_safe_forceExtDict`, `LZ4_decompress_safe_doubleDict` — exist (core dispatch targets).
> lib/lz4.c:2022 `LZ4_decompress_generic(...)` — exists (shared core, `LZ4_FORCE_INLINE`).

### P2.7 Co-call Constraints
**Claim:** `LZ4_decompress_safe_usingDict` has no mandatory paired "begin/end" API — it's a one-shot call. The only pairing constraint in the broader feature is for the streaming entries: `LZ4_createStreamDecode()` must be paired with `LZ4_freeStreamDecode()`; and whenever a dictionary is attached for streaming decode, `LZ4_setStreamDecode()` must be called before each new independent stream of blocks consumed via `LZ4_decompress_safe_continue()`. On the compression side used to produce valid input, `LZ4_createStream`/`LZ4_initStream` pairs with `LZ4_freeStream`.
**Evidence:**
> lib/lz4.h:461-464: "LZ4_createStreamDecode() and LZ4_freeStreamDecode() : creation / destruction of streaming decompression tracking context. A tracking context can be re-used multiple times."
> lib/lz4.h:472-479 `LZ4_setStreamDecode()`doc: "Use this function to start decompression of a new stream of blocks."
> ossfuzz/round_trip_stream_fuzzer.c:72-96: `state.dstream = LZ4_createStreamDecode(); ... LZ4_setStreamDecode(state->dstream, NULL, 0);` (reset at the start of each round) paired with `LZ4_freeStreamDecode(state.dstream)` at `state_free` (line 88).

### P2.8 Prerequisite State
**Claim:** Prerequisite state is purely data, not library context: (1) a valid compressed block (`src`) — normally the output of `LZ4_compress_fast`/`LZ4_compress_default` or the `_continue`/`_extDict` compression variants; (2) if dictionary-based, the dictionary bytes at `dictStart` must already be in memory and unmodified, and must logically represent the data that immediately preceded the compressed block's own data at compression time. No global/thread-local library state, no open handle, no file descriptor.
**Evidence:**
> lib/lz4.h:543: "Dictionary is presumed stable: it must remain accessible and unmodified during decompression."
> tests/fuzzer.c:762-765: dictionary filled via `LZ4_compress_fast_continue(&LZ4_stream, dict, compressedBuffer, dictSize, ...)` before producing the real block that is then decoded with the matching dict in `LZ4_decompress_safe_usingDict` (fuzzer.c:781).
> lib/lz4.c:2044: `const BYTE* const dictEnd = (dictStart == NULL) ? NULL : dictStart + dictSize;` — shows `dictStart` is dereferenced/used for arithmetic so must be valid whenever `dictSize>0`.

### P2.9 Result Usage
**Claim:** Callers use the non-negative return value (byte count) to (a) know how many bytes of `dst` are valid for further processing/verification (checksum, memcmp, forwarding to the application), and (b) in streaming contexts, to advance dictionary/prefix bookkeeping (`dctx->dictSize`/`prefixSize`) for the *next* call. The decompressed bytes in `dst` itself are consumed by checksum functions (`XXH32`) or compared against the original in tests; lz4frame updates its internal dictionary tracking from them.
**Evidence:**
> lib/lz4frame.c:1936-1945: after the call, `RETURN_ERROR_IF(decodedSize < 0, decompressionFailed); if (... contentChecksumFlag) XXH32_update(&(dctx->xxh), dstPtr, (size_t)decodedSize); ... if (dctx->frameInfo.blockMode==LZ4F_blockLinked) { LZ4F_updateDict(dctx, dstPtr, (size_t)decodedSize, dstStart, 0); } dstPtr += decodedSize;` — result length drives checksum update, dictionary update, and output cursor advance.
> tests/fuzzer.c:783-784: `U32 const crcCheck = XXH32(decodedBuffer+dictSize, (size_t)blockSize, 0); FUZ_CHECKTEST(crcCheck!=crcOrig, ...)` — decoded buffer content is hashed and compared to detect corruption.
> tests/fuzzer.c:772-777 (sibling `LZ4_decompress_fast_usingDict`): same pattern, `ret` used to validate exact bytes consumed, decoded bytes hashed via `XXH32`.

### P2.10 Behaviour Controls
**Claim:** The target's own behavior (which internal decode path runs) is selected purely by `dictSize` and the relationship between `dictStart+dictSize` and `dest`:
- `dictSize==0` → `LZ4_decompress_safe` (no-dict / `noDict` directive)
- `dictStart+dictSize==dest` (prefix adjoining dst) and `dictSize>=64KB-1` → `LZ4_decompress_safe_withPrefix64k` (`withPrefix64k` directive, fast path assuming distances can't exceed 64KB window safely)
- `dictStart+dictSize==dest` and `dictSize<64KB-1` → `LZ4_decompress_safe_withSmallPrefix` (`noDict` directive but with `lowPrefix` shifted back by `prefixSize`)
- otherwise (`dictStart` separate from `dest`) → `LZ4_decompress_safe_forceExtDict` (`usingExtDict` directive, exercises dictionary-boundary match-copy code in `LZ4_decompress_generic`)
Sibling entry `LZ4_decompress_safe_partial_usingDict` adds `targetOutputSize` (how much of the decompressed stream the caller actually wants) distinct from `dstCapacity` (allocated size), selecting `earlyEnd_directive = partial_decode` in the shared core, which can stop decoding early/mid-sequence — a materially different code path (early-end bounds checks) versus the non-partial decode_full_block mode.
`LZ4_decompress_safe_continue` additionally has 4 internal sub-paths selected by prior calls' bookkeeping (`prefixSize==0` first call / rolling same buffer / switching buffer→forceExtDict / mixed prefix+extDict→doubleDict), controlled entirely by the `LZ4_streamDecode_t` state installed via `LZ4_setStreamDecode`.
On the compression side, `LZ4_compress_fast`'s `acceleration` parameter and `LZ4_compress_default`'s fixed acceleration(=1) affect only compression ratio/speed, not decoder path, but choosing `dstCapacity` close to/less than `LZ4_compressBound(srcSize)` can make compression fail (`return 0`), which must be checked before decompressing.
**Evidence:**
> lib/lz4.c:2740-2750 (target body, dispatch logic quoted above).
> lib/lz4.c:2753-2766 (`LZ4_decompress_safe_partial_usingDict` — identical dict dispatch, but to the `_partial_*` sibling functions).
> lib/lz4.c:2655-2684 (`LZ4_decompress_safe_continue` 3-way branch on `lz4sd->prefixSize`/`prefixEnd`).
> lib/lz4.c:2021-2033 (`LZ4_decompress_generic` signature): `earlyEnd_directive partialDecoding, /* full, partial */ dict_directive dict, /* noDict, withPrefix64k, usingExtDict */` — the two orthogonal mode switches.
> lib/lz4.h:177-191 (`LZ4_compress_default` doc): "If the function cannot compress 'src' into a more limited 'dst' budget, compression stops *immediately*, and the function result is zero."
> lib/lz4.h:232-236 (`LZ4_compress_fast` doc area, acceleration param, values capped at `LZ4_ACCELERATION_MAX`).

### P2.11 Input Expectations
**Claim:** `src` is expected to be a single, complete LZ4 block (not a framed/LZ4F stream) whose length is exactly `srcSize` — the function does not scan for a terminator and will read exactly up to `iend = ip + srcSize`. There is no magic/header inside the block itself (block format: sequence of [token][literal-length-ext][literals][offset][match-length-ext] tokens) — framing metadata (compressed size, decompressed size) must be supplied out-of-band by the caller, matching lz4.h's note for `LZ4_decompress_safe`: "compressedSize and dstCapacity must be provided to the function, the compressed block does not contain them." `dictSize` must be `>=0` (asserted, not always enforced in release builds) and, when nonzero, `dictStart` must be a valid pointer to at least `dictSize` readable bytes. Internally the generic core special-cases `outputSize==0` (returns 0 only if `srcSize==1 && *src==0`, else -1) and `srcSize==0` (returns -1), and uses `checkOffset = dictSize < 64KB` to decide whether offsets must be bounds-checked against the dictionary window.
**Evidence:**
> lib/lz4.h:204-206 (`LZ4_decompress_safe` doc, same block contract applies): "compressedSize and dstCapacity must be provided to the function, the compressed block does not contain them... consider looking at lz4frame.h instead" (for self-describing format).
> lib/lz4.c:2035 `if ((src == NULL) || (outputSize < 0)) { return -1; }`
> lib/lz4.c:2063-2068: `if (unlikely(outputSize==0)) { if (partialDecoding) return 0; return ((srcSize==1) && (*ip==0)) ? 0 : -1; } if (unlikely(srcSize==0)) { return -1; }`
> lib/lz4.c:2046: `const int checkOffset = (dictSize < (int)(64 KB));` — governs whether match-offset bounds checks against dictionary window are active.
> lib/lz4.c:2738,2749-2750 `assert(dictSize >= 0);` before passing `(size_t)dictSize` — negative dictSize is undefined behaviour in release (no runtime check), a caller responsibility.

## Other feature entries: call protocol & combination with the target

### LZ4_decompress_safe_partial_usingDict
**Claim:** Sibling one-shot entry, same dict-dispatch as target (P2.10) but adds `targetOutputSize` (how much decoded data the caller wants, <= `dstCapacity`) and reaches `earlyEnd_directive = partial_decode` in the shared core, which may stop before fully consuming `src`. No init beyond having a valid compressed block + optional dict, identical to the target. Return value semantics identical (byte count or negative error); real callers clamp `dstCapacity = MIN(targetOutputSize, dstCapacity)` is in fact done internally only by the *no-dict* `LZ4_decompress_safe_partial`/`_withPrefix64k`/`_withSmallPrefix` sub-functions (lib/lz4.c:2478-2480, 2505-2512, 2531-2539) — the `_usingDict` wrapper itself passes `dstCapacity` straight through to those, so caller must still ensure `targetOutputSize <= dstCapacity` directionally. Nothing to free beyond caller buffers.
**Evidence:**
> lib/lz4.c:2753-2766 (definition, quoted above).
> lib/lz4.h:552-562 (doc): "Behaves the same as LZ4_decompress_safe_partial() with the added ability to specify a memory segment for past data."
> ossfuzz/round_trip_fuzzer.c:59-108: four variants exercised back-to-back with the SAME compressed block and varying dict placement (`NULL/0`, small prefix, large prefix, small extDict, large extDict), each followed by `FUZZ_ASSERT(partialSize >= 0)` and `memcmp` verification — shows this entry is meant to be called *after* producing one compressed block (via `LZ4_compress_default`) and *alongside*, not instead of, the plain target, in the same harness.

### LZ4_decompress_safe_continue
**Claim:** Stateful alternative to the target. Requires an `LZ4_streamDecode_t*` previously obtained from `LZ4_createStreamDecode()` (heap) or a user-allocated+zeroed union, and optionally primed via `LZ4_setStreamDecode(dstream, dict, dictSize)` before the first call (or rely on prefixSize==0 default "no dictionary yet" path, lib/lz4.c:2655-2659). Each call must be with the same `dest` for contiguous "rolling" decode (prefix mode) or a different `dest` to force an ext-dict transition (lib/lz4.c:2662,2676-2680); block boundaries matter — "only accepts one block at a time" (lz4.h:500). Return: byte count (>=0) or negative error, same contract as target. Must eventually be freed via `LZ4_freeStreamDecode()`.
**Evidence:**
> lib/lz4.c:2650-2687 (full definition, quoted above in Steps).
> lib/lz4.h:495-509 (doc, block/one-at-a-time constraint, dict-availability constraint).
> ossfuzz/round_trip_stream_fuzzer.c:72-77 (create), 96 (setStreamDecode reset), 107-110 (`state_decompress` calling `LZ4_decompress_safe_continue`), 88 (`LZ4_freeStreamDecode` cleanup).
> In one harness: used as an alternative decoding path to the target driven by the same compressed-block-producing pipeline (`LZ4_compress_fast_continue`/`LZ4_compress_default`), not simultaneously decoding the identical call — it is a *different* entry exercising the same `LZ4_decompress_generic` core via the stateful route.

### LZ4_setStreamDecode
**Claim:** Pure configuration function for `LZ4_decompress_safe_continue`/`LZ4_decompress_fast_continue`. Must be called on an already-allocated `LZ4_streamDecode_t*` (from `LZ4_createStreamDecode()` or a zero-initialized static/stack union) before starting a new independent sequence of blocks; `dictionary`/`dictSize` set the initial prefix state. Returns `1` on success; current implementation (lib/lz4.c:2608-2618) always succeeds for any non-NULL `LZ4_streamDecode` (doc states "0 if error" but no error path is visible in the shown code besides the implicit dereference of `LZ4_streamDecode`). Nothing to free (it doesn't allocate); the context itself is freed later via `LZ4_freeStreamDecode`.
**Evidence:**
> lib/lz4.h:472-479 (doc): "@return : 1 if OK, 0 if error"; "A dictionary can optionally be set. Use NULL or size 0 for a reset order."
> lib/lz4.c:2608-2618 (definition): sets `lz4sd->prefixSize`, `lz4sd->prefixEnd` from `dictionary`/`dictSize`.
> ossfuzz/round_trip_stream_fuzzer.c:96: `LZ4_setStreamDecode(state->dstream, NULL, 0);` called once per round at `state_reset`, i.e. before any `LZ4_decompress_safe_continue` call in that round — it is the required init step preceding the `_continue` entry (not needed before the stateless target).

### LZ4_compress_fast / LZ4_compress_default
**Claim:** These are the realistic *producers* of valid `src` input for the target (and for `_continue`/`_usingDict` variants), not consumers. `LZ4_compress_default(src,dst,srcSize,dstCapacity)` is `LZ4_compress_fast(...,acceleration=1)` (lib/lz4.c:1481-1484). Both allocate/release an internal `LZ4_stream_t` state only on the stack/heap-internally (`LZ4_HEAPMODE`) — caller does not manage that state. Return: number of bytes written to `dst` (>0) or `0` on failure (must check before using `dst` as a valid compressed block, e.g. before feeding it to `LZ4_decompress_safe_usingDict`). dstCapacity should be `>= LZ4_compressBound(srcSize)` (lib/lz4.h:180-183, 217-221) to guarantee success; for a harness, `LZ4_compressBound` sizes the `dst` buffer before calling either compress entry, and that `dst` later becomes the `src` fed to the target.
**Evidence:**
> lib/lz4.c:1462-1484 (both definitions, quoted above).
> lib/lz4.h:177-191 (`LZ4_compress_default` doc): "Compression is guaranteed to succeed if 'dstCapacity' >= LZ4_compressBound(srcSize)... or 0 if compression fails."
> ossfuzz/round_trip_fuzzer.c:22,36-38: `size_t const dstCapacity = LZ4_compressBound(size); int const dstSize = LZ4_compress_default(..., dstCapacity); FUZZ_ASSERT(dstSize > 0);` — canonical "produce input for the decoder" pattern, with `dstSize` immediately reused as `compressedSize` for `LZ4_decompress_safe`/`_partial`/`_partial_usingDict` calls in the same file.
> tests/fuzzer.c:764-765: `LZ4_compress_fast_continue(&LZ4_stream, dict, compressedBuffer, dictSize, ...); blockContinueCompressedSize = LZ4_compress_fast_continue(&LZ4_stream, block, compressedBuffer, blockSize, ...);` is the dict-aware sibling producer whose output is fed, a few lines later, directly into `LZ4_decompress_safe_usingDict` (fuzzer.c:781).

## Build notes
**Claim:** Fuzzers in this project are plain C/C++ (harness files compiled with `$(CC)`, linked with `$(CXX)`), include the target's header via `-I../lib` relative to `ossfuzz/`, with `XXH_NAMESPACE=LZ4_` and `FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION` defined, and link against the static `liblz4.a` built from `lib/`. The current `ossfuzz/decompress_fuzzer.c` is a stub (gold harness replaced), but the Makefile/compile recipe it reuses is the authoritative build evidence.
**Evidence:**
> ossfuzz/Makefile:28,36-37: `LZ4DIR := ../lib` ; `LZ4_CPPFLAGS = $(CPPFLAGS) -I$(LZ4DIR) -DXXH_NAMESPACE=LZ4_ -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION`
> ossfuzz/Makefile:56-57: `$(LZ4DIR)/liblz4.a: $(MAKE) -C $(LZ4DIR) CFLAGS="$(LZ4_CFLAGS)" liblz4.a` — static library target linked in.
> ossfuzz/Makefile:59-69: `%.o: %.c  $(CC) -c $(LZ4_CFLAGS) $(LZ4_CPPFLAGS) $< -o $@` then `%_fuzzer: %_fuzzer.o lz4_helpers.o fuzz_data_producer.o $(LZ4DIR)/liblz4.a $(LIB_FUZZING_DEPS)  $(CXX) $(LZ4_CXXFLAGS) $(LZ4_CPPFLAGS) $(LDFLAGS) $(LIB_FUZZING_ENGINE) $^ -o $@$(EXT)` — harness .c compiled with CC, final link with CXX + libFuzzer engine.
> ossfuzz.sh:18-20 / build.sh:19: `pushd ossfuzz; make V=1 all; popd` — the whole `ossfuzz/` Makefile (above) is the build entry point invoked by OSS-Fuzz's `build.sh`.
> ossfuzz/decompress_fuzzer.c:1,10: "Gold harness replaced by an empty stub for baseline generation." / `QF_EXTERN void qf_target_0(void) __asm__("LZ4_decompress_safe_usingDict");` — confirms the target symbol is the one this fuzzer binary must keep linked; real includes would be `#include "lz4.h"` as seen in sibling fuzzers (e.g. round_trip_fuzzer.c:12 `#include "lz4.h"`).