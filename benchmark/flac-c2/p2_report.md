## target: FLAC__stream_encoder_process_interleaved

### Target definition
`flac/src/libFLAC/stream_encoder.c:2570-2620`
```c
FLAC_API FLAC__bool FLAC__stream_encoder_process_interleaved(FLAC__StreamEncoder *encoder, const FLAC__int32 buffer[], uint32_t samples)
{
	...
	channels = encoder->protected_->channels;
	blocksize = encoder->protected_->blocksize;
	sample_max = INT32_MAX >> (32 - encoder->protected_->bits_per_sample);
	sample_min = INT32_MIN >> (32 - encoder->protected_->bits_per_sample);

	if(encoder->protected_->state != FLAC__STREAM_ENCODER_OK)
		return false;

	j = k = 0;
	do {
		if(encoder->protected_->verify)
			append_to_verify_fifo_interleaved_(...);
		for(i = encoder->private_->current_sample_number; i <= blocksize && j < samples; i++, j++) {
			for(channel = 0; channel < channels; channel++){
				if(buffer[k] < sample_min || buffer[k] > sample_max){
					encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR;
					return false;
				}
				encoder->private_->threadtask[0]->integer_signal[channel][i] = buffer[k++];
			}
		}
		encoder->private_->current_sample_number = i;
		if(i > blocksize) {
			if(!process_frame_(encoder, /*is_last_block=*/false))
				return false;
			...
		}
	} while(j < samples);
	return true;
}
```

### P2.1 Init Sequence
**Claim:** Before calling the target, caller must: `FLAC__stream_encoder_new()` → optional `set_*` configuration setters (channels/bits_per_sample/blocksize/sample_rate/compression_level/verify/apodization/...) → one `FLAC__stream_encoder_init_*()` call (`init_stream`, `init_ogg_stream`, `init_FILE`, `init_ogg_file`) that must return `FLAC__STREAM_ENCODER_INIT_STATUS_OK`. The target itself asserts/checks `encoder->protected_->state == FLAC__STREAM_ENCODER_OK` and returns `false` immediately otherwise.
**Evidence:**
> flac/src/libFLAC/stream_encoder.c:2587-2588: `if(encoder->protected_->state != FLAC__STREAM_ENCODER_OK) return false;`
> flac/include/FLAC/stream_encoder.h:1853,1858: "\param encoder An initialized encoder instance in the OK state." / `\code FLAC__stream_encoder_get_state(encoder) == FLAC__STREAM_ENCODER_OK \endcode`
> flac/include/FLAC/stream_encoder.h:1787-1789 (init_ogg_file doc): "This function should be called after FLAC__stream_encoder_new() and FLAC__stream_encoder_set_*() but before FLAC__stream_encoder_process() or FLAC__stream_encoder_process_interleaved()."
> flac/src/libFLAC/stream_encoder.c:1117: `encoder->protected_->state = FLAC__STREAM_ENCODER_OK;` set inside `init_stream_internal_` only after all validations pass.
> flac/examples/c/encode/file/main.c:86-128,147: real caller sequence `FLAC__stream_encoder_new()` → set_verify/set_compression_level/set_channels/set_bits_per_sample/set_sample_rate/set_total_samples_estimate/set_metadata → `FLAC__stream_encoder_init_file()` checked against `FLAC__STREAM_ENCODER_INIT_STATUS_OK` → then `FLAC__stream_encoder_process_interleaved(encoder, pcm, need)`.

### P2.2 Parameter Construction
**Claim:** `encoder` must be a non-NULL, fully initialized `FLAC__StreamEncoder*` (from `FLAC__stream_encoder_new()` + successful init). `buffer` is a flat `const FLAC__int32[]` of length `samples*channels`, interleaved channel0_sample0, channel1_sample0, ..., channelN_sample0, channel0_sample1, ...; every value must be right-justified in range `[sample_min, sample_max]` where `sample_min=INT32_MIN>>(32-bits_per_sample)`, `sample_max=INT32_MAX>>(32-bits_per_sample)` (computed from the already-configured `bits_per_sample`). `samples` is the sample COUNT PER CHANNEL (not total array length), a `uint32_t`.
**Evidence:**
> flac/src/libFLAC/stream_encoder.c:2582-2585: `channels = encoder->protected_->channels; blocksize = ...; sample_max = INT32_MAX >> (32 - bits_per_sample); sample_min = INT32_MIN >> (32 - bits_per_sample);`
> flac/src/libFLAC/stream_encoder.c:2598-2601: out-of-range sample sets state to `FLAC__STREAM_ENCODER_CLIENT_ERROR` and returns false — no buffer write happens for the offending sample.
> flac/include/FLAC/stream_encoder.h:1882-1887: doc of `buffer`/`samples` params, "samples... the number of samples in one channel... 1000 samples corresponds to a buffer of 2000 values" (2-channel example).
> flac/oss-fuzz/encoder.cc:239-250: harness builds `dat` (FLAC__int32 vector), optionally masks values to fit `bps` (`dat[i] = (int32_t)(((uint32_t)(dat[i]) << (32-bps)) >> (32-bps));`), computes `samples = dat.size()/channels`, calls `encoder.process_interleaved(ptr, samples)` only if `samples>0`.

### P2.3 Object Lifecycle
**Claim:** create (`FLAC__stream_encoder_new`) → configure (`set_channels`, `set_bits_per_sample`, `set_sample_rate`, `set_blocksize`, `set_compression_level`, `set_verify`, `set_apodization`, etc. — all return false if encoder is already past `UNINITIALIZED`) → init (`init_stream`/`init_FILE`/`init_ogg_*`, moves state `UNINITIALIZED`→`OK` or an error state) → use (`process`/`process_interleaved`, repeatable any number of times while state==OK) → finish (`FLAC__stream_encoder_finish`, flushes last partial block, resets state back to `UNINITIALIZED`) → delete (`FLAC__stream_encoder_delete`, frees all private memory; internally calls `finish()` again safely if not already finished).
**Evidence:**
> flac/src/libFLAC/stream_encoder.c:1840-1845,1851-1856,1862-1867,1906-1910 (set_channels/set_bits_per_sample/set_sample_rate/set_blocksize): `if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED) return false;`
> flac/src/libFLAC/stream_encoder.c:723-724 (`init_stream_internal_`): `if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED) return FLAC__STREAM_ENCODER_INIT_STATUS_ALREADY_INITIALIZED;`
> flac/src/libFLAC/stream_encoder.c:1625-1647 (`finish`): if state==UNINITIALIZED, just cleans any stray metadata/file and returns true; if state==OK, flushes/finalizes and (further down, not shown) resets state to UNINITIALIZED.
> flac/src/libFLAC/stream_encoder.c:664-699 (`delete`): sets `is_being_deleted=true`, calls `FLAC__stream_encoder_finish(encoder)`, then frees verify decoder, rice-partition workspaces, bitwriter, threadtask, private_, protected_, encoder itself.
> flac/examples/c/encode/file/main.c:86-163: full lifecycle new→set_*→init_file→process_interleaved loop→finish→delete.

### P2.4 Return Value Handling
**Claim:** Target returns `FLAC__bool` (`true`=all samples accepted/encoded so far OK, `false`=error). On `false`, caller must inspect `FLAC__stream_encoder_get_state(encoder)` to learn the cause — e.g. `FLAC__STREAM_ENCODER_CLIENT_ERROR` (sample out of configured bit-depth range) or any error propagated from `process_frame_()` (memory/bitwriter/verify-mismatch errors). Once state leaves OK, the encoder must not be fed more data; only `finish()`/`delete()` are safe.
**Evidence:**
> flac/include/FLAC/stream_encoder.h:1891-1894: "\retval FLAC__bool \c true if successful, else \c false; in this case, check the encoder state with FLAC__stream_encoder_get_state() to see what went wrong."
> flac/src/libFLAC/stream_encoder.c:2598-2601: sets `FLAC__STREAM_ENCODER_CLIENT_ERROR` and returns false on out-of-range sample.
> flac/src/libFLAC/stream_encoder.c:2608-2609: `if(!process_frame_(encoder, /*is_last_block=*/false)) return false;` — any failure deep in the encode pipeline (writer/verify/alloc) propagates as `false` from the target.
> flac/include/FLAC/stream_encoder.h:237-238: "If the encoder gets into any other state besides FLAC__STREAM_ENCODER_OK or FLAC__STREAM_ENCODER_UNINITIALIZED, it becomes invalid for encoding."
> flac/oss-fuzz/encoder.cc:250-251: harness captures `const bool res = encoder.process_interleaved(ptr, samples);` and just feeds it to `fuzzing::memory::memory_test(res)` (no abort on false) — it continues the `while` loop, but subsequent calls will also fail fast via the state check at the target's top.

### P2.5 Cleanup Sequence
**Claim:** On both success and error paths the harness must call `FLAC__stream_encoder_finish(encoder)` (flushes last partial block / resets state) followed by `FLAC__stream_encoder_delete(encoder)` (frees all memory). `delete()` internally calls `finish()` again defensively, so calling `finish()` is not strictly mandatory before `delete()`, but is "good practice" per the header doc; skipping `finish()` after an error is explicitly allowed. Any `FLAC__StreamMetadata*` objects passed to `set_metadata()` are owned by the caller and must be freed separately with `FLAC__metadata_object_delete()` after `finish()`.
**Evidence:**
> flac/include/FLAC/stream_encoder.h:1822-1825: "In the event of a prematurely-terminated encode, it is not strictly necessary to call this [finish] immediately before FLAC__stream_encoder_delete() but it is good practice..."
> flac/src/libFLAC/stream_encoder.c:676-678 (`delete`): `encoder->private_->is_being_deleted = true; (void)FLAC__stream_encoder_finish(encoder);` then frees workspaces/bitwriter/structs.
> flac/examples/c/encode/file/main.c:153,159-162: `ok &= FLAC__stream_encoder_finish(encoder); ... FLAC__metadata_object_delete(metadata[0]); FLAC__metadata_object_delete(metadata[1]); FLAC__stream_encoder_delete(encoder);`
> flac/oss-fuzz/encoder.cc:257-261 (`end:` label): `const bool res = encoder.finish(); fuzzing::memory::memory_test(res);` — C++ `FLAC::Encoder::Stream` destructor (not shown here but standard in `src/libFLAC++/stream_encoder.cpp`) calls delete-equivalent.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase (grep-confirmed signatures below).
**Evidence:**
> flac/src/libFLAC/stream_encoder.c:2570 `FLAC_API FLAC__bool FLAC__stream_encoder_process_interleaved(...)`
> flac/src/libFLAC/stream_encoder.c:2513 `FLAC_API FLAC__bool FLAC__stream_encoder_process(...)`
> flac/src/libFLAC/stream_encoder.c:1442 `FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_stream(...)`
> flac/src/libFLAC/stream_encoder.c:1463 `FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_ogg_stream(...)`
> flac/src/libFLAC/stream_encoder.c:1555 `FLAC_API FLAC__StreamEncoderInitStatus FLAC__stream_encoder_init_FILE(...)`
> flac/src/libFLAC/stream_encoder.c:1840 `FLAC__stream_encoder_set_channels`, 1851 `set_bits_per_sample`, 1862 `set_sample_rate`, 1873 `set_compression_level`, 1906 `set_blocksize`, 1797 `set_verify`, 1940 `set_apodization`, 2067 `set_max_lpc_order`
> flac/src/libFLAC/stream_encoder.c:583 `FLAC__stream_encoder_new`, 664 `FLAC__stream_encoder_delete`, 1625 `FLAC__stream_encoder_finish`, 2299 `FLAC__stream_encoder_get_state`

### P2.7 Co-call Constraints
**Claim:** Paired/ordered APIs around the target:
- `new` ↔ `delete` (every `FLAC__stream_encoder_new()` must be matched by exactly one `FLAC__stream_encoder_delete()`).
- `init_*` ↔ `finish` (every successful init should be matched by a `finish()` before/within `delete()`; header explicitly recommends pairing).
- `set_*` configuration calls must all happen strictly between `new()` and `init_*()` — they are rejected (`return false`) once state != UNINITIALIZED (i.e. after a successful init). Calling them again after init is a protocol violation the library itself guards against (see `encoder.cc:180-208` where the C++ harness deliberately calls them post-init and asserts they ALL return false/abort otherwise).
- `process`/`process_interleaved` may be called any number of times, interleaved with each other, only while state==OK; no "begin/end" bracket is needed per call, but the overall sequence is bracketed by `init_*` and `finish`.
**Evidence:**
> flac/src/libFLAC/stream_encoder.c:723-724,1845-1846,1856-1857, etc.: all setters / init guard on `state != UNINITIALIZED`.
> flac/oss-fuzz/encoder.cc:180-208: "These sets must fail, because encoder is already initialized" — block of `set_*` calls after `init()`, with `if(res) abort();` validating the post-init rejection contract.
> flac/include/FLAC/stream_encoder.h:1822-1825: pairing recommendation for init_*/finish.
> flac/src/libFLAC/stream_encoder.c:664-678: `delete()` always calls `finish()` first, enforcing the pairing even if caller forgot.

### P2.8 Prerequisite State
**Claim:** Before the target can be called validly: (1) `encoder->protected_->state == FLAC__STREAM_ENCODER_OK`, set only by a successful `init_stream_internal_`/`init_FILE_internal_` call; (2) `channels`, `bits_per_sample`, `blocksize` must already be resolved to valid, non-zero values (init fills in blocksize default 1152/4096 if caller left it 0, validates channels in `[1,FLAC__MAX_CHANNELS]` and bits_per_sample in `[FLAC__MIN_BITS_PER_SAMPLE,FLAC__MAX_BITS_PER_SAMPLE]`); (3) internal per-channel `integer_signal` buffers must already be allocated to `blocksize+OVERREAD_` via `resize_buffers_()`, which happens inside `init_stream_internal_` — so the target must never be invoked before a successful init call.
**Evidence:**
> flac/src/libFLAC/stream_encoder.c:732-733: `if(encoder->protected_->channels == 0 || encoder->protected_->channels > FLAC__MAX_CHANNELS) return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_NUMBER_OF_CHANNELS;`
> flac/src/libFLAC/stream_encoder.c:742-743: bits_per_sample range check -> `FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BITS_PER_SAMPLE`.
> flac/src/libFLAC/stream_encoder.c:748-756: blocksize default-fill and range check -> `FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BLOCK_SIZE`.
> flac/src/libFLAC/stream_encoder.c:1274: `if(!resize_buffers_(encoder, encoder->protected_->blocksize)) { ... }` inside init path, allocating integer_signal buffers before state is set to OK (line 1117).
> flac/src/libFLAC/stream_encoder.c:2587-2588: target bails out immediately if state != OK, i.e. relies on the above being already done.

### P2.9 Result Usage
**Claim:** The target does not return a data object; its "result" is (a) the boolean success/failure plus encoder state, and (b) the side-effect of triggering `process_frame_()` which writes encoded frames via the configured write callback (or to the FILE given at init), and updates internal MD5/verify state. Real callers consume this by: checking the boolean per call; after the whole stream is fed, calling `FLAC__stream_encoder_finish()` (which flushes the last partial frame and returns a final boolean indicating overall success, including verify-mismatch detection); then querying `FLAC__stream_encoder_get_state()` for diagnostics. No other API takes the *return value* of process_interleaved as an input parameter — downstream consumption is via the callback-based output stream (bytes written to file/memory) and the encoder's own state, not a returned handle/buffer visible to the caller.
**Evidence:**
> flac/examples/c/encode/file/main.c:147,153,155-156: `ok = FLAC__stream_encoder_process_interleaved(...); ... ok &= FLAC__stream_encoder_finish(encoder); fprintf(stderr, "encoding: %s\n", ok? "succeeded":"FAILED"); fprintf(stderr, "   state: %s\n", FLAC__StreamEncoderStateString[FLAC__stream_encoder_get_state(encoder)]);`
> flac/oss-fuzz/encoder.cc:250-251,258-260: `const bool res = encoder.process_interleaved(ptr, samples); fuzzing::memory::memory_test(res);` then at `end:` `const bool res = encoder.finish(); fuzzing::memory::memory_test(res);`
> flac/src/libFLAC/stream_encoder.c:2608: inside the target, `process_frame_(encoder, false)` is the real consumer of the copied samples — it runs the whole encode pipeline and eventually calls the write callback (`write_bitbuffer_`) that was registered at `init_*` time; this is how encoded output actually leaves the library.

### P2.10 Behaviour Controls
**Claim:** Processing path inside/around the target is controlled by:
- `channels` (1..FLAC__MAX_CHANNELS=8) — changes inner per-sample loop width and whether mid-side stereo is even possible (only for channels==2).
- `bits_per_sample` (FLAC__MIN_BITS_PER_SAMPLE..FLAC__MAX_BITS_PER_SAMPLE) — directly sets `sample_min`/`sample_max` bounds checked in the target, and the error path (`FLAC__STREAM_ENCODER_CLIENT_ERROR`) taken for out-of-range samples.
- `blocksize` — determines the overread/flush threshold (`i > blocksize` in the target) that triggers `process_frame_()`; also validated at init vs `max_lpc_order`.
- `verify` flag — when true, the target additionally calls `append_to_verify_fifo_interleaved_()` to stash samples for round-trip decode-verification performed inside `process_frame_()`.
- `is_ogg` (chosen via which `init_*` variant is called) — selects native-FLAC vs Ogg-FLAC framing in `write_bitbuffer_`.
- `compression_level` (0-8, clamped if >= table size) — a convenience setter that fans out into `do_mid_side_stereo`, `loose_mid_side_stereo`, `apodization`, `max_lpc_order`, `qlp_coeff_precision`, `do_qlp_coeff_prec_search`, `do_escape_coding`, `do_exhaustive_model_search`, `min/max_residual_partition_order`, `rice_parameter_search_dist` — each individually overridable afterward (since all are plain `set_*` calls with the same UNINITIALIZED-only guard).
- `do_mid_side_stereo`/`loose_mid_side_stereo`, `apodization` spec string, `max_lpc_order`, `do_exhaustive_model_search`, `min/max_residual_partition_order` — select different subframe-encoding strategies evaluated in `process_subframes_`/`process_subframe_`/`evaluate_lpc_subframe_`/`evaluate_fixed_subframe_`/`apply_apodization_` (core functions, not directly in the target but reached only through it).
**Evidence:**
> flac/src/libFLAC/stream_encoder.c:2582-2588: channels/blocksize/sample_min/sample_max pulled straight from `protected_` config at top of target.
> flac/src/libFLAC/stream_encoder.c:2592-2593: `if(encoder->protected_->verify) append_to_verify_fifo_interleaved_(...)`.
> flac/src/libFLAC/stream_encoder.c:2607-2608: `if(i > blocksize) { if(!process_frame_(encoder, false)) return false; ... }`.
> flac/src/libFLAC/stream_encoder.c:735-740: `if(encoder->protected_->channels != 2) { do_mid_side_stereo=false; loose_mid_side_stereo=false; } else if(!do_mid_side_stereo) loose_mid_side_stereo=false;` (mid-side only for stereo).
> flac/src/libFLAC/stream_encoder.c:1873-1903 (`set_compression_level`): fans out into all the strategy setters listed above, from a `compression_levels_[]` table.
> flac/src/libFLAC/stream_encoder.c:726-727: `if(FLAC__HAS_OGG == 0 && is_ogg) return FLAC__STREAM_ENCODER_INIT_STATUS_UNSUPPORTED_CONTAINER;` — is_ogg path selection at init.

### P2.11 Input Expectations
**Claim:** `buffer` must be a flat interleaved array of exactly `samples * channels` valid `FLAC__int32` values, each right-justified to `bits_per_sample` (i.e. within `[INT32_MIN>>(32-bps), INT32_MAX>>(32-bps)]`); no header/magic/termination is expected inside the buffer itself — framing/headers are entirely the library's output concern, not the input's. `samples` counts PCM frames (one value per channel per sample), not raw buffer length, and need not align to `blocksize` (the target handles partial blocks across multiple calls via `current_sample_number` bookkeeping and only flushes a full block + 1 overread sample at a time); the very last partial block is NOT flushed by this function — it is flushed only by `FLAC__stream_encoder_finish()`. Samples out of the valid bit-depth range are rejected with `FLAC__STREAM_ENCODER_CLIENT_ERROR` rather than being silently clamped.
**Evidence:**
> flac/include/FLAC/stream_encoder.h:1870-1876: "The samples need not be block-aligned but they must be sample-aligned, i.e. the first value should be channel0_sample0 and the last value channelN_sampleM. Each sample should be a signed integer, right-justified to the resolution set by FLAC__stream_encoder_set_bits_per_sample()."
> flac/src/libFLAC/stream_encoder.c:2598-2601: explicit range check per sample, rejecting with `CLIENT_ERROR` state and `return false` (not clamped).
> flac/src/libFLAC/stream_encoder.c:2606: comment: "we only process if we have a full block + 1 extra sample; final block is always handled by FLAC__stream_encoder_finish()."
> flac/src/libFLAC/stream_encoder.c:2611-2612: `FLAC__ASSERT(i == blocksize+OVERREAD_); FLAC__ASSERT(OVERREAD_ == 1);` — documents the "+1 overread sample" bookkeeping invariant.
> flac/oss-fuzz/encoder.cc:241-245: harness masks fuzzed `FLAC__int32` values down to the configured `bps` before calling, consistent with the right-justified-range requirement (though the library itself also enforces it and returns false instead of crashing, per line 2598-2601).

### Other Logic-Group entries: call protocol & combination with the target

**FLAC__stream_encoder_process** (`flac/src/libFLAC/stream_encoder.c:2513`)
- Same prerequisite (`state==OK`) and same per-sample range validation, but takes `const FLAC__int32 * const buffer[]` — an array of per-channel pointers instead of interleaved data. Reaches the identical `process_frame_()` core (flac/src/libFLAC/stream_encoder.c:2550ish calls `process_frame_` too). In a harness exercising both input shapes, call this instead of/alongside the target with the same encoder lifecycle (new→set_*→init→process/process_interleaved (any mix, any order)→finish→delete). Return value handling identical to target (P2.4).
- Evidence: flac/src/libFLAC/stream_encoder.c:2513-2568 (declaration/body), flac/include/FLAC/stream_encoder.h:1864 (header declaration).

**FLAC__stream_encoder_init_stream / init_FILE / init_ogg_stream** (stream_encoder.c:1442,1463,1555)
- Must be called exactly once, after `new()`+`set_*()`, before any `process*`. Need valid non-NULL `write_callback` (stream variant) or a non-NULL `FILE*` (FILE variant); `seek_callback` requires `tell_callback` to be non-NULL too, else `FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_CALLBACKS`. Returns `FLAC__StreamEncoderInitStatus`; only `FLAC__STREAM_ENCODER_INIT_STATUS_OK` permits proceeding to `process*`. On failure, state is left in a non-OK, non-UNINITIALIZED error state (per-check) or `ALREADY_INITIALIZED`/`ENCODER_ERROR`; caller should not call `process*` and should go straight to `finish()`/`delete()` for cleanup.
- Evidence: flac/src/libFLAC/stream_encoder.c:723-762 (state/channels/bps/sample_rate/blocksize/max_lpc_order validation chain), :729-730 (callback validation), :1117 (state set to OK on success).

**FLAC__stream_encoder_set_channels / set_bits_per_sample / set_blocksize / set_compression_level** (stream_encoder.c:1840,1851,1906,1873)
- All are simple protected-field setters, valid only while `state==UNINITIALIZED` (return false otherwise — no exception/abort). `set_compression_level` additionally fans out to ~10 other setters (see P2.10) and clamps the input `value` to the largest valid table index instead of failing. These must be called before `init_*`, after `new()`; values are later validated for real by `init_stream_internal_` (channels 1..8, bps 4..32 i.e. FLAC__MIN/MAX_BITS_PER_SAMPLE, blocksize vs FLAC__MIN/MAX_BLOCK_SIZE and vs max_lpc_order).
- Evidence: stream_encoder.c:1840-1849,1851-1860,1906-1910,1873-1904; init-time validation at :732-762.

**FLAC__stream_encoder_finish** (stream_encoder.c:1625)
- Called once per encoding session, after the last `process*` call, to flush the trailing partial block (not flushed by the target itself) and finalize MD5/verify/metadata, and reset state back to `FLAC__STREAM_ENCODER_UNINITIALIZED`. Safe/no-op-ish to call even if state is already UNINITIALIZED (cleans stray metadata/file handle, returns true) — important for error-path harnesses that jump straight to cleanup without a successful init. Returns `FLAC__bool`: false means last-frame processing failed or (if verify mode) a verify mismatch was found — caller should then check `FLAC__stream_encoder_get_state()`.
- Evidence: stream_encoder.c:1625-1647 (UNINITIALIZED short-circuit), :1649+ (OK-state flush path), header doc stream_encoder.h:1811-1836.

### Build notes
**Claim:** Fuzzers for this project are plain C/C++ compiled against the in-tree `libFLAC`/`libFLAC++` headers via `-I$(top_srcdir)/include` (and `-I$(top_srcdir)/oss-fuzz` for harness-local helpers), statically linked against `libFLAC-static.la`/`libFLAC++-static.la` (+ `libogg`, `-lm`), using the OSS-Fuzz `$LIB_FUZZING_ENGINE`. The target is reachable only through the public `FLAC/stream_encoder.h` (C API) or `FLAC++/encoder.h` (C++ wrapper calling the same C symbol).
**Evidence:**
> flac/oss-fuzz/Makefile.am:18: `AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/oss-fuzz`
> flac/oss-fuzz/Makefile.am:20-22: `AM_CFLAGS = ${LIB_FUZZING_ENGINE}` / `AM_CXXFLAGS = -std=c++11 $(LIB_FUZZING_ENGINE)` / `LDADD = $(flac_libs)`
> flac/oss-fuzz/Makefile.am:37-46: `fuzzer_encoder_SOURCES = encoder.cc`, `fuzzer_encoder_v2_SOURCES = encoder_v2.cc` built under `if USE_OSSFUZZERS`.
> flac/oss-fuzz/Makefile.am:72-76: `flac_libs = $(top_builddir)/src/libFLAC/libFLAC-static.la $(top_builddir)/src/libFLAC++/libFLAC++-static.la @OGG_LIBS@ -lm`
> ossfuzz_view/flac-c2/build.sh:42-50: `./configure --with-ogg=... --enable-static --disable-shared ... --enable-oss-fuzzers; make` then (:53-54) `cd $SRC/flac/oss-fuzz; cp fuzzer_* $OUT`.
> flac/oss-fuzz/encoder.cc:31: `#include "FLAC++/encoder.h"` — the real encoder fuzzer (`encoder.cc`, building to `fuzzer_encoder`) uses the C++ wrapper whose `process_interleaved()` method (src/libFLAC++/stream_encoder.cpp:382-386) calls `::FLAC__stream_encoder_process_interleaved(encoder_, buffer, samples)` directly, confirming the public-header path used in the real/gold fuzzer for this target.
> flac/examples/c/encode/file/main.c:36-37: pure-C caller includes `"FLAC/metadata.h"` and `"FLAC/stream_encoder.h"` only — confirms the public C header is sufficient and is the intended include path for a C harness.