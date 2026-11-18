## target: LZ4_compress_HC

### Declaration & definition
`lib/lz4hc.h:66`: `LZ4LIB_API int LZ4_compress_HC (const char* src, char* dst, int srcSize, int dstCapacity, int compressionLevel);`
Doc comment (`lib/lz4hc.h:56-65`): "`dst` must be already allocated. Compression is guaranteed to succeed if `dstCapacity >= LZ4_compressBound(srcSize)`. Max supported `srcSize` value is LZ4_MAX_INPUT_SIZE. `compressionLevel`: any value between 1 and LZ4HC_CLEVEL_MAX will work, values above clamp to LZ4HC_CLEVEL_MAX. @return: number of bytes written into 'dst' or 0 if compression fails."

Definition, `lib/lz4hc.c:1568-1584`:
```
int LZ4_compress_HC(const char* src, char* dst, int srcSize, int dstCapacity, int compressionLevel)
{
    int cSize;
#if defined(LZ4HC_HEAPMODE) && LZ4HC_HEAPMODE==1
    LZ4_streamHC_t* const statePtr = (LZ4_streamHC_t*)ALLOC(sizeof(LZ4_streamHC_t));
    if (statePtr==NULL) return 0;
#else
    LZ4_streamHC_t state;
    LZ4_streamHC_t* const statePtr = &state;
#endif
    cSize = LZ4_compress_HC_extStateHC(statePtr, src, dst, srcSize, dstCapacity, compressionLevel);
#if defined(LZ4HC_HEAPMODE) && LZ4HC_HEAPMODE==1
    FREEMEM(statePtr);
#endif
    return cSize;
}
```
It is entirely self-contained: it allocates (heap or stack, depending on `LZ4HC_HEAPMODE`) and frees its own `LZ4_streamHC_t` state, so callers need **no setup** beyond valid `src`/`dst` pointers and sizes. Internally it calls `LZ4_compress_HC_extStateHC` -> `LZ4_initStreamHC` -> `LZ4_compress_HC_extStateHC_fastReset` -> `LZ4HC_compress_generic` -> `LZ4HC_compress_generic_noDictCtx` -> `LZ4HC_compress_generic_internal` -> (`LZ4HC_compress_hashChain` / `LZ4HC_compress_optimal` / `LZ4MID_compress` depending on level).

### P2.1 Init Sequence
**Claim:** No external initialization is required before calling `LZ4_compress_HC` itself — it self-initializes a temporary `LZ4_streamHC_t`. No global/library init function exists in LZ4.
**Evidence:**
> lib/lz4hc.c:1568-1579: `LZ4_compress_HC` allocates `statePtr` then calls `LZ4_compress_HC_extStateHC(statePtr, ...)` which itself calls `LZ4_initStreamHC(state, sizeof(*ctx))` (lib/lz4hc.c:1563) before compressing.
> ossfuzz/compress_hc_fuzzer.c:35-36: real fuzzer calls `LZ4_compress_HC((const char*)data, dst, size, dstCapacity, level)` directly, with no prior library init call.

### P2.2 Parameter Construction
**Claim:** `src` must point to `srcSize` readable bytes (may be NULL only if `srcSize==0`); `dst` must point to a writable buffer of at least `dstCapacity` bytes (must be non-NULL whenever `dstCapacity>=1`); `srcSize` must be in `[0, LZ4_MAX_INPUT_SIZE]` (0x7E000000); `dstCapacity` must be `>=1` for any non-trivial src, `>= LZ4_compressBound(srcSize)` to *guarantee* success; `compressionLevel` is unrestricted int but is clamped internally to `[LZ4HC_CLEVEL_MIN(2)? , LZ4HC_CLEVEL_MAX(12)]`/default in sub-paths (see P2.10).
**Evidence:**
> lib/lz4hc.c:1434: `if ((U32)*srcSizePtr > (U32)LZ4_MAX_INPUT_SIZE) return 0;  /* Unsupported input size (too large or negative) */`
> lib/lz4hc.c:1435-1438: `if (dstCapacity < 1) return 0; ... if (*srcSizePtr == 0) { *dst = 0; return 1; } assert(src != NULL);`
> lib/lz4hc.h:59-60: "Compression is guaranteed to succeed if `dstCapacity >= LZ4_compressBound(srcSize)`. Max supported `srcSize` value is LZ4_MAX_INPUT_SIZE."
> lib/lz4.h:214: `#define LZ4_MAX_INPUT_SIZE 0x7E000000`
> lib/lz4hc.c:1663-1664 (`LZ4_setCompressionLevel`): `if (compressionLevel < 1) compressionLevel = LZ4HC_CLEVEL_DEFAULT; if (compressionLevel > LZ4HC_CLEVEL_MAX) compressionLevel = LZ4HC_CLEVEL_MAX;` — so out-of-range levels are silently clamped, not rejected.

### P2.3 Object Lifecycle
**Claim:** `LZ4_compress_HC` has no externally visible lifecycle: create/configure/use/destroy of the internal `LZ4_streamHC_t` all happen inside the call. There is nothing for the harness to create or destroy around this specific entry.
**Evidence:**
> lib/lz4hc.c:1568-1584 (full body quoted above): state is allocated, used via `LZ4_compress_HC_extStateHC`, then `FREEMEM(statePtr)` (only in heap mode) before returning.

### P2.4 Return Value Handling
**Claim:** Return value is `>0` = number of bytes written to `dst` (success); `0` = compression failed (bad params, insufficient `dstCapacity`, or allocation failure in heap mode). Never negative. A `1` is returned for a 0-length `*srcSizePtr` case too (just writes a single `0x00` token byte at `*dst`).
**Evidence:**
> lib/lz4hc.h:63-64: "@return : the number of bytes written into 'dst' or 0 if compression fails."
> lib/lz4hc.c:1573: `if (statePtr==NULL) return 0;` (allocation failure path, heap mode)
> lib/lz4hc.c:1437: `if (*srcSizePtr == 0) { *dst = 0; return 1; }`
> ossfuzz/compress_hc_fuzzer.c:35-41: caller checks `if (dstSize > 0)` before trusting the result and feeding it to `LZ4_decompress_safe`.

### P2.5 Cleanup Sequence
**Claim:** Nothing to free after `LZ4_compress_HC` itself; only the caller's own `dst` buffer (and `src` buffer) need freeing by the harness, which it allocated itself.
**Evidence:**
> lib/lz4hc.c:1580-1582: `#if ... LZ4HC_HEAPMODE==1 ... FREEMEM(statePtr); #endif` — internal state cleanup is automatic and internal, not caller-visible.
> ossfuzz/compress_hc_fuzzer.c:59-61: harness frees only its own `dst`, `rt`, and the producer: `free(dst); free(rt); FUZZ_dataProducer_free(producer);` — no LZ4-owned handle to free for this call.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase (lib/lz4hc.c / lib/lz4hc.h / lib/lz4.h), confirmed by direct grep/read.
**Evidence:**
> lib/lz4hc.c:1535 `int LZ4_sizeofStateHC(void)`
> lib/lz4hc.c:1561 `int LZ4_compress_HC_extStateHC(...)`
> lib/lz4hc.c:1568 `int LZ4_compress_HC(...)`
> lib/lz4hc.c:1587 `int LZ4_compress_HC_destSize(...)`
> lib/lz4hc.c:1603 `LZ4_streamHC_t* LZ4_createStreamHC(void)`
> lib/lz4hc.c:1612 `int LZ4_freeStreamHC(...)`
> lib/lz4hc.c:1622 `LZ4_streamHC_t* LZ4_initStreamHC(...)`
> lib/lz4hc.c:1644 `void LZ4_resetStreamHC_fast(...)`
> lib/lz4hc.c:1675 `int LZ4_loadDictHC(...)`
> lib/lz4hc.c:1771 `int LZ4_compress_HC_continue(...)`
> lib/lz4hc.c:1780 `int LZ4_compress_HC_continue_destSize(...)`
> lib/lz4hc.c:1791 `int LZ4_saveDictHC(...)`
> lib/lz4.h:208 `int LZ4_decompress_safe(...)` (consumer, declared in lz4.h not lz4hc.h)
> `LZ4MID_compress` is a static function inside lib/lz4hc.c (internal, not part of public API — see dispatch at lib/lz4hc.c:1446).

### P2.7 Co-call Constraints
**Claim:** For the target `LZ4_compress_HC` itself there is no paired API (it is a fully self-contained single-shot call). Paired APIs exist for the *other* entries in this feature:
- `LZ4_createStreamHC()` (alloc) must be paired with exactly one `LZ4_freeStreamHC()` (free) — standard create/free pairing.
- `LZ4_resetStreamHC_fast()` must be called to (re)start a stream before `LZ4_compress_HC_continue()`/`LZ4_loadDictHC()` on a reused `LZ4_streamHC_t` (or the state must come fresh from `LZ4_createStreamHC()`, which auto-initializes).
- `LZ4_loadDictHC()` must be called (if at all) only after setting compression level and before the first `LZ4_compress_HC_continue()` of the session — order documented explicitly.
**Evidence:**
> lib/lz4hc.h:103-110: "LZ4_createStreamHC() and LZ4_freeStreamHC(): These functions create and release memory for LZ4 HC streaming state."
> lib/lz4hc.h:122-125: "Selecting the compression level can be done with LZ4_resetStreamHC_fast() (starts a new stream) ... LZ4_resetStreamHC_fast() only works on states which have been properly initialized at least once, which is automatically the case when state is created using LZ4_createStreamHC()."
> lib/lz4hc.h:127-130: "After reset, a first 'fictional block' can be designated as initial dictionary, using LZ4_loadDictHC() (Optional). Note: In order for LZ4_loadDictHC() to create the correct data structure, it is essential to set the compression level _before_ loading the dictionary."
> tests/fuzzer.c:336,957-1023: `LZ4dictHC = LZ4_createStreamHC(); ... LZ4_loadDictHC(LZ4dictHC, dict, dictSize); ... LZ4_compress_HC_continue(LZ4dictHC, ...); ... LZ4_freeStreamHC(LZ4_streamHC);` shows create → loadDict → continue (repeatable) → free.

### P2.8 Prerequisite State
**Claim:** `LZ4_compress_HC` needs no prerequisite library/global state (LZ4 has no init/deinit global function). The streaming variants require a properly-sized/aligned `LZ4_streamHC_t` (from `LZ4_createStreamHC()` or `LZ4_initStreamHC()`/stack-allocated + aligned buffer) with compressionLevel set before any dictionary load, and (for `_continue`) prior blocks' memory must remain accessible/unmodified since they act as implicit dictionary.
**Evidence:**
> lib/lz4hc.h:119-120: "Before starting compression, state must be allocated and properly initialized. LZ4_createStreamHC() does both, though compression level is set to LZ4HC_CLEVEL_DEFAULT."
> lib/lz4hc.h:132-135: "Invoke LZ4_compress_HC_continue() to compress each successive block... Previous input blocks, including initial dictionary when present, must remain accessible and unmodified during compression."
> lib/lz4hc.c:1628-1629 (`LZ4_initStreamHC`): `if (size < sizeof(LZ4_streamHC_t)) return NULL; if (!LZ4_isAligned(buffer, LZ4_streamHC_t_alignment())) return NULL;` — explicit size/alignment prerequisite check for externally-supplied state buffers (`LZ4_compress_HC_extStateHC`, `LZ4_compress_HC_destSize`).

### P2.9 Result Usage
**Claim:** The returned compressed size (`>0`) and the filled `dst` buffer are consumed by `LZ4_decompress_safe` (declared in lz4.h) as a round-trip correctness check; this is the standard consumer pattern in both the real OSS-Fuzz harness and the unit test suite.
**Evidence:**
> ossfuzz/compress_hc_fuzzer.c:35-41:
> ```
> int const dstSize = LZ4_compress_HC((const char*)data, dst, size, dstCapacity, level);
> if (dstSize > 0) {
>     int const rtSize = LZ4_decompress_safe(dst, rt, dstSize, size);
>     FUZZ_ASSERT_MSG(rtSize == size, "Incorrect regenerated size");
>     FUZZ_ASSERT_MSG(!memcmp(data, rt, size), "Corruption!");
> }
> ```
> ossfuzz/compress_hc_fuzzer.c:48-55: same pattern for `LZ4_compress_HC_destSize` result — `rtSize == compressedSize` checked against `LZ4_decompress_safe(dst, rt, dstSize, size)`.
> tests/fuzzer.c:1028-1034: streaming-continue output (`compressedBuffer`) is fed to `LZ4_decompress_safe_usingDict(compressedBuffer, decodedBuffer, blockContinueCompressedSize, blockSize, dict, dictSize)` and checked against a CRC of the original block.
> lib/lz4.h:208: `LZ4LIB_API int LZ4_decompress_safe (const char* src, char* dst, int compressedSize, int dstCapacity);` — public decompression counterpart that real callers use on HC-compressed output (LZ4 HC output is format-compatible with the standard LZ4 block decoder).

### P2.10 Behaviour Controls
**Claim:** The key behaviour-selecting parameters across this API family are:
1. `compressionLevel` (all entries): selects `lz4mid` / `lz4hc` (hash-chain) / `lz4opt` (optimal-parse) strategy via `LZ4HC_getCLevelParams(cLevel)`, used in `LZ4HC_compress_generic_internal`.
2. `limitedOutput_directive limit` (`notLimited` / `limitedOutput` / `fillOutput`): internal flag chosen by each public wrapper based on whether `dstCapacity >= LZ4_compressBound(srcSize)` (`notLimited` vs `limitedOutput`), or forced `fillOutput` for the `_destSize` family — this selects the dest-overflow/truncation code path.
3. `dictCtx_directive dict` (`noDictCtx` / `usingDictCtxHc`): selects whether the hash-chain/optimal search also consults a previously-attached external dictionary context.
4. `favorDecSpeed` flag (`LZ4_favorDecompressionSpeed`) passed as `HCfavor_e favor` into `LZ4HC_compress_optimal`, changing match-selection heuristics (only reachable via streaming API, not via plain `LZ4_compress_HC`).
**Evidence:**
> lib/lz4hc.c:1441-1460:
> ```
> cParams_t const cParam = LZ4HC_getCLevelParams(cLevel);
> HCfavor_e const favor = ctx->favorDecSpeed ? favorDecompressionSpeed : favorCompressionRatio;
> if (cParam.strat == lz4mid) { result = LZ4MID_compress(...); }
> else if (cParam.strat == lz4hc) { result = LZ4HC_compress_hashChain(...); }
> else { assert(cParam.strat == lz4opt); result = LZ4HC_compress_optimal(..., cLevel >= LZ4HC_CLEVEL_MAX, dict, favor); }
> ```
> lib/lz4hc.c:1555-1558 (`LZ4_compress_HC_extStateHC_fastReset`): `if (dstCapacity < LZ4_compressBound(srcSize)) return LZ4HC_compress_generic(..., limitedOutput); else return LZ4HC_compress_generic(..., notLimited);`
> lib/lz4hc.c:1593 (`LZ4_compress_HC_destSize`): `return LZ4HC_compress_generic(&ctx->internal_donotuse, source, dest, sourceSizePtr, targetDestSize, cLevel, fillOutput);`
> lib/lz4hc.c:1527-1531 (`LZ4HC_compress_generic`): `if (ctx->dictCtx == NULL) return LZ4HC_compress_generic_noDictCtx(...); else return LZ4HC_compress_generic_dictCtx(...);`
> lib/lz4hc.h:47-50: `#define LZ4HC_CLEVEL_MIN 2 / LZ4HC_CLEVEL_DEFAULT 9 / LZ4HC_CLEVEL_OPT_MIN 10 / LZ4HC_CLEVEL_MAX 12` — thresholds that fuzzer should explore (ossfuzz/compress_hc_fuzzer.c:25 already ranges `level` in `[LZ4HC_CLEVEL_MIN, LZ4HC_CLEVEL_MAX]`).
> lib/lz4hc.c:1668-1671 (`LZ4_favorDecompressionSpeed`): `LZ4_streamHCPtr->internal_donotuse.favorDecSpeed = (favor!=0);`

### P2.11 Input Expectations
**Claim:** `src` is treated as an opaque raw byte buffer (no header/magic/termination expected by the compressor itself); `srcSize` must be within `[0, LZ4_MAX_INPUT_SIZE]` (checked, returns 0 on violation, with negative values caught via unsigned compare); `dstCapacity` must be `>=1` or the call fails immediately; there is no NUL-termination requirement, `src` may contain arbitrary bytes including zeros. The *decompressor* side (`LZ4_decompress_safe`) is what actually validates LZ4-format tokens/offsets in the compressed output, not the HC encoder itself (which, assuming non-corrupted internal state, always emits valid output or returns 0).
**Evidence:**
> lib/lz4hc.c:1434: `if ((U32)*srcSizePtr > (U32)LZ4_MAX_INPUT_SIZE) return 0;  /* Unsupported input size (too large or negative) */` (cast to unsigned catches negative srcSize too)
> lib/lz4hc.c:1435-1438: `if (dstCapacity < 1) return 0; ... if (*srcSizePtr == 0) { *dst = 0; return 1; } assert(src != NULL);`
> lib/lz4hc.c:549,1154,1874 (doc comments on internal compress functions): "- `*srcSizePtr` within `[1, LZ4_MAX_INPUT_SIZE]`" — internal core functions assume this once the `srcSize==0` shortcut is bypassed.
> lib/lz4hc.h:59-60: documents `dstCapacity >= LZ4_compressBound(srcSize)` as the only way to *guarantee* success; smaller buffers may still succeed or may return 0 (limitedOutput truncation).

## Other Entries (API surface) and combined-harness protocol

### LZ4_compress_HC_extStateHC(void* stateHC, const char* src, char* dst, int srcSize, int maxDstSize, int compressionLevel)
**Call protocol:** Caller allocates `stateHC` buffer of size `>= LZ4_sizeofStateHC()`, 8-byte aligned (plain `malloc` suffices). Function self-initializes the state via `LZ4_initStreamHC(state, sizeof(*ctx))` (lib/lz4hc.c:1563) before compressing — no separate init call needed by the harness, just the allocation. Returns 0 on init failure (bad size/alignment) or compression failure, else bytes written. Caller must `free(stateHC)` after use (harness-owned memory, not LZ4-owned).
**Evidence:** lib/lz4hc.h:74-78 ("Same as LZ4_compress_HC(), but using an externally allocated memory segment for `state`. `state` size is provided by LZ4_sizeofStateHC(). Memory segment must be aligned on 8-bytes boundaries"); lib/lz4hc.c:1561-1566; tests/fuzzer.c:334 `void* const stateLZ4HC = malloc((size_t)LZ4_sizeofStateHC());` and tests/fuzzer.c:1091 `free(stateLZ4HC);`.
**Combination with target:** This is exactly what `LZ4_compress_HC` wraps internally (lib/lz4hc.c:1579); a harness can call it directly to test the "externally-allocated/caller-aligned state" path distinct from `LZ4_compress_HC`'s hidden stack/heap state.

### LZ4_sizeofStateHC(void)
**Call protocol:** Pure query function, no prerequisites, no side effects. Returns `(int)sizeof(LZ4_streamHC_t)` — used solely to size the buffer passed to `LZ4_compress_HC_extStateHC`/`LZ4_compress_HC_destSize`. Nothing to free (it allocates nothing).
**Evidence:** lib/lz4hc.c:1535 `int LZ4_sizeofStateHC(void) { return (int)sizeof(LZ4_streamHC_t); }`; ossfuzz/compress_hc_fuzzer.c:46 `void* state = malloc(LZ4_sizeofStateHC());`.
**Combination with target:** Must be called before `LZ4_compress_HC_extStateHC`/`LZ4_compress_HC_destSize` to size the state buffer; not needed for plain `LZ4_compress_HC`.

### LZ4_compress_HC_destSize(void* stateHC, const char* src, char* dst, int* srcSizePtr, int targetDstSize, int compressionLevel)
**Call protocol:** `stateHC` buffer sized via `LZ4_sizeofStateHC()` (caller-owned, must free after). `*srcSizePtr` is in/out: on input the available src length, on output (success) how much was actually consumed (`<= *srcSizePtr` originally). `targetDstSize` is the hard cap on bytes written. Internally forces `fillOutput` mode (lib/lz4hc.c:1593) which truncates compression to fit the budget rather than failing outright. Returns bytes written to `dst` (`<= targetDstSize`) or 0 on failure.
**Evidence:** lib/lz4hc.h:83-94 (doc); lib/lz4hc.c:1587-1594; ossfuzz/compress_hc_fuzzer.c:44-57 (real harness usage, with round-trip check against `compressedSize`, the updated `*srcSizePtr`).
**Combination with target:** Exercises the dest-overflow/truncation path distinct from `LZ4_compress_HC`'s limitedOutput/notLimited; the existing OSS-Fuzz harness calls both `LZ4_compress_HC` and `LZ4_compress_HC_destSize` on the same input in one fuzz iteration (ossfuzz/compress_hc_fuzzer.c:34-57).

### LZ4_createStreamHC(void) / LZ4_freeStreamHC(LZ4_streamHC_t*)
**Call protocol:** `LZ4_createStreamHC()` heap-allocates and zero-initializes a `LZ4_streamHC_t`, sets level to `LZ4HC_CLEVEL_DEFAULT` (9), returns `NULL` on allocation failure. Must be paired 1:1 with `LZ4_freeStreamHC()` (NULL-safe, returns 0 always). No other setup required before first use (level/dict can be customized after creation).
**Evidence:** lib/lz4hc.c:1603-1618; lib/lz4hc.h:103-110.
**Combination with target:** Used only for the streaming (`_continue`) variants, not for `LZ4_compress_HC` itself. A harness exercising both the single-shot target and the streaming entries should keep these as two separate call sequences (one `LZ4_streamHC_t*` per streaming session, freed once at the end).

### LZ4_resetStreamHC_fast(LZ4_streamHC_t* streamHCPtr, int compressionLevel)
**Call protocol:** Requires `streamHCPtr` previously produced by `LZ4_createStreamHC()` or `LZ4_initStreamHC()` (i.e., "properly initialized at least once"); if the state's internal `dirty` flag is set (prior compression failed, lib/lz4hc.c:1648-1649) it falls back to a full `LZ4_initStreamHC`. Otherwise it keeps history (`dictLimit` accounting) but clears current prefix pointers and detaches any `dictCtx`. Always sets compressionLevel via `LZ4_setCompressionLevel` (clamped to `[1(->default 9), 12]`). No return value to check (`void`).
**Evidence:** lib/lz4hc.c:1644-1658; lib/lz4hc.h:122-125,157.
**Combination with target:** Called to start a new stream of blocks (or restart after a failure) before `LZ4_loadDictHC`/`LZ4_compress_HC_continue`; not used by the single-shot `LZ4_compress_HC` path.

### LZ4_loadDictHC(LZ4_streamHC_t* streamHCPtr, const char* dictionary, int dictSize)
**Call protocol:** `streamHCPtr` must already be initialized (via create/reset) with compression level set *before* this call ("essential to set the compression level _before_ loading the dictionary" — lz4hc.h:129-130). `dictionary` buffer of `dictSize` bytes (clamped internally to max 64KB, lib/lz4hc.c:1683-1686) must remain valid/unmodified while subsequent `LZ4_compress_HC_continue` calls reference it as history. Returns the (possibly clamped) dictSize actually retained.
**Evidence:** lib/lz4hc.c:1675-1701; lib/lz4hc.h:127-130,158.
**Combination with target:** Optional step between `LZ4_createStreamHC()`/`LZ4_resetStreamHC_fast()` and `LZ4_compress_HC_continue()`; drives the `dictCtx`/external-dictionary branches in `LZ4HC_compress_generic` (lib/lz4hc.c:1527-1531, 1490-1514) which are otherwise unreachable from plain `LZ4_compress_HC`.

### LZ4_compress_HC_continue(LZ4_streamHC_t* streamHCPtr, const char* src, char* dst, int srcSize, int dstCapacity)
**Call protocol:** Requires an initialized/reset `streamHCPtr` (auto-inits if `prefixStart==NULL`, lib/lz4hc.c:1740-1741, but relying on this is not the documented pattern). Chooses `limitedOutput` vs `notLimited` internally the same way as `LZ4_compress_HC` based on `dstCapacity` vs `LZ4_compressBound(srcSize)` (lib/lz4hc.c:1774-1777). `src` for successive calls should be contiguous with the previous block's end (handles ring-buffer/overlap detection, lib/lz4hc.c:1751-1766) — previous blocks must remain accessible as implicit dictionary. Returns bytes written to `dst`, or 0 on failure (caller must then reset the stream per lz4hc.h:142-143 "In case of failure, the API does not guarantee recovery, so the state _must_ be reset").
**Evidence:** lib/lz4hc.c:1729-1778; lib/lz4hc.h:132-146; tests/fuzzer.c:958-972 (first/continuing block compress, checked with `FUZ_CHECKTEST`).
**Combination with target:** This is the streaming analogue of the target; a harness can alternate single-shot `LZ4_compress_HC` calls (fresh state each time) and `LZ4_compress_HC_continue` calls (persistent state across blocks within one `LZ4_streamHC_t` session) to broaden coverage of noDictCtx/dictCtx/usingDictCtxHc branches inside the shared `LZ4HC_compress_generic` core.

### LZ4_decompress_safe(const char* src, char* dst, int compressedSize, int dstCapacity) [from lz4.h, consumer]
**Call protocol:** Takes the compressed output and its exact length (`compressedSize`, i.e., the target's/entry's own return value) plus a `dst` buffer sized to the known/expected original length. Returns decompressed size on success or negative on error.
**Evidence:** lib/lz4.h:208; ossfuzz/compress_hc_fuzzer.c:38,53 (`LZ4_decompress_safe(dst, rt, dstSize, size)` / `LZ4_decompress_safe(dst, rt, dstSize, size)`); tests/fuzzer.c:1028 (`LZ4_decompress_safe_usingDict` variant for dictionary-based continue blocks).
**Combination with target:** Always the final consumer step in the harness after any compress call succeeds (`dstSize>0`), used to assert round-trip correctness — this is the standard "security-relevant" oracle already used by the project's own OSS-Fuzz harness.

## Build notes
**Claim:** Fuzzers are plain C, compiled against the static LZ4 library (`lib/liblz4.a`), with the HC header included via `-I../lib` (relative to `ossfuzz/`), and linked by `$(CXX)` (C++ driver linking a C object, standard libFuzzer pattern) together with `$(LIB_FUZZING_ENGINE)`.
**Evidence:**
> ossfuzz/Makefile:28: `LZ4DIR := ../lib`
> ossfuzz/Makefile:36: `LZ4_CPPFLAGS = $(CPPFLAGS) -I$(LZ4DIR) -DXXH_NAMESPACE=LZ4_ -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION` — this is how `lz4.h`/`lz4hc.h` get found (`#include "lz4hc.h"` in ossfuzz/compress_hc_fuzzer.c:15 resolves via `-I../lib`).
> ossfuzz/Makefile:56-57: `$(LZ4DIR)/liblz4.a:` rule invokes `$(MAKE) -C $(LZ4DIR) ... liblz4.a` — the static library the fuzzer links against.
> ossfuzz/Makefile:59-60: `%.o: %.c  $(CC) -c $(LZ4_CFLAGS) $(LZ4_CPPFLAGS) $< -o $@` — fuzzer source files are plain C (`.c`), compiled with `$(CC)`.
> ossfuzz/Makefile:68-69: `%_fuzzer: %_fuzzer.o lz4_helpers.o fuzz_data_producer.o $(LZ4DIR)/liblz4.a $(LIB_FUZZING_DEPS)` then `$(CXX) $(LZ4_CXXFLAGS) $(LZ4_CPPFLAGS) $(LDFLAGS) $(LIB_FUZZING_ENGINE) $^ -o $@$(EXT)` — final link step uses the C++ driver (`$(CXX)`) to combine the C object files with the fuzzing engine, standard OSS-Fuzz/libFuzzer pattern.
> ossfuzz/Makefile:44-45: `compress_hc_fuzzer` and `round_trip_hc_fuzzer` are already listed in `FUZZERS :=`, confirming `LZ4_compress_HC` already has dedicated OSS-Fuzz targets in this codebase.
> ossfuzz/ossfuzz.sh:18-20: `pushd ossfuzz; make V=1 all; popd` — invoked from build.sh via `./ossfuzz/ossfuzz.sh`.