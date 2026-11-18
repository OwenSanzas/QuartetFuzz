## target: FLAC::Encoder::Stream::process_interleaved

### P2.0 Main path to keep (every input) — read first
**Claim:** Subclass with write/seek/tell/metadata callbacks → setters from header bytes: `set_verify`, `set_streamable_subset`, `set_channels`, `set_bits_per_sample`, `set_sample_rate`, `set_compression_level`, optional `set_blocksize`, `set_total_samples_estimate`, `set_num_threads(1..4)`, `set_limit_min_bitrate`; fine-grained compression overrides only behind one flag bit (otherwise the level presets incl. subdivide_tukey apodization stay); PADDING + VORBIS_COMMENT metadata via `set_metadata`; `init()`/`init_ogg()`; clamped interleaved samples → `process_interleaved(all)` → `process_interleaved(part)` → `finish()` → state / verify diagnostics → delete metadata.
**Rule for the harness:** keep every step of this main path on every input, fed with the full fuzz input; Logic-Group entries and the other sections of this report only add calls on top of it (selected by spare input bits), they never replace or dilute a main-path step.
**Evidence:**
> include/FLAC++/encoder.h:130,150,151: `set_streamable_subset(bool)`, `set_limit_min_bitrate(bool)`, `set_num_threads(uint32_t)`; include/FLAC/stream_encoder.h:772,1160,1289 C counterparts.
> src/libFLAC/stream_encoder.c:2151-2170: `FLAC__stream_encoder_set_num_threads` (OK up to FLAC__STREAM_ENCODER_MAX_THREADS, must precede init); :1134-1136 thread tasks allocated when `num_threads > 1`; :3441 `if(encoder->protected_->num_threads < 2 || is_last_block)` single- vs multi-thread frame path; :3705 `process_frame_thread_inner_`.
> oss-fuzz/encoder_v2.cc:117-130 (comment at 129): project fuzzer sets `set_verify`, `set_channels`, `set_bits_per_sample`, `set_sample_rate`, `set_total_samples_estimate`, `set_limit_min_bitrate`, `set_num_threads(data[9])`, then `set_compression_level` and only conditionally the individual knobs ("Bias towards regular compression levels").
> oss-fuzz/reencoder.cc:201,261: `encoder.set_num_threads(...)`.
> src/libFLAC/stream_encoder.c:129-138: `compression_levels_[]` presets, levels 6-8 use `"subdivide_tukey(2)"`/`"subdivide_tukey(3)"`; :4293 `set_next_subdivide_tukey`.
> examples/cpp/encode/file/main.cpp:155-164: `process_interleaved` in a loop over chunks, then `finish()`, then `get_state().resolved_as_cstring(encoder)`.

### Declaration & definition
- Declared: `include/FLAC++/encoder.h:183` — `virtual bool process_interleaved(const FLAC__int32 buffer[], uint32_t samples);`
- Defined: `src/libFLAC++/stream_encoder.cpp:382-386`:
  ```
  bool Stream::process_interleaved(const FLAC__int32 buffer[], uint32_t samples)
  {
      FLAC__ASSERT(is_valid());
      return static_cast<bool>(::FLAC__stream_encoder_process_interleaved(encoder_, buffer, samples));
  }
  ```
  It is a thin wrapper that forwards straight to the C core `FLAC__stream_encoder_process_interleaved()` (`src/libFLAC/stream_encoder.c:2570`). All real validation/processing logic lives in the C core.

### P2.1 Init Sequence
**Claim:** Must call, in order: construct `Stream` (default ctor calls `FLAC__stream_encoder_new()`), check `is_valid()`, call any `set_*()` configuration methods, then call `init()` (or `init_ogg()`) and check it returned `FLAC__STREAM_ENCODER_INIT_STATUS_OK`, before any call to `process_interleaved()`/`process()`.
**Evidence:**
> src/libFLAC++/stream_encoder.cpp:55-57: `Stream::Stream(): encoder_(::FLAC__stream_encoder_new()) { }`
> include/FLAC/stream_encoder.h:1546-1548: "This function should be called after FLAC__stream_encoder_new() and FLAC__stream_encoder_set_*() but before FLAC__stream_encoder_process() or FLAC__stream_encoder_process_interleaved()."
> src/libFLAC/stream_encoder.c:2530,2587: `if(encoder->protected_->state != FLAC__STREAM_ENCODER_OK) return false;` — process/process_interleaved refuse to run unless the encoder reached the OK state, which is only set by a successful `init_stream_internal_()` (called from `init()`/`init_ogg()`).
> examples/cpp/encode/file/main.cpp:100-136: real caller sets verify/compression/channels/bps/sample_rate/total_samples_estimate/metadata, THEN calls `encoder.init(argv[2])` and checks the returned `init_status`, only afterwards calling `process_interleaved()` in a loop (lines 138-159).

### P2.2 Parameter Construction
**Claim:** `buffer` is a flat `FLAC__int32` array of length `samples * channels` holding interleaved sample frames (channel-major within each frame); `samples` is the number of sample *frames* (not raw array elements). Each sample value must fit in the configured `bits_per_sample` (checked against `sample_min`/`sample_max` computed from `bits_per_sample`), otherwise the call fails with a client error.
**Evidence:**
> src/libFLAC/stream_encoder.c:2584-2585: `sample_max = INT32_MAX >> (32 - bits_per_sample); sample_min = INT32_MIN >> (32 - bits_per_sample);`
> src/libFLAC/stream_encoder.c:2598-2601: `if(buffer[k] < sample_min || buffer[k] > sample_max){ encoder->protected_->state = FLAC__STREAM_ENCODER_CLIENT_ERROR; return false; }`
> examples/cpp/encode/file/main.cpp:52-53: `static FLAC__int32 pcm[READSIZE * 2/*channels*/];` — buffer sized `samples*channels`.
> include/FLAC++/encoder.h:183 comment: "See FLAC__stream_encoder_process_interleaved()" — same contract as the C function, documented at include/FLAC/stream_encoder.h:1896 and surrounding comments (lines ~1870-1896, not fully shown but referenced at 1122,1548,1611 etc. all pointing to the same process_interleaved semantics).

### P2.3 Object Lifecycle
**Claim:** Create (`Stream()` ctor → `FLAC__stream_encoder_new()`) → configure (`set_*`, only valid while state is `FLAC__STREAM_ENCODER_UNINITIALIZED`) → `init()`/`init_ogg()` (locks config, allocates per-channel `integer_signal` buffers) → repeatedly `process_interleaved()`/`process()` → `finish()` (flush last partial block + verify + STREAMINFO rewrite) → destroy (`~Stream()` which calls `FLAC__stream_encoder_finish()` again if not already finished, then `FLAC__stream_encoder_delete()`).
**Evidence:**
> src/libFLAC++/stream_encoder.cpp:59-65:
  ```
  Stream::~Stream() {
      if(0 != encoder_) {
          (void)::FLAC__stream_encoder_finish(encoder_);
          ::FLAC__stream_encoder_delete(encoder_);
      }
  }
  ```
> src/libFLAC/stream_encoder.c:1840-1849 / 1851-1860 / 1797-1808 (set_channels/set_bits_per_sample/set_verify): all guarded by `if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED) return false;` — setters silently fail (return false) once `init()` has succeeded.
> src/libFLAC/stream_encoder.c:723-724 (`init_stream_internal_`): `if(encoder->protected_->state != FLAC__STREAM_ENCODER_UNINITIALIZED) return FLAC__STREAM_ENCODER_INIT_STATUS_ALREADY_INITIALIZED;` — init can only be called once.
> examples/cpp/encode/file/main.cpp:161: `ok &= encoder.finish();` called once, after the process loop, before destruction/metadata cleanup.

### P2.4 Return Value Handling
**Claim:** `process_interleaved()`/`process()` return `bool` (`false` on error). On `false`, the caller must stop feeding samples and should call `get_state()` (wraps `FLAC__stream_encoder_get_state()`) to learn why (e.g. `FLAC__STREAM_ENCODER_CLIENT_ERROR` for an out-of-range sample, or a verify-decoder mismatch state), then still call `finish()`/let the destructor run for cleanup.
**Evidence:**
> src/libFLAC/stream_encoder.c:2530-2531,2587-2588: early return `false` if `state != FLAC__STREAM_ENCODER_OK`.
> src/libFLAC/stream_encoder.c:2598-2601: sets `state = FLAC__STREAM_ENCODER_CLIENT_ERROR` and returns `false` on out-of-range sample.
> examples/cpp/encode/file/main.cpp:155-164: `ok = encoder.process_interleaved(pcm, need); ... ok &= encoder.finish(); fprintf(...,"state: %s", encoder.get_state().resolved_as_cstring(encoder));` — real caller checks `ok`, still calls `finish()`, then queries state string for diagnostics.
> include/FLAC++/encoder.h:154: `State get_state() const; ///< See FLAC__stream_encoder_get_state()`.

### P2.5 Cleanup Sequence
**Claim:** On success or failure the required cleanup is: call `finish()` once (flushes/final-block + verify + metadata rewrite), free any externally-owned metadata objects passed via `set_metadata()` (they are NOT owned/freed by the encoder), then let the `Stream` destructor run (which calls `FLAC__stream_encoder_finish()` again — safe/no-op if already finished — then `FLAC__stream_encoder_delete()`).
**Evidence:**
> src/libFLAC++/stream_encoder.cpp:59-65 (dtor, shown above).
> src/libFLAC/stream_encoder.c:1629-1647 (`FLAC__stream_encoder_finish`): if state is `FLAC__STREAM_ENCODER_UNINITIALIZED` it only frees `protected_->metadata` (the array pointer copy) and closes any FILE, then returns true — i.e. safe to call multiple times / even if init failed.
> examples/cpp/encode/file/main.cpp:166-168: `FLAC__metadata_object_delete(metadata[0]); FLAC__metadata_object_delete(metadata[1]);` — caller frees metadata objects itself after `finish()`.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current source tree (verified by direct Read/Grep).
**Evidence:**
> `FLAC::Encoder::Stream::process_interleaved` — include/FLAC++/encoder.h:183; src/libFLAC++/stream_encoder.cpp:382
> `FLAC::Encoder::Stream::process` — include/FLAC++/encoder.h (next line after 183, same block); src/libFLAC++/stream_encoder.cpp:376
> `FLAC::Encoder::Stream::init` / `init_ogg` — src/libFLAC++/stream_encoder.cpp:358-368
> `FLAC::Encoder::Stream::finish` — src/libFLAC++/stream_encoder.cpp:370-374
> `FLAC::Encoder::Stream::set_channels/set_bits_per_sample/set_blocksize/set_compression_level/set_verify` — src/libFLAC++/stream_encoder.cpp:78-~230 (each a thin wrapper, grep-confirmed at file top, e.g. lines 78-100 above)
> C core: `FLAC__stream_encoder_process_interleaved` (src/libFLAC/stream_encoder.c:2570), `FLAC__stream_encoder_process` (2513), `FLAC__stream_encoder_init_stream`/`init_ogg_stream` (1442/1463), `FLAC__stream_encoder_finish` (1625), `FLAC__stream_encoder_new`/`delete` (583/664), `FLAC__stream_encoder_set_verify/channels/bits_per_sample/compression_level/blocksize` (1797,1840,1851,1873,1906).

### P2.7 Co-call Constraints
**Claim:** `init()`/`init_ogg()` is a paired "begin" operation; `finish()` is its mandatory "end"/commit counterpart — without `finish()` the last partial block, MD5, verify flush and STREAMINFO rewrite never happen. `set_*()` and `process*()` must not be interleaved with each other post-init (setters fail silently after init as shown in P2.3). `new`/`delete` (ctor/dtor in C++) are the allocate/free pair; the dtor already invokes `finish()`+`delete()`, so an explicit extra `finish()` plus letting the destructor run is the documented idiom (as in the example) and is safe (idempotent, see P2.5).
**Evidence:**
> src/libFLAC/stream_encoder.c:1646 / 1649 (`FLAC__stream_encoder_finish`): handles both "never successfully initialized" and "OK, not yet finished" states distinctly — confirms finish() is the one function that must run exactly once per successful init to flush state, but tolerates redundant calls.
> src/libFLAC++/stream_encoder.cpp:59-65: destructor unconditionally calls finish()+delete(), so "forgetting" an explicit finish() in the harness still flushes on destruction — but P4/adequacy requires the harness to call it explicitly (per Functionality description) to exercise the last-frame path deterministically before any read-back of outputs.

### P2.8 Prerequisite State
**Claim:** Before `process_interleaved()` can do useful work, the encoder object must: (1) be validly constructed (`is_valid()`==true, i.e. `encoder_ != 0`), (2) have `channels` in `[1, FLAC__MAX_CHANNELS]`, `bits_per_sample` in `[FLAC__MIN_BITS_PER_SAMPLE, FLAC__MAX_BITS_PER_SAMPLE]`, a valid `sample_rate`, and (if set) `blocksize` within `[FLAC__MIN_BLOCK_SIZE, FLAC__MAX_BLOCK_SIZE]` and `max_lpc_order <= FLAC__MAX_LPC_ORDER` and `blocksize >= max_lpc_order` — all validated inside `init_stream_internal_()` which must have returned `FLAC__STREAM_ENCODER_INIT_STATUS_OK`, and (3) a non-NULL write callback (mandatory) supplied to `init()`/`init_ogg()` (seek/tell paired: if a seek callback is given a tell callback is required too).
**Evidence:**
> src/libFLAC/stream_encoder.c:723-763 (`init_stream_internal_`): sequential validation of state/is_ogg support/callbacks/channels/bits_per_sample/sample_rate/blocksize/max_lpc_order, each returning a distinct `FLAC__STREAM_ENCODER_INIT_STATUS_*` on failure.
> src/libFLAC/stream_encoder.c:729-730: `if(0 == write_callback || (seek_callback && 0 == tell_callback)) return FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_CALLBACKS;`
> include/FLAC++/encoder.h:189: `virtual ::FLAC__StreamEncoderWriteStatus write_callback(...) = 0;` — pure virtual, the only callback the client (harness subclass) MUST override.
> src/libFLAC++/stream_encoder.cpp:361,367: `init()`/`init_ogg()` pass `write_callback_`, `seek_callback_`, `tell_callback_`, `metadata_callback_` (all static trampolines dispatching to virtuals with safe default "unsupported" implementations at lines 388-409, except write_callback which is pure virtual) plus `client_data=(void*)this`.

### P2.9 Result Usage
**Claim:** `process_interleaved()`/`process()` don't return a data object; their "result" is delivered asynchronously via the client's `write_callback()` (encoded FLAC bytes) and, at the very end, via `finish()`'s return value plus (if verify is enabled) the shadow-decoder state queryable with `get_verify_decoder_state()`/`get_verify_decoder_error_stats()`. Real callers (1) feed the buffer into `write_callback` to accumulate/stream output bytes, (2) call `finish()` once after the process loop and check its bool, (3) optionally call `get_state()`/`get_verify_decoder_state()` for diagnostics.
**Evidence:**
> examples/cpp/encode/file/main.cpp:155,161,164: `encoder.process_interleaved(pcm, need); ... encoder.finish(); ... encoder.get_state().resolved_as_cstring(encoder);` — the canonical post-processing chain.
> include/FLAC++/encoder.h:155-156: `virtual Decoder::Stream::State get_verify_decoder_state() const; virtual void get_verify_decoder_error_stats(...)` — exist specifically to inspect the verify round-trip result after processing/finish.
> src/test_libFLAC++/encoders.cpp:522-523 (test suite usage): `if(!encoder->process_interleaved(samples, sizeof(samples)/sizeof(FLAC__int32))) ...` followed (pattern seen generally in this test file) by state checks and eventual `finish()`.
> oss-fuzz/encoder_v2.cc:46-50, write_callback is a no-op stub (`return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;`) showing the minimal valid callback a fuzz harness may provide — still mandatory to be non-NULL.

### P2.10 Behaviour Controls
**Claim:** Processing path is selected by configuration set before `init()`:
- `set_channels` (1..FLAC__MAX_CHANNELS) changes per-channel loop bounds and whether mid-side stereo is even considered (forced off if channels != 2, src/libFLAC/stream_encoder.c:735-738).
- `set_bits_per_sample` changes `sample_min`/`sample_max` bounds checked in process_interleaved (lines 2584-2585,2598-2601) — directly controls which sample values are accepted vs. rejected as CLIENT_ERROR.
- `set_blocksize` (0 = auto: 1152 if max_lpc_order==0 else 4096, lines 748-752) controls how many interleaved frames accumulate before `process_frame_()` fires (the `+OVERREAD_` logic at lines 2555-2564, 2607-2616).
- `set_compression_level` (0-8) is a convenience that sets many of do_mid_side_stereo/loose_mid_side_stereo/apodization/max_lpc_order/etc at once (src/libFLAC/stream_encoder.c:1873-1905-ish, clamped to table size at 1881-1882).
- `set_verify(true)` enables `append_to_verify_fifo_interleaved_()` (line 2592-2593) and the shadow-decoder check inside `process_frame_()`/`finish()`.
- `init()` vs `init_ogg()` selects native-FLAC vs Ogg-wrapped write path (`is_ogg` flag threaded through `init_stream_internal_`, line 726-727 rejects ogg if unsupported build).
**Evidence:** (as cited above, file:line references repeated)
> src/libFLAC/stream_encoder.c:735-740, 748-756, 1881-1882, 2584-2601, 726-727.

**Additional behaviour controls (previously missing):**
- `set_num_threads(n)` (n>1 → multithreaded frame encoding, stream_encoder.c:1134, 3441-3552, 3705).
- `set_streamable_subset(bool)` and `set_limit_min_bitrate(bool)` (encoder.h:130,150).
- `set_compression_level` presets vs. explicit overrides: any explicit `set_apodization`/LPC/partition setter after the level replaces the preset; keep presets on most inputs and override only behind one flag bit (oss-fuzz/encoder_v2.cc 'bias towards regular compression levels').
- Two `process_interleaved` calls on the same encoder (multi-call accumulation across block boundaries) rather than a single call.

### P2.11 Input Expectations
**Claim:** `buffer` must contain exactly `samples` frames × `channels` interleaved `FLAC__int32` values (no header/terminator — raw PCM samples), each value representable in the configured `bits_per_sample` (else rejected). `samples` may be 0 (loop body simply doesn't execute useful work — `do { } while(j < samples)` with `j=k=0` still executes once per call even for samples=0, since it's a do-while; this is a candidate edge case), any size is accepted across multiple calls (internal accumulation handles partial blocks via `current_sample_number`), and the very last partial block is only flushed by `finish()`, not by `process_interleaved()` itself.
**Evidence:**
> src/libFLAC/stream_encoder.c:2590-2617 (do-while loop body of process_interleaved, shown above) — note it is a `do { ... } while(j < samples)` loop, meaning even `samples==0` executes the loop body once with the inner `for` bounded by `j < samples` (so inner work is skipped, but the verify-fifo call and other boundary code at top of loop still execute once) — worth the harness fuzzing `samples=0`.
> src/libFLAC/stream_encoder.c:2606: "/* we only process if we have a full block + 1 extra sample; final block is always handled by FLAC__stream_encoder_finish() */" — explicit comment confirming finish() is required to flush remainder.
> include/FLAC/stream_encoder.h:111,178: module docs describing process_interleaved as the "pass a single array...properly interleaved" variant.

## Other entries of the Logic Group (how they combine with the target in one harness)

### FLAC::Encoder::Stream::process (variant, non-interleaved)
**Call protocol:** Same preconditions as `process_interleaved` (P2.1/P2.8). Parameter differs: `const FLAC__int32 * const buffer[]` — an array of `channels` pointers, each pointing to `samples` per-channel samples (not flat/interleaved). Internally reuses the exact same `process_frame_()`/overread bookkeeping (src/libFLAC/stream_encoder.c:2513-2568 vs 2570-2620 — structurally parallel). Returns `bool`, same error semantics (CLIENT_ERROR on out-of-range sample at line 2544-2547, or NULL channel pointer check at 2540-2542 — a check absent from the interleaved variant). **Combine with target:** a harness should pick exactly one of `process`/`process_interleaved` per fuzz run (both exercise `process_frame_()`, differing mainly in de-interleave bookkeeping and the extra NULL-channel-pointer check); mixing both on the same encoder instance within one call sequence is legal (both check `current_sample_number`/state the same way) but doubles surface without much added value — oss-fuzz's own `encoder_v2.cc:65,113` picks one path based on an `interleaved` flag bit from fuzz data.
**Evidence:** src/libFLAC/stream_encoder.c:2513-2568; include/FLAC++/encoder.h (process is declared directly before process_interleaved in the same block, line ~182); src/libFLAC++/stream_encoder.cpp:376-380.

### FLAC::Encoder::Stream::init / init_ogg
**Call protocol:** Needs an already-constructed, valid (`is_valid()==true`) `Stream` with all desired `set_*()` configuration applied beforehand (setters fail once initialized — P2.3). Caller must have overridden the pure-virtual `write_callback()`; `init_ogg()` additionally requires `read_callback()` to be meaningfully overridden only if resync/sync is needed (default returns UNSUPPORTED, acceptable for pure encode). Returns `::FLAC__StreamEncoderInitStatus` (NOT bool) — caller MUST check `== FLAC__STREAM_ENCODER_INIT_STATUS_OK` before calling `process*()`; any other value means the encoder is in an error state and `process_interleaved()` will immediately return false (state != OK, lines 2530/2587). `init_ogg()` additionally fails with `FLAC__STREAM_ENCODER_INIT_STATUS_UNSUPPORTED_CONTAINER` if built without Ogg support (line 726-727).
**Combine with target:** exactly one of `init()`/`init_ogg()` is called once, right after configuration setters and right before the `process_interleaved()`/`process()` loop; selecting `init_ogg()` vs `init()` is a good fuzzing axis for exercising the Ogg packet-wrapping write path mentioned in the task description.
**Evidence:** src/libFLAC++/stream_encoder.cpp:358-368; src/libFLAC/stream_encoder.c:1442-1483 (both delegate to `init_stream_internal_`); src/libFLAC/stream_encoder.c:723-730 (shared validation, is_ogg branch at 726-727).

### FLAC::Encoder::Stream::finish
**Call protocol:** Takes no parameters beyond `this`; requires a valid `Stream` (asserted via `FLAC__ASSERT(is_valid())` at src/libFLAC++/stream_encoder.cpp:372). Safe to call even if `init()` was never called or failed (handles `FLAC__STREAM_ENCODER_UNINITIALIZED` gracefully — frees metadata array copy / closes FILE if any, returns true, src/libFLAC/stream_encoder.c:1635-1647). If state is OK, it forces processing of the trailing partial block(s) with `is_last_block=true`, finishes any worker threads, runs verify catch-up, and (if seeking supported) rewrites STREAMINFO. Returns `bool` — `false` indicates a write/verify/seek failure during flush; caller should still proceed to destroy the object (destructor's own `finish()` call will be a no-op second call).
**Combine with target:** MUST be called exactly once after the process loop (success or failure) to flush the last block — calling `finish()` is the only way to encode a non-block-aligned total sample count, which is central to the task description's call for testing "spanning many blocks... overread/blocksize wraparound".
**Evidence:** src/libFLAC++/stream_encoder.cpp:370-374; src/libFLAC/stream_encoder.c:1625-1647 and surrounding (threads/verify flush, partially shown at 1649-1724); examples/cpp/encode/file/main.cpp:161.

### FLAC::Encoder::Stream::set_compression_level / set_channels / set_bits_per_sample / set_blocksize / set_verify
**Call protocol (all five):** Each requires `is_valid()==true` and `state == FLAC__STREAM_ENCODER_UNINITIALIZED` (i.e., must be called before `init()`/`init_ogg()`; calling after init silently returns `false` and has NO EFFECT — does not error out loudly). Each returns `bool` (`true`=accepted, `false`=rejected because already initialized). None of them validate the numeric range themselves (e.g. `set_channels(999)` returns `true` here but is only rejected later, at `init()`, with `FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_NUMBER_OF_CHANNELS`) — so range validation is deferred to `init()`.
- `set_compression_level(0..8)`: internally calls several other setters (`set_do_mid_side_stereo`, `set_apodization`, `set_max_lpc_order`, etc. — src/libFLAC/stream_encoder.c:1873-1905ish) — out-of-range values are clamped to the table size (lines 1881-1882), not rejected.
- `set_channels/set_bits_per_sample/set_blocksize`: store raw value; out-of-range caught at `init()` time (P2.8 evidence).
- `set_verify(true)`: enables shadow-decoder round-trip checking inside `process_frame_()`/`finish()`; if the library was built with `FLAC__MANDATORY_VERIFY_WHILE_ENCODING`, the setter is a silent no-op (protected_->verify untouched, still returns true) — src/libFLAC/stream_encoder.c:1804-1807.
**Combine with target:** all five (plus any other `set_*`) must be called, in any order among themselves, strictly before `init()`/`init_ogg()`, and never again afterward; they directly determine which code paths `process_interleaved()`/`process_frame_()` take (P2.10).
**Evidence:** src/libFLAC/stream_encoder.c:1797-1808 (set_verify), 1840-1849 (set_channels), 1851-1860 (set_bits_per_sample), 1873-1905 (set_compression_level), 1906+ (set_blocksize, grep-confirmed present); src/libFLAC++/stream_encoder.cpp:78-100 (C++ wrappers, each `FLAC__ASSERT(is_valid())` then delegate 1:1 to the C setter).

## Build notes
**Claim:** Fuzz harnesses for this project are built as C++ (compiled with `$CXX`), linking the static `libFLAC++.a` and `libFLAC.a` archives (plus `libogg.a`), with the target's header reachable via `-I $SRC/flac/include/`. The existing gold/stub harness for this target (`oss-fuzz/encoder.cc` under the investigated fuzzer dir, distinct from the project's own `oss-fuzz/` directory) references the C++ mangled symbol directly, confirming the target symbol is `FLAC::Encoder::Stream::process_interleaved(FLAC__int32 const*, unsigned int)`.
**Evidence:**
> ${QF_WORK}/src/flac-c1/build.sh:58-59:
  ```
  $CXX $CXXFLAGS -I $SRC/flac/oss-fuzz/ -I $SRC/flac/include/ ... fuzzer_exo.cpp \
      $SRC/flac/src/libFLAC++/.libs/libFLAC++.a $SRC/flac/src/libFLAC/.libs/libFLAC.a $SRC/libogg-install/lib/libogg.a $LIB_FUZZING_ENGINE -o $OUT/fuzzer_exo
  ```
  — shows C++ compilation, `include/` as the header search path for `FLAC++/encoder.h` / `FLAC/stream_encoder.h`, static linking against `libFLAC++.a` (which contains the target) and its dependency `libFLAC.a`, plus `libogg.a` for Ogg support (needed if `init_ogg()` is exercised).
> ${QF_WORK}/src/flac-c1/build.sh:42-50: `./autogen.sh && ./configure --with-ogg=... --enable-static --disable-shared ... --enable-oss-fuzzers && make` — libFLAC/libFLAC++ are built as static archives with the `--enable-oss-fuzzers` configure flag (enables fuzzer-friendly instrumentation hooks like `alloc_check_threshold`, seen used in `oss-fuzz/encoder_v2.cc:75-76`).
> ${QF_WORK}/src/flac-c1/oss-fuzz/encoder.cc:10: `__asm__("_ZN4FLAC7Encoder6Stream19process_interleavedEPKij")` — the mangled name for `FLAC::Encoder::Stream::process_interleaved(int const*, unsigned int)`, confirming C++ symbol/linkage and that this harness file is compiled as C++ against the FLAC++ namespace.
> ${QF_WORK}/src/flac-c1/flac/oss-fuzz/encoder_v2.cc:34-38: the project's own, richer C-level encoder fuzzer includes `"FLAC/stream_encoder.h"` directly (no FLAC++), illustrating the alternative plain-C entry `FLAC__stream_encoder_process_interleaved` compiled under a C++ compiler per project convention ("This C++ fuzzer uses the FLAC and not FLAC++ because the latter lacks a few hidden functions...").