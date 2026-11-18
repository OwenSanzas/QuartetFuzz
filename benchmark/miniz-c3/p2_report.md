## target: mz_deflate

### P2.1 Init Sequence
**Claim:** Before calling `mz_deflate(pStream, flush)`, the stream must be initialized via `mz_deflateInit()` or `mz_deflateInit2()` (which allocate and `tdefl_init()` the internal `tdefl_compressor` state and store it in `pStream->state`). `mz_deflateReset()` may be called afterward to reuse the same stream object for a fresh compression without reallocating. No other library-wide init is required.
**Evidence:**
> miniz/miniz.c:191-194: `mz_deflateInit` just forwards to `mz_deflateInit2(pStream, level, MZ_DEFLATED, MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY)`
> miniz/miniz.c:196-230: `mz_deflateInit2` allocates `tdefl_compressor` via `pStream->zalloc`, stores it in `pStream->state`, and calls `tdefl_init(pComp, NULL, NULL, comp_flags)`
> miniz/miniz.c:241,247-248: `mz_deflate` immediately checks `(!pStream) || (!pStream->state) ...` -> `MZ_STREAM_ERROR`, i.e. it requires `state` (hence Init) to have run first
> miniz/miniz.c:232-239: `mz_deflateReset` requires `pStream->state`/`zalloc`/`zfree` already set (i.e. Init already ran) and re-runs `tdefl_init` in place
> miniz/tests/miniz_tester.cpp:134-138 / examples/example3.c:134-138: real callers call `deflateInit`(`mz_deflateInit`) once before any `deflate` loop

### P2.2 Parameter Construction
**Claim:** `pStream` must be a non-NULL `mz_stream*` whose `next_in`/`avail_in` describe the input chunk and whose `next_out`/`avail_out` describe writable output space; `next_out` must be non-NULL and `avail_out` must be nonzero on every call. `flush` must be one of `MZ_NO_FLUSH(0)`, `MZ_PARTIAL_FLUSH(1)`, `MZ_SYNC_FLUSH(2)`, `MZ_FULL_FLUSH(3)`, `MZ_FINISH(4)` (checked range `0..MZ_FINISH`); `MZ_PARTIAL_FLUSH` is coerced to `MZ_SYNC_FLUSH`. `avail_in`/`avail_out` are `unsigned int` (32-bit) so `mz_compress2` rejects `source_len`/`*pDest_len` > 0xFFFFFFFF.
**Evidence:**
> miniz/miniz.c:247-250: `if ((!pStream) || (!pStream->state) || (flush < 0) || (flush > MZ_FINISH) || (!pStream->next_out)) return MZ_STREAM_ERROR; if (!pStream->avail_out) return MZ_BUF_ERROR;`
> miniz/miniz.c:252-253: `if (flush == MZ_PARTIAL_FLUSH) flush = MZ_SYNC_FLUSH;`
> miniz/miniz.h:327-347: `mz_stream` struct field types/comments (`next_in`,`avail_in`,`next_out`,`avail_out` are `unsigned int`/pointer)
> miniz/miniz.h:379-389: doc comment: "You must initialize/update the next_in, avail_in, next_out, and avail_out members"
> miniz/miniz.c:325-326: `mz_compress2` guards `(mz_uint64)(source_len | *pDest_len) > 0xFFFFFFFFU` -> `MZ_PARAM_ERROR`

### P2.3 Object Lifecycle
**Claim:** Create (`mz_deflateInit`/`mz_deflateInit2`) → optionally configure/reset (`mz_deflateReset`, or just set `next_in/avail_in/next_out/avail_out` fields directly on the struct — there is no separate "configure" API) → use (repeated `mz_deflate` calls, feeding chunks and selecting flush per call) → destroy (`mz_deflateEnd`). `mz_deflate` itself requires no "configure" call other than the struct fields being set by the caller before each invocation.
**Evidence:**
> miniz/examples/example3.c:134-189: `deflateInit` → loop of `deflate(&stream, flush)` filling `stream.avail_in`/`next_in` each turn → `deflateEnd`
> miniz/tests/large_fuzzer.c:40-74: `deflateInit` → `deflate(..., Z_NO_FLUSH)` twice → `deflate(..., Z_FINISH)` checked for `Z_STREAM_END` → `deflateEnd`
> miniz/miniz.c:318-346 (`mz_compress2`): `mz_deflateInit` → single `mz_deflate(&stream, MZ_FINISH)` → `mz_deflateEnd` on both success and failure paths

### P2.4 Return Value Handling
**Claim:** `mz_deflate` returns `MZ_OK` (more work to do / call again), `MZ_STREAM_END` (all input consumed and flushed — stream must not be fed to `mz_deflate` again), `MZ_STREAM_ERROR` (bad stream/params, `defl_status < 0` from `tdefl_compress`), or `MZ_BUF_ERROR` (no forward progress possible — need more input or more output space). Callers must check the return and stop calling once `MZ_STREAM_END`/error is seen; the doc explicitly says "Don't call mz_deflate() on the stream anymore" after `MZ_STREAM_END`. If called again after `TDEFL_STATUS_DONE` has already been reached, it returns `MZ_STREAM_END` only for `flush==MZ_FINISH`, else `MZ_BUF_ERROR`.
**Evidence:**
> miniz/miniz.h:383-388: documented return values for `mz_deflate`
> miniz/miniz.c:255-256: `if (((tdefl_compressor *)pStream->state)->m_prev_return_status == TDEFL_STATUS_DONE) return (flush == MZ_FINISH) ? MZ_STREAM_END : MZ_BUF_ERROR;`
> miniz/miniz.c:276-296: loop mapping `tdefl_status` (`defl_status < 0` → `MZ_STREAM_ERROR`; `TDEFL_STATUS_DONE` → `MZ_STREAM_END`; output-buffer-full / no-progress → `MZ_BUF_ERROR`/`MZ_OK` break conditions)
> miniz/tests/large_fuzzer.c:66-72: caller checks `err != Z_STREAM_END` and aborts
> miniz/miniz.c:337-342 (`mz_compress2`): `if (status != MZ_STREAM_END) { mz_deflateEnd(&stream); return (status == MZ_OK) ? MZ_BUF_ERROR : status; }`

### P2.5 Cleanup Sequence
**Claim:** Only one cleanup call is needed: `mz_deflateEnd(pStream)`, which frees `pStream->state` (the `tdefl_compressor`) via `pStream->zfree` and sets `state = NULL`. Must be called exactly once per successful Init, on both success and error paths, after which the `mz_stream` struct may be reused only via another Init (or discarded).
**Evidence:**
> miniz/miniz.c:299-309: `mz_deflateEnd`: `if (pStream->state) { pStream->zfree(pStream->opaque, pStream->state); pStream->state = NULL; } return MZ_OK;`
> miniz/miniz.c:225 (`mz_deflateInit2`): on `tdefl_init` failure calls `mz_deflateEnd(pStream)` before returning `MZ_PARAM_ERROR` (cleans up partially-initialized state)
> miniz/miniz.c:340 (`mz_compress2`): calls `mz_deflateEnd(&stream)` on the error path, and line 345 `return mz_deflateEnd(&stream);` on the success path — End is always reached exactly once

### P2.6 API Existence
**Claim:** All entries referenced in this report exist in the current source tree (`miniz/miniz.c`, `miniz/miniz.h`), as plain C functions declared `MINIZ_EXPORT`.
**Evidence:**
> miniz/miniz.h:367 `mz_deflateInit`; :374 `mz_deflateInit2`; :377 `mz_deflateReset`; :389 `mz_deflate`; :395 `mz_deflateEnd`; :398 `mz_deflateBound`; :402-403 `mz_compress`/`mz_compress2`
> miniz/miniz.c:191,196,232,241,299,311,318,348: matching definitions found by grep/read above

### P2.7 Co-call Constraints
**Claim:** `mz_deflateInit`/`mz_deflateInit2` is paired 1:1 with `mz_deflateEnd` (alloc/free of `pStream->state`); calling `mz_deflateEnd` twice is safe (state becomes NULL, so the `if (pStream->state)` guard no-ops) but calling `mz_deflate`/`mz_deflateReset` after `mz_deflateEnd` is invalid (state==NULL triggers `MZ_STREAM_ERROR` in `mz_deflate`, and in `mz_deflateReset` the `(!pStream->state)` check also returns `MZ_STREAM_ERROR`). `mz_deflateReset` is a drop-in replacement for an End+Init pair ("Same as calling mz_deflateEnd() followed by mz_deflateInit()/mz_deflateInit2()" per doc) but does NOT actually free/realloc — it just calls `tdefl_init` again in place, so it must be called on an already-Init'd, not-yet-End'd stream.
**Evidence:**
> miniz/miniz.h:376: "Quickly resets a compressor without having to reallocate anything. Same as calling mz_deflateEnd() followed by mz_deflateInit()/mz_deflateInit2()."
> miniz/miniz.c:234-238: `mz_deflateReset` requires `pStream->state` non-NULL, then `tdefl_init((tdefl_compressor*)pStream->state, NULL, NULL, ((tdefl_compressor*)pStream->state)->m_flags)` (reuses previous comp_flags)
> miniz/miniz.c:247-248: `mz_deflate` returns `MZ_STREAM_ERROR` if `!pStream->state` (e.g., after End)

### P2.8 Prerequisite State
**Claim:** Only the `mz_stream` struct itself is required context; no global library init, no file/network state. Caller must zero or fully set the struct (notably `zalloc`/`zfree`/`opaque`, which `mz_deflateInit2` defaults to `miniz_def_alloc_func`/`miniz_def_free_func` only if left NULL) before calling Init. `next_in`/`avail_in`/`next_out`/`avail_out` must be set by the caller before every `mz_deflate` call (they are consumed/decremented in place, not reset automatically).
**Evidence:**
> miniz/miniz.c:212-215 (`mz_deflateInit2`): `if (!pStream->zalloc) pStream->zalloc = miniz_def_alloc_func; if (!pStream->zfree) pStream->zfree = miniz_def_free_func;`
> miniz/miniz.c:321-322 (`mz_compress2`): `mz_stream stream; memset(&stream, 0, sizeof(stream));` before use — shows the expected zero-init pattern
> miniz/miniz.c:267-274: `mz_deflate` advances `pStream->next_in/avail_in/total_in` and `pStream->next_out/avail_out/total_out` in place each call — caller must refill before the next call

### P2.9 Result Usage
**Claim:** `mz_deflate`'s "result" is the data written into `[next_out_before, next_out_before+ (avail_out_before-avail_out))` and the updated `pStream->total_out`/`total_in`/`adler`. Real callers accumulate/flush this output buffer (write to file, or treat as the final compressed buffer) once per `mz_deflate` call, driven by `avail_out==0` or `status==MZ_STREAM_END`; they do NOT otherwise "parse" the output via another miniz API — the compressed bytes are meant for `mz_inflate`/`uncompress` on a separate decompression path, or written directly to storage/zip records. `mz_compress2` exposes the whole buffer by reading `stream.total_out` once `MZ_STREAM_END` is reached.
**Evidence:**
> miniz/examples/example3.c:163-173: `if ((status == Z_STREAM_END) || (!stream.avail_out)) { uint n = BUF_SIZE - stream.avail_out; fwrite(s_outbuf, 1, n, pOutfile); stream.next_out = s_outbuf; stream.avail_out = BUF_SIZE; }`
> miniz/miniz.c:344 (`mz_compress2`): `*pDest_len = stream.total_out;` after `MZ_STREAM_END`
> miniz/tests/compress_fuzzer.c:21-22: `compress2(compr, &comprLen, data, dataLen, level); uncompress(uncompr, &uncomprLen, compr, comprLen);` — output of the compress path is fed as input to the decompress path (`mz_uncompress`/`tinfl`), a cross-check round-trip pattern
> miniz/tests/miniz_tester.cpp:461-484: `num_out_bytes = cOutBufSize - zstream.avail_out; if (num_out_bytes) fwrite(out_file_buf, 1, num_out_bytes, pOutFile);`

### P2.10 Behaviour Controls
**Claim:** Processing path is selected by: (a) `flush` argument to `mz_deflate` (`MZ_NO_FLUSH`/`MZ_PARTIAL_FLUSH`→coerced `MZ_SYNC_FLUSH`/`MZ_FULL_FLUSH`/`MZ_FINISH`), each forwarded as `tdefl_flush` into `tdefl_compress`; (b) `level` [0,10] passed to `mz_deflateInit`/`mz_deflateInit2`, converted via `tdefl_create_comp_flags_from_zip_params`: level 0 forces `TDEFL_FORCE_ALL_RAW_BLOCKS`, level<=3 sets `TDEFL_GREEDY_PARSING_FLAG` (routes to `tdefl_compress_fast`), else probe-count table selects `tdefl_compress_normal`; (c) `strategy` (`MZ_DEFAULT_STRATEGY`/`MZ_FILTERED`/`MZ_HUFFMAN_ONLY`/`MZ_FIXED`/`MZ_RLE`) each sets a distinct comp_flags bit (`TDEFL_FILTER_MATCHES`, clearing `TDEFL_MAX_PROBES_MASK`, `TDEFL_FORCE_ALL_STATIC_BLOCKS`, `TDEFL_RLE_MATCHES`); (d) `window_bits` (`MZ_DEFAULT_WINDOW_BITS` vs its negation) toggles `TDEFL_WRITE_ZLIB_HEADER` (zlib header + adler32 trailer vs raw deflate), only settable via `mz_deflateInit2` (`mz_deflateInit` hardcodes positive `MZ_DEFAULT_WINDOW_BITS`).
**Evidence:**
> miniz/miniz.c:252-253: flush coercion `MZ_PARTIAL_FLUSH -> MZ_SYNC_FLUSH`
> miniz/miniz.c:266: `tdefl_compress(..., (tdefl_flush)flush)` — flush passed straight through
> miniz_tdef.c:1462-1479: `tdefl_create_comp_flags_from_zip_params` — level/strategy/window_bits → comp_flags bit decisions (quoted above)
> miniz/miniz.h:359-361: doc: "level 1 enables a specially optimized compression function ... purely for performance, not ratio"
> miniz/miniz.h:372: doc: `window_bits` must be `MZ_DEFAULT_WINDOW_BITS` (zlib header+adler) or `-MZ_DEFAULT_WINDOW_BITS` (raw)
> miniz/miniz.c:193: `mz_deflateInit` hardcodes `window_bits=MZ_DEFAULT_WINDOW_BITS, mem_level=9, strategy=MZ_DEFAULT_STRATEGY` — only `mz_deflateInit2` exposes these knobs

### P2.11 Input Expectations
**Claim:** `mz_deflate`'s input is an arbitrary byte buffer (`next_in`/`avail_in`), no required header/format/alignment/termination — it is raw bytes to compress, consumed incrementally across calls; there is no fixed size limit other than `avail_in`/`avail_out` being `unsigned int` (so a single call caps at ~4GB of bytes tracked per call, matching `mz_compress2`'s explicit 0xFFFFFFFF guard). The compressor only requires that `next_out`/`avail_out` describe a non-empty output buffer on every call, and that the caller eventually supplies `flush==MZ_FINISH` so the encoder can flush its last partial block (checked via `m_prev_return_status`/`TDEFL_STATUS_DONE` state machine). No "magic bytes" are expected in the input (unlike `mz_inflate`, which needs a valid deflate/zlib-wrapped stream) — `mz_deflate` is the producer, not the consumer, of that format.
**Evidence:**
> miniz/miniz.h:379-388: documented contract, no format requirement stated for input beyond `next_in`/`avail_in`
> miniz/miniz.c:249-250: `if (!pStream->avail_out) return MZ_BUF_ERROR;` — only requirement enforced on buffers at entry
> miniz/miniz.c:325-326: `mz_compress2`'s explicit 32-bit size guard, the only documented numeric limit
> miniz/miniz.c:255-256,281-285: `MZ_FINISH` is the trigger that lets the state machine reach `TDEFL_STATUS_DONE`/`MZ_STREAM_END`

### Build notes
**Claim:** Fuzzers for this project are built as plain C (`.c` files, compiled with `$CC $CFLAGS`), linking against the amalgamated static library `libminiz.a`, with `miniz.h` included via `-Ibuild/amalgamation` (the CMake "AMALGAMATE_SOURCES=ON" step concatenates miniz.c/miniz_tdef.c/etc. plus the auto-generated `miniz_export.h` stub that defines `MINIZ_EXPORT` to nothing). Final linking uses `$CXX $CXXFLAGS -stdlib=libc++ ... $LIB_FUZZING_ENGINE ./build/libminiz.a`, i.e. the fuzz harness source is compiled as C then the object is link-combined with libFuzzer's C++ runtime.
**Evidence:**
> miniz/tests/ossfuzz.sh:21-25: writes `miniz_export.h` defining empty `MINIZ_EXPORT`
> miniz/tests/ossfuzz.sh:29: `cmake .. -DAMALGAMATE_SOURCES=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_FUZZERS=ON -DBUILD_TESTS=OFF`
> miniz/tests/ossfuzz.sh:35-41: `for f in $(find $SRC -name '*_fuzzer.c'); do b=$(basename -s .c $f); $CC $CFLAGS -Ibuild/amalgamation $f -c -o /tmp/$b.o; $CXX $CXXFLAGS -stdlib=libc++ -Ibuild/amalgamation /tmp/$b.o -o $OUT/$b $LIB_FUZZING_ENGINE ./build/libminiz.a; done`
> ossfuzz_view/miniz-c3/build.sh:19: `$SRC/miniz/tests/ossfuzz.sh`
> ossfuzz_view/miniz-c3/Dockerfile:17,22: `FROM gcr.io/oss-fuzz-base/base-builder`, `git clone ... miniz.git`
> miniz/CMakeLists.txt:271-308: `BUILD_FUZZERS` option gates `add_executable(... _fuzzer ...)` targets for `checksum_fuzzer`, `flush_fuzzer`, `uncompress_fuzzer`, `uncompress2_fuzzer`, `compress_fuzzer`, `small_fuzzer`, `large_fuzzer`, `zip_fuzzer`, `add_in_place_fuzzer`

## Other entries of this Logic Group — call protocol and combination with mz_deflate

### mz_deflateInit(mz_streamp pStream, int level)
**Protocol:** Call first, exactly once per stream object (or after `mz_deflateEnd`/before reuse). `pStream` must point to a caller-allocated `mz_stream` (typically `memset` to 0 so `zalloc`/`zfree`/`opaque` default-initialize). `level` in `[MZ_NO_COMPRESSION(0), MZ_BEST_COMPRESSION(9 or 10)]`/`MZ_DEFAULT_COMPRESSION(-1)`; forwards to `mz_deflateInit2` with `method=MZ_DEFLATED, window_bits=MZ_DEFAULT_WINDOW_BITS(15), mem_level=9, strategy=MZ_DEFAULT_STRATEGY`.
**Returns:** `MZ_OK`, `MZ_STREAM_ERROR` (NULL stream), `MZ_PARAM_ERROR`, `MZ_MEM_ERROR` (zalloc failed).
**Frees:** nothing itself; on failure inside `mz_deflateInit2` it calls `mz_deflateEnd` to clean up a partial allocation.
**Combination:** Must precede every `mz_deflate` call in the harness; harness should check return `== MZ_OK` before proceeding, and must eventually call `mz_deflateEnd` regardless of outcome.
**Evidence:** miniz/miniz.c:191-194; miniz/miniz.h:356-367.

### mz_deflateInit2(mz_streamp pStream, int level, int method, int window_bits, int mem_level, int strategy)
**Protocol:** Alternative to `mz_deflateInit` giving full control of `method` (must be `MZ_DEFLATED`), `window_bits` (`MZ_DEFAULT_WINDOW_BITS` or its negation only — anything else is rejected), `mem_level` (`1..9`, checked but functionally ignored), `strategy` (`MZ_DEFAULT_STRATEGY/MZ_FILTERED/MZ_HUFFMAN_ONLY/MZ_FIXED/MZ_RLE`). Same allocation/lifecycle as `mz_deflateInit`.
**Returns:** `MZ_STREAM_ERROR` (NULL stream), `MZ_PARAM_ERROR` (bad method/mem_level/window_bits, or `tdefl_init` failure), `MZ_MEM_ERROR`, else `MZ_OK`.
**Frees:** nothing on success; on `tdefl_init` failure it calls `mz_deflateEnd(pStream)` itself (so the harness must not double-free if it checks for this failure and still calls End — calling End again afterward is safe since `state` is already NULL).
**Combination:** Use in place of `mz_deflateInit` when the harness wants to fuzz window_bits/strategy/mem_level in addition to level; drives different `tdefl_compress_normal`/`tdefl_compress_fast` and Huffman block paths that `mz_deflate` exercises.
**Evidence:** miniz/miniz.c:196-230; miniz/miniz.h:369-374.

### mz_deflateReset(mz_streamp pStream)
**Protocol:** Call on an already-Init'd (not yet End'd) stream to restart a fresh compression cycle in place, reusing the previously allocated `tdefl_compressor` and its `m_flags` (level/window_bits/strategy are NOT re-specified — they persist from the last Init). Requires `pStream->state`, `zalloc`, `zfree` all non-NULL (i.e. Init already succeeded and End not yet called).
**Returns:** `MZ_OK`, or `MZ_STREAM_ERROR` if preconditions fail.
**Frees:** nothing; no new allocation (it's an in-place `tdefl_init` of the existing buffer) — resets `total_in`/`total_out` to 0.
**Combination:** Useful in a harness loop that wants to drive multiple independent compress passes over the same `mz_stream`/`tdefl_compressor` allocation without paying for `mz_deflateEnd`+`mz_deflateInit` each iteration; must still be followed eventually by one `mz_deflateEnd`.
**Evidence:** miniz/miniz.c:232-239; miniz/miniz.h:376-377.

### mz_deflateEnd(mz_streamp pStream)
**Protocol:** Call exactly once to release the `tdefl_compressor` state after the harness is done with a given `mz_stream` (whether `mz_deflate` reached `MZ_STREAM_END`, hit an error, or the harness simply stops early). Safe to call on a stream whose `state` is already NULL (no-op, still returns `MZ_OK`).
**Returns:** `MZ_OK`, or `MZ_STREAM_ERROR` if `pStream` itself is NULL.
**Frees:** `pStream->state` via `pStream->zfree(pStream->opaque, pStream->state)`, then sets `state = NULL`.
**Combination:** Terminal call pairing 1:1 with `mz_deflateInit`/`mz_deflateInit2`. Must be present on every exit path of the harness (including early-return/error paths) to avoid leaking the `tdefl_compressor` allocation.
**Evidence:** miniz/miniz.c:299-309; miniz/miniz.h:391-395.

### mz_deflateBound(mz_streamp pStream, mz_ulong source_len)
**Protocol:** Pure/stateless helper — `pStream` is accepted but unused (`(void)pStream;`), can be called with `NULL` or an Init'd stream, before or independent of `mz_deflate`. Used to size an output buffer for a given `source_len` before a single-call or streaming compression.
**Returns:** an `mz_ulong` conservative upper bound (`MZ_MAX(128 + source_len*110/100, 128 + source_len + (source_len/(31*1024)+1)*5)`), not a status code.
**Frees:** nothing (no allocation performed).
**Combination:** Call before allocating the output buffer passed as `next_out`/`avail_out` to `mz_deflate`/`mz_compress2`; feeding a smaller-than-bound buffer on purpose is a valid fuzzing dimension to hit `MZ_BUF_ERROR`/partial-output loop paths in `mz_deflate`.
**Evidence:** miniz/miniz.c:311-316 (`(void)pStream;` shown); miniz/miniz.h:397-398; also used internally by `mz_compressBound` (miniz.c:353-356) which `mz_compress`/`mz_compress2` callers rely on for sizing (miniz/tests/compress_fuzzer.c:65 `compressBound(size)`, miniz/tests/flush_fuzzer.c:66 `compressBound(size)`).

### mz_compress2(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len, int level)
**Protocol:** Single-call convenience wrapper — NOT a prerequisite of `mz_deflate`, but an alternative, higher-level entry that itself performs the whole Init→deflate(MZ_FINISH)→End sequence internally on a locally-declared, zeroed `mz_stream`. Caller supplies `pDest`/`*pDest_len` (capacity, typically from `mz_deflateBound`/`mz_compressBound`), `pSource`/`source_len`, and `level`.
**Returns:** `MZ_OK` on success (with `*pDest_len` updated to actual compressed size), or any `mz_deflateInit`/`mz_deflate` error code, or `MZ_PARAM_ERROR` if `source_len|*pDest_len` exceeds 32 bits.
**Frees:** internally guarantees exactly one `mz_deflateEnd` call on every path (success at line 345, failure at line 340) — caller has nothing to free since the `mz_stream` is stack-local to `mz_compress2`.
**Combination:** If the harness chooses this entry instead of the raw `mz_deflate` loop, it should NOT call `mz_deflateInit`/`mz_deflateEnd` itself around it — `mz_compress2` owns its own stream object end-to-end. This is a separate, simpler attack surface than driving `mz_deflate` directly with attacker-controlled chunking/flush; per the task's "Logic Group" the harness is expected to exercise `mz_deflate` directly (with multi-chunk `next_in`/`avail_in`/`flush` sequences) as the primary target, using `mz_deflateInit`/`mz_deflateInit2` + `mz_deflateReset` + `mz_deflateEnd` as its lifecycle, and may optionally also call `mz_compress2`/`mz_deflateBound` as secondary single-shot variants.
**Evidence:** miniz/miniz.c:318-346; miniz/miniz.h:400-403; miniz/tests/compress_fuzzer.c:21 (`compress2(...)` = `mz_compress2` via zlib-compat macro, miniz.h:452-608 region defines `compress2`→forwards to `mz_compress2`, confirmed by zlib-compat section pattern at miniz.h:502-510 for the deflate family).

### Not part of this entry set
No other `mz_deflate*`/`mz_compress*` symbols were found beyond those listed; `mz_inflate*` and `mz_uncompress*` (the decompression counterparts) exist in the same files but belong to the separate inflate Logic Group, not to this deflate feature, and are only used by callers (e.g. `tests/compress_fuzzer.c`) as a round-trip check consuming `mz_deflate`'s output, not as part of the deflate call protocol itself.
**Evidence:** miniz/miniz.h:410-449 (separate `MINIZ_NO_INFLATE_APIS` block, `mz_inflateInit`/`mz_inflateInit2`/`mz_inflateReset`/`mz_inflate`/`mz_inflateEnd`).