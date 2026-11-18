## target: llhttp_execute

### P2.0 Main path to keep (every input) — read first
**Claim:** Per input: 3 control bytes → `llhttp_settings_init` with all span and notify callbacks installed (input-chosen return values: `on_headers_complete` 0/1/2/-1, notify callbacks `HPE_PAUSED` on a bit, span callbacks -1 on a bit) → `llhttp_init(type from input)` → the six classic lenient setters from input bits → `llhttp_execute` over 1..4 chunks with `llhttp_resume` / `llhttp_resume_after_upgrade` after pauses and error getters on hard errors → optional `llhttp_finish` → result getters. `llhttp_set_lenient_header_value_relaxed` is never called (library infinite loop in this version).
**Rule for the harness:** keep every step of this main path on every input, fed with the full fuzz input; Logic-Group entries and the other sections of this report only add calls on top of it (selected by spare input bits), they never replace or dilute a main-path step.
**Evidence:**
> src/native/api.h:39-47: `on_headers_complete` return contract `0` / `1` / `2` (HPE_PAUSED_UPGRADE) / `-1` / `HPE_PAUSED`; `on_body` "Possible return values 0, -1, HPE_USER".
> README.md:186-197: after `HPE_PAUSED` "the execution can be resumed with `llhttp_resume()`"; after upgrade callers "need to invoke `llhttp_resume_after_upgrade()`"; README.md:204,219 `llhttp_finish` / `llhttp_message_needs_eof`.
> src/native/api.h:234-305: `llhttp_set_lenient_headers`, `_chunked_length`, `_keep_alive`, `_transfer_encoding`, `_version`, `_data_after_close`; 317-354 the newer `_optional_lf_after_cr`, `_optional_cr_before_lf`, `_optional_crlf_after_chunk`, `_spaces_after_chunk_size`; 371 `llhttp_set_lenient_header_value_relaxed`.
> src/llhttp/http.ts:841-846: `checkLenient = testLenientFlags(HEADERS, {1: n('header_value_lenient')}, testLenientFlags(HEADER_VALUE_RELAXED, {1: n('header_value_relaxed')}, error))`; 848-858 `n('header_value_otherwise').peek('\r', ...).peek('\n', ...).otherwise(checkLenient)`; 866-868 `n('header_value_relaxed').match(RELAXED_HEADER_CHARS, n('header_value_relaxed')).otherwise(n('header_value_otherwise'))` — neither `.otherwise` consumes a byte, so a byte outside RELAXED_HEADER_CHARS loops forever.
> src/native/http.c:37-95 (`llhttp__after_headers_complete`): lenient_flags and F_CHUNKED / content_length / upgrade select the body paths.


### P2.1 Init Sequence
**Claim:** Before calling `llhttp_execute`, the caller must (1) zero/initialize a `llhttp_settings_t` via `llhttp_settings_init`, optionally set `on_*` callback pointers, then (2) call `llhttp_init(parser, type, settings)` which calls the generated `llhttp__internal_init(parser)` and sets `parser->type`/`parser->settings`. No other global init is required. `llhttp_reset` may be used instead of a fresh `llhttp_init` to start a new message while keeping type/settings/lenient flags.
**Evidence:**
> src/native/api.c:33-39: `llhttp_init` calls `llhttp__internal_init(parser)` then sets `parser->type = type; parser->settings = (void*) settings;`
> src/native/api.c:130-132: `llhttp_settings_init` is `memset(settings, 0, sizeof(*settings));`
> README.md:78,87,93 (canonical usage): `llhttp_settings_init(&settings); settings.on_message_complete = handle_on_message_complete; llhttp_init(&parser, HTTP_BOTH, &settings); ... llhttp_execute(&parser, request, request_len);`
> src/native/api.h:74-77: "NOTE: lifetime of `settings` has to be at least the same as the lifetime of the `parser`... settings has to be either a static variable or be allocated with malloc, new, etc."

### P2.2 Parameter Construction
**Claim:** `llhttp_execute(llhttp_t* parser, const char* data, size_t len)` requires: `parser` a pointer to an already-`llhttp_init`'d (or `llhttp_reset`'d) `llhttp_t`; `data` a caller-owned buffer (need not be NUL-terminated, raw bytes, may contain arbitrary/binary content) valid for at least `len` bytes; `len` the exact number of bytes available to parse in this call. The function may be called multiple times with different chunks of the same logical stream (streaming/incremental parse) — the parser retains state between calls.
**Evidence:**
> src/native/api.c:125-127: `llhttp_errno_t llhttp_execute(llhttp_t* parser, const char* data, size_t len) { return llhttp__internal_execute(parser, data, data + len); }` — passes `data` and `data+len` (begin/end pointers) straight to the generated core.
> src/native/api.h:116-131 (doc comment on llhttp_execute): "Parse full or partial request/response, invoking user callbacks along the way." — confirms partial/chunked calls are supported.
> src/native/api.h:203-206: "Returns the pointer to the last parsed byte before the returned error. The pointer is relative to the `data` argument of `llhttp_execute()`" — confirms `data` is treated as a flat byte buffer indexed by `llhttp_get_error_pos`.

### P2.3 Object Lifecycle
**Claim:** Create (`llhttp_t parser;` stack/heap struct, no own alloc API for parser object itself other than `llhttp_alloc`/`llhttp_free` used only in the `__wasm__` build) → configure (`llhttp_settings_init` + set callbacks + `llhttp_init`, optionally `llhttp_set_lenient_*`) → use (`llhttp_execute` one or more times, optionally `llhttp_pause`/`llhttp_resume`/`llhttp_resume_after_upgrade` interleaved, optionally `llhttp_finish` at EOF) → destroy (no explicit destructor for the non-wasm/native build; the struct is plain memory, caller frees/owns it; `llhttp_alloc`'d parsers must `llhttp_free`).
**Evidence:**
> src/native/api.c:71-79 (compiled only `#if defined(__wasm__)`): `llhttp_t* llhttp_alloc(...){ ... malloc(sizeof(llhttp_t)); llhttp_init(...); return parser; }` / `void llhttp_free(llhttp_t* parser){ free(parser); }` — this pair exists but is wasm-only; the OSS-Fuzz native build does not compile with `__wasm__` defined (see Build notes), so the harness should allocate `llhttp_t`/`llhttp_settings_t` itself (stack, static, or malloc) and does not need to call `llhttp_free`.
> src/native/api.c:110-122: `llhttp_reset` preserves type/settings/data/lenient_flags and re-`llhttp__internal_init`s — the supported "parse next message with same parser" reset path, used instead of destroy+recreate for pipelining.

### P2.4 Return Value Handling
**Claim:** `llhttp_execute` returns an `llhttp_errno_t`. `HPE_OK` (0) means success/all bytes consumed without error. Any other non-pause errno means a fatal parse error: per the header doc, once execute returns a non-pause error it will keep returning that same error on every subsequent call until `llhttp_init()` is invoked again (the parser is "poisoned"). Two special pause values must be handled specially: `HPE_PAUSED` (returned when a callback returned `HPE_PAUSED`, or after `llhttp_pause()`), resumable with `llhttp_resume()`; `HPE_PAUSED_UPGRADE` (returned after a CONNECT/Upgrade message is fully parsed, i.e. `on_headers_complete` returned `2`), resumable with `llhttp_resume_after_upgrade()`. `llhttp_get_errno(parser)` / `llhttp_get_error_reason(parser)` / `llhttp_get_error_pos(parser)` can be used to inspect the error after a call.
**Evidence:**
> src/native/api.h:116-131: "If any of `llhttp_data_cb` returns errno not equal to `HPE_OK` - the parsing interrupts, and such errno is returned... If `HPE_PAUSED` was used... the execution can be resumed with `llhttp_resume()`... In a special case of CONNECT/Upgrade ... `HPE_PAUSED_UPGRADE` is returned... NOTE: if this function ever returns a non-pause type error, it will continue to return the same error upon each successive call up until `llhttp_init()` is called."
> src/native/api.c:188-190: `llhttp_errno_t llhttp_get_errno(const llhttp_t* parser) { return parser->error; }`
> src/native/api.c:203-205: `llhttp_get_error_pos` returns `parser->error_pos`.
> src/native/api.c:170-185: `llhttp_resume`/`llhttp_resume_after_upgrade` each gate on the exact current `parser->error` value (`HPE_PAUSED` / `HPE_PAUSED_UPGRADE` respectively) before clearing it to 0, otherwise no-op — calling the wrong resume function is a silent no-op, not an error.

### P2.5 Cleanup Sequence
**Claim:** For the native (non-wasm) build used by OSS-Fuzz there is no dedicated "destroy parser" call — `llhttp_t` and `llhttp_settings_t` are plain structs owned by the caller (stack-allocated in the README example) and simply go out of scope / are freed by the harness's own allocator if heap-allocated. No internal buffers are allocated by `llhttp_init`/`llhttp_execute` that need freeing. If the harness used `llhttp_alloc` (wasm-only) it must pair it with `llhttp_free`.
**Evidence:**
> src/native/api.c:33-39,110-122,125-132: none of `llhttp_init`, `llhttp_reset`, `llhttp_execute`, `llhttp_settings_init` call `malloc`/allocate heap memory in the native path (only the `#if defined(__wasm__)` block at lines 42-81 does, guarding `llhttp_alloc`/`llhttp_free`).
> README.md:73-99: example declares `llhttp_t parser; llhttp_settings_t settings;` as plain stack locals with no explicit free at the end of `main`.

### P2.6 API Existence
**Claim:** All entry functions named in the task exist in the current codebase (`src/native/api.c` / `src/native/api.h`), except that `llhttp_init`, `llhttp_execute`, etc. depend on the generated `llhttp__internal_t`/`llhttp__internal_init`/`llhttp__internal_execute` symbols which are produced at build time (not checked into `src/`) via `make generate` (`bin/generate.ts`) into `build/llhttp.h` and `build/c/llhttp.c`.
**Evidence:**
> src/native/api.c:33 `void llhttp_init(...)`; :110 `void llhttp_reset(...)`; :125 `llhttp_errno_t llhttp_execute(...)`; :130 `void llhttp_settings_init(...)`; :135 `llhttp_errno_t llhttp_finish(...)`; :160 `void llhttp_pause(...)`; :170 `void llhttp_resume(...)`; :179 `void llhttp_resume_after_upgrade(...)`; :237 `llhttp_set_lenient_headers`; :246 `llhttp_set_lenient_chunked_length`; :263 `llhttp_set_lenient_transfer_encoding`; :255 `llhttp_set_lenient_keep_alive`; :287 `llhttp_set_lenient_optional_lf_after_cr`; :295 `llhttp_set_lenient_optional_crlf_after_chunk`; :303 `llhttp_set_lenient_optional_cr_before_lf`; :311 `llhttp_set_lenient_spaces_after_chunk_size` — all present, all exist exactly as named.
> src/native/http.c:129,156: `llhttp_message_needs_eof` and `llhttp_should_keep_alive` definitions exist (core helper, also public per api.h:150,156).
> Makefile:37-42: `build/llhttp.h: generate` / `build/c/llhttp.c: generate` — confirms `llhttp__internal_*` symbols (used by api.c but not defined in `src/`) are produced by the `generate` step (`node --import tsx bin/generate.ts`), run by `build.sh` (`npm ci`; `yes | make build/libllhttp.a`) before compiling the fuzzer.
> No separate `llhttp.h`/`llhttp.c` exists under `src/` prior to generation — confirmed by `Glob` finding zero matches for `**/llhttp.h`/`**/llhttp.c` in the source tree.

### P2.7 Co-call Constraints
**Claim:** Paired/ordering constraints found:
- `llhttp_settings_init(settings)` must precede setting any `on_*` members and must precede `llhttp_init` (settings is passed by pointer into init and stored; init does not itself zero it).
- `llhttp_init(parser, type, settings)` must precede the first `llhttp_execute` call on that parser.
- `llhttp_pause(parser)` must NOT be called from within a user callback (doc warns callbacks should return `HPE_PAUSED` instead); it is meant to be called from outside, between `llhttp_execute` calls, to force the *next* call to return `HPE_PAUSED`.
- `llhttp_resume(parser)` is only meaningful (non-no-op) immediately after an `llhttp_execute` call that returned `HPE_PAUSED`; `llhttp_resume_after_upgrade(parser)` only after a call that returned `HPE_PAUSED_UPGRADE`. Calling either when `parser->error` doesn't match the expected value is a silent no-op.
- `llhttp_finish(parser)` is meant to be called once, after the last `llhttp_execute` call, to signal EOF — it is not a replacement for `llhttp_execute` and does not take data.
- `llhttp_reset(parser)` substitutes for a second `llhttp_init` call when starting a new message on the same parser object while keeping type/settings/lenient flags (used for pipelined messages in one run).
**Evidence:**
> src/native/api.h:161-163: "Important: do not call this from user callbacks! User callbacks must return `HPE_PAUSED` if pausing is required." (on `llhttp_pause`)
> src/native/api.c:170-176: `llhttp_resume` early-returns if `parser->error != HPE_PAUSED`.
> src/native/api.c:179-185: `llhttp_resume_after_upgrade` early-returns if `parser->error != HPE_PAUSED_UPGRADE`.
> src/native/api.h:135-144 (doc on `llhttp_finish`): "This method should be called when the other side has no further bytes to send... This method will invoke `on_message_complete()` callback if the request was terminated safely."
> src/native/api.c:106-110 doc + :110-122 impl of `llhttp_reset`: "Reset an already initialized parser back to the start state, preserving the existing parser type, callback settings, user data, and lenient flags."

### P2.8 Prerequisite State
**Claim:** Before the first `llhttp_execute`, the parser must have: a valid `type` (`HTTP_REQUEST`=1, `HTTP_RESPONSE`=2, `HTTP_BOTH`=0) set via `llhttp_init`; a `settings` pointer that remains valid for the parser's entire lifetime (not stack-local if it would go out of scope before the parser is done, per the lifetime NOTE); optionally lenient flags set via the `llhttp_set_lenient_*` setters (these act on `parser->lenient_flags`, so must be called after `llhttp_init`/`llhttp_reset`, since those routines reset/preserve that field). No network/file/global state is required; the parser is self-contained.
**Evidence:**
> src/native/api.h:72-77 (doc on llhttp_init): "NOTE: lifetime of `settings` has to be at least the same as the lifetime of the `parser`... settings has to be either a static variable or be allocated with `malloc`, `new`, etc."
> src/llhttp/constants.ts:52-56: `export const TYPE = { BOTH: 0, REQUEST: 1, RESPONSE: 2 }` — the three valid `llhttp_type_t` values generated into the public header's enum.
> src/native/api.c:110-122: `llhttp_reset` explicitly *preserves* `lenient_flags` captured before `llhttp__internal_init`, and `llhttp_init` → `llhttp__internal_init` (generated) is what zeroes it initially — so lenient setters logically apply after init/reset, not before.
> src/native/api.c:237-325: each `llhttp_set_lenient_*` function directly OR's/AND's `parser->lenient_flags`, requiring `parser` to already be an initialized `llhttp_t`.

### P2.9 Result Usage
**Claim:** `llhttp_execute`'s direct return value (`llhttp_errno_t`) is consumed by the caller to decide whether to continue feeding more data, call `llhttp_resume`/`llhttp_resume_after_upgrade`, or abort/report error via `llhttp_errno_name`/`llhttp_get_error_reason`/`llhttp_get_error_pos`. The parsed *data* itself (method, URL, headers, body, status) is not returned by `llhttp_execute` directly — it is delivered through the registered `llhttp_settings_t` callbacks during the call (spans `(at, length)` pointing into the caller's `data` buffer for `on_url`/`on_header_field`/`on_header_value`/`on_body`/etc., and `llhttp_cb` notifications for completion events), and through parser-state getters callable any time after (or during, from within a callback) execute returns: `llhttp_get_type`, `llhttp_get_http_major/minor`, `llhttp_get_method`, `llhttp_get_status_code`, `llhttp_get_upgrade`. After execute completes (or pauses), callers typically call `llhttp_should_keep_alive(parser)` to decide on connection reuse, and `llhttp_message_needs_eof(parser)` to decide whether `llhttp_finish(parser)` must be called at stream EOF.
**Evidence:**
> README.md:93-98: canonical result-usage pattern — `enum llhttp_errno err = llhttp_execute(&parser, request, request_len); if (err == HPE_OK) {...} else { fprintf(stderr, "Parse error: %s %s\n", llhttp_errno_name(err), ...`
> src/native/api.c:351-355 (`llhttp__on_url`) / :407-411 (`llhttp__on_header_field`) / :421-425 (`llhttp__on_header_value`) / :449-453 (`llhttp__on_body`): each uses `SPAN_CALLBACK_MAYBE(s, on_X, p, endp - p)` to hand `(at=p, length=endp-p)` spans — pointers into the `data` buffer passed to `llhttp_execute` — to the user's registered callback.
> src/native/http.c:116-127 (`llhttp__after_message_complete`): calls `llhttp_should_keep_alive(parser)` internally, i.e. that function is part of the core's own control flow and is also exposed publicly (api.h:152-156) for the embedder to query after execute returns.
> src/native/http.c:129-169: `llhttp_message_needs_eof`/`llhttp_should_keep_alive` implementations read `parser->flags`, `parser->http_major/minor`, `parser->method`/`parser->status_code` — i.e. state set by execute, queried afterward.
> src/native/api.h:146-150,152-156: doc on these two getters: "Returns 1 if the incoming message is parsed until the last byte, and has to be completed by calling llhttp_finish() on EOF" / "Returns 1 if there might be any other messages following the last that was successfully parsed."

### P2.10 Behaviour Controls
**Claim:** Processing path is selected by:
1. `llhttp_type_t` passed to `llhttp_init`/implicit in `llhttp_reset` — `HTTP_REQUEST` vs `HTTP_RESPONSE` vs `HTTP_BOTH` (auto-detect) chooses request-line vs status-line grammar (e.g. `llhttp__after_headers_complete` branches on `parser->type == HTTP_RESPONSE`).
2. `on_headers_complete` callback return value — `0` proceed, `1` assume no body, `2` assume no body AND make `llhttp_execute` return `HPE_PAUSED_UPGRADE`, `-1` error, `HPE_PAUSED` pause.
3. `lenient_flags` bits set by the `llhttp_set_lenient_*` family, each gating a distinct branch, e.g. `LENIENT_CHUNKED_LENGTH`/`LENIENT_TRANSFER_ENCODING` directly checked in `llhttp__after_headers_complete` to decide whether conflicting `Transfer-Encoding`+chunked-not-final triggers `HPE_INVALID_TRANSFER_ENCODING` (return code 5) or is tolerated.
4. Request/response headers parsed (`Transfer-Encoding: chunked` sets `F_CHUNKED`, presence of `Content-Length` sets `content_length>0`) select between `llhttp__after_headers_complete` return paths 0 (no body)/1 (CONNECT+pause)/2 (chunk_size_start)/3 (body_identity)/4 (body_identity_eof)/5 (invalid transfer-encoding), which map onto different generated-state-machine continuations.
5. `parser->upgrade` / `HTTP_CONNECT` method / 101 status combined with `F_UPGRADE`/`F_CONNECTION_UPGRADE` flags select the "exit to different protocol" early-return path (`HPE_PAUSED_UPGRADE`-eligible).
**Evidence:**
> src/native/api.h:37-46 (settings_s doc on `on_headers_complete`): the 0/1/2/-1/HPE_PAUSED return-value contract.
> src/native/http.c:37,41-50: `hasBody = parser->flags & F_CHUNKED || parser->content_length > 0;` then branch on `parser->upgrade && (method==CONNECT || F_SKIPBODY || !hasBody)` or `type==HTTP_RESPONSE && status_code==101` → `return 1` (exit to other protocol).
> src/native/http.c:70-95: `else if (parser->flags & F_CHUNKED) return 2;` vs `else if (parser->flags & F_TRANSFER_ENCODING) { if (type==HTTP_REQUEST && !(lenient_flags & LENIENT_CHUNKED_LENGTH) && !(lenient_flags & LENIENT_TRANSFER_ENCODING)) return 5; else ... }` — direct lenient_flags gating.
> src/native/http.c:11-26 (`llhttp__before_headers_complete`): sets `parser->upgrade` from `F_UPGRADE`/`F_CONNECTION_UPGRADE`/`HTTP_CONNECT` method, feeding the branch above.
> src/llhttp/constants.ts:52-56: `TYPE = {BOTH:0, REQUEST:1, RESPONSE:2}` — the type values that select grammar entry point.

**Additional behaviour controls (previously missing):**
- Callback return values are behaviour controls on every input: `on_headers_complete` 0/1/2/-1 (api.h:39-47), notify callbacks `HPE_PAUSED` (then `llhttp_resume`), span callbacks -1 → `HPE_USER`.
- Chunking of the stream into 1..4 `llhttp_execute` calls, and `llhttp_finish` on a bit (README.md:186-219).
- Optional callbacks left NULL on one bit exercise the `CALLBACK_MAYBE` / `SPAN_CALLBACK_MAYBE` skip paths (src/native/api.c).
- Library hang: `llhttp_set_lenient_header_value_relaxed(parser, 1)` without `llhttp_set_lenient_headers` loops forever on a header-value byte outside RELAXED_HEADER_CHARS (src/llhttp/http.ts:841-868). Do not enable it in a fuzz harness for this version.

### P2.11 Input Expectations
**Claim:** `llhttp_execute`'s `data`/`len` is an opaque byte range (no required NUL-termination — `len` fully determines extent; `data+len` is the end pointer passed to the generated core). No minimum/maximum buffer size is enforced by the public API itself; size limits (e.g. header count/length) are enforced inside the generated state machine via error codes rather than input preconditions. The function tolerates partial input (a message split across multiple `llhttp_execute` calls) — this is its primary intended streaming mode, matching the comment "Parse full or partial request/response". It expects well-formed-or-not HTTP/1.x bytes: method, URL, version, headers (`field: value\r\n` pairs, optionally folded/lenient per flags), optional chunked or content-length body, terminated by `\r\n\r\n` after headers; malformed input is reported via specific `llhttp_errno_t` codes (e.g. `HPE_INVALID_METHOD`, `HPE_INVALID_HEADER_TOKEN`, `HPE_INVALID_CONTENT_LENGTH`, `HPE_INVALID_CHUNK_SIZE`, `HPE_INVALID_TRANSFER_ENCODING`, `HPE_INVALID_EOF_STATE`) rather than crashing/undefined behavior, by design.
**Evidence:**
> src/native/api.c:125-127: `llhttp__internal_execute(parser, data, data + len)` — the only input validation is the pointer arithmetic itself; no bounds length cap in this layer.
> src/native/api.h:116-118: "Parse full or partial request/response, invoking user callbacks along the way." — explicit partial-call support.
> src/native/api.h:203-209 (`llhttp_get_error_pos`): "Returns the pointer to the last parsed byte before the returned error. The pointer is relative to the `data` argument of `llhttp_execute()`." — confirms byte-indexed, not string-based, processing.
> src/llhttp/constants.ts:8-50 (`ERROR` map): enumerates the full set of `HPE_*` codes returned on malformed input (e.g. `INVALID_METHOD:6, INVALID_URL:7, INVALID_HEADER_TOKEN:10, INVALID_CONTENT_LENGTH:11, INVALID_CHUNK_SIZE:12, INVALID_EOF_STATE:14, INVALID_TRANSFER_ENCODING:15`).

## Other entries of this feature (call protocol, combined with the target)

### llhttp_init
**Call protocol:** `void llhttp_init(llhttp_t* parser, llhttp_type_t type, const llhttp_settings_t* settings)`. Must be called before any `llhttp_execute` on `parser`, and again whenever the parser needs to be fully reset after a fatal (non-pause) error (per api.h:128-130 doc on `llhttp_execute`: "if this function ever returns a non-pause type error, it will continue to return the same error upon each successive call up until `llhttp_init()` is called"). `type` selects `HTTP_REQUEST`(1)/`HTTP_RESPONSE`(2)/`HTTP_BOTH`(0) grammar entry. `settings` must outlive `parser`. No return value; always succeeds (calls generated `llhttp__internal_init` then sets two fields). Nothing to free afterward.
**Evidence:** src/native/api.c:33-39; src/native/api.h:72-80,128-130; src/llhttp/constants.ts:52-56.
**Combination with target:** Called exactly once per logical "parser lifetime" before the first `llhttp_execute`, or re-called to recover from a poisoned (fatal-errored) parser before continuing to fuzz more input.

### llhttp_settings_init
**Call protocol:** `void llhttp_settings_init(llhttp_settings_t* settings)`. Zero-initializes the whole struct (`memset(settings,0,sizeof(*settings))`) so unset callback slots are NULL (CALLBACK_MAYBE/SPAN_CALLBACK_MAYBE treat NULL as "skip, err=0"). Must be called before assigning individual `on_*` members and before passing `settings` to `llhttp_init`. No return value, nothing to free (caller owns the struct's storage, typically `static` or stack with lifetime ≥ parser's).
**Evidence:** src/native/api.c:130-132; src/native/api.c:7-16 (CALLBACK_MAYBE treats `settings->NAME == NULL` as no-op).
**Combination with target:** Called once at harness setup, before `llhttp_init`; the harness registers whichever `on_*` callbacks it wants exercised (e.g. ones that return varied codes 0/1/2/-1/HPE_PAUSED to steer `llhttp_execute`'s internal control flow).

### llhttp_finish
**Call protocol:** `llhttp_errno_t llhttp_finish(llhttp_t* parser)`. Called after all available bytes have been fed via `llhttp_execute` (i.e., to simulate EOF/connection close). If `parser->error != 0` (already in an error state) it immediately returns 0 (no-op). Otherwise dispatches on internal `parser->finish` state: `HTTP_FINISH_SAFE_WITH_CB` → invokes `on_message_complete` callback, returns its errno if non-OK, else falls through; `HTTP_FINISH_SAFE` → returns `HPE_OK`; `HTTP_FINISH_UNSAFE` → sets reason "Invalid EOF state" and returns `HPE_INVALID_EOF_STATE`; any other value aborts(). Caller should check the returned `llhttp_errno_t` like an `llhttp_execute` result.
**Evidence:** src/native/api.c:135-157; src/native/api.h:135-144.
**Combination with target:** Optional, called once at the very end of a harness run (after the last `llhttp_execute` call) especially to exercise `llhttp_message_needs_eof`-driven body-until-close semantics; only meaningful when `llhttp_get_errno(parser)==HPE_OK` beforehand (an existing error short-circuits it to a silent 0 return).

### llhttp_resume
**Call protocol:** `void llhttp_resume(llhttp_t* parser)`. Only effective (clears `parser->error` to 0) when `parser->error == HPE_PAUSED`; otherwise no-op. No return value. Must be called by the harness (not from within a callback) between two `llhttp_execute` calls, after the first returned `HPE_PAUSED`.
**Evidence:** src/native/api.c:170-176; src/native/api.h:167-173.
**Combination with target:** `llhttp_execute(...) -> HPE_PAUSED` ⇒ `llhttp_resume(parser)` ⇒ `llhttp_execute(parser, remaining_data, remaining_len)` continuing from where it paused — essential for exercising pause/resume control flow driven by callback return values.

### llhttp_resume_after_upgrade
**Call protocol:** `void llhttp_resume_after_upgrade(llhttp_t* parser)`. Only effective when `parser->error == HPE_PAUSED_UPGRADE`; otherwise no-op. No return value.
**Evidence:** src/native/api.c:179-185; src/native/api.h:175-181.
**Combination with target:** `llhttp_execute(...) -> HPE_PAUSED_UPGRADE` (after a CONNECT/Upgrade message is fully parsed, or `on_headers_complete` returned `2`) ⇒ harness decides whether to continue parsing the tunnel/upgraded stream (`llhttp_resume_after_upgrade` then `llhttp_execute` again with subsequent bytes as "new protocol data" reinterpretation) — exercises the CONNECT/Upgrade-specific branch at src/native/http.c:42-50 (`return 1`).

### llhttp_set_lenient_headers / llhttp_set_lenient_chunked_length / llhttp_set_lenient_transfer_encoding (and related lenient setters)
**Call protocol:** `void llhttp_set_lenient_X(llhttp_t* parser, int enabled)`. Must be called on an already-`llhttp_init`'d parser (writes to `parser->lenient_flags`), typically right after `llhttp_init`/`llhttp_reset` and before `llhttp_execute`. `enabled` nonzero sets the corresponding bit (`LENIENT_HEADERS`, `LENIENT_CHUNKED_LENGTH`, `LENIENT_TRANSFER_ENCODING`, `LENIENT_KEEP_ALIVE`, `LENIENT_VERSION`, `LENIENT_DATA_AFTER_CLOSE`, `LENIENT_OPTIONAL_LF_AFTER_CR`, `LENIENT_OPTIONAL_CRLF_AFTER_CHUNK`, `LENIENT_OPTIONAL_CR_BEFORE_LF`, `LENIENT_SPACES_AFTER_CHUNK_SIZE`, `LENIENT_HEADER_VALUE_RELAXED`), zero clears it. No return value.
**Evidence:** src/native/api.c:237-325; src/native/api.h:223-281 (security-relevant doc warnings: "Enabling this flag can pose a security issue since you will be exposed to request smuggling attacks. USE WITH CAUTION!").
**Combination with target:** Called 0+ times after `llhttp_init`/`llhttp_reset`, before `llhttp_execute`, to select strict vs lenient branches in `llhttp__after_headers_complete` (src/native/http.c:73-93) and other generated-grammar checks — directly the "divergent paths for the same bytes" security surface described in the task background; the harness should vary these flags (e.g. derived from fuzz input bytes) across iterations.

### llhttp_reset
**Call protocol:** `void llhttp_reset(llhttp_t* parser)`. Called on an already-`llhttp_init`'d parser, any time after a message completes (or even mid-stream to force restart), to begin parsing a new message while preserving `type`, `settings`, `data` (user pointer), and `lenient_flags`. Internally re-runs `llhttp__internal_init`. No return value.
**Evidence:** src/native/api.c:106-122.
**Combination with target:** Used between consecutive `llhttp_execute` call sequences in one harness run to fuzz pipelined/multiple-message inputs without re-registering settings/type/lenient flags each time — cheaper and more representative than a full `llhttp_init` call for the "next request on the same connection" scenario.

## Build notes
**Claim:** The fuzzer is built as plain C, compiled against a static library `libllhttp.a` built from generated+native sources, with the target's header included via `-I./build/` (the generated `build/llhttp.h`, which in turn `#include`s `src/native/api.h`'s declarations are folded into it by the generator). The driver file is `test/fuzzers/fuzz_parser.c`, compiled with `$CC $CFLAGS` (C, not C++) and linked by `$CXX $CXXFLAGS $LIB_FUZZING_ENGINE` against `./build/libllhttp.a`.
**Evidence:**
> ossfuzz_view/llhttp-c1/build.sh:18-25:
> ```
> rm -rf "${WORK:?}/"*
> make clean
> npm ci
> yes | make build/libllhttp.a
> $CC $CFLAGS -c ./test/fuzzers/fuzz_parser.c -I./build/ ./build/libllhttp.a -o $WORK/fuzz_parser.o
> $CXX $CXXFLAGS $LIB_FUZZING_ENGINE -fuse-ld=lld -I./build/ ./build/libllhttp.a $WORK/fuzz_parser.o -o $OUT/fuzz_parser
> ```
> (`npm ci` + `make build/libllhttp.a` triggers the TypeScript→C generator (`Makefile:87-88`: `generate: node --import tsx bin/generate.ts`) producing `build/llhttp.h` and `build/c/llhttp.c`, then compiles+archives `build/c/llhttp.o build/native/api.o build/native/http.o` into `build/libllhttp.a` — Makefile:19-39.)
> ossfuzz_view/llhttp-c1/Dockerfile:17-23: base image `gcr.io/oss-fuzz-base/base-builder`, installs `make autoconf automake libtool`, runs `install_javascript.sh` (Node/npm toolchain for the generator), clones `nodejs/llhttp` into `$SRC/llhttp`.
> The current harness stub `test/fuzzers/fuzz_parser.c` (lines 1-16) is an empty placeholder that only force-links the `llhttp_execute` symbol via `__asm__` alias and does not call it — this is the "baseline" stub the downstream Harness Generator is meant to replace with a real harness that follows the init→settings→execute(→finish/resume) protocol documented above.
> The target's header the real harness should `#include` is the generated `llhttp.h` under `-I./build/` (not `src/native/api.h` directly), since `build/llhttp.h` is the public combined header that declares `llhttp_t`, `llhttp_settings_t`, `llhttp_type_t`, `llhttp_errno_t` plus all `llhttp_*` entry points (api.h is `#include`d/merged into it by the generator per `src/llhttp/c-headers.ts` and `Makefile:37-39`: `build/native/%.o: src/native/%.c build/llhttp.h src/native/api.h`).