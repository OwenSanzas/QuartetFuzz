## target: LZ4F_decompress

### P2.1 Init Sequence
**Claim:** A decompression context must be created with `LZ4F_createDecompressionContext()` before any call to `LZ4F_decompress`. No other global/library init is required. If `LZ4F_getFrameInfo()` is used first (optional), it consumes header bytes from `srcBuffer` and the caller must advance its own buffer pointer accordingly before continuing with `LZ4F_decompress`.
**Evidence:**
> lz4frame.h:499: "@pre @p dctx is a valid state created by LZ4F_createDecompressionContext()."
> lz4frame.h:388-398: `LZ4F_createDecompressionContext(LZ4F_dctx** dctxPtr, unsigned version)` — "@version provided MUST be LZ4F_VERSION… dctx memory can be released using LZ4F_freeDecompressionContext()"
> lz4frame.c:1302-1329: `LZ4F_createDecompressionContext_advanced`/`LZ4F_createDecompressionContext` calloc the `LZ4F_dctx`, store `version`, return `LZ4F_OK_NoError` or `allocation_failed`.
> lz4frame.h:509-511: "@warning If you called LZ4F_getFrameInfo() beforehand, you must advance @p srcBuffer and decrease *srcSizePtr by the number of bytes it consumed … Failing to do so can cause decompression failure or, worse, silent corruption."
> ossfuzz/lz4_helpers.c:36-37: canonical pattern — `LZ4F_dctx* dctx; LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION); FUZZ_ASSERT(dctx);` then `LZ4F_decompress(...)`.

### P2.2 Parameter Construction
**Claim:** `dctx` is a non-NULL, previously-created `LZ4F_dctx*`. `dstBuffer`/`dstSizePtr` and `srcBuffer`/`srcSizePtr` are in/out pairs: `*dstSizePtr` = capacity of `dstBuffer` on entry (may be 0, in which case `dstBuffer` may be NULL), `*srcSizePtr` = number of available bytes in `srcBuffer` on entry. Both are overwritten with actual bytes written/consumed. `dOptPtr` may be NULL (defaults used) or point to a caller-owned, zero-initialized `LZ4F_decompressOptions_t`.
**Evidence:**
> lz4frame.h:482-487: "`dctx` A valid decompression context…`dstBuffer` …; `dstSizePtr` In: capacity… Out: bytes written…; `srcBuffer`…; `srcSizePtr` In: bytes available… Out: bytes consumed…; `optionsPtr` pass NULL for defaults."
> lz4frame.c:1663: `if (dstBuffer == NULL) assert(*dstSizePtr == 0);` — dstBuffer may be NULL only if *dstSizePtr==0 (used internally, e.g. by LZ4F_getFrameInfo with o=0).
> lz4frame.c:1664-1665: `MEM_INIT(&optionsNull,0,sizeof(optionsNull)); if (decompressOptionsPtr==NULL) decompressOptionsPtr = &optionsNull;` — NULL options accepted.
> ossfuzz/lz4_helpers.c:33-43: `LZ4F_decompressOptions_t opts; memset(&opts,0,sizeof(opts)); opts.stableDst=1; … LZ4F_decompress(dctx, dst, &dstSize, src, &srcConsumed, &opts);` — zero-initialize before setting fields (reserved fields must be 0, lz4frame.h:381-382).

### P2.3 Object Lifecycle
**Claim:** Create (`LZ4F_createDecompressionContext`) → optionally query header (`LZ4F_getFrameInfo`/`LZ4F_headerSize`) → call `LZ4F_decompress` (or `LZ4F_decompress_usingDict`) repeatedly, feeding/consuming chunks, until return value is 0 (frame done) or an error → optionally reset (`LZ4F_resetDecompressionContext`) to reuse for another frame, or just keep feeding next frame's bytes directly → destroy with `LZ4F_freeDecompressionContext`.
**Evidence:**
> lz4frame.h:472: "Call repeatedly until the return value is 0 (frame fully decoded) or an error is reported."
> lz4frame.h:506-507: "After finishing a frame (return==0), you may immediately start feeding the next frame into the same dctx (optionally, one can use LZ4F_resetDecompressionContext())."
> lz4frame.c:2016-2020 and 2058-2061: on successful completion of `dstage_getSuffix` (no checksum or checksum verified), `LZ4F_resetDecompressionContext(dctx)` is called internally, resetting `dStage` to `dstage_getFrameHeader` — the context is self-resetting on success, no caller action required between frames.
> lz4frame.h:394-396 / lz4frame.c:1331-1341: `LZ4F_freeDecompressionContext` frees `dctx->tmpIn`, `dctx->tmpOutBuffer`, then `dctx` itself; "should be == 0 if decompression has been completed fully and correctly" (returns the leftover `dStage` as a diagnostic code).

### P2.4 Return Value Handling
**Claim:** Return value is `size_t`; test with `LZ4F_isError(ret)`. `>0` = hint for next call's src size and frame not yet complete; `0` = frame fully decoded (any leftover `*srcSizePtr` bytes belong to a subsequent frame); error code (test via `LZ4F_isError`) means `dctx` is left in an undefined/non-resumable state and must be reset via `LZ4F_resetDecompressionContext()` before reuse.
**Evidence:**
> lz4frame.h:489-497: "@retval >0 Hint…@retval 0 The current frame is fully decoded…@retval error An error code; test with LZ4F_isError(ret). After an error, dctx is not resumable: call LZ4F_resetDecompressionContext() before reusing it."
> lz4frame.c:295-309: `unsigned LZ4F_isError(LZ4F_errorCode_t code)` checks `code > (LZ4F_errorCode_t)(-ERROR_maxCode)`, and `LZ4F_getErrorName` maps codes to strings.
> ossfuzz/lz4_helpers.c:44-46: `FUZZ_ASSERT(!LZ4F_isError(rc)); FUZZ_ASSERT(rc == 0); FUZZ_ASSERT(srcConsumed == srcSize);` — real caller treats non-zero/error rc as a bug when it expects a single complete frame from a single call.

### P2.5 Cleanup Sequence
**Claim:** On both success and error paths, release the context exactly once with `LZ4F_freeDecompressionContext(dctx)` after decompression work is done (it is safe to call even on a dctx mid-frame or in an error state — "can accept NULL input, like free()"). There is no separate buffer to free — `dctx` owns and frees its own `tmpIn`/`tmpOutBuffer` internal buffers. Caller-provided `dstBuffer`/`srcBuffer` are managed by the caller.
**Evidence:**
> lz4frame.c:1331-1341: `LZ4F_freeDecompressionContext` — "can accept NULL input, like free()"; frees `dctx->tmpIn`, `dctx->tmpOutBuffer`, `dctx`.
> ossfuzz/lz4_helpers.c:48: `LZ4F_freeDecompressionContext(dctx);` called unconditionally after the single `LZ4F_decompress` call (after the `FUZZ_ASSERT` checks on `rc`, i.e. the harness aborts before reaching free() if rc is bad — but in a generated harness, free must happen on every exit path, success or error, since FUZZ_ASSERT may be an assert() that can be disabled in production builds).

### P2.6 API Existence
**Claim:** All functions discussed exist in the current codebase at the stated locations.
**Evidence:**
> lz4frame.h:516-520: `LZ4F_decompress` declared.
> lz4frame.h:398: `LZ4F_createDecompressionContext` declared; lz4frame.c:1319 defined.
> lz4frame.h:399: `LZ4F_freeDecompressionContext` declared; lz4frame.c:1331 defined.
> lz4frame.h:528: `LZ4F_resetDecompressionContext` declared; lz4frame.c:1354 defined.
> lz4frame.h:465-467 / lz4frame.c:1512: `LZ4F_getFrameInfo` declared/defined.
> lz4frame.h:571-576 / lz4frame.c:2154: `LZ4F_decompress_usingDict` declared/defined.
> lz4frame.h:419 / lz4frame.c:1471: `LZ4F_headerSize` declared/defined.
> lz4frame.h:225-227 / lz4frame.c:486: `LZ4F_compressFrame` declared/defined.
> (Not directly part of target call but mentioned round-trip): `LZ4F_compressFrameBound` lz4frame.h:235 / lz4frame.c:408.

### P2.7 Co-call Constraints
**Claim:** `LZ4F_createDecompressionContext` / `LZ4F_freeDecompressionContext` is the mandatory create/destroy pair (exactly one free per create, order: create first, free last). `LZ4F_resetDecompressionContext` has no matching "unreset" call — it's an idempotent state-machine reset, callable any time after creation, and is implicitly invoked by `LZ4F_decompress` itself on successful frame completion. There is no explicit "begin/end" pair for decompression itself (unlike the compression side's `LZ4F_compressBegin`/`LZ4F_compressUpdate`/`LZ4F_compressEnd`); the frame boundary is detected automatically by the state machine (return value 0).
**Evidence:**
> lz4frame.c:2016-2020, 2058-2061, 2105-2107: three places where `LZ4F_resetDecompressionContext(dctx)` is called internally by `LZ4F_decompress` on frame completion (no-checksum completion, checksum-verified completion, skippable-frame completion).
> lz4frame.h:523-528: "This method can also be used to abruptly stop any unfinished decompression, and start a new one using same context resources" — no corresponding "start" call needed, decompress just resumes at `dstage_getFrameHeader`.
> lz4frame.c:1331-1341: single free function, idempotent for NULL.

### P2.8 Prerequisite State
**Claim:** Beyond a created `dctx`, `LZ4F_decompress` requires no other global state/connections. Internal per-frame buffers (`tmpIn`, `tmpOutBuffer`, sized from `maxBlockSize`/`maxBufferSize`) are lazily allocated inside `LZ4F_decodeHeader` (invoked from the `dstage_getFrameHeader` stage) once the frame header (BD byte / blockSizeID) has been parsed — i.e., the dctx is *not* fully configured for block decoding until the header stage has run at least once.
**Evidence:**
> lz4frame.c:1298: `struct LZ4F_dctx_s` fields include `tmpIn`, `maxBlockSize`, `tmpOutBuffer`, `maxBufferSize`, `header[LZ4F_HEADER_SIZE_MAX]`.
> lz4frame.c:1365-1369: `LZ4F_decodeHeader()` comment: "output: set internal values of dctx, such as dctx->frameInfo and dctx->dStage. Also allocates internal buffers."
> lz4frame.c:1678-1690 (`dstage_getFrameHeader`/`dstage_storeFrameHeader`): first stage of the state machine always runs `LZ4F_decodeHeader` (directly, or after buffering header bytes across calls) before any block-decoding stage can be reached.

### P2.9 Result Usage
**Claim:** The primary "result" of `LZ4F_decompress` is the decompressed bytes written into the caller's `dstBuffer` (count given by `*dstSizePtr`) plus the function's own return value (hint/0/error) and the updated `*srcSizePtr`. Real callers (a) compare the output bytes to expected/original data (round-trip tests), (b) feed back unconsumed `srcBuffer` bytes and reuse `dstBuffer` on the next iteration of the same call, and (c) use `LZ4F_isError`/return value to decide whether to loop again, reset, or abort. `LZ4F_getFrameInfo()` itself calls `LZ4F_decompress(dctx, NULL, &o, NULL, &i, NULL)` internally to obtain the "recommended next srcSize" hint once header-only state has been reached.
**Evidence:**
> ossfuzz/round_trip_frame_fuzzer.c:34-36: `size_t const rtSize = FUZZ_decompressFrame(rt, size, dst, dstSize); FUZZ_ASSERT_MSG(rtSize == size, "Incorrect regenerated size"); FUZZ_ASSERT_MSG(!memcmp(data, rt, size), "Corruption!");` — decompressed buffer (rt) is memcmp'd against the original input.
> ossfuzz/lz4_helpers.c:40-50: `dstSize` (out-param of `LZ4F_decompress`) is returned to caller (`FUZZ_decompressFrame`) as the final decompressed size; `dctx` is then freed — no further use of `dctx` once `rc==0` and all data consumed.
> lz4frame.c:1526-1527: `LZ4F_getFrameInfo` — "returns : recommended nb of bytes for LZ4F_decompress()" via `return LZ4F_decompress(dctx, NULL, &o, NULL, &i, NULL);` — demonstrates `LZ4F_decompress`'s return value being consumed as a "next read size" hint by another library API.
> lz4frame.h:494-495: "If *srcSizePtr is less than the provided value, the unconsumed tail is the start of another frame (if any)" — documents how callers chain multiple frames' decode calls using the srcSizePtr out value.

### P2.10 Behaviour Controls
**Claim:** Behavior of `LZ4F_decompress` is controlled by: (1) `dOptPtr->skipChecksums` — once set to 1 on any call, disables block & content checksum verification for the rest of the frame; (2) `dOptPtr->stableDst` — tells the decoder the last 64KB of the dst buffer remain valid/stable across calls, enabling it to skip internal copy-to-tmpOutBuffer bookkeeping for `blockMode==LZ4F_blockLinked` frames; (3) the frame's own header bytes (parsed by `LZ4F_decodeHeader`, not by caller-supplied flags) select processing paths: magic number (regular vs skippable frame, `dstage_getSFrameSize`/`dstage_skipSkippable` vs normal stages), FLG byte bits (version, blockChecksumFlag, blockMode independent/linked, contentSizeFlag, contentChecksumFlag, dictIDFlag), BD byte blockSizeID (bounds `maxBlockSize`), and each block header's top bit `LZ4F_BLOCKUNCOMPRESSED_FLAG` selecting `dstage_copyDirect` (raw) vs `dstage_getCBlock`/LZ4-decode path, and size==0 signalling end-of-frame → `dstage_getSuffix`.
**Evidence:**
> lz4frame.c:1669: `dctx->skipChecksum |= (decompressOptionsPtr->skipChecksums != 0);` — sticky, once-set-stays-set per frame.
> lz4frame.c:2047, 2051-2057: `if (!dctx->skipChecksum) { … RETURN_ERROR_IF(readCRC != resultCRC, contentChecksum_invalid); }` under `#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION` — content checksum check is also compiled out under the fuzzing build macro (see Build notes).
> lz4frame.c:2114-2118: `if ((dctx->frameInfo.blockMode==LZ4F_blockLinked) && … && (!decompressOptionsPtr->stableDst) && …)` — `stableDst` gates the "preserve history within tmpOut" dictionary bookkeeping branch.
> lz4frame.c:2063-2108 (`dstage_getSFrameSize`/`dstage_skipSkippable`): skippable-frame magic (`0x184D2A5x`) path reads a 4-byte size and skips that many bytes, bypassing normal block decoding entirely.
> lz4frame.h:375-383: `LZ4F_decompressOptions_t` fields `stableDst`, `skipChecksums`, two `reserved` fields that "must be set to zero".
> lz4frame.c:1471-1495 (`LZ4F_headerSize`) and the header-decode logic it mirrors: FLG byte `contentSizeFlag`/`dictIDFlag` bits change header length; same bits are consumed by `LZ4F_decodeHeader` to set `dctx->frameInfo`.

### P2.11 Input Expectations
**Claim:** `srcBuffer` must begin at a frame boundary (magic number `LZ4F_MAGICNUMBER` 0x184D2204, or a skippable-frame magic `0x184D2A50..0x184D2A5F`) the first time a given `dctx` reaches `dstage_getFrameHeader`; subsequent calls may deliver arbitrary, partial chunks (the state machine buffers partial header/block-header/block/suffix bytes in `dctx->header`/`dctx->tmpIn` across calls — no fixed minimum except `LZ4F_MIN_SIZE_TO_KNOW_HEADER_LENGTH` for header-size determination). Each compressed block's declared size is bounds-checked against the block size derived from the BD byte's blockSizeID (must be one of the 4 valid IDs, `max64KB`..`max4MB`); block size 0 is the explicit "no more blocks" end marker before the optional 4-byte content checksum suffix. `*srcSizePtr`/`*dstSizePtr` give the only size limits; there is no implicit NUL-termination or separate length field outside the frame format itself.
**Evidence:**
> lz4frame.h:406-408: `#define LZ4F_MAGICNUMBER 0x184D2204U` / `#define LZ4F_MAGIC_SKIPPABLE_START 0x184D2A50U` / `#define LZ4F_MIN_SIZE_TO_KNOW_HEADER_LENGTH 5`.
> lz4frame.c:1480-1487 (`LZ4F_headerSize`): `if ((LZ4F_readLE32(src) & 0xFFFFFFF0U) == LZ4F_MAGIC_SKIPPABLE_START) return 8;` then, outside fuzzing mode, `RETURN_ERROR_IF` style check `if (LZ4F_readLE32(src) != LZ4F_MAGICNUMBER) RETURN_ERROR(frameType_unknown);` (magic check compiled out under `FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION`, see Build notes).
> lz4frame.c:1678-1690 (`dstage_getFrameHeader`/`dstage_storeFrameHeader`): "if (srcEnd-srcPtr == 0) return minFHSize;" and byte-by-byte buffering into `dctx->header` until `tmpInTarget` (minFHSize, extendable with contentSize/dictID fields) is reached — demonstrates chunked/partial-input tolerance.
> lz4frame.c:2063-2096 (`dstage_getSFrameSize`/`dstage_storeSFrameSize`): skippable frame reads its own 4-byte little-endian size field then skips exactly that many bytes via `dstage_skipSkippable`.
> lz4frame.c:2014-2061 (`dstage_getSuffix`/`dstage_storeSuffix`): final 4-byte content checksum (if `contentChecksumFlag` set) read via `LZ4F_readLE32`; absence of checksum flag ends the frame immediately with no suffix bytes consumed.

## Other Logic-Group Entries (co-called with LZ4F_decompress)

### LZ4F_createDecompressionContext
**Protocol:** Must be called once before using a given `LZ4F_dctx*` with any decompression entry (`LZ4F_decompress`, `LZ4F_decompress_usingDict`, `LZ4F_getFrameInfo`, `LZ4F_resetDecompressionContext`). Signature: `LZ4F_errorCode_t LZ4F_createDecompressionContext(LZ4F_dctx** dctxPtr, unsigned version)`. `version` MUST be `LZ4F_VERSION` (macro). Returns `LZ4F_OK_NoError` (0) on success or an error code (test with `LZ4F_isError`); on failure `*dctxPtr` is set but allocation failed — `*dctxPtr` must still be checked for non-NULL before use.
**Evidence:** lz4frame.h:388-398, lz4frame.c:1318-1329 (`RETURN_ERROR_IF(LZ4F_decompressionContextPtr == NULL, parameter_null)`, `RETURN_ERROR(allocation_failed)`); ossfuzz/lz4_helpers.c:37-38 (`LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION); FUZZ_ASSERT(dctx);`).
**Combines with target:** allocates the `dctx` passed as first argument into every `LZ4F_decompress` call; must be freed exactly once with `LZ4F_freeDecompressionContext`.

### LZ4F_freeDecompressionContext (cleanup counterpart, same entry group as create)
**Protocol:** `LZ4F_errorCode_t LZ4F_freeDecompressionContext(LZ4F_dctx* dctx)`. Accepts NULL (no-op like `free()`). Must be called exactly once per successful `LZ4F_createDecompressionContext`, after the harness is done calling `LZ4F_decompress`/`LZ4F_decompress_usingDict`/`LZ4F_getFrameInfo` on that context — regardless of whether the last `LZ4F_decompress` call returned success or an error.
**Evidence:** lz4frame.h:394-396,399; lz4frame.c:1331-1341; ossfuzz/lz4_helpers.c:48.

### LZ4F_resetDecompressionContext
**Protocol:** `void LZ4F_resetDecompressionContext(LZ4F_dctx* dctx)` — "always successful", no return value to check. Resets `dStage` to `dstage_getFrameHeader`, clears `dict`/`dictSize`/`skipChecksum`/`frameRemainingSize`. Callable any time after `LZ4F_createDecompressionContext`, typically after `LZ4F_decompress` returns an error (to make the context reusable) or to abandon a partially-decoded frame and start a fresh one on the same `dctx`. The target itself invokes this internally upon successful frame completion, so the harness does not need to call it between well-formed frames — but a harness that wants to deliberately feed a new frame after an error path, or abort mid-frame and restart, should call it explicitly.
**Evidence:** lz4frame.h:523-528; lz4frame.c:1354-1362; lz4frame.c:2018,2059,2106 (internal auto-reset on success).
**Combines with target:** takes the same `dctx` as `LZ4F_decompress`; no separate allocation/free — operates in place on the existing context.

### LZ4F_getFrameInfo
**Protocol:** `LZ4F_errorCode_t LZ4F_getFrameInfo(LZ4F_dctx* dctx, LZ4F_frameInfo_t* frameInfoPtr, const void* srcBuffer, size_t* srcSizePtr)`. Requires the same created `dctx` as `LZ4F_decompress` (and may be called either before any decompression has started on that frame, or after decoding has progressed past the header — behavior differs, see below). `frameInfoPtr` must be a valid, non-NULL, caller-allocated `LZ4F_frameInfo_t*` (filled by copy). `srcSizePtr` must be non-NULL; `*srcSizePtr` in = bytes available, out = bytes actually consumed for header decode (0 if dctx already had the header). Returns a size_t hint/error, test via `LZ4F_isError`.
Two call situations:
  - dStage still at/below `dstage_storeFrameHeader` but not mid-store (`dStage <= dstage_getFrameHeader` i.e. not yet started): decodes header directly from `srcBuffer` via `LZ4F_headerSize`+`LZ4F_decodeHeader`, consuming header bytes.
  - `dStage == dstage_storeFrameHeader` (header decode already in progress, partial): automatic failure `frameDecoding_alreadyStarted`.
  - `dStage > dstage_storeFrameHeader` (header already fully decoded): ignores `srcBuffer`, copies cached `dctx->frameInfo`, and internally calls `LZ4F_decompress(dctx, NULL, &o, NULL, &i, NULL)` to get the next-hint value.
**Evidence:** lz4frame.c:1512-1545 (full body, truncated read above); lz4frame.h:421-467 (doc).
**Combines with target:** If called first, the harness MUST advance its own `srcBuffer`/`srcSize` tracking by the `*srcSizePtr` bytes `LZ4F_getFrameInfo` consumed before calling `LZ4F_decompress` on the remainder (lz4frame.h:509-511 warning) — otherwise header bytes get double-processed or decoding desyncs.

### LZ4F_decompress_usingDict
**Protocol:** `size_t LZ4F_decompress_usingDict(LZ4F_dctx* dctxPtr, void* dstBuffer, size_t* dstSizePtr, const void* srcBuffer, size_t* srcSizePtr, const void* dict, size_t dictSize, const LZ4F_decompressOptions_t* decompressOptionsPtr)`. Same `dctx`/buffer contract as `LZ4F_decompress`; additionally takes `dict`/`dictSize` which it stashes into `dctx->dict`/`dctx->dictSize` only `if (dctx->dStage <= dstage_init)` (i.e., only effective before/at the very start of decoding a frame — setting it mid-frame on later calls has no effect), then delegates entirely to `LZ4F_decompress`. `dict` must remain valid/accessible for the whole frame's decoding (it is used in place, not copied).
**Evidence:** lz4frame.c:2149-2166; lz4frame.h:567-576 (stable since v1.10): "Dictionary is used \"in place\", without any preprocessing. It must remain accessible throughout the entire frame decoding."
**Combines with target:** Direct variant/wrapper of the target — a harness exercising the dictionary path calls this instead of (or in addition to, on a fresh dctx/frame) `LZ4F_decompress`; return-value/error handling identical to the target (same retval contract, since it's literally `return LZ4F_decompress(...)`).

### LZ4F_headerSize
**Protocol:** `size_t LZ4F_headerSize(const void* src, size_t srcSize)` — pure helper, no `dctx` needed, no lifecycle. Requires `src != NULL` and `srcSize >= LZ4F_MIN_SIZE_TO_KNOW_HEADER_LENGTH` (5 bytes), else returns error (`srcPtr_wrong`/`frameHeader_incomplete`, test via `LZ4F_isError`). Returns the frame-header length (minFHSize + optional contentSize(8)/dictID(4) field sizes), or 8 for skippable frames.
**Evidence:** lz4frame.c:1471-1495; lz4frame.h:410-419.
**Combines with target:** Independent of a `dctx`; a harness can call it on raw fuzz bytes to decide how to split input into "header region" vs "block region" before feeding chunks into `LZ4F_decompress`, or just to pre-validate/skip malformed headers without needing a context at all.

### LZ4F_compressFrame
**Protocol:** `size_t LZ4F_compressFrame(void* dstBuffer, size_t dstCapacity, const void* srcBuffer, size_t srcSize, const LZ4F_preferences_t* preferencesPtr)` — one-shot, stateless (no `LZ4F_cctx` needed, allocates its own transient state, by default on the stack). `dstCapacity` MUST be `>= LZ4F_compressFrameBound(srcSize, preferencesPtr)`. `preferencesPtr` optional (NULL → all defaults). Returns bytes written or an error code (test via `LZ4F_isError`). No cleanup needed (nothing persistent is returned/allocated to the caller).
**Evidence:** lz4frame.h:208-227; lz4frame.c:479-517 (`LZ4F_compressFrame` delegates to `LZ4F_compressFrame_usingCDict`); lz4frame.h:229-235 (`LZ4F_compressFrameBound`).
**Combines with target:** Used by a round-trip harness to synthesize a syntactically-valid frame from fuzz bytes, which is then fed into `LZ4F_decompress` (via the same `dctx`-create → decompress → free sequence) to validate lossless round-trip and exercise deeper block/checksum logic beyond early header-rejection paths. Real example: ossfuzz/round_trip_frame_fuzzer.c:23-39 calls `LZ4F_compressFrame(dst, dstCapacity, data, size, &prefs)` then `FUZZ_decompressFrame(rt, size, dst, dstSize)` (wrapper around create→decompress→free, ossfuzz/lz4_helpers.c:30-51), then `memcmp(data, rt, size)`.

## Build notes

**Claim:** Fuzzer sources for this feature are compiled as C (the harness files themselves, e.g. `decompress_frame_fuzzer.c`, `round_trip_frame_fuzzer.c`, are `.c`), but linked with `$(CXX)` (C++ linker, standard for libFuzzer-based OSS-Fuzz targets). The target's header `lz4frame.h` is reached via `-I$(LZ4DIR)` where `LZ4DIR := ../lib` relative to the `ossfuzz/` directory (i.e. `#include "lz4frame.h"` resolves because the lib dir is on the include path, not a system/installed path). The library is linked as a static archive `liblz4.a` built from `../lib`. A macro `-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION` is always defined for fuzz builds, which **disables some safety/integrity checks inside `LZ4F_decompress`'s own code** (magic-number check in `LZ4F_headerSize`, content-checksum verification in `dstage_getSuffix`) — the downstream harness generator should be aware these checks are intentionally bypassed in this build mode, so malformed-magic / bad-checksum inputs will NOT be rejected the way they would in a production build.

**Evidence:**
> ossfuzz/Makefile:28,36-37: `LZ4DIR := ../lib` ; `LZ4_CPPFLAGS = $(CPPFLAGS) -I$(LZ4DIR) -DXXH_NAMESPACE=LZ4_ -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION`
> ossfuzz/Makefile:59-60: `%.o: %.c  $(CC) -c $(LZ4_CFLAGS) $(LZ4_CPPFLAGS) $< -o $@` — harness `.c` files compiled with `$(CC)`.
> ossfuzz/Makefile:68-69: `%_fuzzer: %_fuzzer.o lz4_helpers.o fuzz_data_producer.o $(LZ4DIR)/liblz4.a $(LIB_FUZZING_DEPS)` then `$(CXX) $(LZ4_CXXFLAGS) $(LZ4_CPPFLAGS) $(LDFLAGS) $(LIB_FUZZING_ENGINE) $^ -o $@$(EXT)` — final link step uses C++ compiler driver with `$(LIB_FUZZING_ENGINE)` (libFuzzer) and the static lib.
> ossfuzz/Makefile:39-49: `FUZZERS := compress_fuzzer decompress_fuzzer round_trip_fuzzer round_trip_stream_fuzzer compress_hc_fuzzer round_trip_hc_fuzzer compress_frame_fuzzer round_trip_frame_fuzzer round_trip_frame_uncompressed_fuzzer decompress_frame_fuzzer` — existing targets include two frame-decompression-relevant fuzzers.
> ossfuzz/decompress_frame_fuzzer.c:1 "Gold harness replaced by an empty stub for baseline generation" — this project's current `decompress_frame_fuzzer.c` is a stub only keeping the symbol `LZ4F_decompress` linked (`__asm__("LZ4F_decompress")`), confirming it is the designated target harness file to be (re)written by the downstream generator; `round_trip_frame_fuzzer.c` remains a full, working reference implementation of the create→compress→decompress→compare→free pattern.
> ossfuzz/ossfuzz.sh:18-23: `pushd ossfuzz; make V=1 all; popd; cp -v ossfuzz/*_fuzzer $OUT/` — OSS-Fuzz `build.sh` simply runs the ossfuzz Makefile and copies resulting binaries to `$OUT`.
> lz4frame.c:1484-1487 and 2051-2057: `#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION` guards around the magic-number check in `LZ4F_headerSize` and the content-checksum verification in `LZ4F_decompress`'s `dstage_getSuffix` stage — both are compiled OUT when `FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION` is defined (as it always is per the Makefile), so the fuzz build intentionally skips these two validation points to reach deeper code.