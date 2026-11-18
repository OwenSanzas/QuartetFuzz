## target: gnutls_handshake

### P2.1 Init Sequence
**Claim:** Before `gnutls_handshake()` a session must be created with `gnutls_init(&session, flags)`, credentials must be set with `gnutls_credentials_set()`, priorities must be set (via `gnutls_priority_set_direct()`/`gnutls_priority_set()`/`gnutls_set_default_priority()`), and transport I/O must be wired with `gnutls_transport_set_push_function()`/`gnutls_transport_set_pull_function()` (or `gnutls_transport_set_ptr`/`gnutls_handshake_set_read_function()` for the QUIC/handshake-io fuzz variant). `gnutls_handshake()` itself checks on its first call (`STATE == STATE0`) that priorities were set, otherwise it fails.
**Evidence:**
> lib/handshake.c:2847-2851: `/* first call */ if (session->internals.priorities == NULL || session->internals.priorities->cs.size == 0) return gnutls_assert_val(GNUTLS_E_NO_PRIORITIES_WERE_SET);`
> fuzz/gnutls_handshake_client_fuzzer.c:54-72: order is `gnutls_init` → `gnutls_certificate_allocate_credentials` → `gnutls_credentials_set` → `gnutls_priority_set_direct` → `gnutls_transport_set_push_function`/`gnutls_transport_set_pull_function`/`gnutls_handshake_set_read_function` → `gnutls_handshake` loop.
> doc/examples/ex-client-resume.c:56-82: real client: `gnutls_init` → `gnutls_server_name_set` → `gnutls_session_set_verify_cert` → `gnutls_set_default_priority` → `gnutls_transport_set_int` → `gnutls_handshake_set_timeout` → `gnutls_credentials_set` → (optional) `gnutls_session_set_data` → `gnutls_handshake`.

### P2.2 Parameter Construction
**Claim:** `gnutls_handshake` takes a single parameter, `gnutls_session_t session`, which must be a non-NULL handle returned by a successful `gnutls_init()`. There is no other parameter to construct; all configuration happens through prior calls on the same `session` object (credentials, priorities, transport callbacks).
**Evidence:**
> gnutls.h:1321: `int gnutls_handshake(gnutls_session_t session);`
> lib/state.c:575-584: `gnutls_init(gnutls_session_t *session, unsigned int flags)` allocates `*session = gnutls_calloc(1, sizeof(struct gnutls_session_int));` — this is the only way to obtain a valid `session`.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is create (`gnutls_init`) → configure (credentials, priorities, transport callbacks, optional session resumption data) → use (`gnutls_handshake`, looped on `GNUTLS_E_AGAIN`/`GNUTLS_E_INTERRUPTED`, then `gnutls_record_recv`/`gnutls_record_send`) → optionally `gnutls_bye()` → destroy (`gnutls_deinit`). Credentials objects (e.g. `gnutls_certificate_credentials_t`) must outlive the session since gnutls only keeps a pointer to them (freed separately by the caller after `gnutls_deinit`).
**Evidence:**
> fuzz/gnutls_client_fuzzer.c:40-76: `gnutls_init` → … → `gnutls_handshake` loop → `gnutls_record_recv` loop → `gnutls_deinit(session)` → `gnutls_certificate_free_credentials(xcred)`.
> lib/auth.c:77-82 (gnutls_credentials_set doc comment): "you will have to keep the structure allocated until you call gnutls_deinit(), or a future call of gnutls_credentials_set() with the same @type replaces the @cred structure."

### P2.4 Return Value Handling
**Claim:** Return `GNUTLS_E_SUCCESS` (0) on success, negative error code otherwise. Non-fatal codes that require re-invoking the function are `GNUTLS_E_AGAIN`, `GNUTLS_E_INTERRUPTED`, `GNUTLS_E_WARNING_ALERT_RECEIVED`, and (TLS ≤1.2 rehandshake) `GNUTLS_E_GOT_APPLICATION_DATA`; also `GNUTLS_E_LARGE_PACKET` for DTLS. Callers must test each non-fatal code with `gnutls_error_is_fatal()` and loop; otherwise treat as a fatal error and abort the session (the function itself calls `_gnutls_abort_handshake()` on fatal failure internally).
**Evidence:**
> lib/handshake.c:2791-2805 (doc comment): "The non-fatal errors expected by this function are: %GNUTLS_E_INTERRUPTED, %GNUTLS_E_AGAIN, %GNUTLS_E_WARNING_ALERT_RECEIVED... On these non-fatal errors call this function again, until it returns 0; cf. gnutls_record_get_direction() and gnutls_error_is_fatal()."
> lib/handshake.c:2917-2919: `if (ret < 0) { return _gnutls_abort_handshake(session, ret); }`
> fuzz/gnutls_client_fuzzer.c:61-63: `do { res = gnutls_handshake(session); } while (res < 0 && gnutls_error_is_fatal(res) == 0);`
> fuzz/gnutls_handshake_client_fuzzer.c:74-89: retry loop additionally pumps handshake bytes via `handshake_pull()` on `GNUTLS_E_AGAIN`, bounded by `HANDSHAKE_MAX_RETRY_COUNT`.

### P2.5 Cleanup Sequence
**Claim:** On both success and error paths the caller must call `gnutls_deinit(session)` to free the session (it is documented safe to call unconditionally even after `gnutls_init()` failure since 3.8.0), then free any separately-allocated credentials structures (e.g. `gnutls_certificate_free_credentials(xcred)`, `gnutls_psk_free_client_credentials`, etc.) that were passed to `gnutls_credentials_set`. Priority caches created with `gnutls_priority_init` must be freed with `gnutls_priority_deinit`, but `gnutls_priority_set_direct` already internally deinit's its own temporary cache.
**Evidence:**
> lib/state.c:754-809 `gnutls_deinit()`: frees auth info, handshake buffers, epoch/record parameters, resumption data, priorities (`gnutls_priority_deinit(session->internals.priorities)`), etc.
> fuzz/gnutls_client_fuzzer.c:74-75: `gnutls_deinit(session); gnutls_certificate_free_credentials(xcred);`
> lib/priority.c:3680-3683 (`gnutls_priority_set_direct`): `/* ensure that the session holds the only reference for the struct */ gnutls_priority_deinit(prio);`

### P2.6 API Existence
**Claim:** All functions cited below exist in the current codebase (verified by grep/read of definitions).
**Evidence:**
> lib/handshake.c:2821: `int gnutls_handshake(gnutls_session_t session)`
> lib/handshake.c:2664: `int gnutls_rehandshake(gnutls_session_t session)`
> lib/tls13/key_update.c:245: `int gnutls_session_key_update(gnutls_session_t session, unsigned flags)`
> lib/priority.c:3662: `int gnutls_priority_set_direct(gnutls_session_t session, const char *priorities, const char **err_pos)`
> lib/auth.c:98: `int gnutls_credentials_set(gnutls_session_t session, gnutls_credentials_type_t type, void *cred)`
> lib/session.c:291: `int gnutls_session_set_data(gnutls_session_t session, const void *session_data, size_t session_data_size)`
> lib/system_override.c:81: `void gnutls_transport_set_pull_function(gnutls_session_t session, gnutls_pull_func pull_func)`
> lib/state.c:575: `int gnutls_init(gnutls_session_t *session, unsigned int flags)`
> lib/state.c:754: `void gnutls_deinit(gnutls_session_t session)`
> lib/errors.c:459: `int gnutls_error_is_fatal(int error)`
> lib/record.c:280: `int gnutls_bye(gnutls_session_t session, gnutls_close_request_t how)`

### P2.7 Co-call Constraints
**Claim:** `gnutls_init`/`gnutls_deinit` are a mandatory create/destroy pair for every session. `gnutls_credentials_set` must be paired with keeping the credential structure alive until `gnutls_deinit` (and freeing it only afterward). `gnutls_rehandshake` (server-side) requires a subsequent `gnutls_handshake()` call to actually perform the renegotiation after the peer agrees (TLS ≤1.2) — under TLS1.3 `gnutls_rehandshake` is itself a full key update and subsequent `gnutls_handshake()` calls become no-ops server-side. `gnutls_session_key_update` is only valid after a completed TLS 1.3 handshake (checks `vers->tls13_sem`). `gnutls_transport_set_pull_timeout_function` must be set whenever a custom pull function is registered and `gnutls_handshake_set_timeout()` is used.
**Evidence:**
> lib/handshake.c:2642-2657 (`gnutls_rehandshake` doc): "the calling application should proceed to calling gnutls_handshake() to negotiate the new parameters... Under TLS 1.3 ... calls to gnutls_handshake() in server side are a no-op."
> lib/handshake.c:2670-2675: `if (session->security_parameters.entity == GNUTLS_CLIENT) return GNUTLS_E_INVALID_REQUEST; if (vers->tls13_sem) { return gnutls_session_key_update(session, GNUTLS_KU_PEER); }`
> lib/tls13/key_update.c:250-251: `if (!vers->tls13_sem) return GNUTLS_E_INVALID_REQUEST;`
> lib/handshake.c:2826-2840 (inside `gnutls_handshake`): if `initial_negotiation_completed` and TLS1.3, client re-enters via `gnutls_session_key_update(session, GNUTLS_KU_PEER)`, server returns a no-op `0`.
> lib/system_override.c:111-114 (doc of `gnutls_transport_set_pull_timeout_function`): "this callback must be set when a custom pull function is registered."

### P2.8 Prerequisite State
**Claim:** Before the first `gnutls_handshake()` call the session must have (a) `session->internals.priorities` set and non-empty (else `GNUTLS_E_NO_PRIORITIES_WERE_SET`), (b) entity role fixed at `gnutls_init()` time (`GNUTLS_CLIENT` or `GNUTLS_SERVER`, this selects `handshake_client` vs `handshake_server`), (c) transport read/write wired so the internal I/O layer can send/receive handshake messages, and (d) for server-side certificate authentication, credentials with a loaded certificate/key set via `gnutls_certificate_set_x509_key_mem()` before `gnutls_credentials_set()`.
**Evidence:**
> lib/handshake.c:2842-2851: STATE0 first-call checks `session->internals.priorities`.
> lib/handshake.c:2907-2913: `if (session->security_parameters.entity == GNUTLS_CLIENT) { do { ret = handshake_client(session); } while (ret == 1); } else { ret = handshake_server(session); }`
> fuzz/gnutls_handshake_server_fuzzer.c:56-101: server fuzzer loads RSA/ECDSA/Ed25519/Ed448 certs via `gnutls_certificate_set_x509_key_mem()` and `gnutls_certificate_set_known_dh_params()` before `gnutls_credentials_set()`, before `gnutls_handshake()`.

### P2.9 Result Usage
**Claim:** After `gnutls_handshake()` returns 0, the completed session is used for application data I/O: `gnutls_record_send()`/`gnutls_record_recv()`. Client resumption data is extracted with `gnutls_session_get_data2()` and later fed back via `gnutls_session_set_data()` before a subsequent `gnutls_handshake()` on a new session. `gnutls_session_is_resumed()` queries whether resumption actually happened. `gnutls_bye()` is used to terminate the session gracefully before `gnutls_deinit()`.
**Evidence:**
> doc/examples/ex-client-resume.c:81-108: after `gnutls_handshake`, first connection calls `gnutls_session_get_data2(session, &sdata)`; second connection (after `gnutls_session_set_data(session, sdata.data, sdata.size)` pre-handshake) calls `gnutls_session_is_resumed(session)`, then `gnutls_record_send`/`gnutls_record_recv`.
> fuzz/gnutls_client_fuzzer.c:64-71: `if (res >= 0) { while (true) { res = gnutls_record_recv(session, buf, sizeof(buf)); if (res <= 0) break; } }` — record_recv is the direct consumer of a successful handshake.
> doc/examples/ex-serv-x509.c:146-160 (grep match) and ex-client-x509.c:88-112: handshake followed by `gnutls_record_recv`.

### P2.10 Behaviour Controls
**Claim:** The entity flag passed to `gnutls_init(&session, GNUTLS_CLIENT|GNUTLS_SERVER[,|GNUTLS_DATAGRAM|GNUTLS_NONBLOCK|...])` selects client vs server state machine and TLS vs DTLS transport mode (`IS_DTLS()` checks elsewhere in handshake.c). The priority string passed to `gnutls_priority_set_direct()` selects protocol version range (TLS1.0-1.3/DTLS), enabled key-exchange/cipher/sig algorithms, and gates which internal TLS1.3 vs legacy branches run (checked via `vers->tls13_sem` throughout `gnutls_handshake`/`gnutls_rehandshake`/`gnutls_session_key_update`). The credential type passed to `gnutls_credentials_set()` (`GNUTLS_CRD_CERTIFICATE`, `GNUTLS_CRD_PSK`, `GNUTLS_CRD_ANON`, `GNUTLS_CRD_SRP`) selects which key-exchange/authentication code path the handshake core takes. Supplying (or not) data via `gnutls_session_set_data()` selects full vs abbreviated/resumed handshake. The `flags` argument to `gnutls_session_key_update(session, flags)` (`GNUTLS_KU_PEER` or 0) controls whether the peer is also asked to update keys.
**Evidence:**
> lib/handshake.c:2826-2840: branches on `vers->tls13_sem` and `session->security_parameters.entity` to decide no-op / key-update / legacy path.
> lib/auth.c:92-93 (doc): "For %GNUTLS_CRD_CERTIFICATE, @cred should be #gnutls_certificate_credentials_t." (similarly ANON/SRP in same comment block, lines 84-90).
> lib/session.c:279-282 (doc of gnutls_session_set_data): "Sets all session parameters, in order to resume a previously established session... This function should be called before gnutls_handshake()."
> lib/tls13/key_update.c:226 (doc): "@flags: zero of %GNUTLS_KU_PEER" — controls whether peer key update is requested.
> fuzz/gnutls_handshake_client_fuzzer.c:63-64 and gnutls_handshake_server_fuzzer.c:104-105: both force `"NORMAL:-VERS-TLS-ALL:+VERS-TLS1.3"` to focus fuzzing on the TLS1.3 state machine, demonstrating priority string as the primary path selector used by the project's own fuzzers.

### P2.11 Input Expectations
**Claim:** `gnutls_handshake()` does not take the raw network bytes as a direct parameter; instead it reads them through the pull callback (`gnutls_transport_set_pull_function`) or, for the fuzz-only decoupled-I/O mode, through `gnutls_handshake_write()` fed by `gnutls_handshake_set_read_function`. The bytes must be valid TLS/DTLS record-layer-wrapped handshake messages (ClientHello, ServerHello, Certificate, etc.); the internal parsers (`_gnutls_recv_handshake`, `read_client_hello`, `read_server_hello`, …) perform their own length/structure validation and report errors as negative return codes, which `gnutls_handshake` propagates. There is no documented hard overall size limit at the `gnutls_handshake` level itself (DTLS has an MTU-driven `GNUTLS_E_LARGE_PACKET` condition). The custom fuzz harness protocol (fuzz/handshake.h) additionally frames raw input in the fuzz corpus as repeated `[4-byte encryption level][4-byte length][payload]` records consumed by `handshake_pull()`/`gnutls_handshake_write()` — this is a fuzzer/test-only wire format, not part of the public API contract.
**Evidence:**
> lib/handshake.c:2791-2805 (doc): non-fatal codes imply the function expects to be re-entered as more bytes arrive; `%GNUTLS_E_LARGE_PACKET` documented for DTLS/MTU.
> fuzz/handshake.h:53-89 `handshake_pull()`: parses the 4+4 byte level/size header out of the fuzz input then calls `gnutls_handshake_write(session, (gnutls_record_encryption_level_t)level, data->data, size)` to inject it into the handshake I/O layer consumed by `gnutls_handshake()`.
> fuzz/gnutls_client_fuzzer.c (mem.h not shown) uses `gnutls_transport_set_pull_function(session, mem_pull)` to directly feed the raw fuzz buffer as a TLS byte stream (standard transport path, no extra framing) — contrast with the handshake_*_fuzzer variants which use the io-level framing above.

## Other entries: call protocol and combination with the target

### gnutls_rehandshake
**Claim:** Server-side only trigger for renegotiation (TLS ≤1.2) or TLS1.3 key update. Requires an already-completed handshake on `session` (called after the initial `gnutls_handshake()` succeeded). Returns `GNUTLS_E_INVALID_REQUEST` if called on a client. No parameters besides `session`; nothing to free specific to this call. In a harness exercising rehandshake, call order is: `gnutls_init(GNUTLS_SERVER)` → configure → `gnutls_handshake()` (full handshake) → `gnutls_rehandshake()` → `gnutls_handshake()` again (TLS ≤1.2) or rely on it being a no-op (TLS1.3).
**Evidence:**
> lib/handshake.c:2664-2690: implementation; `_dtls_async_timer_delete`, `_gnutls_send_empty_handshake(... GNUTLS_HANDSHAKE_HELLO_REQUEST ...)`, `STATE = STATE50` then `STATE0`.
> lib/handshake.c:2670-2671: `if (session->security_parameters.entity == GNUTLS_CLIENT) return GNUTLS_E_INVALID_REQUEST;`

### gnutls_session_key_update
**Claim:** Post-handshake TLS1.3-only function; requires `vers->tls13_sem` true (i.e. a completed TLS1.3 handshake on `session`), else returns `GNUTLS_E_INVALID_REQUEST`. Parameter `flags` is `0` or `GNUTLS_KU_PEER`. Behaves like `gnutls_record_send()` — can return `GNUTLS_E_AGAIN`/`GNUTLS_E_INTERRUPTED`, must be retried. Internally reached from `gnutls_handshake()` (client TLS1.3 re-entry) and `gnutls_rehandshake()` (server TLS1.3). Nothing extra to free.
**Evidence:**
> lib/tls13/key_update.c:245-276: full implementation, state machine `STATE150`→`STATE0`, calls `_tls13_update_secret` and `update_sending_key`.
> lib/handshake.c:2826-2838: `gnutls_handshake()` calls `gnutls_session_key_update(session, GNUTLS_KU_PEER)` directly when re-entered post-handshake on a TLS1.3 client.

### gnutls_priority_set_direct
**Claim:** Configuration-only call, must be made on `session` before the first `gnutls_handshake()` call (STATE0 check requires non-empty priorities). Internally calls `gnutls_priority_init()` + `gnutls_priority_set()` + `gnutls_priority_deinit()` so the caller does not need to manage a separate `gnutls_priority_t` cache. `priorities` string selects protocol versions/kx/ciphers/extensions; `err_pos` (optional, may be NULL) receives the offset of a syntax error. Returns `GNUTLS_E_INVALID_REQUEST` on syntax error, else `GNUTLS_E_SUCCESS`/error code.
**Evidence:**
> lib/priority.c:3662-3684: implementation as described.
> lib/handshake.c:2848-2851: consumes `session->internals.priorities` set by this call (via `gnutls_priority_set`).

### gnutls_credentials_set
**Claim:** Configuration call, must be made before `gnutls_handshake()` for the authentication/key-exchange methods it enables; can be called multiple times with different `type` values (`GNUTLS_CRD_CERTIFICATE`, `GNUTLS_CRD_PSK`, `GNUTLS_CRD_ANON`, `GNUTLS_CRD_SRP`) to register multiple credential sets simultaneously. `cred` must be a pointer to an already-allocated and populated credentials structure (e.g., from `gnutls_certificate_allocate_credentials()` + `gnutls_certificate_set_x509_key_mem()`); gnutls keeps only a pointer, so the structure must remain valid until `gnutls_deinit()` or a replacing `gnutls_credentials_set()` call. Returns `GNUTLS_E_SUCCESS` or `GNUTLS_E_MEMORY_ERROR`.
**Evidence:**
> lib/auth.c:98-168: implementation, linked-list of `auth_cred_st` by `type`; TLS1.3 certificate-usage sanity checks.
> fuzz/gnutls_handshake_server_fuzzer.c:59-101: `gnutls_certificate_allocate_credentials` → multiple `gnutls_certificate_set_x509_key_mem` (RSA/ECDSA/Ed25519/Ed448) → `gnutls_certificate_set_known_dh_params` → `gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, xcred)`, all before `gnutls_priority_set_direct`/`gnutls_handshake`.

### gnutls_session_set_data
**Claim:** Client-side-only configuration call for abbreviated/resumed handshakes, must be called before `gnutls_handshake()`. Parameters: `session_data` (non-NULL) and `session_data_size` (non-zero), normally the exact datum previously obtained from `gnutls_session_get_data2()`. Internally unpacks the data (`_gnutls_session_unpack`) — a malformed/attacker-influenced blob here can be rejected with an error but is explicitly a fuzzing-relevant path (ticket-parsing). Returns `GNUTLS_E_INVALID_REQUEST` if data NULL/empty, or the unpack error code, else 0.
**Evidence:**
> lib/session.c:273-330: full implementation; doc states "This function should be called before gnutls_handshake()."
> doc/examples/ex-client-resume.c:71-76: `gnutls_session_set_data(session, sdata.data, sdata.size)` called conditionally before the handshake loop on the second connection.

### gnutls_transport_set_pull_function (and paired push/pull-timeout/read functions)
**Claim:** Configuration call, must be done before `gnutls_handshake()` is invoked (handshake reads via these callbacks as soon as STATE0 processing begins). Sets `session->internals.pull_func`; paired with `gnutls_transport_set_push_function()` (sets `push_func`) for bidirectional I/O, and `gnutls_transport_set_pull_timeout_function()` when a custom pull function + `gnutls_handshake_set_timeout()`/blocking DTLS is used. No return value (void); nothing to free (function pointer only, no allocation).
**Evidence:**
> lib/system_override.c:81-85: `void gnutls_transport_set_pull_function(...) { session->internals.pull_func = pull_func; }`
> lib/system_override.c:111-114 (doc): custom pull function requires setting the pull-timeout callback too.
> fuzz/gnutls_handshake_client_fuzzer.c:70-72: `gnutls_transport_set_push_function(session, error_push); gnutls_transport_set_pull_function(session, error_pull); gnutls_handshake_set_read_function(session, handshake_discard);` — here push/pull are deliberately stubbed to always fail, and instead handshake bytes are injected out-of-band via `gnutls_handshake_write()` called from `handshake_pull()` (fuzz/handshake.h:82-84), which is itself invoked by the harness loop when `gnutls_handshake()` returns `GNUTLS_E_AGAIN`.

## Build notes
**Claim:** Fuzzers are plain C, compiled against the project's own (just-built, static) `libgnutls` and gnulib (`libgnu`), with the target header reached via the project's own include dir (`lib/includes`), not a system-installed gnutls. On OSS-Fuzz, the compiled fuzzer binaries are produced by `fuzz/Makefile.am`'s `oss-fuzz` target using `$CC`/`$CFLAGS` with explicit include paths.
**Evidence:**
> fuzz/Makefile.am:2: `AM_CPPFLAGS = -I$(top_srcdir)/lib/includes -I$(top_builddir)/lib/includes -I$(srcdir) -DSRCDIR=\"$(abs_srcdir)\" -DTEST_RUN`
> fuzz/Makefile.am:5-9: `LDADD = ../gl/libgnu.la ../lib/libgnutls.la $(LIBOBJS) ... $(LIBS)` — links against the in-tree built `libgnutls.la`.
> fuzz/Makefile.am:94-100 (`oss-fuzz` target): `$$CC $$CFLAGS -I$(top_srcdir)/lib/includes/ -I$(top_srcdir) ...` compiles each `*_fuzzer.c` with these include paths plus OSS-Fuzz's `$CC`/`$CFLAGS`/`$LIB_FUZZING_ENGINE`.
> ossfuzz_view/gnutls-c1/build.sh:84-100: `cd "$SRC"/gnutls && ./bootstrap && ./configure --enable-fuzzer-target --disable-shared --enable-static ... && make -C gl && make -C lib && cd fuzz && make oss-fuzz`, then copies `*_fuzzer` binaries, `.dict`/`.options` files, and seed corpora to `$OUT`.
> ossfuzz_view/gnutls-c1/Dockerfile:17-51: base image `gcr.io/oss-fuzz-base/base-builder`, builds dependency libs (libunistring, libidn2, gmp, libtasn1, nettle) from source into `$SRC/deps` before building gnutls itself — all static libs linked into the fuzzer binary.
> fuzz/gnutls_handshake_client_fuzzer.c:29-32: `#include <gnutls/crypto.h>` / `#include <gnutls/gnutls.h>` — target's header is included via the standard `gnutls/gnutls.h` path resolved against `lib/includes` (which contains the `gnutls/` subdirectory with the installed-style headers), not a system path.