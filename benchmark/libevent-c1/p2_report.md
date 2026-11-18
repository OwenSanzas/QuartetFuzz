## target: evtag_unmarshal

### Summary of definition
`event_tagging.c:472-485`
```c
int
evtag_unmarshal(struct evbuffer *src, ev_uint32_t *ptag, struct evbuffer *dst)
{
	int len;

	if ((len = evtag_unmarshal_header(src, ptag)) == -1)
		return (-1);

	if (evbuffer_add(dst, evbuffer_pullup(src, len), len) == -1)
		return (-1);

	evbuffer_drain(src, len);

	return (len);
}
```
Declared at `include/event2/tag.h:113-115` (resolved project path `include/event2/tag.h`, same relative path as task's `event2/tag.h`).

### P2.1 Init Sequence
**Claim:** No mandatory library-wide init is required before calling `evtag_unmarshal`. The project's own historical convention calls `evtag_init(void)` first, but `evtag_init` is a no-op in current code, so it is not functionally required (only a stylistic step some tests keep). The only hard prerequisite is that `src` must be a valid, already-allocated `struct evbuffer *` that contains (or is expected to contain) tag-encoded bytes, and `dst` must be a valid, already-allocated `struct evbuffer *` to receive the payload.
**Evidence:**
> event_tagging.c:105-108: `void evtag_init(void) { }` — body is empty, confirming it performs no state setup.
> test/regress.c:2654: `evtag_init();` called before `evtag_unmarshal_timeval` fuzz loop (stylistic legacy call, found in real test code).
> test/regress.c:2648: `struct evbuffer *tmp = evbuffer_new();` — evbuffer must be created with `evbuffer_new()` before any evtag_* call uses it.
> event_tagging.c:472: signature requires `struct evbuffer *src` and `struct evbuffer *dst`, both non-NULL evbuffers.

### P2.2 Parameter Construction
**Claim:** `src` — a `struct evbuffer *` created via `evbuffer_new()` (or any evbuffer API), filled with raw bytes via `evbuffer_add()`/`evbuffer_add_buffer()` etc. before the call; may contain arbitrary attacker-controlled bytes. `ptag` — pointer to a `ev_uint32_t` to receive the decoded tag id; may be `NULL` if the caller doesn't need the tag (checked internally, see evidence). `dst` — a `struct evbuffer *`, also must be already allocated via `evbuffer_new()`; receives the raw payload bytes copied out of `src`. `dst` must not itself be `src` (no aliasing is handled/expected in the implementation; the pattern in rpcgen always uses a fresh temp buffer, see P2.9).
**Evidence:**
> event_tagging.c:443-456 (`evtag_unmarshal_header`): `if (decode_tag_internal(ptag, evbuf, 1) == -1) return (-1);` and `decode_tag_internal` at event_tagging.c:243-244 does `if (ptag != NULL) *ptag = number;` showing `ptag` may legitimately be NULL.
> event_tagging.c:479: `evbuffer_add(dst, evbuffer_pullup(src, len), len)` — `dst` is written into directly; must be a valid evbuffer.
> event_rpcgen.py:388-390: generated callers always allocate a brand-new `tmp = evbuffer_new();` then call `evtag_unmarshal(evbuf, &tag, tmp)`, i.e. `dst` is always distinct from `src`.

### P2.3 Object Lifecycle
**Claim:** Create (`evbuffer_new()` for both src and dst) → configure (fill `src` with data via `evbuffer_add`/`evbuffer_add_buffer`/network read) → use (`evtag_unmarshal(src, &tag, dst)`) → consume result from `dst` (e.g. pass to a `_unmarshal` routine) → destroy (`evbuffer_free()` on both buffers, in caller-chosen order, typically dst first since it is temp-scoped).
**Evidence:**
> event_rpcgen.py:388-399: `struct evbuffer *tmp = evbuffer_new(); ... evtag_unmarshal(evbuf, &tag, tmp) ... msg_unmarshal(msg, tmp) ... error: evbuffer_free(tmp); return (res);` — complete create→use→consume→destroy pattern for generated unmarshal wrappers (the canonical real-world caller of the target).
> test/regress.c:2648,2682: `tmp = evbuffer_new();` ... `evbuffer_free(tmp);` bracket the whole test using several evtag_* functions.

### P2.4 Return Value Handling
**Claim:** `evtag_unmarshal` returns `-1` on any failure (header decode failure, `len > INT_MAX`, insufficient payload available in `src`, or `evbuffer_add` failure/OOM); otherwise it returns the number of payload bytes copied into `dst` (>= 0, same as `len` from the header, which can be 0). Callers must treat `-1` specially and must not trust the return length blindly beyond checking it's not `-1`; in rpcgen-generated code it is directly compared with `== -1`.
**Evidence:**
> event_tagging.c:476-485: `if ((len = evtag_unmarshal_header(src, ptag)) == -1) return (-1); if (evbuffer_add(dst, evbuffer_pullup(src, len), len) == -1) return (-1); ... return (len);`
> event_rpcgen.py:390: `if (evtag_unmarshal(evbuf, &tag, tmp) == -1 || tag != need_tag) goto error;` — real caller checks both the -1 sentinel and (separately, downstream) the decoded tag.
> event_tagging.c:449-453 (in `evtag_unmarshal_header`, which the target calls): `if (evtag_decode_int(&len, evbuf) == -1 || len > INT_MAX) return (-1); if (evbuffer_get_length(evbuf) < len) return (-1);` — shows the length bound checks performed before the target ever touches payload bytes.

### P2.5 Cleanup Sequence
**Claim:** On both success and failure paths the caller is responsible for freeing `dst` (the target itself never frees any evbuffer, on success or failure) and eventually freeing `src`. In the canonical rpcgen pattern, `dst` ("tmp") is freed via a single `error:`-labelled cleanup that runs regardless of the outcome; `src` is normally owned/freed by an even higher-level caller (e.g. the RPC layer), not by code calling `evtag_unmarshal` directly.
**Evidence:**
> event_rpcgen.py:396-400: `res = 0; error: evbuffer_free(tmp); return (res);` — `tmp` (== `dst` param of `evtag_unmarshal`) is freed unconditionally after use, whether `evtag_unmarshal`/`_unmarshal` succeeded or not.
> test/regress.c:2682: `evbuffer_free(tmp);` under a `end:` label — same unconditional-free pattern for a src buffer used with the evtag_* family.
> event_tagging.c:472-485: no `evbuffer_free` call inside `evtag_unmarshal` itself — confirms the target does not own/free either buffer.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase at the cited locations.
**Evidence:**
> event_tagging.c:443 `evtag_unmarshal_header`
> event_tagging.c:472 `evtag_unmarshal` (target)
> event_tagging.c:490 `evtag_unmarshal_int`
> event_tagging.c:516 `evtag_unmarshal_int64`
> event_tagging.c:544 `evtag_unmarshal_fixed`
> event_tagging.c:563 `evtag_unmarshal_string`
> event_tagging.c:585 `evtag_unmarshal_timeval`
> event_tagging.c:262 `evtag_marshal`, :271 `evtag_marshal_buffer`, :282 `evtag_marshal_int`, :293 `evtag_marshal_int64`, :305 `evtag_marshal_string`, :312 `evtag_marshal_timeval`
> event_tagging.c:201 `decode_tag_internal` (static), :367 `decode_int_internal` (static), :374 `decode_int64_internal` (static)
> include/event2/tag.h:61-143 declares all public entries above (`evtag_init`, `evtag_unmarshal_header`, `evtag_marshal*`, `evtag_encode_int*`, `evtag_unmarshal`, `evtag_peek`, `evtag_peek_length`, `evtag_payload_length`, `evtag_consume`, `evtag_unmarshal_int`, `evtag_unmarshal_int64`, `evtag_unmarshal_fixed`, `evtag_unmarshal_string`, `evtag_unmarshal_timeval`).
> evbuffer primitives `evbuffer_new`, `evbuffer_free`, `evbuffer_add`, `evbuffer_remove`, `evbuffer_pullup`, `evbuffer_drain`, `evbuffer_get_length` all declared in `include/event2/buffer.h:152,159,358,373,1041,739,256`.

### P2.7 Co-call Constraints
**Claim:** `evbuffer_new()`/`evbuffer_free()` must be paired for each evbuffer used (both `src` and `dst`). `evtag_marshal*` (encode) and `evtag_unmarshal*` (decode) are the natural inverse pair used to build round-trip test/fuzz inputs. Within the `*_unmarshal_*` family there is no explicit "begin/commit" pairing beyond create/use/free of the evbuffers; `evtag_unmarshal_header` + manual payload consumption (`evbuffer_pullup`+`evbuffer_add`+`evbuffer_drain`, as the target itself does) is the "paired" idiom seen repeatedly (also in `_fixed`, `_string`, `_timeval` variants, each pairs `evtag_unmarshal_header` with its own exactly-once drain of `len` bytes).
**Evidence:**
> event_tagging.c:476-482: header unmarshal paired with `evbuffer_pullup`+`evbuffer_add`+`evbuffer_drain(src, len)` exactly once.
> event_tagging.c:551-559 (`evtag_unmarshal_fixed`): `evtag_unmarshal_header` then `evbuffer_remove(src, data, len)` (which itself drains) — one header call, one payload-consuming call.
> event_tagging.c:593-609 (`evtag_unmarshal_timeval`): one `evtag_unmarshal_header` call then two `decode_int_internal` calls at increasing offsets, finished by a single `evbuffer_drain(evbuf, len)` in the `done:` path — shows header-call then payload sub-decodes then one final drain, regardless of success/fail (drain always executes).
> test/regress.c:2648/2682 and event_rpcgen.py:388/399: every `evbuffer_new()` is matched by exactly one `evbuffer_free()`.

### P2.8 Prerequisite State
**Claim:** The only prerequisite state is: (1) `src` and `dst` already exist as `struct evbuffer *` objects (no shared global state, no connection/context object required); (2) `src` should contain bytes in the tag/length/value wire format documented in the file header comment for a non-error round-trip, though the function is explicitly designed to also safely reject arbitrary/malformed bytes (hence it is the main documented fuzz target). No bufferevent, event_base, or socket is required.
**Evidence:**
> event_tagging.c:72-94 (top-of-file wire-format comment): documents `Stream = TaggedData*`, `TaggedData = Tag Length Data`, `Tag = HByte* LByte`, `Integer = NNibbles Nibble* Padding?` — the expected input grammar.
> test/regress.c:2645-2683 (`evtag_fuzz`): feeds pure random bytes directly into a freshly-created evbuffer with no other setup, demonstrating no additional prerequisite context is needed to call the `evtag_unmarshal_*` family safely.

### P2.9 Result Usage
**Claim:** The canonical consumer of `evtag_unmarshal`'s filled `dst` evbuffer is a generated `<msgname>_unmarshal(msg, tmp)` function (from `event_rpcgen.py`), which further parses the now-isolated payload bytes field-by-field (itself calling `evtag_unmarshal_int`, `_string`, `_fixed`, etc. on `tmp`). After that, `tmp` is freed. The decoded `*ptag` value is compared against an expected tag id (`tag != need_tag`) to validate message framing before trusting the payload.
**Evidence:**
> event_rpcgen.py:388-401:
> ```c
> struct evbuffer *tmp = evbuffer_new();
> if (evtag_unmarshal(evbuf, &tag, tmp) == -1 || tag != need_tag)
>   goto error;
> if (%(name)s_unmarshal(msg, tmp) == -1)
>   goto error;
> res = 0;
> error:
>   evbuffer_free(tmp);
>   return (res);
> ```
> event_rpcgen.py:1011-1013 (generated per-field code calling `evtag_unmarshal_<type>` similarly on tag-id-checked sub-fields) shows the same tag-id-check-then-consume pattern recurring at the field level, i.e. downstream code always gates on the tag before trusting payload content.
> No direct non-generated, non-test caller of `evtag_unmarshal` itself was found elsewhere in project sources (`evrpc.c` does not call it directly; `event_rpcgen.py`-generated code is the only real consumer pattern found via Grep across the tree).

### P2.10 Behaviour Controls
**Claim:** `evtag_unmarshal` itself has no flags/modes — it always copies exactly `len` raw bytes (the length decoded from the header) with no type interpretation. Within the sibling entries, several parameters/paths select different processing:
- `need_tag` (in `evtag_unmarshal_int/_int64/_fixed/_string/_timeval`): if decoded tag != `need_tag`, the call fails immediately — gates all payload-specific parsing.
- `len` (in `evtag_unmarshal_fixed`): caller-supplied expected length; mismatch vs. decoded `tag_len` (`(size_t)tag_len != len`) causes failure — exercises the "exact length" branch not present in plain `evtag_unmarshal`.
- nibble count bound in `DECODE_INT_INTERNAL` macro: `maxnibbles` is 8 for `decode_int_internal` (32-bit ints, used by `evtag_unmarshal_int`/`_fixed`/`_timeval`) vs 16 for `decode_int64_internal` (64-bit, used only by `evtag_unmarshal_int64`) — selects which nibble-count ceiling rejects malformed length encodings.
- `dodrain` parameter of `decode_tag_internal` (1 in all `evtag_unmarshal_*`/`evtag_decode_tag`, 0 in `evtag_peek`/`evtag_peek_length`/`evtag_payload_length`) — selects whether the tag bytes are consumed or only peeked.
- shift>=28 branch inside `decode_tag_internal` (event_tagging.c:221-227) — rejects tag encodings that would overflow 32 bits (continuation bytes beyond 5 total), a distinct error path reachable by feeding tags with many 0x80-continuation bytes.
**Evidence:**
> event_tagging.c:499: `if (need_tag != tag) return (-1);` (evtag_unmarshal_int)
> event_tagging.c:552: `tag != need_tag) return (-1);` (evtag_unmarshal_fixed, combined with header call)
> event_tagging.c:555-556: `if ((size_t)tag_len != len) return (-1);` (evtag_unmarshal_fixed exact-length check)
> event_tagging.c:370-371 vs :376-377: `DECODE_INT_INTERNAL(number, 8, ...)` vs `DECODE_INT_INTERNAL(number, 16, ...)` — maxnibbles differs between 32-bit and 64-bit decode paths.
> event_tagging.c:247-253 and :400-404,406-408,424-429: `dodrain` argument 1 vs 0 passed to `decode_tag_internal` from `evtag_decode_tag`/`evtag_unmarshal_header` vs `evtag_peek`/`evtag_peek_length`/`evtag_payload_length`.
> event_tagging.c:221-227: `if (shift >= 28) { if (shift > 28) return (-1); if ((lower & 0x7f) > 15) return (-1); } ...` — overflow-guard branch in tag decoding.

### P2.11 Input Expectations
**Claim:** The target expects `src` to begin with a valid tag encoding (one or more bytes, continuation bit `0x80` set on all but the last byte, at most enough bytes so the accumulated shift does not exceed 32 bits — else rejected), immediately followed by a nibble-encoded length integer (first byte's high nibble = nibbles-1, 0..8 nibbles for the 32-bit length field used here, consuming `(nibbles>>1)+1` bytes), and then exactly that many raw bytes as payload. There is no magic/terminator byte; truncated input (not enough bytes pulled up, or `evbuffer_get_length(src) < len`) is explicitly rejected, and a decoded length `> INT_MAX` is explicitly rejected. Payload length can legitimately be 0. The function performs no NUL-termination or type-checking of the payload itself (raw bytes only); that distinguishes it from `_string`/`_fixed`/`_int*`/`_timeval` siblings.
**Evidence:**
> event_tagging.c:72-93 (wire-format doc comment, see P2.8 quote).
> event_tagging.c:213-247 (`decode_tag_internal`): pulls up at most `sizeof(number)+1` (5) bytes, scans for a byte without the high bit set (`done=1`) within `pullup_len`, else returns -1 (`if (!done) return (-1);`).
> event_tagging.c:449-453 (`evtag_unmarshal_header`): `if (evtag_decode_int(&len, evbuf) == -1 || len > INT_MAX) return (-1); if (evbuffer_get_length(evbuf) < len) return (-1);` — explicit INT_MAX ceiling and truncation check before the target copies anything.
> event_tagging.c:479: `evbuffer_add(dst, evbuffer_pullup(src, len), len)` — `len==0` is valid (evbuffer_pullup/evbuffer_add with 0 length is a no-op copy, not rejected), consistent with payload length 0 being acceptable.

## Other Entries of the Logic Group (call protocol, as used alongside the target)

### evtag_unmarshal_header
**Protocol:** Takes `(struct evbuffer *evbuf, ev_uint32_t *ptag)`. No init beyond a valid, allocated `evbuf`. `ptag` may be NULL. Returns `-1` on error, else the payload length (`>=0`, bounded to `INT_MAX`). On success it has *already drained* the tag+length header bytes from `evbuf` (via `decode_tag_internal(..., 1)` and `evtag_decode_int`), leaving exactly `len` payload bytes at the front of `evbuf` — caller must then consume those `len` bytes itself (the target does this via `evbuffer_pullup`+`evbuffer_add`+`evbuffer_drain`; other siblings use `evbuffer_remove` or `mm_malloc`+`evbuffer_remove`). Nothing to free (it allocates nothing). Used standalone (e.g. `evtag_consume`) to skip a tagged record without extracting its payload.
**Evidence:**
> event_tagging.c:442-456: body shown above; `evtag_consume` (event_tagging.c:458-467) calls it and then just `evbuffer_drain(evbuf, len)` to skip the payload entirely, showing the "peek header + skip payload" usage mode, as opposed to the target's "peek header + copy payload elsewhere" mode.
**Combination with target:** `evtag_unmarshal` literally *is* `evtag_unmarshal_header` (event_tagging.c:476) followed by payload copy; a harness exercising the target already exercises this entry's core logic. Calling `evtag_unmarshal_header` directly (with a NULL `dst`-equivalent, i.e. just not materializing payload) additionally covers the "peek-only" use without a `dst` evbuffer at all — useful to broaden coverage without needing a second evbuffer.

### evtag_unmarshal_int
**Protocol:** `(struct evbuffer *evbuf, ev_uint32_t need_tag, ev_uint32_t *pinteger)`. Needs an allocated `evbuf` positioned at a tag boundary. Internally re-implements tag-decode + explicit `need_tag` match + `evtag_decode_int` length + `evbuffer_get_length(evbuf) < len` check + `decode_int_internal(pinteger, evbuf, 0)` (32-bit nibble decode, maxnibbles=8) + unconditional `evbuffer_drain(evbuf, len)`. Returns `-1` on any failure (including `need_tag` mismatch or `result > len`), else the number of bytes the integer encoding itself used (`result`, which is `<= len`; any extra bytes in `[result, len)` are padding that is drained but not reflected in `pinteger`). Nothing to free.
**Evidence:** event_tagging.c:489-513.
**Combination with target:** independent decode path (does NOT call `evtag_unmarshal_header`/target); a harness can call it directly on the same kind of raw `src` evbuffer the target consumes, to cover the 32-bit-nibble/`need_tag`-mismatch branches the plain target never reaches.

### evtag_unmarshal_int64
**Protocol:** Same shape as `evtag_unmarshal_int` but `ev_uint64_t *pinteger` and uses `decode_int64_internal` (maxnibbles=16), exercising the wider nibble-count ceiling. Returns `-1`/`result` same convention. Nothing to free.
**Evidence:** event_tagging.c:515-539.
**Combination with target:** independent of the target; add to harness to cover the `maxnibbles=16` branch of `DECODE_INT_INTERNAL` not reached via `evtag_unmarshal`/`evtag_unmarshal_int`.

### evtag_unmarshal_fixed
**Protocol:** `(struct evbuffer *src, ev_uint32_t need_tag, void *data, size_t len)`. **Caller must pre-allocate `data` with at least `len` writable bytes** (stack buffer or heap) — this function does not allocate. Calls `evtag_unmarshal_header(src, &tag)` (same header-parse as target) then requires `tag_len == len` exactly (strict, unlike target which accepts any length) then `evbuffer_remove(src, data, len)`. Returns `0` on success, `-1` on error. Nothing to free (caller owns `data`'s lifetime, typically stack-allocated in callers).
**Evidence:** event_tagging.c:543-560.
**Combination with target:** shares `evtag_unmarshal_header` with the target; a harness should call this with a small fixed-size stack buffer (e.g. `char buf[N]; evtag_unmarshal_fixed(src, tag, buf, N)`) on the *same style* of `src` evbuffer used for the target, to hit the `tag_len != len` mismatch branch (a path the target itself can't hit since it has no "expected length").

### evtag_unmarshal_string
**Protocol:** `(struct evbuffer *evbuf, ev_uint32_t need_tag, char **pstring)`. `*pstring` is **output-allocated by this function** via `mm_malloc(tag_len + 1)` — caller must `free`/`mm_free` (or call the matching project free, see note) the resulting string after use, since this function returns ownership. On `mm_malloc` failure returns `-1` without touching `*pstring` (left uninitialized — caller must not use `*pstring` unless return is `0`). On success NUL-terminates and returns `0`.
**Evidence:** event_tagging.c:562-582, esp. :573-579 `*pstring = mm_malloc(tag_len + 1); ... evbuffer_remove(evbuf, *pstring, tag_len); (*pstring)[tag_len] = '\0';`.
**Combination with target:** shares `evtag_unmarshal_header`. A harness exercising this entry must free the returned string on success to avoid a leak (use `free()` — libevent's `mm_malloc`/`mm_free` default to libc malloc/free unless `event_set_mem_functions` is used; no such override is set in the harness context here). This path specifically exercises "allocation sized by attacker-controlled length" (`tag_len` comes straight from the wire).

### evtag_unmarshal_timeval
**Protocol:** `(struct evbuffer *evbuf, ev_uint32_t need_tag, struct timeval *ptv)`. `ptv` is caller-allocated (stack `struct timeval`). Calls `evtag_unmarshal_header` then two sequential `decode_int_internal` calls at offsets `0` and `offset` to fill `tv_sec`/`tv_usec`, checked against `offset+offset2 > len`. Always drains `len` bytes at the `done:` label regardless of success (`evbuffer_drain(evbuf, len)` unconditionally executes), so unlike other siblings it never leaves a partially-consumed record on `need_tag` mismatch or decode failure. Returns `0` success, `-1` failure.
**Evidence:** event_tagging.c:584-610, esp. the `done:`/final `evbuffer_drain(evbuf, len)` at line 608 executing on every path including `goto done` from mismatches.
**Combination with target:** shares `evtag_unmarshal_header`; this is the function used directly in the project's own fuzz-style regression test (`test/regress.c:2645` `evtag_fuzz`) feeding pure random bytes with no other setup — strong precedent that this is safe/intended to call on arbitrary untrusted buffers, same as the target.

### evtag_marshal (and evtag_marshal_buffer/_int/_int64/_string/_timeval)
**Protocol:** `void evtag_marshal(struct evbuffer *evbuf, ev_uint32_t tag, const void *data, ev_uint32_t len)` — pure encoder, no return value, cannot fail observably (barring allocation failure inside evbuffer internals which aborts/returns via evbuffer's own contract, not surfaced here). Requires only an allocated `evbuf` and optionally non-NULL `data` for `len>0`. Used to build syntactically valid tag/length/value records.
**Evidence:** event_tagging.c:261-268 (encodes tag via `evtag_encode_tag`, then length via `evtag_encode_int`, then raw bytes via `evbuffer_add`).
**Combination with target:** this is the direct *inverse* of the target. A harness can optionally use `evtag_marshal(src_evbuf, tag, payload, payload_len)` to produce a "well-formed" `src` for round-trip coverage (`evtag_marshal` → `evtag_unmarshal` should recover `tag`/payload), in addition to feeding raw/fuzzed bytes directly into `src` for the decoder's error-path coverage — exactly the dual strategy the task description recommends and the one `test/regress.c:2645-2683` (`evtag_fuzz`, mixing pure-random bytes with one `evtag_marshal_timeval`-produced well-formed prefix + corruption) already demonstrates for a sibling entry.

## Build notes

**Claim:** Fuzzers for this project are compiled as C++ (`.cc`), statically linked against libevent's built static archives, with the project root and an `include` directory on the include path so `#include <event2/tag.h>` resolves. The OSS-Fuzz-provided `evtag_fuzzer.cc` currently present in the fuzzing dir is a stub/placeholder (not a real harness) that only keeps the symbol `evtag_unmarshal` linked in via an `__asm__` alias trick and returns 0 immediately — it is not a working P2-compliant harness and must be rewritten by the downstream generator using the protocol documented above.

**Evidence:**
> `ossfuzz_view/libevent-c1/build.sh:31-37`:
> ```bash
> for fuzzers in $(find $SRC -name '*_fuzzer.cc'); do
>   fuzz_basename=$(basename -s .cc $fuzzers)
>   $CXX $CXXFLAGS -std=c++17 -I../ -Iinclude \
>       $fuzzers $LIB_FUZZING_ENGINE ./lib/libevent.a ./lib/libevent_core.a  \
>       ./lib/libevent_pthreads.a ./lib/libevent_extra.a \
>       -o $OUT/$fuzz_basename
> done
> ```
> — confirms: C++ compiler (`$CXX`), `-std=c++17`, include paths `-I../` (project source root, where `event2/tag.h`-style includes resolve relative to the install/staging layout) and `-Iinclude` (the generated/installed headers directory containing `event2/tag.h`), linked statically against `libevent.a`, `libevent_core.a`, `libevent_pthreads.a`, `libevent_extra.a`.
> `ossfuzz_view/libevent-c1/build.sh:19-28`: project is configured/built via CMake first (`cmake -DEVENT__LIBRARY_TYPE=STATIC ... && make && make install`) producing the static libs and installed headers referenced above.
> `ossfuzz_view/libevent-c1/Dockerfile:17-22`: base image `gcr.io/oss-fuzz-base/base-builder`; sources cloned from upstream `libevent/libevent` repo; fuzzer sources (`*.cc`, `*.c`, `*.dict`) copied from the OSS-Fuzz integration directory into `$SRC`.
> `ossfuzz_view/libevent-c1/evtag_fuzzer.cc:1-16`: current stub —
> ```c
> /* Gold harness replaced by an empty stub for baseline generation. */
> ...
> QF_EXTERN void qf_target_0(void) __asm__("evtag_unmarshal");
> __attribute__((used)) static void (*const qf_keep[])(void) = {qf_target_0};
> QF_EXTERN int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
>   (void)data; (void)size;
>   return 0;
> }
> ```
> confirms no real call to `evtag_unmarshal` exists yet in the current build artifact; the downstream harness generator must write real code using `#include <event2/buffer.h>` and `#include <event2/tag.h>` (both found under `include/event2/...` in this source tree, matching the `-Iinclude` build flag) to construct `struct evbuffer *src/dst` via `evbuffer_new()`, fill `src` from fuzzer `data`/`size` via `evbuffer_add()`, call `evtag_unmarshal(src, &tag, dst)`, and free both evbuffers.