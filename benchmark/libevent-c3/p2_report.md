## target: evws_new_session

### P2.1 Init Sequence
**Claim:** Before calling `evws_new_session`, the caller must have a running `struct evhttp` server (`evhttp_new()` + `evhttp_bind_socket*()`) with a URI callback registered via `evhttp_set_cb()`/`evhttp_set_gencb()`, and must be inside that callback holding the `struct evhttp_request *req` delivered by libevent's HTTP layer (so `req->evcon` is a live, server-side `evhttp_connection`). No separate "ws init" call exists; `evws_new_session` is the first and only WS-layer call needed to upgrade the request.
**Evidence:**
> sample/ws-chat-server.c:235-243: `http_server = evhttp_new(base); ... evhttp_bind_socket_with_handle(http_server, "0.0.0.0", 8080); evhttp_set_cb(http_server, "/ws", on_ws, NULL);`
> sample/ws-chat-server.c:148-157: `on_ws(struct evhttp_request *req, void *arg) { ... client->evws = evws_new_session(req, on_msg_cb, client, 0);`
> test/regress_http.c:234: `evhttp_set_cb(myhttp, "/ws", http_on_ws_cb, base);` (registered on the `http_setup()` test server before any WS traffic is sent)
> ws.c:449-450: `evcon = evhttp_request_get_connection(req); ws_http_server = evcon->http_server;` — dereferences `evcon` without a NULL check, so `req` must already be bound to a live `evhttp_connection` (i.e. `req` must be the request object handed to an evhttp callback, not a freshly-allocated unattached `evhttp_request_new()` object).

### P2.2 Parameter Construction
**Claim:** `req` must be a non-NULL, currently-active `struct evhttp_request *` received in an HTTP request callback (its `evhttp_request_get_input_headers`/`get_output_headers` must be valid and it must have a non-NULL `evcon`). `cb` (`ws_on_msg_cb`) must be a valid non-NULL function pointer invoked later with `(evws, type, data, len, arg)`. `arg` is an opaque user pointer, may be NULL. `options` is a bitmask; only `BEV_OPT_THREADSAFE` is interpreted (bufferevent locking enabled on the hijacked bufferevent).
**Evidence:**
> ws.c:417-428: headers are read via `evhttp_request_get_input_headers(req)`/`evhttp_find_header` — requires `req` to have valid input headers already parsed by the HTTP layer (i.e. request handshake must have completed through libevent's HTTP request parsing before the callback and `evws_new_session` run).
> ws.c:459-462: `if (options & BEV_OPT_THREADSAFE) { if (bufferevent_enable_locking_(evws->bufev, NULL) < 0) goto error; }`
> test/sample/ws-chat-server.c:157: `evws_new_session(req, on_msg_cb, client, 0)` — options=0, no locking.
> test/regress_ws.c:626-627: `evws_new_session(req, on_ws_msg_cb, (void*)0xDEADBEEF, BEV_OPT_THREADSAFE);` — options=BEV_OPT_THREADSAFE exercised together with a thread-safe underlying bufferevent (`ws_threadsafe_bevcb` using `BEV_OPT_THREADSAFE` on the socket bufferevent, set via `evhttp_set_bevcb`).

### P2.3 Object Lifecycle
**Claim:** Create (`evws_new_session`) → configure (`evws_connection_set_closecb`, optional) → use (`evws_send_text`/`evws_send_binary`/`evws_close`, and incoming data processed internally via the installed read callback) → destroy (`evws_connection_free`, normally triggered internally after `evws_close()` finishes writing, or by `evhttp_free()` at server teardown). There is no separate explicit "destroy" call the harness must make in the success path if it relies on the close handshake; `evws_connection_free` is also safe/necessary to call directly (e.g. error path, or forced cleanup in a fuzz harness that bypasses event-loop-driven teardown).
**Evidence:**
> ws.c:405-480: `evws_new_session()` — create.
> ws.c:534-540: `evws_connection_set_closecb()` — configure (optional, can be called any time after creation while `evws` is alive).
> ws.c:164-197: `evws_close()` sets `bufferevent_setcb(..., close_after_write_cb, close_event_cb, evws)`; `close_after_write_cb` / `close_event_cb` eventually call `evws_connection_free(ctx)` once output flushed or on any bufferevent event.
> ws.c:93-114: `evws_connection_free()` — destroy: invokes close callback, removes from `http->ws_sessions` TAILQ, frees `bufev`, frees `incomplete_frames`, frees the `evws` struct itself.
> http.c:4266-4268: `while ((evws = TAILQ_FIRST(&http->ws_sessions)) != NULL) { evws_connection_free(evws); }` inside `evhttp_free()` — server teardown also frees all live WS sessions.

### P2.4 Return Value Handling
**Claim:** Return value is `struct evws_connection *` or `NULL` on error (bad/missing handshake headers, allocation failure, or `evhttp_start_ws_` failure). On `NULL`, the function itself has already either sent an `HTTP_BADREQUEST` reply and freed `req`/`evcon` state as appropriate, or left `req` untouched for the caller to handle, depending on which failure branch is hit. Callers must check for `NULL` and not use the returned pointer or `req` afterward in the `req_owned==0` branch.
**Evidence:**
> ws.c:409, 471, 479: `struct evws_connection *evws = NULL; ... return evws; ... error: ... return NULL;`
> ws.c:473-479: `error: if (evws) evws_connection_free(evws); if (req_owned) evhttp_send_reply(req, HTTP_BADREQUEST, NULL, NULL); return NULL;` — note: once `evhttp_start_ws_(req)` succeeds, `req_owned` becomes 0 (ws.c:457) because `evhttp_start_ws_` already freed `req`/`evcon` (ws.c:452 comment, http.c:3433-3434); so after that point `req` must not be touched again even on a later failure (e.g. `bufferevent_enable_locking_` failing at ws.c:460-461 goes to `error` with `req_owned==0`, meaning `evhttp_send_reply` is correctly skipped since `req` is already freed).
> sample/ws-chat-server.c:157-162: `if (!client->evws) { free(client); log_d(...); return; }` — typical caller pattern: check NULL, cleanup own context, return without touching `req` further.
> test/regress_ws.c:134-137, 452-454, 626-631: all call sites check `if (!evws) return;` before proceeding.

### P2.5 Cleanup Sequence
**Claim:** On success, cleanup is: `evws_connection_free(evws)` which (in order) calls the close callback if set, removes the session from `http->ws_sessions`, frees `evws->bufev` (the underlying bufferevent — this is the correct and only place the bufferevent is freed), frees `evws->incomplete_frames` evbuffer if present, then frees the `evws` struct. The harness must not separately call `bufferevent_free()` on the bufferevent obtained from `evws_connection_get_bufferevent()`, since `evws_connection_free` owns it. On the `evws_new_session` error path, `evws_connection_free` is only called if `evws` was allocated (i.e. after `mm_calloc` succeeded); if header validation failed before allocation, nothing WS-related needs freeing, but the original `req` is still owned by the HTTP layer (error-reply path handles it or it's handled by normal evhttp flow).
**Evidence:**
> ws.c:93-114 (`evws_connection_free`): cbclose invoked → TAILQ_REMOVE from `http->ws_sessions` → `bufferevent_free(evws->bufev)` → `evbuffer_free(evws->incomplete_frames)` → `mm_free(evws)`.
> ws.c:441-444, 473-475: `evws = mm_calloc(...)`; on later failure `goto error` → `if (evws) evws_connection_free(evws);`.
> ws.c:453-457: once `evhttp_start_ws_` succeeds, `req`/`evcon` are already freed internally (see http.c:3433-3434), so the harness/caller must never call `evhttp_request_free`/`evhttp_connection_free` on them again.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase at the cited locations.
**Evidence:**
> include/event2/ws.h:37-39 `evws_new_session`; :43 `evws_send_text`; :47-48 `evws_send_binary`; :52 `evws_close`; :56-57 `evws_connection_set_closecb`; :61 `evws_connection_free`; :67-68 `evws_connection_get_bufferevent`.
> ws.c:406 `evws_new_session` def; :522 `evws_send_text` def; :528 `evws_send_binary` def; :179 `evws_close` def; :535 `evws_connection_set_closecb` def; :94 `evws_connection_free` def; :543 `evws_connection_get_bufferevent` def.
> ws.c:226 `get_ws_frame`; :317 `ws_evhttp_read_cb`; :206 `ws_message_limit_exceeded_`; :483 `make_ws_frame`; :152 `ws_gen_accept_key`; :200 `evws_force_disconnect_` — all exist as `static` functions in ws.c.
> http.c:3414 `evhttp_start_ws_` def; http-internal.h:245 declaration (internal, not in public `ws.h`).

### P2.7 Co-call Constraints
**Claim:** (1) `evws_new_session` pairs with `evws_connection_free` (direct or via the `evws_close()`-triggered callbacks, or via `evhttp_free()`) — every successfully created session must eventually be freed exactly once. (2) `evhttp_new()`/`evhttp_bind_socket*()` pairs with `evhttp_free()`. (3) `evws_close()` is a "begin close" call; it is not re-entrant/idempotent-unsafe beyond the first call (`evws->closed` guard) and leads asynchronously to `evws_connection_free`. (4) `bufferevent_lock`/`bufferevent_unlock` are paired inside `evws_send()` around `make_ws_frame` + `evbuffer_add` to the bufferevent's output buffer.
**Evidence:**
> ws.c:185-187: `if (evws->closed) return; evws->closed = true;` — guards double-close.
> ws.c:164-170, 172-176: `close_after_write_cb`/`close_event_cb` both ultimately call `evws_connection_free(ctx)`.
> ws.c:515,518: `bufferevent_lock(evws->bufev); ... bufferevent_unlock(evws->bufev);` in `evws_send()`.
> http.c:4266-4268: `evhttp_free()` walks and frees all remaining `ws_sessions` — a safety net pairing for sessions never explicitly closed.

### P2.8 Prerequisite State
**Claim:** Requires: (a) an `event_base`; (b) an `evhttp` server object created with `evhttp_new(base)`; (c) the server must have accepted an incoming HTTP connection and parsed a full HTTP request (so `req->evcon != NULL` and input headers are populated) — this happens automatically inside libevent's HTTP state machine before the user's registered callback (set via `evhttp_set_cb`/`evhttp_set_gencb`) runs; (d) the incoming request's headers must satisfy `Upgrade: websocket`, `Connection: ...Upgrade...`, and `Sec-WebSocket-Key` present, or `evws_new_session` returns NULL without touching the bufferevent.
**Evidence:**
> ws.c:417-428: three sequential header checks (`Upgrade`, `Connection`, `Sec-WebSocket-Key`) each `goto error` on failure.
> ws.c:449-450: `evcon = evhttp_request_get_connection(req); ws_http_server = evcon->http_server;` — unconditionally dereferences `evcon`, so `req->evcon` must be non-NULL (true for any request reaching a server-side URI callback through normal libevent dispatch).
> test/regress_ws.c:582-600: shows the full prerequisite chain: `http_setup()` creates server+callback, `http_connect`+`create_bev` establish a real HTTP connection, then the handshake request is written to the socket (not constructed in-process) so the server's own HTTP parser populates `req` before `http_on_ws_limit_cb`/`evws_new_session` runs.

### P2.9 Result Usage
**Claim:** The returned `struct evws_connection *` is used as: (a) the handle passed to all other WS entry points — `evws_send_text`, `evws_send_binary`, `evws_close`, `evws_connection_set_closecb`, `evws_connection_get_bufferevent`, `evws_connection_free`; (b) stored by the application in its own per-connection context (e.g. `struct client`) for later use from other callbacks (e.g. a chat broadcast); (c) `evws_connection_get_bufferevent()` exposes the raw `struct bufferevent *` for low-level operations like `bufferevent_getfd()`.
**Evidence:**
> sample/ws-chat-server.c:157,164,172: `client->evws = evws_new_session(...); fd = bufferevent_getfd(evws_connection_get_bufferevent(client->evws)); evws_connection_set_closecb(client->evws, on_close_cb, client);`
> test/regress_ws.c:134,139-140: `evws = evws_new_session(...); evws_connection_set_closecb(evws, on_ws_close_cb, ...); evws_send_text(evws, hello);`
> test/regress_ws.c:100,105 (inside `on_ws_msg_cb`, the user callback invoked by `ws_evhttp_read_cb` with the session handle): `evws_send_text(evws, reply); ... evws_send_binary(evws, "\xde\xad\xbe\xef\x55", 5);` — the message callback itself is a "result consumer" entry point that receives `evws` and typically calls send/close back on it.
> ws.c:354-359 (`ws_evhttp_read_cb`): `evws->cb(evws, type, data, ...)` — the library itself passes the live `evws` handle into the user `ws_on_msg_cb`, which is how real callers then call `evws_send_*`/`evws_close` from within message handling.

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting inputs across the API surface:
- `evws_new_session`'s `options` bit `BEV_OPT_THREADSAFE` toggles `bufferevent_enable_locking_` on the hijacked bufferevent.
- Presence/casing/content of `Upgrade`, `Connection`, `Sec-WebSocket-Key`, `Sec-WebSocket-Protocol` headers in `req` select accept vs. reject (NULL) path, and whether `Sec-WebSocket-Protocol` is echoed back.
- `evws_send_text` vs `evws_send_binary` select `TEXT_FRAME` (0x1) vs `BINARY_FRAME` (0x2) framing in `make_ws_frame`; `len` (`strlen` for text, explicit `packet_len` for binary) selects 7-bit / 16-bit(126) / 64-bit(127) length encoding branches (`len<=125`, `len<=65535`, else) in `make_ws_frame` (ws.c:490-504).
- `evws_close`'s `reason` parameter (`WS_CR_NONE`/`WS_CR_NORMAL`/`WS_CR_PROTO_ERR`/`WS_CR_DATA_TOO_BIG` or arbitrary `uint16_t`) is encoded big-endian into the close frame body.
- Raw bytes fed to the hijacked bufferevent after `evws_new_session` select opcode/FIN/MASK/length-field branches in `get_ws_frame` (TEXT/BINARY/CLOSING/PING/PONG/reserved 0x3-0x7,0xb-0xf → ERROR_FRAME; fragmentation via FIN=0 → INCOMPLETE_FRAME, reassembled across calls via `evws->incomplete_frames`); length fields >`WS_MAX_RECV_FRAME_SZ`(10MiB, ws.c:45,266) or cumulative message size > `WS_MAX_RECV_MSG_SZ`(10MiB, ws.c:50,214) select the DoS-prevention `ERROR_FRAME`/`evws_close(WS_CR_DATA_TOO_BIG)` branches.
**Evidence:**
> ws.c:459-461 (`options & BEV_OPT_THREADSAFE`).
> ws.c:417-439 (header-driven accept/reject and `Sec-WebSocket-Protocol` echo).
> ws.c:522-532 (`evws_send_text`/`evws_send_binary` → `evws_send(evws, TEXT_FRAME|BINARY_FRAME, ...)`); ws.c:490-504 (length-branch selection in `make_ws_frame`).
> ws.c:179-197 (`evws_close` reason encoding); include/event2/ws.h:14-17 (`WS_CR_*` constants).
> ws.c:240-313 (`get_ws_frame` opcode/fin/mask/length decoding and `ERROR_FRAME`/`INCOMPLETE_FRAME` branches); ws.c:341-389 (`ws_evhttp_read_cb` switch on frame type); ws.c:206-220 (`ws_message_limit_exceeded_` cap enforcement).

### P2.11 Input Expectations
**Claim:** For `evws_new_session` itself, the "input" is the HTTP request's headers (already parsed by libevent's HTTP engine) — it does not parse raw bytes itself; it only reads specific header values via `evhttp_find_header`/string compares, and reads `req->evcon` directly. Downstream, once the session is created, the installed `ws_evhttp_read_cb`/`get_ws_frame` expect raw bytes per RFC6455: base header ≥2 bytes (else `INCOMPLETE_DATA`), optional 2 or 8 extra length bytes depending on the 7-bit length field (126/127), optional 4-byte mask key when MASK bit set, then payload. No explicit termination byte; framing is length-prefixed. Single-frame payload capped at 10 MiB (`WS_MAX_RECV_FRAME_SZ`); reassembled multi-fragment message capped at 10 MiB (`WS_MAX_RECV_MSG_SZ`); frames with a declared 64-bit length exceeding the cap return `ERROR_FRAME` (leading to forced disconnect) rather than being parsed further.
**Evidence:**
> ws.c:417-428 (header-based "input" to `evws_new_session`).
> ws.c:236-238: `if (buf_len < 2) { return INCOMPLETE_DATA; }`
> ws.c:250-253, 257-261: 16-bit/64-bit extended length require `buf_len>=4`/`buf_len>=10` respectively or `INCOMPLETE_DATA`.
> ws.c:266-278: 64-bit length `> WS_MAX_RECV_FRAME_SZ` → `ERROR_FRAME` with `*out_len=0`.
> ws.c:281-283: `if (buf_len < payload_len + pos + (masked?4:0)) return INCOMPLETE_DATA;` — full-frame-availability check before any unmasking/pointer arithmetic.
> ws.c:206-220 (`ws_message_limit_exceeded_`): `if (msg_len > WS_MAX_RECV_MSG_SZ - buffered) { evws_close(...WS_CR_DATA_TOO_BIG); return 1; }`.

## Other entries' call protocol (besides target) and how they combine with evws_new_session in one harness

### evws_send_text(struct evws_connection *evws, const char *packet_str)
**Prerequisite:** `evws` must be a non-NULL, live (not yet freed/closed) handle returned by a prior successful `evws_new_session`. `packet_str` must be a NUL-terminated C string (function does `strlen(packet_str)` internally — ws.c:524); a non-terminated buffer is an OOB-read risk the harness must avoid (i.e. harness must NUL-terminate any fuzz-derived buffer before passing it here).
**Behaviour/paths:** Always encodes `TEXT_FRAME` (0x1); the only data-dependent path is the length-field branch in `make_ws_frame` (≤125 / ≤65535 / >65535) driven by `strlen(packet_str)`.
**Return value:** `void` — no error reporting; internally always succeeds in appending to the output evbuffer (ws.c:521-525).
**Cleanup:** Nothing to free from this call itself; it only appends bytes to `evws->bufev`'s output buffer, which is freed later as part of `evws_connection_free`.
**Combination:** Call after `evws_new_session` returns non-NULL, any number of times, before `evws_close`/`evws_connection_free`. Typically called either by the harness directly on the just-created session, or from inside the `ws_on_msg_cb` the harness supplies to `evws_new_session` (echo pattern), as in test/regress_ws.c:100 and sample/ws-chat-server.c pattern.
**Evidence:** ws.c:510-525; test/regress_ws.c:134,140.

### evws_send_binary(struct evws_connection *evws, const char *packet_data, size_t packet_len)
**Prerequisite:** Same as `evws_send_text` re: live `evws`; `packet_data` need not be NUL-terminated since an explicit `packet_len` is given — this is the safer variant for arbitrary fuzz-derived bytes/lengths.
**Behaviour/paths:** Encodes `BINARY_FRAME` (0x2); `packet_len` directly selects the 7-bit/16-bit/64-bit length-encoding branch in `make_ws_frame` (ws.c:490-504) — a good lever for the fuzz input to pick boundary values (125/126/65535/65536).
**Return value:** `void`.
**Cleanup:** none beyond what `evws_connection_free` does for the connection overall.
**Combination:** Call any time after a successful `evws_new_session`, e.g. to mirror the attacker-controlled "inbound length field" fuzzing with the matching "outbound length field" encoder, exercising `make_ws_frame`'s boundary branches with fuzzer-chosen `packet_len`.
**Evidence:** ws.c:527-532; test/regress_ws.c:105 (`evws_send_binary(evws, "\xde\xad\xbe\xef\x55", 5)`).

### evws_connection_set_closecb(struct evws_connection *evws, ws_on_close_cb cb, void *cbarg)
**Prerequisite:** `evws` must be a live handle from `evws_new_session`. Can be called at any point in the session's life (even immediately after creation, as in all real call sites) — it only stores two fields, no side effects.
**Behaviour/paths:** No branching; unconditionally stores `cb`/`cbarg` into `evws->cbclose`/`evws->cbclose_arg` (ws.c:535-540). `cb` may be NULL to "unset" an existing close callback.
**Return value:** `void`.
**Cleanup:** None; the stored callback is invoked once by `evws_connection_free` (ws.c:97-98) and not freed separately (the harness owns `cbarg`'s lifetime, if heap-allocated, and must free it itself, typically in the close callback body).
**Combination:** Call once right after `evws_new_session` returns non-NULL (matches every real call site: test/regress_ws.c:139, sample/ws-chat-server.c:172). The registered close callback fires exactly once when `evws_connection_free` runs (whether triggered by `evws_close`'s async completion, by `evhttp_free`, or by a direct `evws_connection_free` call), so the harness can use it to detect/assert teardown or free its own per-session context.
**Evidence:** ws.c:93-98 (invocation site), 534-540 (setter); test/regress_ws.c:116-126 (`on_ws_close_cb` example incrementing a counter).

### evws_close(struct evws_connection *evws, uint16_t reason)
**Prerequisite:** `evws` live, `evws->bufev` non-NULL (always true post `evws_new_session` success). Idempotency guard: a second call after `evws->closed==true` is a silent no-op (ws.c:185-186).
**Behaviour/paths:** `reason` (any `uint16_t`, commonly `WS_CR_NORMAL`=1000/`WS_CR_PROTO_ERR`=1002/`WS_CR_DATA_TOO_BIG`=1009/`WS_CR_NONE`=0) is big-endian-encoded into a 4-byte RFC6455 close frame written to the connection's output buffer; then `bufferevent_setcb` switches the bufferevent's callbacks to `close_after_write_cb`/`close_event_cb` (ws.c:195-196), which asynchronously call `evws_connection_free` once the output is flushed or any bufferevent event fires. This is also invoked internally as `evws_force_disconnect_(evws)` (reason=`WS_CR_NONE`) from `ws_evhttp_read_cb` on `CLOSING_FRAME`/`ERROR_FRAME`/oversized-message paths (ws.c:200-203, 377-380).
**Return value:** `void`.
**Cleanup:** After calling `evws_close`, the harness must NOT call `bufferevent_setcb`/send/free on `evws` again from the "normal" API — the bufferevent callbacks are now owned by the close machinery, and `evws` itself will be freed asynchronously by `close_after_write_cb`/`close_event_cb` once the event loop runs. If the harness does not pump the event loop, `evws_connection_free` will not be triggered by this path and the harness must free `evws` itself via a direct call to avoid leaking in a single-shot (non-looping) fuzz harness.
**Combination:** Typically the last WS-layer call on a session before relying on the event loop (or a direct `evws_connection_free`) to reclaim it. In a libFuzzer harness without a running `event_base_dispatch` loop, prefer calling `evws_connection_free(evws)` directly instead of (or in addition to carefully triggering) `evws_close`, to guarantee deterministic cleanup within one fuzz iteration.
**Evidence:** ws.c:178-197, 199-203, 377-380; test/regress_ws.c:498-500 (`TEST_WS_CLOSE_TOO_BIG` reason observed client-side after server-triggered `evws_close(evws, WS_CR_DATA_TOO_BIG)` from `ws_message_limit_exceeded_`).

### evws_connection_get_bufferevent(struct evws_connection *evws)
**Prerequisite:** `evws` live, non-NULL.
**Behaviour/paths:** Pure accessor, no branching; returns `evws->bufev` (ws.c:542-546). Never NULL for a session created successfully by `evws_new_session` (since creation fails/goto error if `evhttp_start_ws_` returns NULL, ws.c:453-456).
**Return value:** `struct bufferevent *` — the same bufferevent libevent's HTTP layer was using for `req`'s connection, now solely owned by the WS layer.
**Cleanup:** The returned bufferevent must NOT be freed directly by the harness; it is owned by `evws` and freed exclusively inside `evws_connection_free` (ws.c:106-108). The harness may, however, use it as the feed point for raw attacker bytes: `bufferevent_read`-side injection is normally done by writing to the peer socket/fd (real network tests) or, for an in-process/fd-less harness, by directly pushing bytes into `bufferevent_get_input(bev)` via `evbuffer_add` and invoking the read callback, since `evws_new_session` already installed `ws_evhttp_read_cb` on this bufferevent via `bufferevent_setcb` (ws.c:464-465).
**Combination:** Call after `evws_new_session` succeeds, to (a) query the fd (`bufferevent_getfd`) as in sample/ws-chat-server.c:164, or (b) in a fuzz harness, to obtain the bufferevent and drive `ws_evhttp_read_cb`/`get_ws_frame` by injecting fuzz-controlled bytes into its input buffer (`evbuffer_add(bufferevent_get_input(bev), data, size)`) and then invoking the installed read callback (directly, or by running the event loop so bufferevent fires it).
**Evidence:** ws.c:542-546; sample/ws-chat-server.c:164; ws.c:464-465 (`bufferevent_setcb(evws->bufev, ws_evhttp_read_cb, NULL, ws_evhttp_error_cb, evws)` — confirms this is the callback reachable via the accessor's returned bufferevent).

### How these combine in one harness
A single harness iteration plausibly: (1) creates an `event_base` and `evhttp` server, registers a URI callback with `evhttp_set_cb`/`evhttp_set_gencb`; (2) drives a real or constructed HTTP request through the server so that, inside the callback, `req` is valid and has the WS upgrade headers (either via a real socket/bufferevent-pair feeding the server, as in test/regress_ws.c's `http_connect`+`create_bev` pattern, or by directly constructing an `evhttp_request` with `evhttp_request_get_input_headers`+`evhttp_add_header` and a connected `evcon`, mirroring `evhttp_parse_firstline_`/`evhttp_parse_headers_` usage seen in http_message_fuzzer.cc); (3) calls `evws_new_session(req, cb, arg, options)`, checks for NULL; (4) optionally calls `evws_connection_set_closecb`; (5) obtains the bufferevent via `evws_connection_get_bufferevent` and injects fuzz-controlled WebSocket frame bytes into its input buffer to drive `ws_evhttp_read_cb`→`get_ws_frame` (the dominant attack surface per the task description); (6) optionally calls `evws_send_text`/`evws_send_binary` (from inside `cb` or directly) to exercise the outbound `make_ws_frame` path; (7) calls `evws_close` and/or `evws_connection_free` (directly, for deterministic single-iteration cleanup) and finally `evhttp_free(http)`/`event_base_free(base)`.
**Evidence:** test/regress_ws.c:504-554 (`http_ws_test_with_connection` — full handshake-over-socket pattern); sample/ws-chat-server.c:217-249 (`main` — server setup/teardown pattern); http_message_fuzzer.cc:46-71 (existing in-repo pattern for constructing an `evhttp_request`/headers without a real socket, usable as a template for a lower-overhead harness).

## Build notes
**Claim:** Fuzzers for this project are C++ (`.cc`), compiled by `build.sh` which globs `$SRC/*_fuzzer.cc`, compiles each with `$CXX $CXXFLAGS -std=c++17 -I../ -Iinclude`, and links statically against `libevent.a`, `libevent_core.a`, `libevent_pthreads.a`, `libevent_extra.a` plus `$LIB_FUZZING_ENGINE`. The target's header `event2/ws.h` is reachable via the `-Iinclude` path (installed headers from `make install` into the build's `include/` dir) as `#include <event2/ws.h>` (public) or, for internal pieces used only inside the library itself (`evhttp_start_ws_`, `http-internal.h`), via `-I../` (source tree root) as seen in `http_message_fuzzer.cc`'s `#include "libevent/http-internal.h"`.
**Evidence:**
> ossfuzz_view/libevent-c3/build.sh:19-28 (cmake configure + `make install` producing `./include` and `./lib/*.a`).
> ossfuzz_view/libevent-c3/build.sh:31-37: `for fuzzers in $(find $SRC -name '*_fuzzer.cc'); do ... $CXX $CXXFLAGS -std=c++17 -I../ -Iinclude $fuzzers $LIB_FUZZING_ENGINE ./lib/libevent.a ./lib/libevent_core.a ./lib/libevent_pthreads.a ./lib/libevent_extra.a -o $OUT/$fuzz_basename; done`
> ossfuzz_view/libevent-c3/http_message_fuzzer.cc:27-34: `extern "C" { #include "libevent/include/event2/event.h" ... #include "libevent/http-internal.h" }` — shows the `-I../` (i.e. `$SRC`) path resolving `libevent/...` relative includes, used for internal headers not exposed by the public `ws.h`.
> ossfuzz_view/libevent-c3/ws_fuzzer.cc:1-16 — current placeholder harness only pins the symbol `evws_new_session` via `__asm__` alias and does not call it; confirms no working reference harness currently exists for this target (the downstream Harness Generator must build it from scratch using the protocol above).
> ossfuzz_view/libevent-c3/ws_fuzzer.dict:1-21 — dictionary tokens for opcode bytes (`\x00`,`\x01`,`\x02`,`\x08`,`\x09`,`\x0a`), FIN+opcode header bytes (`\x81`,`\x82`), mask bit (`\x80`), extended-length markers (`\x7e`,`\x7f`), and the WS handshake UUID, confirming the intended fuzz target is the raw WebSocket frame bytes consumed after `evws_new_session` wires up `ws_evhttp_read_cb`/`get_ws_frame`, matching this report's P2.10/P2.11 analysis.
> ossfuzz_view/libevent-c3/Dockerfile:17-22: `FROM gcr.io/oss-fuzz-base/base-builder`; clones `libevent` fresh and `fuzzing` dict repo, copies `build.sh run_tests.sh *.cc *.c *.dict` into `$SRC` — confirms C++ libFuzzer-style harness convention (`*_fuzzer.cc`) is what's expected for `ws_fuzzer`.