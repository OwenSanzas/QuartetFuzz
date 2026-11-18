## target: LZ4F_compressFrame

### P2.1 Init Sequence
**Claim:** No explicit per-call init object is required from the caller for `LZ4F_compressFrame` itself: it is documented as a "stateless operation (no LZ4F_cctx state needed)" and internally creates/destroys its own `LZ4F_cctx_t` (on stack by default, or heap if compiled with `LZ4F_HEAPMODE=1`). The only mandatory step before calling it is sizing the destination buffer with `LZ4F_compressFrameBound(srcSize, preferencesPtr)` using the *same* `preferencesPtr` that will be passed to `LZ4F_compressFrame`.
**Evidence:**
> lib/lz4frame.h:212-220: "Note : it's a stateless operation (no LZ4F_cctx state needed)... @dstCapacity MUST be >= LZ4F_compressFrameBound(srcSize, preferencesPtr)."
> lib/lz4frame.c:491-509: `#if (LZ4F_HEAPMODE) ... LZ4F_createCompressionContext(&cctxPtr, LZ4F_VERSION) ... #else LZ4F_cctx_t cctx; ... MEM_INIT(&cctx,0,sizeof(cctx)); cctx.version = LZ4F_VERSION; ...`
> tests/frametest.c:311-317: `size_t const c9SizeBound = LZ4F_compressFrameBound(sizeof(s9Buffer), NULL); void* const c9Buffer = malloc(c9SizeBound); ... LZ4F_compressFrame(c9Buffer, c9SizeBound, s9Buffer, sizeof(s9Buffer), &pref);`
> ossfuzz/round_trip_frame_fuzzer.c:20-32: preferences built first, then `dstCapacity = LZ4F_compressFrameBound(LZ4_compressBound(size), &prefs)` then `malloc(dstCapacity)` then `LZ4F_compressFrame(dst, dstCapacity, data, size, &prefs)`.

### P2.2 Parameter Construction
**Claim:** Parameters: `dstBuffer` (writable buffer of size `dstCapacity`, allocated by caller, no alignment requirement found), `dstCapacity` (size_t, must be >= `LZ4F_compressFrameBound(srcSize, preferencesPtr)` or the call fails with `dstMaxSize_tooSmall`), `srcBuffer`/`srcSize` (arbitrary bytes, srcBuffer may be NULL only if srcSize==0, see test below), `preferencesPtr` (may be NULL for defaults, otherwise a fully-initialized `LZ4F_preferences_t`, ideally zero-initialized via `LZ4F_INIT_PREFERENCES` or `memset` first per header comment, with `reserved[3]` fields set to 0 "for forward compatibility").
**Evidence:**
> lib/lz4frame.h:225-227 (declaration): `LZ4F_compressFrame(void* dstBuffer, size_t dstCapacity, const void* srcBuffer, size_t srcSize, const LZ4F_preferences_t* preferencesPtr)`
> lib/lz4frame.h:188-199: `LZ4F_preferences_t` must be "first init to 0, using memset() or LZ4F_INIT_PREFERENCES... All reserved fields must be set to zero."
> lib/lz4frame.c:458: `RETURN_ERROR_IF(dstCapacity < LZ4F_compressFrameBound(srcSize, &prefs), dstMaxSize_tooSmall);` (checked inside `LZ4F_compressFrame_usingCDict`, which the target forwards to)
> tests/frametest.c:337: `LZ4F_compressFrame(c0Buffer, c0SizeBound, NULL, 0, NULL);` — srcBuffer==NULL valid only with srcSize==0.

### P2.3 Object Lifecycle
**Claim:** `LZ4F_compressFrame` is a one-shot wrapper: it creates its own internal cctx (heap or stack), calls `LZ4F_compressFrame_usingCDict` (which internally does Begin→Update→End), then frees/discards the internal cctx before returning. The caller never sees or manages an `LZ4F_cctx`. This differs from the streaming API (`LZ4F_createCompressionContext` → `LZ4F_compressBegin` → `LZ4F_compressUpdate`* → `LZ4F_flush`* → `LZ4F_compressEnd` → `LZ4F_freeCompressionContext`), which the harness does NOT need for the plain target.
**Evidence:**
> lib/lz4frame.c:486-526 (full body of `LZ4F_compressFrame`): heap path `LZ4F_createCompressionContext`/`LZ4F_freeCompressionContext`; stack path manual `LZ4_stream_t`/`LZ4F_free` cleanup; both paths call `LZ4F_compressFrame_usingCDict` once.
> lib/lz4frame.c:460-475 (`LZ4F_compressFrame_usingCDict` body): `LZ4F_compressBegin_usingCDict` (write header) → `LZ4F_compressUpdate` (compress all data) → `LZ4F_compressEnd` (flush+footer) → return total size.

### P2.4 Return Value Handling
**Claim:** Return is `size_t`: either the number of bytes written to `dstBuffer`, or an error code. Caller MUST test with `LZ4F_isError(ret)` before using the return value as a length; `LZ4F_getErrorName(ret)` can be used to print/diagnose. A zero-but-valid-length result is possible for empty input (0-size frame still has non-zero header/footer bytes, so `LZ4F_compressFrame` return is never 0 for a real frame — but is still checked only via `LZ4F_isError`, not by comparing to 0).
**Evidence:**
> lib/lz4frame.h:222-223: "`@return` : number of bytes written into dstBuffer. or an error code if it fails (can be tested using LZ4F_isError())".
> lib/lz4frame.h:106-107: `LZ4FLIB_API unsigned LZ4F_isError(LZ4F_errorCode_t code);` / `LZ4FLIB_API const char* LZ4F_getErrorName(LZ4F_errorCode_t code);`
> ossfuzz/round_trip_frame_fuzzer.c:32-33: `size_t const dstSize = LZ4F_compressFrame(...); FUZZ_ASSERT(!LZ4F_isError(dstSize));`
> tests/frametest.c:317-318: `size_t const c9Size = LZ4F_compressFrame(...); CONTROL(!LZ4F_isError(c9Size));`
> programs/lz4io.c:1103-1104: `if (LZ4F_isError(errorCode)) END_PROCESS(30, "Allocation error : can't create LZ4F context : %s", LZ4F_getErrorName(errorCode));` (pattern used throughout lz4io.c for all LZ4F_* return codes).

### P2.5 Cleanup Sequence
**Claim:** Nothing to free on the caller side that is produced *by* `LZ4F_compressFrame` itself — the function's own internal cctx is freed/discarded internally on both success and error paths before it returns (both HEAPMODE and stack-mode branches run their respective cleanup unconditionally after the inner call, not gated on success). The caller only owns and must free `dstBuffer` (and `srcBuffer`, if heap-allocated) via its own allocator (e.g., plain `free()`), nothing lz4-specific.
**Evidence:**
> lib/lz4frame.c:513-524: `result = LZ4F_compressFrame_usingCDict(...); #if (LZ4F_HEAPMODE) LZ4F_freeCompressionContext(cctxPtr); #else if (... HC level...) LZ4F_free(cctxPtr->lz4CtxPtr, cctxPtr->cmem); #endif return result;` — cleanup always runs regardless of `result`.
> ossfuzz/round_trip_frame_fuzzer.c:38-40: `free(dst); free(rt); FUZZ_dataProducer_free(producer);` — caller frees its own buffers after use.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited paths/lines: `LZ4F_compressFrame`, `LZ4F_compressFrame_usingCDict`, `LZ4F_compressFrameBound`, `LZ4F_compressBound`, `LZ4F_createCompressionContext`, `LZ4F_freeCompressionContext`, `LZ4F_compressBegin`, `LZ4F_compressBegin_usingCDict`, `LZ4F_compressUpdate`, `LZ4F_flush`, `LZ4F_compressEnd`, `LZ4F_createCDict`, `LZ4F_freeCDict`, `LZ4F_createDecompressionContext`, `LZ4F_freeDecompressionContext`, `LZ4F_decompress`, `LZ4F_decompress_usingDict`, `LZ4F_resetDecompressionContext`, `LZ4F_getFrameInfo`, `LZ4F_isError`, `LZ4F_getErrorName`.
**Evidence:**
> lib/lz4frame.h (declarations): line 225 (`LZ4F_compressFrame`), 235 (`LZ4F_compressFrameBound`), 275-276 (`LZ4F_createCompressionContext`/`LZ4F_freeCompressionContext`), 322 (`LZ4F_compressBound`), 398-399 (`LZ4F_createDecompressionContext`/`LZ4F_freeDecompressionContext`), 517-520 (`LZ4F_decompress`), 572-576 (`LZ4F_decompress_usingDict`), 598-599 (`LZ4F_createCDict`/`LZ4F_freeCDict`), 617-618 (`LZ4F_compressFrame_usingCDict`), 629-632 (`LZ4F_compressBegin_usingCDict`).
> lib/lz4frame.c: definitions at 408 (`LZ4F_compressFrameBound`), 430 (`LZ4F_compressFrame_usingCDict`), 486 (`LZ4F_compressFrame`).

### P2.7 Co-call Constraints
**Claim:** For the plain target there is no mandatory pairing visible to the caller (create/free of the internal cctx is hidden). For the sibling entries used in the same harness: `LZ4F_createCompressionContext` must be paired with `LZ4F_freeCompressionContext`; `LZ4F_createDecompressionContext` must be paired with `LZ4F_freeDecompressionContext`; `LZ4F_createCDict` must be paired with `LZ4F_freeCDict`. `LZ4F_compressFrameBound` must always be computed with the *same* preferences later given to `LZ4F_compressFrame`/`LZ4F_compressFrame_usingCDict`, and must be called (or an equivalently-sized buffer used) *before* compressing, not after.
**Evidence:**
> lib/lz4frame.h:270-273: "Once all streaming compression jobs are completed, the state object can be released using LZ4F_freeCompressionContext()."
> lib/lz4frame.h:394-396: "dctx memory can be released using LZ4F_freeDecompressionContext(); Result ... indicates current state ...".
> tests/frametest.c:828-848: `LZ4F_createCompressionContext(&cctx,...)` then later `LZ4F_freeCompressionContext(cctx)` then re-`LZ4F_createCompressionContext` for a second usage; `cdict = LZ4F_createCDict(...)` used across multiple `LZ4F_compressFrame_usingCDict` calls, no corresponding `LZ4F_freeCDict(cdict)` shown in this excerpt for `cdict` itself but `LZ4F_freeCDict(cda)` is shown for the `_advanced` variant at line 836 — downstream harness must free any `LZ4F_CDict*` it creates.
> lib/lz4frame.h:220: `@dstCapacity MUST be >= LZ4F_compressFrameBound(srcSize, preferencesPtr)` — ordering constraint (bound call logically precedes/accompanies the compress call, same prefs struct).

### P2.8 Prerequisite State
**Claim:** No global library init call exists/is required (no `LZ4_init`-style call found). The only prerequisite "state" is: (1) a correctly populated/zeroed `LZ4F_preferences_t` (or NULL), and (2) a destination buffer sized via `LZ4F_compressFrameBound`. No file, socket, or thread-pool state is required for `LZ4F_compressFrame` (that is only present in the CLI tool `programs/lz4io.c`, not the library API).
**Evidence:**
> Grep across `lib/lz4frame.c` and `lib/lz4frame.h` found no `LZ4F_initLib`/global-init function; the header's first functional section is `LZ4F_isError` (lib/lz4frame.h:106) followed directly by the simple compression API at line 208.
> lib/lz4frame.h:190-192: "Structure must be first init to 0, using memset() or LZ4F_INIT_PREFERENCES ... All reserved fields must be set to zero."

### P2.9 Result Usage
**Claim:** The produced frame in `dstBuffer` (length = return value) is consumed by `LZ4F_decompress` (after `LZ4F_createDecompressionContext`) or `LZ4F_decompress_usingDict` (if a dictionary was used during compression). This is exactly the pattern in the project's own round-trip fuzzer and tests: compress with `LZ4F_compressFrame`/`LZ4F_compressFrame_usingCDict`, then decompress the output and compare to the original.
**Evidence:**
> ossfuzz/round_trip_frame_fuzzer.c:31-36: `size_t const dstSize = LZ4F_compressFrame(dst, dstCapacity, data, size, &prefs); FUZZ_ASSERT(!LZ4F_isError(dstSize)); size_t const rtSize = FUZZ_decompressFrame(rt, size, dst, dstSize); FUZZ_ASSERT_MSG(rtSize == size, "Incorrect regenerated size"); FUZZ_ASSERT_MSG(!memcmp(data, rt, size), "Corruption!");`
> ossfuzz/lz4_helpers.c:36-48 (`FUZZ_decompressFrame`): `LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION); ... LZ4F_decompress(dctx, dst, &dstSize, src, &srcConsumed, &opts); FUZZ_ASSERT(!LZ4F_isError(rc)); FUZZ_ASSERT(rc == 0); FUZZ_ASSERT(srcConsumed == srcSize); LZ4F_freeDecompressionContext(dctx);`
> tests/frametest.c:317-323: compress then immediately `LZ4F_decompress(dctx, d9Buffer, &dstSize, c9Buffer, &srcSize, NULL)` on the produced buffer.
> tests/frametest.c:840-871: `LZ4F_compressFrame_usingCDict(... cdict ...)` output fed into `LZ4F_decompress_usingDict(dctx, decodedBuffer, &decodedSize, compressedBuffer, &compressedSize, CNBuffer, dictSize, NULL)` — the *same* dictionary bytes (`CNBuffer`/`dictSize`) used on both sides.

### P2.10 Behaviour Controls
**Claim:** Behavior-selecting fields, all inside `LZ4F_preferences_t`/`LZ4F_frameInfo_t`, each exercising a distinct code path in the shared core:
- `frameInfo.blockSizeID` (`LZ4F_default`/0, `LZ4F_max64KB`, `LZ4F_max256KB`, `LZ4F_max1MB`, `LZ4F_max4MB`) — selects block size/count via `LZ4F_optimalBSID`/`LZ4F_getBlockSize`, and single-block inputs force `blockMode = LZ4F_blockIndependent` automatically.
- `frameInfo.blockMode` (`LZ4F_blockLinked` vs `LZ4F_blockIndependent`) — selects *_continue vs plain compressors in `LZ4F_selectCompression`.
- `frameInfo.contentChecksumFlag` / `frameInfo.blockChecksumFlag` — toggle XXH32 checksum code in `LZ4F_makeBlock`/`LZ4F_compressEnd`.
- `frameInfo.contentSize` — if nonzero, auto-corrected to actual `srcSize` and written into the header, enlarging header size.
- `compressionLevel` — negative values select "fast acceleration" (`LZ4_compress_fast*`), values `>= LZ4HC_CLEVEL_MIN` switch the whole cctx to the HC path (also forces heap allocation of the HC context even in non-HEAPMODE build, see stack-mode branch), clamped at `LZ4HC_CLEVEL_MAX`.
- `favorDecSpeed` — only effective at HC levels `>= LZ4HC_CLEVEL_OPT_MIN`.
- `autoFlush` — forced to 1 inside `LZ4F_compressFrame_usingCDict` regardless of caller's setting (one-shot nature).
- `dictID` (in frameInfo) — written into header to help decoder pick a dictionary; only meaningfully settable via `preferencesPtr` (not via the `_usingCDict` dict buffer alone).
**Evidence:**
> lib/lz4frame.c:450-453: `prefs.frameInfo.blockSizeID = LZ4F_optimalBSID(prefs.frameInfo.blockSizeID, srcSize); prefs.autoFlush = 1; if (srcSize <= LZ4F_getBlockSize(prefs.frameInfo.blockSizeID)) prefs.frameInfo.blockMode = LZ4F_blockIndependent;`
> lib/lz4frame.c:447-448: `if (prefs.frameInfo.contentSize != 0) prefs.frameInfo.contentSize = (U64)srcSize;`
> lib/lz4frame.c:503-509: `if (preferencesPtr == NULL || preferencesPtr->compressionLevel < LZ4HC_CLEVEL_MIN) { LZ4_initStream(...); cctxPtr->lz4CtxType = ctxFast; }` (else: HC path taken, see line 520-523 freeing the HC-allocated ctx).
> lib/lz4frame.h:195-197: comment documents compressionLevel/autoFlush/favorDecSpeed semantics directly in the struct.
> ossfuzz/fuzz_data_producer.c:50-73 and ossfuzz/lz4_helpers.c:5-28: both project fuzz helpers construct `LZ4F_preferences_t` by randomizing exactly these fields (`blockSizeID`, `blockMode`, `contentChecksumFlag`, `blockChecksumFlag`, `compressionLevel` including negative "fast" values, `autoFlush`, `favorDecSpeed`), confirming these are the intended fuzz-relevant switches.

### P2.11 Input Expectations
**Claim:** `srcBuffer`/`srcSize` have no format requirement — `LZ4F_compressFrame` treats input as an arbitrary opaque byte buffer (it is the *producer* of the LZ4 frame format, not a consumer of one). `srcSize` of 0 is valid (produces a minimal/empty frame; `srcBuffer` may be NULL in that case, see tests/frametest.c:337). There is no NUL-termination requirement (binary-safe, size-driven). The only documented input/size constraint is on `dstCapacity`, which must be `>= LZ4F_compressFrameBound(srcSize, preferencesPtr)`, enforced internally via `RETURN_ERROR_IF(dstCapacity < LZ4F_compressFrameBound(srcSize, &prefs), dstMaxSize_tooSmall)`.
**Evidence:**
> lib/lz4frame.h:208-210: "Compress srcBuffer content into an LZ4-compressed frame. It's a one shot operation, all input content is consumed, and all output is generated." — no format precondition on srcBuffer stated.
> lib/lz4frame.c:458: `RETURN_ERROR_IF(dstCapacity < LZ4F_compressFrameBound(srcSize, &prefs), dstMaxSize_tooSmall);`
> tests/frametest.c:332-338: zero-size, NULL-srcBuffer case exercised and asserted valid: `LZ4F_compressFrame(c0Buffer, c0SizeBound, NULL, 0, NULL); CONTROL(!LZ4F_isError(c0Size));`

### Other entries of this feature (how they combine with the target in one harness)

**LZ4F_compressFrameBound(srcSize, preferencesPtr)** — size helper, not a stateful object API.
- Needs: nothing before it; takes the *same* `srcSize`/`preferencesPtr` intended for the compress call.
- Returns: `size_t` byte-count upper bound (not an error code per lib/lz4frame.h:229-234, but same-shaped internal arithmetic as compress).
- Usage: call first, `malloc()` the result, then pass that buffer+capacity to `LZ4F_compressFrame`/`LZ4F_compressFrame_usingCDict`. No free needed for its return value (it's a number, not a pointer).
- Evidence: lib/lz4frame.c:408-418 (definition); ossfuzz/round_trip_frame_fuzzer.c:23-24 (`dstCapacity = LZ4F_compressFrameBound(...); dst = malloc(dstCapacity);`); programs/lz4io.c:1111 (`ress.dstBufferSize = LZ4F_compressFrameBound(chunkSize, &ress.preparedPrefs);`).

**LZ4F_compressFrame_usingCDict(cctx, dstBuffer, dstCapacity, srcBuffer, srcSize, cdict, preferencesPtr)** — dictionary variant of the target, same core.
- Needs before it: an `LZ4F_cctx*` from `LZ4F_createCompressionContext(&cctx, LZ4F_VERSION)` (explicit, unlike the plain target which hides this), and optionally an `LZ4F_CDict*` from `LZ4F_createCDict(dictBuffer, dictSize)` (cdict may be NULL → behaves like the plain target but with an explicit context).
- Parameters: identical to target plus leading `cctx` and `cdict`; `dstCapacity` must still be `>= LZ4F_compressFrameBound(srcSize, preferencesPtr)` (lib/lz4frame.c:458, same check).
- Returns: `size_t`, same `LZ4F_isError` contract as the target (lib/lz4frame.h:415).
- Cleanup: caller must call `LZ4F_freeCompressionContext(cctx)` (can be reused across multiple calls first, see tests/frametest.c:848-850 which frees+recreates between uses) and `LZ4F_freeCDict(cdict)` if created.
- Combines with target: a harness can call the plain `LZ4F_compressFrame` on one branch of fuzz data and, in a second scenario, explicitly create cctx+cdict and call `LZ4F_compressFrame_usingCDict` to exercise `LZ4F_compressBegin_usingCDict`'s dictID/dict-loading path (`LZ4_loadDict`/`LZ4_loadDictHC`) that the plain target never reaches with cdict!=NULL.
- Evidence: lib/lz4frame.h:601-618 (declaration+doc: "`@cctx` must point to a context created by LZ4F_createCompressionContext()."); lib/lz4frame.c:430-476 (definition, shared Begin/Update/End sequence with the target); tests/frametest.c:828-853 (create cctx, create cdict, call with cdict=NULL then with cdict, free/recreate cctx between).

**LZ4F_decompress(dctx, dstBuffer, dstSizePtr, srcBuffer, srcSizePtr, decompressOptionsPtr)** — consumer of the target's output.
- Needs before it: `LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION)` (lib/lz4frame.h:398, "@version provided MUST be LZ4F_VERSION"). May be called in a loop since one call may not consume/produce everything; `*dstSizePtr`/`*srcSizePtr` are in/out capacity-then-actual-used.
- Parameters/flags: `decompressOptionsPtr` may be NULL for defaults; `stableDst=1` option (as used in ossfuzz/lz4_helpers.c:35) tells the decoder dst buffer won't move/change, enabling some optimizations.
- Returns: `size_t` — 0 means frame fully decoded; >0 is "hint" of next expected srcSize (frame not finished); error tested via `LZ4F_isError`. On error, `dctx` becomes non-resumable until `LZ4F_resetDecompressionContext` (lib/lz4frame.h:496-497).
- Cleanup: `LZ4F_freeDecompressionContext(dctx)` once done (lib/lz4frame.h:394-396, "Result of LZ4F_freeDecompressionContext() indicates current state ... should be == 0 if decompression has been completed fully and correctly").
- Combines with target: this is the direct round-trip inverse used by the project's own `round_trip_frame_fuzzer.c` — compress with the target, then feed the exact `dst`/`dstSize` output into `LZ4F_decompress`, assert full consumption (`srcConsumed == srcSize`) and `rc == 0` (fully terminated frame).
- Evidence: lib/lz4frame.h:388-399, 516-520; ossfuzz/lz4_helpers.c:30-51; ossfuzz/round_trip_frame_fuzzer.c:34-36.

**LZ4F_decompress_usingDict(dctxPtr, dstBuffer, dstSizePtr, srcBuffer, srcSizePtr, dict, dictSize, decompressOptionsPtr)** — dictionary-aware inverse of `LZ4F_compressFrame_usingCDict`.
- Needs before it: same `LZ4F_createDecompressionContext` as plain decompress; needs the *same* raw dictionary bytes/size that were used to build the `LZ4F_CDict` on the compression side (dictionary is used "in place" on decompress, no digested form) — "It must remain accessible throughout the entire frame decoding." (lib/lz4frame.h:570).
- Parameters: identical to `LZ4F_decompress` plus `dict`/`dictSize`.
- Returns/cleanup: identical contract to `LZ4F_decompress` (same `LZ4F_isError`, same `LZ4F_freeDecompressionContext`).
- Combines with target: pairs specifically with `LZ4F_compressFrame_usingCDict(..., cdict, ...)` output, NOT with the plain `LZ4F_compressFrame` (which never sets a dictID in the frame): tests/frametest.c:840-871 compress with cdict then decompress with the same raw `CNBuffer`/`dictSize`.
- Evidence: lib/lz4frame.h:567-576; tests/frametest.c:804-808, 863-871.

### Build notes
**Claim:** Fuzzers for this project are plain C (compiled with `$(CC)`/`$(CXX)` per the Makefile's generic rule, though link step uses `$(CXX)`), built via `ossfuzz/Makefile` invoked from `build.sh` → `ossfuzz/ossfuzz.sh`. The target's header is included via `-I$(LZ4DIR)` where `LZ4DIR := ../lib` (i.e. `lib/lz4frame.h` is found relative to the `ossfuzz/` directory), and fuzzers link statically against `lib/liblz4.a` built from the same source tree (no system-installed lz4). `XXH_NAMESPACE=LZ4_` and `FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION` are defined for all fuzzer compilations.
**Evidence:**
> ossfuzz/Makefile:28-37: `LZ4DIR := ../lib` ... `LZ4_CPPFLAGS = $(CPPFLAGS) -I$(LZ4DIR) -DXXH_NAMESPACE=LZ4_ -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION`
> ossfuzz/Makefile:56-60: `$(LZ4DIR)/liblz4.a: $(MAKE) -C $(LZ4DIR) ... liblz4.a` and `%.o: %.c  $(CC) -c $(LZ4_CFLAGS) $(LZ4_CPPFLAGS) $< -o $@`
> ossfuzz/Makefile:68-69: `%_fuzzer: %_fuzzer.o lz4_helpers.o fuzz_data_producer.o $(LZ4DIR)/liblz4.a $(LIB_FUZZING_DEPS) \n\t$(CXX) $(LZ4_CXXFLAGS) $(LZ4_CPPFLAGS) $(LDFLAGS) $(LIB_FUZZING_ENGINE) $^ -o $@$(EXT)` — fuzzer binaries link `liblz4.a` plus the project's own `lz4_helpers.o`/`fuzz_data_producer.o` glue.
> ossfuzz/compress_frame_fuzzer.c and ossfuzz/round_trip_frame_fuzzer.c both `#include "lz4frame.h"` directly (no subdirectory prefix), consistent with `-I../lib`.
> ${QF_WORK}/src/lz4-c1/build.sh:19: `./ossfuzz/ossfuzz.sh` — the OSS-Fuzz-view build.sh simply delegates to the project's own `ossfuzz.sh`, which runs `make V=1 all` inside `ossfuzz/` and copies `ossfuzz/*_fuzzer` to `$OUT/` (ossfuzz/ossfuzz.sh:16-23).
> ${QF_WORK}/ossfuzz_view/lz4-c1/Dockerfile:17-22: based on `gcr.io/oss-fuzz-base/base-builder`, clones `https://github.com/lz4/lz4.git` into `/src/lz4`, confirming standard OSS-Fuzz C build environment (clang/clang++ via `$CC`/`$CXX`, sanitizers via base-builder defaults).