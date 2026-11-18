## target: evutil_parse_sockaddr_port

### Declaration & purpose
`include/event2/util.h:697-698`:
```
EVENT2_EXPORT_SYMBOL
int evutil_parse_sockaddr_port(const char *str, struct sockaddr *out, int *outlen);
```
Doc comment (`util.h:677-696`) states recognized formats `"[IPv6]:port"`, `"[IPv6]"`, `"IPv6"`, `"IPv4:port"`, `"IPv4"`; "if no port is specified, port is set to 0"; `out` "should probably be a `struct sockaddr_storage`"; `outlen` is in/out — input is bytes available in `out`, output is bytes written; returns -1 on malformed address/out-of-range port/insufficient `out` size, 0 on success. Defined `evutil.c:2373-2476`.

### P2.1 Init Sequence
**Claim:** No libevent global/library init (no `event_base_new`, no `evthread_use_*`) is required before calling the target — it is a pure string-parsing utility with no static state. The only "init" a caller performs is preparing the input string and the output buffer/length.
**Evidence:**
> evutil.c:2374 `evutil_parse_sockaddr_port(const char *ip_as_string, struct sockaddr *out, int *outlen)` — body only touches locals, `mm_strdup`/`mm_free` indirectly via `evutil_inet_pton_scope`, and libc (`strchr`, `memcpy`, `atoi`, `if_nametoindex`, `strtoul`); no call to any event-base or thread-init API.
> sample/le-proxy.c:261-271 calls it right after `memset(&listen_on_addr,0,...)` / `socklen = sizeof(listen_on_addr)` with no other libevent init beforehand (the `event_base_new()` call only happens afterwards, at line 279, unrelated to the parse call).
> test/regress_util.c:303-306 calls it from a bare `memset(&ss,0,sizeof ss); int len = sizeof(ss);` with no setup.

### P2.2 Parameter Construction
**Claim:** `str` must be a NUL-terminated C string (not NULL — not checked, a NULL `str` will crash in `strchr`/`*ip_as_string`). `out` must point to storage large enough for either `struct sockaddr_in` or `struct sockaddr_in6`; `struct sockaddr_storage` is recommended. `outlen` must be a non-NULL pointer to an `int` initialized to the byte capacity of `out` before the call; after a successful call it holds the number of bytes written (`sizeof(sin)` or `sizeof(sin6)`).
**Evidence:**
> evutil.c:2389 `cp = strchr(ip_as_string, ':');` then `if (*ip_as_string == '[')` — dereferences `str` with no NULL check, so `str==NULL` is undefined behaviour (caller must guarantee NUL-terminated, non-NULL string).
> evutil.c:2451 `if ((int)sizeof(sin6) > *outlen) return -1;` and :2470 `if ((int)sizeof(sin) > *outlen) return -1;` — `*outlen` must be pre-set to the real capacity of `out`.
> util.h:689-692 doc: "`out` ... should probably be a struct sockaddr_storage", "`outlen` ... number of bytes that 'out' can safely hold. Set to the number of bytes used in 'out' on success."
> test/regress_util.c:299-306 — canonical usage: `struct sockaddr_storage ss; int len = sizeof(ss); ... evutil_parse_sockaddr_port(ent->parse, (struct sockaddr*)&ss, &len);`

### P2.3 Object Lifecycle
**Claim:** There is no "object" created by the target itself beyond filling caller-owned memory; lifecycle is simply: caller allocates `sockaddr_storage` (stack or heap) → calls target to fill it → uses the filled sockaddr (e.g., pass to `bind`/`connect`/`evutil_format_sockaddr_port_`/`evutil_sockaddr_cmp`) → storage is freed/goes out of scope normally (no explicit destroy function).
**Evidence:**
> evdns.c:3379-3389 `struct sockaddr_storage ss; ... evutil_parse_sockaddr_port(...,&ss,&len); sa = (struct sockaddr *) &ss; if (sockaddr_getport(sa) == 0) sockaddr_setport(sa, 53);` — stack storage, no separate alloc/free calls.
> test/regress_util.c:359-377 — stack `struct sockaddr_storage ss;` reused across two parse+format round trips with only `memset`/`len=sizeof(ss)` reinitialization between.

### P2.4 Return Value Handling
**Claim:** Return is `0` on success, `-1` on any failure (malformed `[`/`]` bracket, address substring >127 bytes, port <=0 or >65535, pton failure, or `out` too small). Callers must check `< 0` and must NOT use `out`/`*outlen` contents if it failed — on failure `*outlen` is left unmodified from its input value, and `out`'s partial contents are undefined (only filled via `memcpy` after all checks pass).
**Evidence:**
> evutil.c:2393,2397,2414,2431,2436,2449,2452,2469,2471: every failure path is `return -1;` with no writes to `*out`/`*outlen` on those particular early-return paths (the `memcpy(out,&sin[6],...)`/`*outlen=...` only occur at success, e.g. :2454-2456 and :2472-2474).
> sample/le-proxy.c:261-271 `if (evutil_parse_sockaddr_port(...)<0) { /* fallback: treat argv[i] as bare port number */ ... }` — real-world error-path fallback.
> evdns.c:3383-3388 `if (evutil_parse_sockaddr_port(...)) { log(...); return 4; }` — treats any non-zero return as fatal for that call.
> test/regress_util.c:306-312 checks `r<0` against expected `ent->safamily` (0 meaning expected failure).

### P2.5 Cleanup Sequence
**Claim:** No explicit cleanup/free is required for the target's own output (it's a flat struct, not a handle). Internally, `evutil_inet_pton_scope` (reached via the AF_INET6 path) allocates/frees a temporary string (`mm_strdup`/`mm_free`) entirely inside its own call — the target's caller never sees or must free that.
**Evidence:**
> evutil.c:2252-2257 `if (!(tmp_src = mm_strdup(src))) { return -1; } ... r = evutil_inet_pton(af, tmp_src, dst); mm_free(tmp_src); return r;` — alloc+free is self-contained in `evutil_inet_pton_scope`.
> evutil.c:2454/2472 `memcpy(out, &sin6, sizeof(sin6));`/`memcpy(out, &sin, sizeof(sin));` — the result is a plain value copy into caller-owned memory; nothing to free afterward.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited locations.
**Evidence:**
> evutil_parse_sockaddr_port — evutil.c:2374, declared util.h:698.
> evutil_inet_pton_scope — evutil.c:2226, declared util.h:670.
> evutil_inet_pton — evutil.c:2262, declared util.h:674.
> evutil_format_sockaddr_port_ — evutil.c:2479 (no EVENT2_EXPORT_SYMBOL; internal, declared nowhere in public util.h — found only by its use as extern in evutil.c and referenced from test/regress_util.c:365,374,380).
> evutil_inet_ntop — evutil.c:2130, declared util.h:665.

### P2.7 Co-call Constraints
**Claim:** No lock/unlock or begin/commit pairing exists for the target itself. The natural pairing exercised by the project's own tests is parse → format (round trip): `evutil_parse_sockaddr_port` followed by `evutil_format_sockaddr_port_` on the filled sockaddr. `evutil_inet_pton_scope`/`evutil_inet_pton` are called *by* the target, not meant to be paired with it by the harness directly (though they can be fuzzed independently as the Logic Group's other entries).
**Evidence:**
> test/regress_util.c:362-377 (`regress_sockaddr_port_format`): `evutil_parse_sockaddr_port("192.168.1.1:80",...)` then `evutil_format_sockaddr_port_((struct sockaddr*)&ss, cbuf, sizeof(cbuf));` then `tt_str_op(cp,==,"192.168.1.1:80");` repeated for an IPv6 case at :371-377.

### P2.8 Prerequisite State
**Claim:** No global/context state is required: no event_base, no thread-safety init, no file descriptors, no network connection. The only "state" the AF_INET6+zone path touches is the OS network-interface table via `if_nametoindex()`, which is read-only/system-provided and not something the harness must set up (a numeric fallback via `strtoul` is used if the name lookup fails).
**Evidence:**
> evutil.c:2245-2249 `if_index = if_nametoindex(scope + 1); if (if_index == 0) { if_index = strtoul(scope + 1, &check, 10); if (check[0] != '\0') return 0; }` — tries OS interface-name resolution first, falls back to parsing the zone text as a plain number; no setup call needed for either path to be exercised (an unknown name simply falls through to the numeric branch, and a non-numeric unknown name returns failure, i.e. 0 from `evutil_inet_pton_scope`, hence -1 from the target).

### P2.9 Result Usage
**Claim:** Callers use the filled `struct sockaddr*`/`*outlen` (a) as a formatting input to `evutil_format_sockaddr_port_` for human-readable round trip, (b) as a comparison input to `evutil_sockaddr_cmp`, (c) directly passed to socket syscalls (`bind`,`connect`) or to other libevent APIs (`evdns_base_nameserver_ip_add`, DNS `bind-to:`/hosts parsing) that store/forward the sockaddr. The harness should at minimum feed the result into `evutil_format_sockaddr_port_` to complete the parse→format logic group.
**Evidence:**
> test/regress_util.c:365-368 `cp = evutil_format_sockaddr_port_((struct sockaddr*)&ss, cbuf, sizeof(cbuf)); tt_str_op(cp,==,"192.168.1.1:80");` — direct consumer of the filled sockaddr.
> evutil.c:2484-2499 (`evutil_format_sockaddr_port_` body) dispatches on `sa->sa_family` (`AF_INET`/`AF_INET6`) exactly as set by the target, and calls `evutil_inet_ntop` on the address field the target filled (`sin->sin_addr`/`sin6->sin6_addr`) and formats the port the target filled (`sin->sin_port`/`sin6->sin6_port`).
> evdns.c:3383-3391 `evutil_parse_sockaddr_port(ip_as_string,(struct sockaddr *)&ss,&len)` then `sa = (struct sockaddr *) &ss; if (sockaddr_getport(sa) == 0) sockaddr_setport(sa, 53);` then stored into the DNS base's nameserver list (further down, not shown) — consumes both the address bytes and the family tag the target set.
> evdns.c:4375-4378 `evutil_parse_sockaddr_port(val,(struct sockaddr*)&base->global_outgoing_address,&len) ... base->global_outgoing_addrlen = len;` — stores the filled sockaddr plus the updated `*outlen` into persistent config state.
> sample/le-proxy.c:261-277 passes the filled `sockaddr_storage`/`socklen` into `evconnlistener_new_bind`/`bufferevent_socket_connect` (further down in the file) — i.e. real socket operations consume the result.

### P2.10 Behaviour Controls
**Claim:** Behaviour is selected purely by the shape of `str`, not by any flag argument (the function has no mode/flags parameter): a leading `'['` forces the bracketed-IPv6 path; a bare address containing a second `':'` after the first implies IPv6 without brackets; exactly one `':'` implies `IPv4:port`; no `':'` implies a bare address (family then decided by the pton call, defaulting to the `is_ipv6` flag computed from string shape, not by auto-detecting IPv4 vs IPv6 content). Inside `evutil_inet_pton`, the compile-time macros `EVENT__HAVE_INET_PTON` / `USE_INTERNAL_PTON` select the OS `inet_pton` vs libevent's own hand-rolled IPv6 tokenizer (gap `::` handling, `words[8]`, embedded IPv4-in-IPv6 tail). Inside `evutil_inet_pton_scope`, presence of `'%'` in the address selects the zone-id path (`if_nametoindex` then `strtoul` fallback) vs a direct passthrough to `evutil_inet_pton`.
**Evidence:**
> evutil.c:2389-2424 — the full if/else chain selecting `is_ipv6`/`addr_part`/`port_part` by string shape (`'['`, two colons, one colon, no colon).
> evutil.c:2264 `#if defined(EVENT__HAVE_INET_PTON) && !defined(USE_INTERNAL_PTON) return inet_pton(af, src, dst); #else ...` — compile-time dispatch, not a runtime flag.
> evutil.c:2239-2243 `scope = strchr(src, '%'); if (scope == NULL) return evutil_inet_pton(af, src, dst);` — '%' presence gates the zone-id logic.
> evutil.c:2132 similarly for `evutil_inet_ntop`: `#if defined(EVENT__HAVE_INET_NTOP) && !defined(USE_INTERNAL_NTOP)`.

### P2.11 Input Expectations
**Claim:** Input `str` is a plain NUL-terminated C string with no length parameter and no binary header. Constraints enforced in code: (1) if it starts with `'['`, there must be a matching `']'` later in the string or the call fails (evutil.c:2392-2394); (2) the address substring (either the bracketed part or the part before a single `:`) must be ≤127 bytes (`sizeof(buf)-1`) or the call fails (evutil.c:2396-2398, 2413-2415) — oversized addresses are rejected, not truncated; (3) if a port substring is present, `atoi()` is applied and the result must satisfy `1 <= port <= 65535` (0, negative, non-numeric-yielding-0, or >65535 all rejected) (evutil.c:2429-2432); (4) the address part (after stripping brackets/port) must be accepted by `evutil_inet_pton`/`evutil_inet_pton_scope`, which for IPv4 requires `sscanf(src,"%u.%u.%u.%u%c",...)==4` with each octet ≤255 and no trailing garbage captured beyond what `%c` would catch (evutil.c:2271-2277 internal path; OS `inet_pton` otherwise), and for IPv6 requires a valid colon/hex-word grammar with at most one `::` gap and a correct final word count (evutil.c:2280 onward, not fully quoted here but is the internal fallback decoder); (5) a `'%'` in the IPv6 address introduces a zone id, resolved by `if_nametoindex` or decimal `strtoul`, and an unresolvable/non-numeric zone causes failure (evutil.c:2245-2249).
**Evidence:**
> evutil.c:2392-2394 `if (!(cp = strchr(ip_as_string, ']'))) { return -1; }`
> evutil.c:2396-2398 `if (len > sizeof(buf)-1) { return -1; }` (buf is `char buf[128]` at :2378, so max address length inside brackets is 127 bytes).
> evutil.c:2413-2415 same 127-byte cap for the non-bracketed `addr:port` case.
> evutil.c:2429-2432 `port = atoi(port_part); if (port <= 0 || port > 65535) { return -1; }`
> evutil.c:2271-2277 IPv4 sscanf/range-check fallback decoder.
> test/regress_util.c:282-293 (`sa_port_ents`) encodes expected pass/fail cases: `"[ffff::1"` (no `]` → fail), `"[ffff::1]:65599"` (port out of range → fail), `"[ffff::1]:0"`/`"[ffff::1]:-1"` (port <=0 → fail), valid cases `"[ffff::1]:1000"`, `"::1"`, `"1:2::1"`, `"192.168.0.1:50"`, `"1.2.3.4"`.

### Other Logic-Group entries: call protocol & combination with target

**evutil_inet_pton_scope** (`util.h:670`, `evutil.c:2226`)
- Needs before it: nothing beyond a non-NULL `src` C-string and a `dst` buffer sized for the family (`struct in_addr`/`struct in6_addr`) and a non-NULL `unsigned *indexp` (dereferenced unconditionally at evutil.c:2233 `*indexp = 0;` — passing NULL crashes).
- Parameters/paths: `af` must be `AF_INET` or `AF_INET6`; for `af!=AF_INET6` it just forwards to `evutil_inet_pton` (evutil.c:2236-2237); for `AF_INET6`, presence of `'%'` selects the zone path (mm_strdup/mm_free internally, evutil.c:2252-2257).
- Return: `1` success, `0` parse failure, `-1` only on `mm_strdup` OOM (evutil.c:2253).
- Nothing to free by the caller (internal strdup is freed internally).
- Combination with target: this is exactly what the target calls for its IPv6 branch (evutil.c:2447-2448); the harness can call it directly with strings containing `%zoneName` or `%123` to hit the zone path independent of the target's bracket/port splitting, feeding its `dst`/`indexp` results nowhere further (or into a manually built `sockaddr_in6` then to `evutil_format_sockaddr_port_`/`evutil_inet_ntop` for round trip).

**evutil_inet_pton** (`util.h:674`, `evutil.c:2262`)
- Needs before it: `src` non-NULL ("Replacement for inet_pton for platforms which lack it. src must not be NULL." — util.h:672); `dst` sized for the family.
- Parameters/paths: `af==AF_INET` → sscanf-based decimal-dotted decoder (evutil.c:2267-2278); `af==AF_INET6` (if compiled) → manual word/gap tokenizer handling `::`, embedded IPv4 tail, word count 0..8 (evutil.c:2280 onward); compile-time `EVENT__HAVE_INET_PTON`/`USE_INTERNAL_PTON` choose OS vs internal implementation (evutil.c:2264).
- Return: `1` success, `0` malformed, (OS `inet_pton` can also return `-1` for unsupported `af`).
- Nothing to free.
- Combination: called directly by the target for AF_INET and indirectly for AF_INET6 via `evutil_inet_pton_scope`; harness can call it standalone on isolated address strings to target the internal IPv6 tokenizer's gap/word-count logic without going through the bracket/port splitter.

**evutil_format_sockaddr_port_** (`evutil.c:2479`; NOT declared in public `util.h` — internal linkage symbol, no `EVENT2_EXPORT_SYMBOL`)
- Needs before it: a filled `struct sockaddr` with `sa_family` set to `AF_INET` or `AF_INET6` (anything else falls to the generic `"<addr with socktype %d>"` branch, evutil.c:2502-2504) — exactly what the target produces. `out`/`outlen` must be a writable buffer and its capacity (used via `evutil_snprintf`, which truncates safely, evutil.c:2489,2497,2502).
- Parameters/paths: dispatches on `sa->sa_family`; AF_INET formats `"addr:port"`, AF_INET6 formats `"[addr]:port"`, anything else formats a placeholder — these are the only three paths.
- Return: always returns `out` (never NULL) — pointer equality check in tests (`tt_ptr_op(cp,==,cbuf)`, regress_util.c:367).
- Nothing to free (writes into caller-supplied buffer).
- Combination: natural consumer of the target's output in the "parse → format" round trip used by the project's own `regress_sockaddr_port_format` test (regress_util.c:359-383); harness should call it right after the target on the filled sockaddr_storage to validate the round trip and additionally exercise the "unknown family" placeholder path by independently crafting an `ss.ss_family` value outside {AF_INET,AF_INET6} as the test does at regress_util.c:379-383.
- Note: because it is not `EVENT2_EXPORT_SYMBOL`, a harness built only against the public installed header `event2/util.h` (per the "public API" / P3 security-boundary expectation) will NOT see a declaration for it; it must be forward-declared `extern` in the harness (as the project's own test does implicitly by building against the library's internal headers) or the harness should limit itself to `evutil_inet_ntop`/`evutil_inet_pton*`/the target for strict public-API-only harnesses.

**evutil_inet_ntop** (`util.h:665`, `evutil.c:2130`)
- Needs before it: `src` pointing to a valid `struct in_addr`/`struct in6_addr` (the one embedded in the sockaddr produced by the target or `evutil_inet_pton`), `dst` buffer with capacity `len`.
- Parameters/paths: `af==AF_INET` → formats `"%d.%d.%d.%d"`; `af==AF_INET6` → full textual compression logic including `::` longest-gap detection and the embedded-IPv4 special case (`words[0..4]==0 && (...)`, evutil.c:2159-2176); compile-time `EVENT__HAVE_INET_NTOP`/`USE_INTERNAL_NTOP` select OS vs internal implementation.
- Return: `dst` on success, `NULL` if the formatted string would not fit in `len` (evutil.c:2144-2145, 2172-2173, 2214-2215) — callers must check for NULL (as `evutil_format_sockaddr_port_` does at evutil.c:2488,2496).
- Nothing to free.
- Combination: called by `evutil_format_sockaddr_port_` on the address bytes the target filled in; harness can also call it directly on the `sin_addr`/`sin6_addr` extracted from the target's output to exercise formatting edge cases (e.g., the IPv4-in-IPv6 compressed form) independent of the splitter/port logic.

### Logic-Group notes: verification against source

- **Input format for utils_fuzzer**: The Logic Group note that "the harness should treat the fuzz bytes as that string (ensuring NUL termination)" matches the signature (`const char *str`, no length param) — confirmed at evutil.c:2374/util.h:698. **However**, the actual `utils_fuzzer.cc` present in this OSS-Fuzz tree (`${QF_WORK}/ossfuzz_view/libevent-c2/utils_fuzzer.cc` and the matching copy under `src/libevent-c2/utils_fuzzer.cc`) is a stub: it only keeps `evutil_parse_sockaddr_port` linked via `__asm__` aliasing and its `LLVMFuzzerTestOneInput` ignores `data`/`size` and returns 0 without calling anything (utils_fuzzer.cc:10-16, quoted above). It is NOT the real upstream libevent `utils_fuzzer.cc`; it is a placeholder ("Gold harness replaced by an empty stub for baseline generation", utils_fuzzer.cc:1) that the downstream Harness Generator is expected to replace with real logic driven by this report.
- **Seed corpus / dictionary**: No dictionary or seed corpus file specific to `utils_fuzzer` was found in the OSS-Fuzz directory (`${QF_WORK}/ossfuzz_view/libevent-c2` listing: only `ws_fuzzer.dict` exists, tied to `ws_fuzzer.cc`, and `build.sh:49-53` only copies `http.dict`/`http_message_fuzzer.dict`/`ws_fuzzer.dict` — nothing for utils). The best available source of example valid/invalid strings is the project's own test tables: `test/regress_util.c:282-293` (`sa_port_ents`, parse+port cases) and `test/regress_util.c:392-...` (`sa_pred_entries`, bare-address cases for loopback/private-range predicates, also parsed via the target at line 417). These should seed the corpus; there is no separate binary seed-corpus directory referenced by `build.sh`.
- **Init rejections — corrected/confirmed**: The Logic Group description's list matches the source exactly: unmatched `'['` (evutil.c:2392-2394), address substring > 127 bytes i.e. `sizeof(buf)-1` with `buf[128]` (evutil.c:2378,2396-2398,2413-2415), port `<=0` or `>65535` (evutil.c:2430-2432), pton failure (evutil.c:2448-2449, 2468-2469), and `out` too small for the resulting struct (evutil.c:2451-2452, 2470-2471). Confirmed: **there is no strict/lax mode switch** — the function takes exactly 3 parameters (`str,out,outlen`) with no flags argument (util.h:698), so these rejections cannot be relaxed by any caller-supplied option; the only way to change behaviour is the compile-time `USE_INTERNAL_PTON`/`EVENT__HAVE_INET_PTON` macros affecting the nested `evutil_inet_pton`, not the target's own acceptance logic.
- **Structured input**: Confirmed — `sa_port_ents` (regress_util.c:277-294) and `sa_pred_entries` (regress_util.c:388-...,392) are the two tables; both are consumed through `evutil_parse_sockaddr_port` in `regress_sockaddr_port_parse` (regress_util.c:296-...) and `test_evutil_predicates`-style function (around line 410-420 using `sa_pred_entries`). No formal grammar/BNF exists; it's example-string based.
- **Test hooks / debug switches**: Confirmed — no runtime toggle found. The only switches are the compile-time macros `USE_INTERNAL_PTON` and `EVENT__HAVE_INET_PTON` (evutil.c:2264) and the ntop equivalents `USE_INTERNAL_NTOP`/`EVENT__HAVE_INET_NTOP` (evutil.c:2132). `EVENT__HAVE_INET_PTON`/`EVENT__HAVE_INET_NTOP` are generated into `event2/event-config.h`/`evconfig-private.h` by the CMake/autoconf build (included at evutil.c:35-36) based on the host platform's `inet_pton`/`inet_ntop` availability — they are not meant to be flipped by a fuzz harness at runtime; `USE_INTERNAL_PTON`/`USE_INTERNAL_NTOP` are not defined anywhere in the current tree's build files (`build.sh` passes no such `-D` flag), so on the Linux OSS-Fuzz builder the OS `inet_pton`/`inet_ntop` path is used by default and the internal hand-rolled decoder is dead code unless the harness's own build adds `-DUSE_INTERNAL_PTON` (not currently done by `build.sh`).
- **From-memory deserializer**: None found. The target consumes a plain C string directly; there is no separate "deserialize from buffer" step (e.g. no `evutil_parse_sockaddr_port_from_buffer`-style API) — the fuzz bytes map 1:1 to the `str` argument (after NUL-termination).

### Build notes
**Claim:** Fuzzers in this OSS-Fuzz project are C++ (`.cc`), compiled with `$CXX $CXXFLAGS -std=c++17`, with include paths `-I../ -Iinclude` (i.e., the libevent source root and its `include/` directory containing `event2/util.h`), and linked statically against `libevent.a`, `libevent_core.a`, `libevent_pthreads.a`, `libevent_extra.a` plus `$LIB_FUZZING_ENGINE`. The library itself is built via CMake with tests/samples disabled and TLS backends disabled.
**Evidence:**
> ossfuzz_view/libevent-c2/build.sh:21-28: `cmake -DEVENT__DISABLE_MBEDTLS=ON -DEVENT__DISABLE_OPENSSL=ON -DEVENT__LIBRARY_TYPE=STATIC -DEVENT__DISABLE_TESTS=ON -DEVENT__DISABLE_SAMPLES=ON ../ && make -j$(nproc) && make install`
> ossfuzz_view/libevent-c2/build.sh:31-37: `for fuzzers in $(find $SRC -name '*_fuzzer.cc'); do ... $CXX $CXXFLAGS -std=c++17 -I../ -Iinclude $fuzzers $LIB_FUZZING_ENGINE ./lib/libevent.a ./lib/libevent_core.a ./lib/libevent_pthreads.a ./lib/libevent_extra.a -o $OUT/$fuzz_basename; done`
> ossfuzz_view/libevent-c2/Dockerfile:17-22: `FROM gcr.io/oss-fuzz-base/base-builder` then clones `libevent` and the Google `fuzzing` dict/corpus repo, copies `build.sh run_tests.sh *.cc *.c *.dict` into `$SRC`.
> Since `evutil_parse_sockaddr_port` is declared `EVENT2_EXPORT_SYMBOL` in the public `event2/util.h` (util.h:697-698) and that header is reachable via the `-Iinclude` path, a harness only needs `#include <event2/util.h>` to get a correct prototype for the target, `evutil_inet_pton`, `evutil_inet_pton_scope`, and `evutil_inet_ntop`; `evutil_format_sockaddr_port_` has no public declaration (not `EVENT2_EXPORT_SYMBOL`, evutil.c:2478-2479) and must be forward-declared manually by the harness if used.