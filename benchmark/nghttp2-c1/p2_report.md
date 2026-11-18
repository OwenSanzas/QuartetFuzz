## target: nghttp2_session_mem_recv2

### P2.1 Init Sequence
**Claim:** A `nghttp2_session` object must exist (created by `nghttp2_session_server_new`/`nghttp2_session_client_new` family) before calling `nghttp2_session_mem_recv2`. Callback table (`nghttp2_session_callbacks`) must be built and installed via those constructors beforehand; the callbacks object itself can be deleted immediately after session creation (its contents are copied). No other global init is required.
**Evidence:**
> fuzz/fuzz_target.cc:55-73: `nghttp2_session_callbacks_new(&callbacks); ... nghttp2_session_server_new(&session, callbacks, nullptr); nghttp2_session_callbacks_del(callbacks); nghttp2_submit_settings(...); send_pending(session); nghttp2_session_mem_recv2(session, data, size);`
> lib/nghttp2_session.c:625: `(*session_ptr)->callbacks = *callbacks;` (session_new copies callback struct, so callbacks object can be freed right after construction)
> lib/nghttp2_session.c:7116-7131: `nghttp2_session_mem_recv2` dereferences `session` directly (`session_mem_recv(session, in, inlen)`), so `session != NULL` and fully constructed is a precondition.

### P2.2 Parameter Construction
**Claim:** `session` is a non-NULL, previously-constructed `nghttp2_session*`. `in` is a pointer to a buffer of raw (possibly partial/fragmented) bytes from the peer; may be `NULL` only if `inlen == 0` (asserted internally). `inlen` is `size_t`, the number of valid bytes in `in`; any size including 0 is accepted, and input can be split arbitrarily across repeated calls (resumable state machine).
**Evidence:**
> lib/includes/nghttp2/nghttp2.h:3707-3708: "Processes data |in| as an input from the remote endpoint. The |inlen| indicates the number of bytes to receive in the |in|."
> lib/nghttp2_session.c:5445-5448: `if (in == NULL) { assert(inlen == 0); in = static_in; }` — internal `session_mem_recv` tolerates NULL only when inlen is 0.
> tests/nghttp2_session_test.c:1507: `rv = nghttp2_session_mem_recv2(session, &data[i], 1);` — shows the API is routinely fed one byte at a time to test resumable parsing.

### P2.3 Object Lifecycle
**Claim:** Create session (server_new/client_new/*_new2/*_new3) → optionally submit initial SETTINGS/configure via submit_* APIs → flush with `nghttp2_session_send`/`nghttp2_session_mem_send2` → repeatedly call `nghttp2_session_mem_recv2` with inbound bytes (interleaved with sends to flush any reactive frames: SETTINGS ACK, WINDOW_UPDATE, GOAWAY, RST_STREAM) → `nghttp2_session_del` once at the end.
**Evidence:**
> fuzz/fuzz_target.cc:67-77: server_new → submit_settings → send_pending (mem_send2 loop) → mem_recv2(data,size) → send_pending again → `nghttp2_session_del(session)`.
> lib/nghttp2_session.c:7133-7153: `nghttp2_session_recv` shows the canonical driving loop: read via recv callback, then call `nghttp2_session_mem_recv2`, repeat until EOF/WOULDBLOCK.
> lib/nghttp2_session.c:798-834: `nghttp2_session_del` frees `hd_deflater`, `hd_inflater`, streams map, outbound queues, aob framebufs, and finally the session struct itself — the only destructor and must be the last call on the session.

### P2.4 Return Value Handling
**Claim:** Return value is `nghttp2_ssize` (signed). `>= 0` is the number of bytes consumed from `in` (may be less than `inlen` if `NGHTTP2_ERR_PAUSE` was triggered by a header/data-chunk callback). `< 0` is an error code: `NGHTTP2_ERR_NOMEM`, `NGHTTP2_ERR_CALLBACK_FAILURE`, `NGHTTP2_ERR_BAD_CLIENT_MAGIC` (server session failed to see the HTTP/2 client preface), or `NGHTTP2_ERR_FLOODED` (checked after parsing, via outbound queue size vs `session->max_outbound_queue_size`). Fatal errors (checked with `nghttp2_is_fatal`) should terminate use of the session (only `nghttp2_session_del` is safe afterward); `NGHTTP2_ERR_FLOODED`/`BAD_CLIENT_MAGIC` are non-fatal-but-terminal per docs ("must be closed").
**Evidence:**
> lib/nghttp2_session.c:7116-7131:
> ```
> nread = session_mem_recv(session, in, inlen);
> if (nread < 0 && nghttp2_is_fatal((int)nread)) { return nread; }
> if (nghttp2_session_get_outbound_queue_size(session) > session->max_outbound_queue_size) {
>   return NGHTTP2_ERR_FLOODED;
> }
> return nread;
> ```
> lib/includes/nghttp2/nghttp2.h:3725-3739 (doc block): lists NGHTTP2_ERR_NOMEM, NGHTTP2_ERR_CALLBACK_FAILURE, NGHTTP2_ERR_BAD_CLIENT_MAGIC, NGHTTP2_ERR_FLOODED as possible returns, with the flooded case noting "session... must be closed."
> src/shrpx_http2_session.cc:1777-1779 and src/nghttp.cc:1099-1102: real callers check `rv < 0` and log/terminate the connection on error, never retry past a negative return.

### P2.5 Cleanup Sequence
**Claim:** On both success and error paths the only cleanup is a single `nghttp2_session_del(session)` call; there is nothing specific to free for the `in`/`inlen` parameters (caller owns that buffer). If `nghttp2_session_callbacks_new` was used, `nghttp2_session_callbacks_del(callbacks)` should be called once right after the `*_new` call (callbacks struct is copied into the session, not referenced afterward).
**Evidence:**
> fuzz/fuzz_target.cc:68: `nghttp2_session_callbacks_del(callbacks);` immediately after `nghttp2_session_server_new`.
> fuzz/fuzz_target.cc:76: `nghttp2_session_del(session);` once, after all recv/send activity, regardless of whether mem_recv2 or mem_send2 reported errors in between.
> lib/nghttp2_session.c:798-834: `nghttp2_session_del` is idempotent-safe against NULL (`if (session == NULL) return;`) and frees every allocation owned by the session (HPACK (de)compressors, streams, outbound queues, frame buffers).

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited locations.
**Evidence:**
> lib/nghttp2_session.c:7116 `nghttp2_session_mem_recv2`
> lib/nghttp2_session.c:698 `nghttp2_session_server_new`, :705 `nghttp2_session_server_new2`, :712 `nghttp2_session_server_new3`
> lib/nghttp2_session.c:664 `nghttp2_session_client_new`, :671 `_new2`, :678 `_new3`
> lib/nghttp2_submit.c:217 `nghttp2_submit_settings`
> lib/nghttp2_session.c:7927 `nghttp2_session_upgrade2` (and :7834 `nghttp2_session_upgrade_internal`, :7900 `nghttp2_session_upgrade` deprecated variant)
> lib/nghttp2_session.c:7976 `nghttp2_session_consume`
> lib/nghttp2_session.c:3135 `nghttp2_session_mem_send2`
> lib/nghttp2_session.c:7133 `nghttp2_session_recv`
> lib/nghttp2_session.c:798 `nghttp2_session_del`
> lib/nghttp2_callbacks.c:29 `nghttp2_session_callbacks_new`, :39 `nghttp2_session_callbacks_del`

### P2.7 Co-call Constraints
**Claim:** (1) `nghttp2_session_callbacks_new`/`nghttp2_session_callbacks_del` are paired around session construction. (2) `nghttp2_session_*_new*` must be paired with exactly one terminal `nghttp2_session_del`. (3) `nghttp2_session_consume`/`nghttp2_session_consume_connection`/`nghttp2_session_consume_stream` are only valid when the session was created with option `NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE` set (otherwise they return `NGHTTP2_ERR_INVALID_STATE`) — i.e. they are paired with an explicit option flag, not freely callable. (4) `nghttp2_session_mem_recv2` and `nghttp2_session_mem_send2`/`nghttp2_session_send` are conventionally interleaved: after feeding input, the harness should drain pending output frames generated as a reaction (SETTINGS ACK, WINDOW_UPDATE, PING ACK, GOAWAY) before possibly feeding more input or deleting the session.
**Evidence:**
> lib/nghttp2_session.c:7985-7987: `if (!(session->opt_flags & NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE)) { return NGHTTP2_ERR_INVALID_STATE; }` inside `nghttp2_session_consume`.
> fuzz/fuzz_target.cc:72-74: `send_pending(session); nghttp2_session_mem_recv2(session, data, size); send_pending(session);` — recv call bracketed by send-drain calls.
> src/shrpx_http2_upstream.cc:1139-1148: after `nghttp2_session_mem_recv2`, code checks return and then typically triggers `nghttp2_session_send`/equivalent write-back in the surrounding event loop (comment at :1148 notes it "should consume all input bytes").

### P2.8 Prerequisite State
**Claim:** Session must know whether it is server or client (fixed at construction, cannot change later) because that determines the very first inbound state: server sessions expect the 24-byte HTTP/2 client connection preface (`NGHTTP2_IB_READ_CLIENT_MAGIC`) unless `nghttp2_option_set_no_recv_client_magic` was used; client sessions start directly at `NGHTTP2_IB_READ_FIRST_SETTINGS` and must have already queued/sent their own preface+SETTINGS (handled internally via `aob.state = NGHTTP2_OB_SEND_CLIENT_MAGIC`). `session->local_settings` / `session->max_outbound_queue_size` (configured via options or defaults at `session_new`) constrain what the parser accepts (frame size limits, flood threshold).
**Evidence:**
> lib/nghttp2_session.c:630-646: at construction, server sessions with magic-checking enabled get `iframe->state = NGHTTP2_IB_READ_CLIENT_MAGIC; iframe->payloadleft = NGHTTP2_CLIENT_MAGIC_LEN;`, else `NGHTTP2_IB_READ_FIRST_SETTINGS`; client (!server) sessions queue `NGHTTP2_CLIENT_MAGIC` bytes to send.
> lib/nghttp2_session.c:5464-5479 (`session_mem_recv`, case `NGHTTP2_IB_READ_CLIENT_MAGIC`): compares input against `NGHTTP2_CLIENT_MAGIC`, returns `NGHTTP2_ERR_BAD_CLIENT_MAGIC` on mismatch.
> lib/nghttp2_session.c:584-586: `NGHTTP2_OPT_MAX_OUTBOUND_QUEUE_SIZE` option sets `session->max_outbound_queue_size`, which `nghttp2_session_mem_recv2` checks post-parse (line 7125-7127) to decide flooding.

### P2.9 Result Usage
**Claim:** The target does not return a new object; it mutates `session`'s internal state (streams, inflight settings, HPACK dynamic tables, outbound queues) and invokes the user-supplied callbacks (on_frame_recv, on_header, on_data_chunk_recv, etc.) as a side effect. Real callers consume the *return value* (bytes consumed / error code) to decide whether to keep feeding more bytes, and consume the *side effects* by calling `nghttp2_session_mem_send2`/`nghttp2_session_send` to flush reactive frames, and `nghttp2_session_want_read`/`nghttp2_session_want_write` to decide on further I/O. `nghttp2_session_consume*` is used afterward (only with auto-window-update disabled) to explicitly release flow-control credit for bytes delivered via `on_data_chunk_recv_callback`.
**Evidence:**
> fuzz/fuzz_target.cc:40-48,73-74: `send_pending` loops `nghttp2_session_mem_send2(session, &data)` until it returns 0, called right after `nghttp2_session_mem_recv2`.
> src/shrpx_http2_session.cc:1777-1779: `auto rv = nghttp2_session_mem_recv2(...); if (rv < 0) { Log{ERROR,this} << ...; }` followed (elsewhere in same event loop, not shown here) by a write-back phase.
> examples/libevent-server.c:280-289: comment "Invocation of nghttp2_session_mem_recv2() may make ... outbound data" then `readlen = nghttp2_session_mem_recv2(...)`, followed in that file by triggering the write watcher to flush pending frames.
> lib/nghttp2_session.c:7976-8008 (`nghttp2_session_consume`): reads data produced by frame parsing and feeds it into `session_update_connection_consumed_size`/`session_update_stream_consumed_size`, which (not shown) queue WINDOW_UPDATE frames consumed by a later `nghttp2_session_mem_send2`.

### P2.10 Behaviour Controls
**Claim:** Multiple inputs select different internal processing paths: (a) server-vs-client flag at session construction selects preface handling; (b) `nghttp2_option_set_no_recv_client_magic` skips preface validation; (c) `NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE` gates whether `nghttp2_session_consume*` is legal/necessary; (d) `max_outbound_queue_size` option controls the flood threshold enforced right after `session_mem_recv`; (e) within the byte stream itself: frame type byte (`iframe->sbuf.pos[3]`), flags byte (END_STREAM/END_HEADERS/PADDED/PRIORITY/ACK), 24-bit length field vs negotiated `local_settings`/`remote_settings` max_frame_size, and HPACK dynamic-table-size-update bytes, all select different switch branches inside `session_mem_recv`'s `iframe->state` state machine; (f) `nghttp2_submit_settings` values (e.g. `NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS`, `INITIAL_WINDOW_SIZE`, `MAX_FRAME_SIZE`) change the constraints the inbound parser enforces on subsequent frames (checked in `nghttp2_session_on_settings_received`/`session_process_settings_frame`, part of the Core).
**Evidence:**
> lib/nghttp2_session.c:5492-5512: first-settings path explicitly checks `iframe->sbuf.pos[3] != NGHTTP2_SETTINGS || (iframe->sbuf.pos[4] & NGHTTP2_FLAG_ACK)` and terminates the session if violated — showing type/flags bytes drive branching immediately.
> lib/nghttp2_session.c:633-639: `server && (opt_flags & NGHTTP2_OPTMASK_NO_RECV_CLIENT_MAGIC) == 0` selects magic-check vs not.
> lib/nghttp2_session.c:7985-7987 and :8013-8015: `NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE` gates `nghttp2_session_consume`/`nghttp2_session_consume_connection`.
> lib/nghttp2_session.c:584-586: `NGHTTP2_OPT_MAX_OUTBOUND_QUEUE_SIZE` sets the threshold used at line 7125-7127.
> fuzz/fuzz_target.cc:70-71: `nghttp2_settings_entry iv{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}; nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, &iv, 1);` shows how settings values are injected before feeding recv data.

### P2.11 Input Expectations
**Claim:** Input is a raw HTTP/2 wire byte stream, not framed/delimited by the caller — the function itself maintains a resumable parser state (`session->iframe.state`) so any chunking (including 1 byte at a time) is valid and must be correctly resumed across calls. For a server session (default), the first 24 bytes must equal the fixed HTTP/2 client preface `NGHTTP2_CLIENT_MAGIC` unless disabled via option; afterward the stream must contain a well-formed first SETTINGS frame (`NGHTTP2_IB_READ_FIRST_SETTINGS` expects type==NGHTTP2_SETTINGS and flags without ACK) before falling into the general per-frame header/payload reading loop. There is no fixed overall size limit on `inlen`; frame length is bounded by the negotiated `max_frame_size` (checked inside the core) rather than by this API directly. `in == NULL` is only acceptable with `inlen == 0`.
**Evidence:**
> lib/includes/nghttp2/nghttp2.h:3707-3717 (doc): "Processes data |in| as an input from the remote endpoint... In the current implementation, this function always tries to processes |inlen| bytes of input data unless either an error occurs or NGHTTP2_ERR_PAUSE is returned..."
> lib/nghttp2_session.c:5445-5448: `if (in == NULL) { assert(inlen == 0); in = static_in; }`.
> lib/nghttp2_session.c:5464-5479: client-magic byte-by-byte comparison against `NGHTTP2_CLIENT_MAGIC`, tolerant of partial delivery (`readlen = nghttp2_min_size(inlen, iframe->payloadleft)`).
> lib/nghttp2_session.c:5482-5514: first-SETTINGS-frame-required check right after the magic.
> tests/nghttp2_session_test.c:1507: feeding the parser 1 byte at a time (`&data[i], 1`) is an explicitly tested/supported usage pattern, confirming resumability per-byte.

## Other entries: call protocol and combination with the target

### nghttp2_session_server_new / nghttp2_session_client_new (and *_new2/_new3 variants)
**Claim:** Source/lifecycle entry; must be called exactly once before any `nghttp2_session_mem_recv2`/`mem_send2` call, and determines server-vs-client parsing path (see P2.1/P2.8). Parameters: `session_ptr` (out, non-NULL double pointer), `callbacks` (non-NULL, built via `nghttp2_session_callbacks_new` + setters), `user_data` (opaque, passed to callbacks), and for `*_new2`/`*_new3` an optional `nghttp2_option*` (may be NULL) and `nghttp2_mem*` custom allocator (may be NULL for `*_new3`, not present in `_new`/`_new2`). Return: `int`, `0` on success, `NGHTTP2_ERR_NOMEM` on allocation failure, or other negative error; on failure `*session_ptr` is not usable and nothing needs freeing (constructor cleans up its own partial allocations internally, see fail labels at lib/nghttp2_session.c:654-661). On success, free via `nghttp2_session_del`.
**Evidence:**
> lib/nghttp2_session.c:698-730 (`nghttp2_session_server_new`/`_new2`/`_new3` chain calling `session_new`).
> lib/nghttp2_session.c:664-693 (`nghttp2_session_client_new` family, same shape, sets `session->next_stream_id = 1` for client).
> lib/nghttp2_session.c:654-661: `fail_aob_framebuf`/`fail_hd_inflater`/`fail_hd_deflater`/`fail_session` labels free partially-constructed state and return the error — caller does not need extra cleanup on construction failure.
> fuzz/fuzz_target.cc:67: `nghttp2_session_server_new(&session, callbacks, nullptr);` — called once, first, before submit_settings/mem_recv2.

### nghttp2_submit_settings
**Claim:** Configuration entry, called any time after session creation (including before any send/recv) to queue an outbound SETTINGS frame and influence `session->local_settings` constraints that the inbound parser (core `session_process_settings_frame`/`nghttp2_session_on_settings_received`) later enforces on frames from the peer (e.g. HEADERS/CONTINUATION size vs `MAX_FRAME_SIZE`, HPACK dynamic table size vs header-table-size setting). Parameters: `session` (non-NULL), `flags` (currently ignored — `(void)flags;`), `iv` (array of `nghttp2_settings_entry{settings_id, value}`, may be NULL if `niv==0`), `niv` (count). Return: `int`, 0 on success or negative error (e.g. `NGHTTP2_ERR_NOMEM`, `NGHTTP2_ERR_INVALID_ARGUMENT` for bad values — enforced inside `nghttp2_session_add_settings`, not shown here). Nothing to free directly; the queued frame is owned by the session and released by `nghttp2_session_del` or once sent.
**Evidence:**
> lib/nghttp2_submit.c:217-221: `int nghttp2_submit_settings(...) { (void)flags; return nghttp2_session_add_settings(session, NGHTTP2_FLAG_NONE, iv, niv); }`.
> fuzz/fuzz_target.cc:70-71: `nghttp2_settings_entry iv{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}; nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, &iv, 1);` then flushed via `send_pending` before `mem_recv2`.

### nghttp2_session_upgrade2
**Claim:** Alternate source for injecting a SETTINGS payload (from an HTTP/1.1 Upgrade header's `HTTP2-Settings` value) directly into the settings-processing core, bypassing the byte-stream parser. Must be called (if at all) immediately after session creation and before any `nghttp2_session_mem_recv2` calls expecting stream id 1 to pre-exist as the upgraded stream — internally it creates/initializes stream 1 (`nghttp2_session_get_stream(session, 1)` asserted non-NULL after `nghttp2_session_upgrade_internal`). Parameters: `session`, `settings_payload` (raw base64-decoded SETTINGS payload bytes, non-NULL unless `settings_payloadlen==0`), `settings_payloadlen`, `head_request` (int flag — selects `NGHTTP2_HTTP_FLAG_METH_HEAD` on the synthesized stream, affecting later HTTP semantics checks on DATA frames), `stream_user_data` (opaque). Return: `int`, 0 on success or negative error from `nghttp2_session_upgrade_internal` (malformed payload, etc.). Not part of the minimal recv-loop; it's an alternative/additional path exercised separately, not typically combined with feeding a preface through `mem_recv2` for the same stream.
**Evidence:**
> lib/nghttp2_session.c:7927-7948 (`nghttp2_session_upgrade2` body) and :7834 (`nghttp2_session_upgrade_internal`, the shared helper also used by deprecated `nghttp2_session_upgrade` at :7900-7925).
> lib/nghttp2_session.c:7911-7912: `stream = nghttp2_session_get_stream(session, 1); assert(stream);` — confirms stream 1 must already exist post-call, i.e. this is a one-shot setup call, not repeatable per connection.

### nghttp2_session_consume (and nghttp2_session_consume_connection / nghttp2_session_consume_stream)
**Claim:** Consumer entry, called after frames have been parsed by `nghttp2_session_mem_recv2` (typically inside/after `on_data_chunk_recv_callback`), and only valid when the session has `NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE` set via `nghttp2_option_set_no_auto_window_update` at construction time (else returns `NGHTTP2_ERR_INVALID_STATE` immediately). Parameters: `session`, `stream_id` (must be nonzero, else `NGHTTP2_ERR_INVALID_ARGUMENT`), `size` (number of bytes being "consumed"/acknowledged for flow control). Return: `int`, 0 on success (even if the stream no longer exists — looked up and silently ignored if gone, see `if (!stream) return 0;`) or a fatal error code. Side effect: queues WINDOW_UPDATE frames, drained by a later `nghttp2_session_mem_send2`/`nghttp2_session_send`. Nothing to free.
**Evidence:**
> lib/nghttp2_session.c:7976-8008 (`nghttp2_session_consume` body, including the `NGHTTP2_OPTMASK_NO_AUTO_WINDOW_UPDATE` gate at 7985-7987 and the nonexistent-stream short-circuit at 7995-7999).
> lib/nghttp2_session.c:8010-8024 / :8026+ : connection-only and stream-only variants with the same option gate.

### nghttp2_session_mem_send2 (and nghttp2_session_send)
**Claim:** Consumer/variant entry used to serialize whatever state changes (SETTINGS ACK, WINDOW_UPDATE, GOAWAY, RST_STREAM, response HEADERS/DATA if server) the inbound parsing produced. Called in a loop until it returns 0 (no more data) or negative (error). Parameters: `session`, `data_ptr` (out pointer set to an internal buffer owned by the session — caller must not free it). Return: `nghttp2_ssize`, `>0` = bytes available at `*data_ptr` to send, `0` = nothing more to send now, `<0` = error (fatal errors should stop further calls). Must be called both before the target (to flush any session-startup frames like the client preface / initial SETTINGS) and after it (to flush reactive frames), matching the "round trip" described in the feature summary.
**Evidence:**
> lib/nghttp2_session.c:3135-3160 (`nghttp2_session_mem_send2` body: loops via `nghttp2_session_mem_send_internal`, then `session_after_frame_sent1` if a frame is in flight).
> fuzz/fuzz_target.cc:40-48: `send_pending` calls `nghttp2_session_mem_send2(session, &data)` in a loop `while (n != 0)`, used both before and after `nghttp2_session_mem_recv2` (fuzz/fuzz_target.cc:72-74).

### nghttp2_session_recv
**Claim:** A variant driver that itself repeatedly calls `nghttp2_session_mem_recv2` internally, using a user-supplied `recv_callback`/`recv_callback2` to pull bytes, rather than the harness supplying a fixed buffer directly — demonstrates the intended call-order pattern (loop: receive bytes → `nghttp2_session_mem_recv2` → check return) but is not itself typically combined with a fuzz harness that already has the full input buffer in memory (the harness would use `nghttp2_session_mem_recv2` directly, as `fuzz_target.cc` does, instead of `nghttp2_session_recv`).
**Evidence:**
> lib/nghttp2_session.c:7133-7153 (`nghttp2_session_recv` body): `readlen = session_recv(session, buf, sizeof(buf)); if (readlen > 0) { proclen = nghttp2_session_mem_recv2(session, buf, (size_t)readlen); ... }` — the canonical "recv callback + mem_recv2" composition.

## Build notes
**Claim:** The fuzz target (`fuzz/fuzz_target.cc`) is compiled as C++ (std=c++17) against the static library `lib/libnghttp2.a` built via CMake (`ENABLE_LIB_ONLY=ON`), with the public header path `lib/includes` (both as `../lib/includes` and `lib/includes` relative include dirs) providing `<nghttp2/nghttp2.h>` where `nghttp2_session_mem_recv2` is declared. No other external libraries are linked besides `$LIB_FUZZING_ENGINE`.
**Evidence:**
> ossfuzz_view/nghttp2-c1/build.sh:18-21: `mkdir build; cd build; cmake -DENABLE_LIB_ONLY=ON -DBUILD_STATIC_LIBS=ON ../; make; make check`.
> ossfuzz_view/nghttp2-c1/build.sh:24,30-32: `export CXXFLAGS="${CXXFLAGS} -std=c++17"` then `$CXX $CXXFLAGS -I../lib/includes -Ilib/includes -I../lib/ ../fuzz/fuzz_target.cc -o $OUT/nghttp2_fuzzer $LIB_FUZZING_ENGINE lib/libnghttp2.a`.
> fuzz/fuzz_target.cc:1: `#include <nghttp2/nghttp2.h>` resolved via the `-I../lib/includes`/`-Ilib/includes` flags (lib/includes/nghttp2/nghttp2.h is the installed public header containing the target's declaration).
> ossfuzz_view/nghttp2-c1/Dockerfile:17-26: base image `gcr.io/oss-fuzz-base/base-builder`, clones `nghttp2/nghttp2` git repo, no extra link libraries beyond build tools (make/autoconf/automake/libtool/pkg-config) are installed.