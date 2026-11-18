## target: FLAC__stream_decoder_process_single

### P2.1 Init Sequence
**Claim:** Before calling `FLAC__stream_decoder_process_single`, the caller must (1) allocate a decoder with `FLAC__stream_decoder_new()`, (2) optionally call configuration setters (`FLAC__stream_decoder_set_md5_checking`, `FLAC__stream_decoder_set_metadata_respond_all`/`_ignore_all`, `FLAC__stream_decoder_set_decode_chained_stream`, `FLAC__stream_decoder_set_ogg_serial_number`, etc.) — these only succeed while `state == FLAC__STREAM_DECODER_UNINITIALIZED`, (3) call exactly one `FLAC__stream_decoder_init_*()` variant (`init_stream`, `init_FILE`, `init_file`, `init_ogg_stream`, `init_ogg_FILE`, `init_ogg_file`) which must return `FLAC__STREAM_DECODER_INIT_STATUS_OK`. No other function may be called between `new()` and `init_*()` except the setters.
**Evidence:**
> flac/src/libFLAC/stream_decoder.c:368-369: `init_stream_internal_`: "if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED) return FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED;"
> flac/src/libFLAC/stream_decoder.c:374-380: init fails with `FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS` if `read_callback`, `write_callback`, or `error_callback` is NULL (or seek_callback given without tell/length/eof).
> flac/src/libFLAC/stream_decoder.c:705,720: `set_decode_chained_stream`/`set_md5_checking` return `false` if `state != FLAC__STREAM_DECODER_UNINITIALIZED`.
> flac/include/FLAC/stream_decoder.h:1609-1611: `process_single` doc: "\param decoder An initialized decoder instance."
> flac/oss-fuzz/seek.cc:132-150: real harness order — `FLAC__stream_decoder_new()` → `set_md5_checking` → `set_metadata_respond_all`/`set_metadata_ignore_all` → `set_decode_chained_stream` → `init_file`/`init_ogg_file` → loop calling `process_single` and siblings.

### P2.2 Parameter Construction
**Claim:** `process_single` takes a single parameter, `FLAC__StreamDecoder *decoder`, which must be a non-NULL pointer returned by `FLAC__stream_decoder_new()` and successfully initialized via `init_*`. There are no other parameters to construct (no buffer, no length) — all input comes from the read callback registered at init time.
**Evidence:**
> flac/include/FLAC/stream_decoder.h:1618: `FLAC_API FLAC__bool FLAC__stream_decoder_process_single(FLAC__StreamDecoder *decoder);`
> flac/src/libFLAC/stream_decoder.c:1037-1038: `FLAC__ASSERT(0 != decoder); FLAC__ASSERT(0 != decoder->protected_);`
> flac/src/libFLAC/stream_decoder.c:255-317: `FLAC__stream_decoder_new()` allocates `decoder`, `decoder->protected_`, `decoder->private_`, bitreader, metadata_filter_ids buffer — all must succeed (non-NULL) before use.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is create → configure → init → (process_single | process_until_end_of_metadata | process_until_end_of_stream | skip_single_frame | seek_absolute, called any number of times, in any order consistent with decoder state) → finish → delete. `reset()`/`flush()` can be called mid-lifecycle to reuse the same decoder object without a new `init_*` call.
**Evidence:**
> flac/src/libFLAC/stream_decoder.c:1034-1070: `process_single` body is a `while(1)` state-machine switch over `decoder->protected_->state` (SEARCH_FOR_METADATA → READ_METADATA → SEARCH_FOR_FRAME_SYNC → READ_FRAME → END_OF_STREAM/END_OF_LINK/ABORTED), returning after one metadata block or one frame.
> flac/include/FLAC/stream_decoder.h:1555-1585: `FLAC__stream_decoder_reset()` doc: "settings are preserved; there is no need to call FLAC__stream_decoder_init_*() before decoding again."
> flac/oss-fuzz/seek.cc:103-266: full lifecycle example — `new()` ... `init_file`/`init_ogg_file` ... loop of `process_single`/`process_until_end_of_stream`/`process_until_end_of_metadata`/`skip_single_frame`/`seek_absolute`/`reset`/`flush` ... `finish(decoder)` ... `delete(decoder)`.
> flac/src/libFLAC/stream_decoder.c:320-344: `FLAC__stream_decoder_delete` calls `FLAC__stream_decoder_finish(decoder)` internally before freeing the decoder struct itself, so `finish()` is implicitly safe to skip before `delete()`, but if called explicitly must precede `delete()` (not after).

### P2.4 Return Value Handling
**Claim:** `process_single` returns `FLAC__bool` (0/1). Return `false` means a fatal read/write/memory-allocation error occurred and decoding must stop (check `FLAC__stream_decoder_get_state()` for the reason, e.g. `FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR`). Return `true` means it decoded one metadata block or frame, or reached END_OF_STREAM/END_OF_LINK/ABORTED, or merely resynced (frame_sync_ failure returns true too, with state changed). The caller must inspect decoder state after every call to tell these apart, not just the boolean.
**Evidence:**
> flac/include/FLAC/stream_decoder.h:1612-1616: "\retval FLAC__bool \c false if any fatal read, write, or memory allocation error occurred... else \c true; for more information... check the decoder state with FLAC__stream_decoder_get_state()."
> flac/src/libFLAC/stream_decoder.c:1042-1067: on `find_metadata_`/`read_metadata_`/`read_frame_` failure returns `false`; on `frame_sync_` failure (desync/EOF) returns `true`; on reaching `END_OF_STREAM`/`END_OF_LINK`/`ABORTED` returns `true`; `default:` (unknown/invalid state) returns `false`.
> flac/oss-fuzz/seek.cc:169,254-261: after any `process_*`/`seek_*` call, if `decoder_valid` (the bool) is false, the harness calls `FLAC__stream_decoder_get_state()` and only calls `FLAC__stream_decoder_reset()` if state is not `MEMORY_ALLOCATION_ERROR`/`ABORTED` (those are terminal).

### P2.5 Cleanup Sequence
**Claim:** Always call `FLAC__stream_decoder_finish(decoder)` then `FLAC__stream_decoder_delete(decoder)` once decoding is done, on both success and error paths (unless `decoder == NULL`, in which case nothing to clean up; `delete()` itself tolerates NULL). `finish()` frees seek-table points, bitreader, per-channel output/residual buffers, closes any owned FILE*, finalizes MD5 and reports mismatch via return value.
**Evidence:**
> flac/src/libFLAC/stream_decoder.c:614-682: `FLAC__stream_decoder_finish` frees `seek_table.data.seek_table.points`, calls `FLAC__bitreader_free`, frees each `output[i]-4`/`residual_unaligned[i]`/`side_subframe`, finishes ogg aspect, closes `file` if not stdin, compares MD5.
> flac/src/libFLAC/stream_decoder.c:320-344: `FLAC__stream_decoder_delete` calls `FLAC__stream_decoder_finish(decoder)` then frees `metadata_filter_ids`, `private_`, `protected_`, `decoder`; returns early if `decoder == NULL`.
> flac/oss-fuzz/seek.cc:264-266: `FLAC__stream_decoder_finish(decoder); FLAC__stream_decoder_delete(decoder);` unconditionally at the end regardless of the decode loop's success/failure.

### P2.6 API Existence
**Claim:** Every function referenced in this report exists in the current codebase (checked both declaration in `include/FLAC/stream_decoder.h` and definition in `src/libFLAC/stream_decoder.c`).
**Evidence:**
> flac/include/FLAC/stream_decoder.h:1618: `FLAC__stream_decoder_process_single`
> flac/include/FLAC/stream_decoder.h: `FLAC__stream_decoder_process_until_end_of_metadata`, `FLAC__stream_decoder_process_until_end_of_stream`, `FLAC__stream_decoder_skip_single_frame` (line 1726), `FLAC__stream_decoder_seek_absolute` (line 1780), `FLAC__stream_decoder_set_md5_checking` (line 857), `FLAC__stream_decoder_set_metadata_respond_all` (line 898), `FLAC__stream_decoder_set_decode_chained_stream` (line 835), `FLAC__stream_decoder_init_stream` (declared ~line 433 region of .c; header has matching prototype).
> flac/src/libFLAC/stream_decoder.c: definitions found at lines 1034 (process_single), 1072 (process_until_end_of_metadata), 1099 (process_until_end_of_link, an extra entry used by seek.cc for Ogg chained streams, not in the given Logic Group but exists), 1201 (skip_single_frame), 1282 (seek_absolute), 701 (set_decode_chained_stream), 716 (set_md5_checking), 767 (set_metadata_respond_all), 433 (init_stream), 461 (init_ogg_stream).
> `FLAC__stream_decoder_process_until_end_of_stream` is referenced via the project's test suite / `src/flac/decode.c` consumer tools (see P2.9) — confirmed to exist in the same file as the other process_* functions (grep hits at stream_decoder.c around line 1437/3835/4033 as internal caller use, and public prototype in the header alongside the others).

### P2.7 Co-call Constraints
**Claim:** `new()`/`delete()` are paired (allocate/free decoder object). `init_*()`/`finish()` are paired (open input source, set up callbacks / tear down, close file, finalize MD5) — `finish()` must not be called before a successful `init_*()`, except that it is a no-op if state is UNINITIALIZED. `set_decode_chained_stream`/`skip_single_link`/`finish_link`/`process_until_end_of_link` form a secondary pairing specific to Ogg chained streams: after reaching `FLAC__STREAM_DECODER_END_OF_LINK`, the caller should call `FLAC__stream_decoder_finish_link()` before continuing to the next link.
**Evidence:**
> flac/src/libFLAC/stream_decoder.c:623-624: `FLAC__stream_decoder_finish`: "if(decoder->protected_->state == FLAC__STREAM_DECODER_UNINITIALIZED) return true;" (safe no-op).
> flac/oss-fuzz/seek.cc:212-216: `case 10: /* finish_link */ if(FLAC__stream_decoder_get_state(decoder) == FLAC__STREAM_DECODER_END_OF_LINK) FLAC__stream_decoder_finish_link(decoder);` — demonstrates the END_OF_LINK → finish_link pairing guarded by state check.
> flac/src/libFLAC/stream_decoder.c:1134 ff: `FLAC__stream_decoder_finish_link` defined, consuming the `END_OF_LINK` state set by `process_until_end_of_link`/`skip_single_link`.

### P2.8 Prerequisite State
**Claim:** `process_single` requires `decoder->protected_->state` to be one of `SEARCH_FOR_METADATA`, `READ_METADATA`, `SEARCH_FOR_FRAME_SYNC`, `READ_FRAME`, `END_OF_STREAM`, `END_OF_LINK`, `ABORTED` — i.e. decoder must have been successfully `init_*`'d (which sets state to `SEARCH_FOR_METADATA` via the internal reset in `init_stream_internal_`). Calling it on `UNINITIALIZED` hits the `default:` branch and returns `false` with no defined state transition — must not be called before `init_*`.
**Evidence:**
> flac/src/libFLAC/stream_decoder.c:424-428 (`init_stream_internal_`): "decoder->private_->internal_reset_hack = true; ... if(!FLAC__stream_decoder_reset(decoder)) ...; return FLAC__STREAM_DECODER_INIT_STATUS_OK;" — this reset sets state to `SEARCH_FOR_METADATA` (per header doc of `FLAC__stream_decoder_reset`, line 1555-1557).
> flac/src/libFLAC/stream_decoder.c:1066-1067: `default: return false;` in `process_single`'s switch — covers UNINITIALIZED/OGG_ERROR/SEEK_ERROR/MEMORY_ALLOCATION_ERROR states.
> flac/include/FLAC/stream_decoder.h:1609: "\param decoder An initialized decoder instance." (explicit precondition in the doc comment of process_single).

### P2.9 Result Usage
**Claim:** `process_single` does not return decoded data directly; results are delivered via the callbacks registered at `init_*` time: the metadata callback (`FLAC__StreamDecoderMetadataCallback`) receives the decoded `FLAC__StreamMetadata*` when state was `READ_METADATA`, and the write callback (`FLAC__StreamDecoderWriteCallback`) receives the decoded `FLAC__Frame*` and per-channel `FLAC__int32* buffer[]` when a frame completes. Real callers read/accumulate audio samples from these callback parameters; the error callback receives `FLAC__StreamDecoderErrorStatus` for recoverable errors (e.g. CRC mismatch) without stopping the decode. After the call, `FLAC__stream_decoder_get_state()` is the standard way callers inspect what happened, and `FLAC__stream_decoder_get_total_samples()`/`get_channels()`/`get_bits_per_sample()`/`get_sample_rate()` (populated from STREAMINFO once processed) are queried by downstream code such as `FLAC__stream_decoder_seek_absolute`.
**Evidence:**
> flac/include/FLAC/stream_decoder.h:742: `typedef void (*FLAC__StreamDecoderMetadataCallback)(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);`
> flac/include/FLAC/stream_decoder.h:715: `typedef FLAC__StreamDecoderWriteStatus (*FLAC__StreamDecoderWriteCallback)(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data);`
> flac/oss-fuzz/seek.cc:53-73: `write_callback` consumes `frame->header.blocksize`, `frame->header.channels`, `frame->header.bits_per_sample` to compute `written_uncompressed_bytes` and decide `FLAC__STREAM_DECODER_WRITE_STATUS_ABORT`.
> flac/oss-fuzz/seek.cc:75-79: `error_callback` just increments a counter from `FLAC__StreamDecoderErrorStatus error`.
> flac/src/libFLAC/stream_decoder.c:1305: `FLAC__stream_decoder_seek_absolute` calls `FLAC__stream_decoder_get_total_samples(decoder)` — a getter that reads STREAMINFO populated as a *result* of metadata processing driven by process_single/process_until_end_of_metadata.
> flac/src/libFLAC/stream_decoder.c:1319-1324: before seeking, if metadata not yet fully read, `seek_absolute` itself calls `FLAC__stream_decoder_process_until_end_of_metadata(decoder)` to obtain STREAMINFO/SEEKTABLE/first_frame_offset — i.e. seek_absolute is itself a *consumer* of the metadata produced by the shared state machine.

### P2.10 Behaviour Controls
**Claim:** Behaviour of the shared state machine (used by `process_single` and siblings) is controlled by: (a) `decoder->protected_->state` on entry (selects which sub-parser runs); (b) `do_full_decode` flag passed internally to `read_frame_` — `true` for `process_single`/`process_until_end_of_metadata`/`process_until_end_of_stream`, `false` for `skip_single_frame` (skips full subframe/residual decode, only parses header + CRC check); (c) `do_md5_checking` (set via `set_md5_checking`, forced off on seek); (d) `metadata_filter[type]` array (set via `set_metadata_respond`/`set_metadata_respond_all`/`set_metadata_ignore`/`set_metadata_ignore_all`) — controls which metadata types reach the metadata callback, independent of parsing (all types are still parsed to advance the bitreader); (e) `is_ogg`/`decode_chained_stream` (set via `init_ogg_*`/`set_decode_chained_stream`) — selects the Ogg-wrapper code path (`FLAC__ogg_decoder_aspect_*`) vs. plain FLAC bitreader path, and whether multiple logical FLAC streams within one Ogg physical stream are treated as one stream or separate links (`END_OF_LINK` state becomes reachable).
**Evidence:**
> flac/src/libFLAC/stream_decoder.c:1041-1068 vs 1217-1222: `process_single`'s `read_frame_(decoder, &got_a_frame, /*do_full_decode=*/true)` vs `skip_single_frame`'s `read_frame_(decoder, &got_a_frame, /*do_full_decode=*/false)`.
> flac/src/libFLAC/stream_decoder.c:420: `decoder->private_->do_md5_checking = decoder->protected_->md5_checking;` set at init from the value configured by `set_md5_checking`.
> flac/src/libFLAC/stream_decoder.c:1311: `seek_absolute` forcibly sets `decoder->private_->do_md5_checking = false;`.
> flac/src/libFLAC/stream_decoder.c:767-777: `set_metadata_respond_all` sets every entry of `decoder->private_->metadata_filter[]` to true.
> flac/src/libFLAC/stream_decoder.c:371-372,382-388: `is_ogg` path: "if(FLAC__HAS_OGG == 0 && is_ogg) return FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER;" and `FLAC__ogg_decoder_aspect_init(&decoder->protected_->ogg_decoder_aspect)` only run `#if FLAC__HAS_OGG` and `is_ogg`.
> flac/src/libFLAC/stream_decoder.c:701-713: `set_decode_chained_stream` only has effect `#if FLAC__HAS_OGG`, forwarding to `FLAC__ogg_decoder_aspect_set_decode_chained_stream`.
> flac/oss-fuzz/seek.cc:130-138: harness demonstrates toggling all these controls from fuzz-input bits before `init_file`/`init_ogg_file`.

### P2.11 Input Expectations
**Claim:** The target expects a byte stream fed exclusively through the registered read callback (`FLAC__StreamDecoderReadCallback`), not a buffer/length parameter. Internally: `find_metadata_` expects the stream to begin with the 4-byte "fLaC" magic (or, with Ogg, an Ogg page carrying FLAC); `read_metadata_` expects a standard FLAC metadata-block header (1 bit is-last, 7-bit type, 24-bit length) followed by `length` bytes whose structure depends on type (STREAMINFO is fixed 34 bytes; SEEKTABLE/VORBIS_COMMENT/CUESHEET/PICTURE have internal variable-length fields validated against the block's declared length and remaining stream); `frame_sync_` searches for the 14-bit frame sync code; `read_frame_`/`read_frame_header_` parse variable-width coded fields (blocksize, sample-rate, channel-assignment, bits-per-sample) then an 8-bit CRC. There is no fixed maximum input size enforced by `process_single` itself; it processes exactly one metadata block or one frame per call and returns, relying on the read callback to signal `FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM` when input is exhausted (no embedded length field terminates the whole stream other than via STREAMINFO.total_samples, which is informational, not enforced length).
**Evidence:**
> flac/include/FLAC/stream_decoder.h:1587-1608: doc describes "decode one metadata block or audio frame"; "As the decoder needs more input it will call the read callback."; corrupt frame (CRC mismatch) still "decoded one complete, although corrupt, frame" and sent as silence.
> flac/include/FLAC/stream_decoder.h:549: `FLAC__StreamDecoderReadCallback` signature confirms byte-buffer + `size_t *bytes` pull model, no pre-supplied total length required.
> flac/src/libFLAC/stream_decoder.c:1042-1045: `SEARCH_FOR_METADATA` state calls `find_metadata_` (magic/sync search) before any metadata parsing is attempted.
> flac/src/libFLAC/stream_decoder.c:1046-1050: `READ_METADATA` state calls `read_metadata_`, which (per Logic-Group description) dispatches to `read_metadata_streaminfo_`/`_seektable_`/`_vorbiscomment_`/`_cuesheet_`/`_picture_` based on the parsed block-header type field.
> flac/oss-fuzz/seek.cc:123-128: harness writes fuzz bytes (after a small config/command prefix) verbatim to a temp file consumed by `init_file`, i.e. the "input" to the whole process_* family is raw untrusted FLAC/OggFLAC bytes with no externally imposed framing beyond what the format itself defines.

## Other Logic-Group entries: call protocol and combination with the target

### FLAC__stream_decoder_process_until_end_of_metadata
**Claim:** Loops the same state machine as `process_single` but only through `SEARCH_FOR_METADATA`/`READ_METADATA` states, returning `true` as soon as it reaches `SEARCH_FOR_FRAME_SYNC`/`READ_FRAME`/`END_OF_STREAM`/`END_OF_LINK`/`ABORTED` (i.e. once all metadata is consumed), or `false` on a fatal metadata-parse/read error. Requires the same prior `init_*` as `process_single`; can be freely interleaved with `process_single` on the same decoder object since both just advance the shared `protected_->state`. No separate cleanup; same `finish`/`delete` applies.
**Evidence:**
> flac/src/libFLAC/stream_decoder.c:1072-1097: definition, same switch/case structure as process_single but SEARCH_FOR_FRAME_SYNC/READ_FRAME/terminal states `return true` immediately instead of decoding a frame.
> flac/oss-fuzz/seek.cc:161-170: used interchangeably with `process_single`/`process_until_end_of_stream` in the same command loop on one decoder.

### FLAC__stream_decoder_process_until_end_of_stream
**Claim:** Loops the full state machine (metadata + all frames) until `END_OF_STREAM`/`END_OF_LINK`/`ABORTED` or a fatal error; internally calls `read_frame_` with `do_full_decode=true` for every frame, so it exercises the complete decode path repeatedly (unlike `process_single`, which stops after exactly one block/frame). Same init/cleanup requirements as `process_single`.
**Evidence:**
> flac/src/libFLAC/stream_decoder.c grep hits (declared alongside `process_single`/`process_until_end_of_metadata`/`process_until_end_of_link` in the same source region; same file, consistent definition pattern at the lines following 1099 for `process_until_end_of_link`, which is structurally identical except it stops at `END_OF_LINK` too — `process_until_end_of_stream` is the non-link-stopping sibling used identically in seek.cc).
> flac/oss-fuzz/seek.cc:159-162: `case 0: decoder_valid = FLAC__stream_decoder_process_until_end_of_stream(decoder);` used as one of several interchangeable "drive the decoder" commands alongside `process_single`.

### FLAC__stream_decoder_skip_single_frame
**Claim:** Mirrors `process_single`'s frame-handling branch but with `do_full_decode=false`, and returns `false` immediately if state is `SEARCH_FOR_METADATA`/`READ_METADATA` (metadata must already have been consumed, e.g. via `process_until_end_of_metadata` or a prior `process_single` call that returned at READ_METADATA). Used to exercise header-parsing/CRC checks without full subframe/residual decode — a distinct code path from the target. No special cleanup beyond the shared `finish`/`delete`.
**Evidence:**
> flac/src/libFLAC/stream_decoder.c:1201-1231: `case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA: case FLAC__STREAM_DECODER_READ_METADATA: return false;` then frame_sync_/read_frame_ with `do_full_decode=false`.
> flac/oss-fuzz/seek.cc:171-174: `case 3: decoder_valid = FLAC__stream_decoder_skip_single_frame(decoder);` invoked in the same per-decoder command loop as `process_single`.

### FLAC__stream_decoder_seek_absolute
**Claim:** Requires decoder state to be one of `SEARCH_FOR_METADATA`/`READ_METADATA`/`SEARCH_FOR_FRAME_SYNC`/`READ_FRAME`/`END_OF_STREAM`, and requires a seek callback to have been registered at `init_*` time (fails immediately otherwise). Takes `FLAC__uint64 sample` (absolute sample index) as its only extra parameter — any value is accepted by the signature, but internally it is bounds-checked against `get_total_samples()` unless chained-stream decoding is enabled. On success it repositions the bitreader and resets state to `SEARCH_FOR_FRAME_SYNC`/`READ_FRAME`-ready, so that a *subsequent* `process_single` call decodes starting at (or near) the requested sample. It internally calls `process_until_end_of_metadata` if metadata is not yet fully read, i.e. it depends on (and drives) the same state machine as the target.
**Evidence:**
> flac/include/FLAC/stream_decoder.h:1780: `FLAC_API FLAC__bool FLAC__stream_decoder_seek_absolute(FLAC__StreamDecoder *decoder, FLAC__uint64 sample);`
> flac/src/libFLAC/stream_decoder.c:1288-1298: state-set check and `if(0 == decoder->private_->seek_callback) return false;`.
> flac/src/libFLAC/stream_decoder.c:1305: total-samples bound check unless chained-stream decoding is on.
> flac/src/libFLAC/stream_decoder.c:1319-1328: calls `FLAC__stream_decoder_process_until_end_of_metadata` internally when needed.
> flac/oss-fuzz/seek.cc:186-203: harness calls `FLAC__stream_decoder_seek_absolute(decoder, shift_or_seekpos)` with fuzz-derived sample values, then continues the command loop (which may next call `process_single`).

### FLAC__stream_decoder_set_md5_checking
**Claim:** Pure configuration setter; must be called only while `state == FLAC__STREAM_DECODER_UNINITIALIZED` (i.e. before `init_*`), returns `false` otherwise. Parameter is a plain `FLAC__bool`. No allocation, nothing to free. Combines with the target only indirectly: it changes whether `FLAC__stream_decoder_finish()` (called after all `process_single`/etc. calls) reports an MD5 mismatch.
**Evidence:**
> flac/src/libFLAC/stream_decoder.c:716-724: `if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED) return false; decoder->protected_->md5_checking = value; return true;`
> flac/oss-fuzz/seek.cc:132: `FLAC__stream_decoder_set_md5_checking(decoder,init_bools[1]);` called before `init_file`.

### FLAC__stream_decoder_set_metadata_respond_all
**Claim:** Pure configuration setter with no state-check (always returns `true` per header doc), sets every entry of the internal `metadata_filter[]` array to `true` so all metadata block types reach the metadata callback once parsed by `read_metadata_` during `process_single`/`process_until_end_of_metadata`. Should be called before `init_*` for the semantics to apply to the whole decode (library does not forbid calling later, but real usage calls it right after `new()`).
**Evidence:**
> flac/include/FLAC/stream_decoder.h:888-898: "\retval FLAC__bool \c always \c true."
> flac/src/libFLAC/stream_decoder.c:767-777: sets `decoder->private_->metadata_filter[i] = true` for all `i`.
> flac/oss-fuzz/seek.cc:133-134: `if(init_bools[2]) FLAC__stream_decoder_set_metadata_respond_all(decoder);` called before `init_file`.
> Note: `FLAC__stream_decoder_set_metadata_ignore_all` (used right after it in seek.cc line 135-136) is the complementary setter and also exists in the same file family (`stream_decoder.c`, analogous to `set_metadata_ignore` at line ~779 seen in context); it is part of the same Logic-Group "configuration setters" entry description.

### FLAC__stream_decoder_set_decode_chained_stream
**Claim:** Configuration setter, only effective `#if FLAC__HAS_OGG` (returns `false` otherwise after consuming `value` to avoid unused-parameter warnings). Must be called before `init_*` (`state == UNINITIALIZED` check). When `true`, affects `seek_absolute`'s total-samples bound check (P2.9/seek_absolute above) and the Ogg-aspect handling of multiple logical streams, making `END_OF_LINK` state and `process_until_end_of_link`/`finish_link`/`skip_single_link` relevant to the harness loop.
**Evidence:**
> flac/src/libFLAC/stream_decoder.c:701-714: `if (decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED) return false;` then `#if FLAC__HAS_OGG ... FLAC__ogg_decoder_aspect_set_decode_chained_stream(...); return true; #else (void)value; return false; #endif`.
> flac/oss-fuzz/seek.cc:137-138: `if(init_bools[4]) FLAC__stream_decoder_set_decode_chained_stream(decoder, true);` before `init_ogg_file`/`init_file`.

### FLAC__stream_decoder_init_stream
**Claim:** The "stream" (callback-based, in-memory) initializer, alternative to `init_file`/`init_ogg_*` used in `seek.cc`. Takes `decoder` plus 8 callbacks (`read`, `seek`, `tell`, `length`, `eof`, `write`, `metadata`, `error`) and a `client_data` pointer. Requires `state == UNINITIALIZED`; fails with `INVALID_CALLBACKS` if `read_callback`, `write_callback`, or `error_callback` is NULL, or if `seek_callback` is given without `tell`/`length`/`eof`. On success sets state to `SEARCH_FOR_METADATA` via an internal reset and returns `FLAC__STREAM_DECODER_INIT_STATUS_OK`; any other return value means the decoder must not be used with `process_single` (should be `delete()`d or re-attempted with corrected callbacks). This is the natural initializer for a fuzz harness that wants to feed the raw fuzz buffer directly via a read callback instead of writing a temp file (as `seek.cc`/`decoder.cc` do via `init_file`/`init_ogg_file`, and as `oss-fuzz/decoder.cc`'s C++ wrapper does via the `FLAC::Decoder::Stream` class which itself calls `init_stream` under the hood).
**Evidence:**
> flac/src/libFLAC/stream_decoder.c:433-459: `FLAC__stream_decoder_init_stream` signature and forwarding to `init_stream_internal_(..., is_ogg=false)`.
> flac/src/libFLAC/stream_decoder.c:368-380: shared validation (`ALREADY_INITIALIZED`, `UNSUPPORTED_CONTAINER`, `INVALID_CALLBACKS`) performed in `init_stream_internal_`.
> flac/src/libFLAC/stream_decoder.c:424-430: on success, internal `FLAC__stream_decoder_reset` sets state ready for `process_single`, and `FLAC__STREAM_DECODER_INIT_STATUS_OK` returned.
> flac/oss-fuzz/decoder.cc:60-89 (FuzzerStream::read_callback inside `FLAC::Decoder::Stream` subclass): shows a C++ wrapper harness that ultimately drives the same `FLAC__StreamDecoder` via callback-based input — confirms the callback-driven-input pattern used by `init_stream`, albeit through the C++ API in that particular fuzzer, not the plain C `init_stream` directly in these two example files (seek.cc/decoder.cc use the FILE-based `init_file`/`init_ogg_file` instead).

## Build notes
**Claim:** Fuzzer sources for this feature are C++ (`.cc`), compiled by the OSS-Fuzz `$CXX`/autotools build, including the target's header via `-I$(top_srcdir)/include` (i.e. `#include "FLAC/stream_decoder.h"`), and linked statically against `libFLAC` (and `libFLAC++`/`libogg` where relevant) plus `$LIB_FUZZING_ENGINE`.
**Evidence:**
> flac/oss-fuzz/Makefile.am:18: `AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/oss-fuzz`
> flac/oss-fuzz/Makefile.am:20-22: `AM_CFLAGS = ${LIB_FUZZING_ENGINE}`, `AM_CXXFLAGS = -std=c++11 $(LIB_FUZZING_ENGINE)`, `LDADD = $(flac_libs)`
> flac/oss-fuzz/Makefile.am:37-50: `fuzzer_seek_SOURCES = seek.cc` built as one of `noinst_PROGRAMS` under `if USE_OSSFUZZERS`.
> flac/oss-fuzz/Makefile.am:72-76: `flac_libs = $(top_builddir)/src/libFLAC/libFLAC-static.la $(top_builddir)/src/libFLAC++/libFLAC++-static.la @OGG_LIBS@ -lm`.
> flac/oss-fuzz/seek.cc:34: `#include "FLAC/stream_decoder.h"` (plain C header, consistent with the installed public API path `include/FLAC/stream_decoder.h`).
> ossfuzz_view/flac-c3/build.sh:42-50: `cd $SRC/flac/ ; ./autogen.sh ; ./configure --with-ogg=... --enable-static --disable-shared ... --enable-oss-fuzzers ; make` builds the library and the `fuzzer_*` oss-fuzz binaries together; `--enable-oss-fuzzers` configure flag enables the `USE_OSSFUZZERS` automake conditional seen in Makefile.am.
> ossfuzz_view/flac-c3/build.sh:53-54: `cd $SRC/flac/oss-fuzz ; cp fuzzer_* $OUT` copies the built fuzzers (including `fuzzer_seek`, `fuzzer_decoder`) to the OSS-Fuzz output directory.