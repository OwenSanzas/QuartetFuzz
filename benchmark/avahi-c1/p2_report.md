## target: avahi_string_list_parse

### P2.1 Init Sequence
**Claim:** No library-wide init is required before `avahi_string_list_parse`. It is a pure, standalone parsing function: it takes a raw byte buffer `data`/`size` and a `AvahiStringList **ret` out-parameter. No `avahi_client`/`avahi_server`/`avahi_simple_poll` context, no prior `avahi_string_list_new`/`add_*` call, and no global state is needed. The only preconditions are the two `assert()`s in its body.
**Evidence:**
> avahi-common/strlst.c:69-74:
> ```
> int avahi_string_list_parse(const void* data, size_t size, AvahiStringList **ret) {
>     const uint8_t *c;
>     AvahiStringList *r = NULL;
>     assert(data);
>     assert(ret);
> ```
> avahi-common/strlst-test.c:81: `r = avahi_string_list_parse(data, size, &b);` — called directly on a freshly filled local buffer `data[1024]`, no prior initialization of any avahi subsystem.
> avahi-core/dns.c:579-588: called while decoding a DNS TXT RR from a wire packet (`avahi_dns_packet_get_rptr(p)`), itself only after `rdlength > 0` has been checked — this is data validation, not library init.

### P2.2 Parameter Construction
**Claim:** `data` is any readable byte buffer (may be heap, stack, or a pointer into a larger wire packet) and must not be NULL (`assert(data)`); `size` is the number of valid bytes at `data` (can legitimately be 0); `ret` is `AvahiStringList **`, a non-NULL out-parameter (`assert(ret)`) that the function assigns on success (0) and leaves **untouched** on failure (-1).
**Evidence:**
> avahi-common/strlst.c:73-74: `assert(data); assert(ret);` — both pointers must be valid (non-NULL); passing NULL for either is undefined behaviour (debug builds abort).
> avahi-common/strlst.c:99: `*ret = r;` only executed on the success path (after the while loop completes without `goto fail`).
> avahi-common/strlst.c:103-105: on `fail:` label, `*ret` is **not** written; caller must not read `*ret` after a -1 return unless they pre-initialized it.
> avahi-common/strlst-test.c:34: buffer is a plain stack array `uint8_t data[1024]`; size is `size_t size` previously obtained from `avahi_string_list_serialize`.
> avahi-core/dns.c:582: `data` is `avahi_dns_packet_get_rptr(p)` (pointer into the packet buffer) and `size` is `rdlength` taken from the DNS record header, i.e. attacker controlled length from the wire.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: (1) obtain/construct a byte buffer (receive over network, or build via `avahi_string_list_serialize`), (2) call `avahi_string_list_parse(data, size, &list)`, (3) on success (`return 0`) use `list` (which may legitimately be `NULL` for an empty/zero-size input) through the other entry points (`equal`, `to_string`, `find`, `get_pair`, `copy`, `serialize`, `length`, `get_next`, `get_text`, `get_size`), (4) release with `avahi_string_list_free(list)`. On failure (`return -1`) the function has already freed any partially-built list internally — the caller must NOT call `avahi_string_list_free` on `*ret` since `*ret` was never set.
**Evidence:**
> avahi-common/strlst.c:69-106 (full function): builds the list node by node via `avahi_string_list_add_arbitrary`, and on any failure (`k > size` overflow, or OOM from `avahi_string_list_add_arbitrary` returning NULL) jumps to `fail:` which calls `avahi_string_list_free(r)` (r is the partially built list) and returns -1 — cleanup is internal to the function on the error path.
> avahi-common/strlst-test.c:81-90: `r = avahi_string_list_parse(data, size, &b); assert(r==0); ... avahi_string_list_free(b);` — create via parse, use (`equal`, `to_string`), then free.
> avahi-common/strlst-test.c:132-134: `r = avahi_string_list_parse(data, size, &a); assert(r == 0); assert(!a);` — size==1 buffer containing a single NUL byte (empty-list serialization) parses to `a == NULL`; a successful parse CAN yield a NULL list with no node to free.

### P2.4 Return Value Handling
**Claim:** Return value is `int`: `0` = success (the `AvahiStringList*` behind `ret` is valid, possibly NULL for an empty encoding); `-1` = failure (malformed length-prefix that overflows the remaining buffer, or allocation failure, OR in `strlst.h` doc `-1` for too-long overall encoding). Callers must check for `< 0` and must NOT free/use `*ret` in that case (it was not touched, and any partial internal list was already freed internally).
**Evidence:**
> avahi-common/strlst.c:83-84: `if (k > size) goto fail; /* Overflow */`
> avahi-common/strlst.c:89-90: `if (!(n = avahi_string_list_add_arbitrary(r, c, k))) goto fail; /* OOM */`
> avahi-common/strlst.c:99-105: success returns 0 after `*ret = r;`; failure path returns -1 without touching `*ret`.
> avahi-core/dns.c:582-583: `if (avahi_string_list_parse(...) < 0) return -1;` — caller treats any negative return as a hard parse failure and aborts record consumption (no free of `r->data.txt.string_list` attempted here since it was never set).
> avahi-compat-libdns_sd/compat.c:1119-1121: `if (avahi_string_list_parse(txtRecord, txtLen, &txt) < 0) return kDNSServiceErr_Invalid;` — maps failure to a public compat error code, `txt` is left untouched/uninitialized-safe because it was declared `AvahiStringList *txt = NULL;` at compat.c:1100 before the call (caller-side safety net, not provided by the function itself).

### P2.5 Cleanup Sequence
**Claim:** Only one resource is owned by the result: the `AvahiStringList*` chain itself, freed exactly once via `avahi_string_list_free(list)`. If `avahi_string_list_parse` failed (`-1`), nothing needs to be freed by the caller for `*ret` (either leave the caller's own pre-initialized NULL, or it is simply unset). Any `char*`/`uint8_t*` strings extracted afterward via `avahi_string_list_to_string`, `avahi_string_list_get_pair` (key/value) must be freed with `avahi_free()` independently of the list.
**Evidence:**
> avahi-common/strlst.c:108-116: `void avahi_string_list_free(AvahiStringList *l) { while (l) { n = l->next; avahi_free(l); l = n; } }` — single list-wide free function, walks `next` pointers.
> avahi-common/strlst-test.c:90: `avahi_string_list_free(b);` after use (b from parse).
> avahi-common/strlst-test.c:124-125: `avahi_string_list_free(a); avahi_string_list_free(b);` — both original list and copy freed independently (they are fully separate allocations after `avahi_string_list_copy`).
> avahi-common/strlst.c:109 (to_string) `avahi_free(t)` pattern shown at strlst-test.c:60,68,88,98,109-110,121-122 — extracted strings from `to_string`/`get_pair` freed with `avahi_free`, separate from `avahi_string_list_free`.
> avahi-compat-libdns_sd/compat.c:1123-1126: `if (type_info_parse(...) < 0) { avahi_string_list_free(txt); return kDNSServiceErr_Invalid; }` — list freed on a *later* failure after a successful parse, demonstrating free-on-any-subsequent-error-path discipline.

### P2.6 API Existence
**Claim:** All of the following exist in the current codebase, declared in `avahi-common/strlst.h` and defined in `avahi-common/strlst.c`: `avahi_string_list_parse` (h:120, c:69), `avahi_string_list_serialize` (h:117, c:229), `avahi_string_list_equal` (h:124, c:296), `avahi_string_list_to_string` (h:112, c:162), `avahi_string_list_find` (h:153, c:441), `avahi_string_list_get_pair` (h:160, c:486), `avahi_string_list_copy` (h:127, c:354), and the core helpers `avahi_string_list_add_anonymous` (h:92, c:34), `avahi_string_list_add_arbitrary` (h:85, c:49), `avahi_string_list_free` (h:64, c:108), `avahi_string_list_reverse` (h:130, c:118).
**Evidence:**
> avahi-common/strlst.h:64,85,92,112,117,120,124,127,130,153,160 (declarations, see quoted header excerpt above).
> avahi-common/strlst.c:34,49,69,108,118,162,229,296,354,441,486 (definitions, confirmed by Read above).

### P2.7 Co-call Constraints
**Claim:** The relevant pairing is `avahi_string_list_parse` (or any other list-producing call: `_new`, `_add*`, `_copy`) paired with exactly one terminal `avahi_string_list_free`. There is no explicit "begin/commit" pairing beyond create/destroy. `avahi_string_list_serialize` and `avahi_string_list_parse` are an encode/decode pair (round-trip), not a resource-lifetime pair — `serialize` does not allocate anything owned by the caller when `data` is non-NULL (it writes into caller-supplied `data` buffer) and returns only a `size_t` used-length; when `data == NULL` it is a dry-run size query (no allocation at all).
**Evidence:**
> avahi-common/strlst.c:229-294: `avahi_string_list_serialize(AvahiStringList *l, void *data, size_t size)` — `if (data) {...}` branch writes bytes into caller buffer; `else {...}` branch (data==NULL) only computes `used` without touching `data`. No `avahi_free`/allocation inside this function.
> avahi-common/strlst-test.c:62-64: `n = avahi_string_list_serialize(a, NULL, 0); size = avahi_string_list_serialize(a, data, sizeof(data)); assert(size == n);` — classic "query size then fill" pairing, matching the doc comment at strlst.h:117.
> avahi-common/strlst-test.c:81,90: parse → (use) → free is the only lifetime pairing needed for the target itself.

### P2.8 Prerequisite State
**Claim:** No connection, context or configuration object is required. The only "state" relevant is the content and length of the input buffer. When used inside the wider avahi stack (daemon/client), the buffer is prepared by the DNS packet layer (`avahi_dns_packet_get_rptr`) which has already validated `rdlength` fits inside the received packet before calling parse — but that upstream validation is not a dependency of `avahi_string_list_parse` itself, which is purely self-contained and safe to call directly with arbitrary fuzz-input bytes/length.
**Evidence:**
> avahi-core/dns.c:579-588 — shows the *typical* real-world prerequisite (a validated DNS packet and rdlength), but this is for realism only; the function's own contract (asserts + bounds-check of `k` against local `size`) does not require any packet infrastructure.
> avahi-common/strlst-test.c:81 — calls `avahi_string_list_parse(data, size, &b)` with just a raw local array and no DNS/packet context at all, proving no such prerequisite is enforced by the function.

### P2.9 Result Usage
**Claim:** The resulting `AvahiStringList*` (possibly NULL) is consumed by: `avahi_string_list_equal(a,b)` (structural compare, no mutation), `avahi_string_list_to_string(l)` (returns a new `avahi_free()`-owned `char*`), `avahi_string_list_serialize(l, data, size)` (round-trip re-encode, does not take ownership), `avahi_string_list_find(l, key)` (returns a pointer *into* the same list — must not outlive it, must not be freed separately), `avahi_string_list_get_pair(l, &key, &value, &size)` (allocates new `avahi_free()`-owned `key`/`value` strings derived from one node), `avahi_string_list_copy(l)` (returns an independent new list requiring its own `avahi_string_list_free`), `avahi_string_list_length(l)`, `avahi_string_list_get_next/get_text/get_size` (read-only per-node accessors), and finally `avahi_string_list_free(l)` releases it.
**Evidence:**
> avahi-common/strlst-test.c:84: `avahi_string_list_equal(a, b)` — compares original `a` built by hand against `b` from parse(serialize(a)).
> avahi-common/strlst-test.c:86-88: `t = avahi_string_list_to_string(b); printf(...); avahi_free(t);` — converts parsed list to text, then frees the text (not the list).
> avahi-common/strlst-test.c:100-110: `p = avahi_string_list_find(a, "seven"); ... r = avahi_string_list_get_pair(p, &t, &v, NULL); ... avahi_free(t); avahi_free(v);` — find returns a node pointer, get_pair on that node allocates key/value which the caller frees.
> avahi-common/strlst-test.c:92-98: `b = avahi_string_list_copy(a); assert(avahi_string_list_equal(a,b)); t = avahi_string_list_to_string(b); avahi_free(t);` — copy produces an independent list.
> avahi-client/client-test.c:293-298: parsed `txt` list fed into `avahi_string_list_serialize(txt, NULL, 0)` (size check) and then into `avahi_entry_group_add_service_strlst(group, ..., txt)` (higher-level API consuming the list as a TXT record payload), then `avahi_string_list_free(txt)`.
> avahi-core/dns.c:582: parsed list stored directly into `r->data.txt.string_list`, becoming part of an `AvahiRecord`'s owned state (freed later as part of record teardown, not shown here but implied by record ownership).
> avahi-common/strlst.c:547-556 (`avahi_string_list_get_service_cookie`): consumes `avahi_string_list_find` + `avahi_string_list_get_pair` results (`value`) and frees `value` with `avahi_free` after use — demonstrates the expected find→get_pair→avahi_free(value) chain over parsed data.

### P2.10 Behaviour Controls
**Claim:** The target itself has no flags — behaviour is entirely driven by the bytes in `data`/`size`: each leading byte `k` (0-255) either (a) `k == 0`: empty string, skipped (not added to list) — strlst.c:86 `if (k>0)`; (b) `0 < k <= remaining size`: allocate/copy a `k`-byte node; (c) `k > remaining size`: abort entire parse with -1 and free partial list. `size == 0` short-circuits the `while` loop immediately, yielding `*ret = NULL`, return 0 (empty list, not an error). Related entries have their own mode-selecting parameters: `avahi_string_list_serialize(l, data, size)` — `data == NULL` selects "size-query" mode vs `data != NULL` selects "write" mode, and internally truncates any single string >255 bytes (strlst.c:246-248) and clamps to the remaining `size-1` budget (strlst.c:250-252), additionally special-casing an entirely-empty list to emit one NUL byte (strlst.c:264-272, DNS-SD §6.1). `avahi_string_list_get_pair(l, key, value, size)` — passing `NULL` for `key`/`value`/`size` individually selects which outputs are produced (strlst.c:493,497,500,506,514,525). `avahi_string_list_new_from_array(array, length)` (not in the target's entry set but related) uses `length == -1` to mean NUL-terminated array vs `length >= 0` to mean fixed count (strlst.c:368,374) — relevant if the harness also constructs comparison lists this way.
**Evidence:**
> avahi-common/strlst.c:77-97 (full while loop of parse, quoted above) — the three-way branch on `k` vs `size`.
> avahi-common/strlst.c:246-252: truncate-to-255 and clamp-to-remaining-size logic in `serialize`.
> avahi-common/strlst.c:264-272: empty-list → single NUL byte special case in `serialize`.
> avahi-common/strlst.c:486-530 (`get_pair`): `if (key) ...`, `if (value) ...`, `if (size) ...` — each output pointer optional.

### P2.11 Input Expectations
**Claim:** Input is the raw DNS TXT-record RDATA wire format: a concatenation of DNS "character-strings," each a 1-byte length prefix `k` (0-255, since it's read as a single `uint8_t`) followed by exactly `k` raw bytes (arbitrary binary, including embedded NUL — strlst.h:34-39 states the list "can contain any number of binary characters, including NUL bytes"). There is no overall terminator byte; parsing ends when `size` (byte count) reaches 0. The function explicitly checks, for every chunk, that `k` does not exceed the number of bytes remaining (`if (k > size) goto fail;` at strlst.c:83) — this is the critical anti-over-read boundary check. `size == 0` is legal and yields an empty (`NULL`) list, not an error (confirmed by strlst-test.c:132-134 behaviour pattern and by the `while(size>0)` loop guard at strlst.c:77). No maximum total `size` is enforced by this function itself (an attacker can supply many 0..255-length chunks to grow the list arbitrarily, bounded only by available memory — explicitly called out as "OOM amplification" risk in the task description, matching the `goto fail; /* OOM */` at strlst.c:90 when `avahi_string_list_add_arbitrary` fails under memory pressure).
**Evidence:**
> avahi-common/strlst.c:76-97: byte-walking loop with `k = *(c++); size--; if (k > size) goto fail;` — the core format/boundary contract.
> avahi-common/strlst.h:34-39: "Linked list of strings that can contain any number of binary characters, including NUL bytes... DNS TXT record data."
> avahi-common/strlst.h:120 + 115-116: "Inverse of avahi_string_list_serialize()" / "Strings longer than 255 bytes are truncated" (serialize side constraint, implying parse never needs to represent >255-byte chunks from its own round trip, though parse itself places no explicit upper bound other than the single-byte length prefix's natural 0-255 range).
> avahi-core/dns.c:579-588: real-world caller supplies `rdlength` (from the DNS record header, attacker-controlled over the network) directly as `size`, confirming untrusted-network-length semantics.

## Other Logic-Group Entries: call protocol and combination with the target

### avahi_string_list_serialize
**Protocol:** Takes `(AvahiStringList *l, void *data, size_t size)`. Call with `data=NULL` first to query the required buffer length (`strlst.c:62` pattern), then call again with a real buffer of at least that length to fill it; returns `size_t` = bytes actually used. No allocation/free of its own; `l` may be NULL (empty list) — `size==0` is clamped internally to be used as `size > 1` check only in the fill branch (strlst.c:239) so callers should still pass a sane buffer. Reverses and re-reverses `l` internally via `avahi_string_list_reverse` (strlst.c:236,262) — the list ordering is preserved on return (non-destructive to caller's `l` pointer, though it is a different pointer value during the call — this is fine since `l` itself isn't realloc'd, just the `next` pointers are temporarily flipped and flipped back).
**Combination with target:** Harness should round-trip: `parsed = avahi_string_list_parse(fuzz_data, fuzz_size, &list)` → `n = avahi_string_list_serialize(list, NULL, 0)` → `buf = malloc(n); used = avahi_string_list_serialize(list, buf, n)` → optionally `avahi_string_list_parse(buf, used, &list2)` → `avahi_string_list_equal(list, list2)`. Matches avahi-common/strlst-test.c:62-64,81-84 exactly.
**Evidence:** avahi-common/strlst.c:229-294 (full body, quoted above); avahi-common/strlst-test.c:62-64,127-134.

### avahi_string_list_equal
**Protocol:** `int avahi_string_list_equal(const AvahiStringList *a, const AvahiStringList *b)`. Pure read-only comparator, no init/cleanup of its own; both `a` and `b` may be NULL (equal if both NULL). Returns 1/0 (bool-like int), no error code.
**Combination with target:** Called directly on the result of `avahi_string_list_parse` vs. another list (e.g. the original list before serialize, or a second parse) to assert round-trip structural equality. avahi-common/strlst-test.c:84: `avahi_string_list_equal(a, b)` where `b` came from `avahi_string_list_parse`.
**Evidence:** avahi-common/strlst.c:296-314.

### avahi_string_list_to_string
**Protocol:** `char* avahi_string_list_to_string(AvahiStringList *l)`. Requires only a (possibly NULL/empty) list `l`; allocates and returns a new NUL-terminated `char*` via `avahi_new`, caller must `avahi_free()` it (strlst.h:110 "avahi_free() the result!"). Internally calls `avahi_string_list_reverse(l)` twice (strlst.c:193,222) to produce correct output ordering then restore original order — so `l`'s `next`-chain is temporarily mutated and restored; not safe to call concurrently from two threads on the same list.
**Combination with target:** Call on the list returned by `avahi_string_list_parse` to exercise per-byte escaping logic over parser-produced (possibly embedded-NUL / non-ASCII) content; always pair with `avahi_free(result)`. avahi-common/strlst-test.c:86-88.
**Evidence:** avahi-common/strlst.c:162-227; avahi-common/strlst.h:109-112.

### avahi_string_list_find
**Protocol:** `AvahiStringList *avahi_string_list_find(AvahiStringList *l, const char *key)`. `key` must be non-NULL (`assert(key)`, strlst.c:444) and NUL-terminated. Returns a pointer *into* `l`'s existing nodes (or NULL if not found) — **not** a new allocation; must not be freed separately from `l`. Internally calls `strcasecmp`/`strncasecmp` directly on `l->text`, which assumes the node's bytes up to its first NUL behave like a C string — nodes produced by `avahi_string_list_parse` may contain embedded NUL bytes or lack a NUL at `l->size` boundary semantics other than the implicit NUL terminator written by `avahi_string_list_add_anonymous` (strlst.c:44: `n->text[size] = 0;`), so parsed nodes are always NUL-terminated at `text[size]` by construction — safe for this call, but any *content* before that implicit terminator can still cause `find` to match on a truncated-at-embedded-NUL prefix.
**Combination with target:** Call with keys derived from or unrelated to the fuzz input on the list from `avahi_string_list_parse`, to exercise key/value pair search over attacker-controlled node content. avahi-common/strlst-test.c:100,112.
**Evidence:** avahi-common/strlst.c:441-456; avahi-common/strlst.c:34-47 (`add_anonymous` NUL-termination guarantee).

### avahi_string_list_get_pair
**Protocol:** `int avahi_string_list_get_pair(AvahiStringList *l, char **key, char **value, size_t *size)`. Requires non-NULL `l` (`assert(l)`, strlst.c:489) — caller must ensure the node pointer (e.g. result of `avahi_string_list_find`) is valid and not NULL before calling. `key`, `value`, `size` are each optional (pass NULL to skip). On success returns 0 and allocates `*key` (`avahi_strdup`/`avahi_strndup`) and `*value` (`avahi_memdup`) when requested — both owned by caller, free with `avahi_free()`. Returns -1 on allocation failure, in which case any already-allocated `*key` is freed internally before returning (strlst.c:516-519) — caller should not free `*key` again on -1.
**Combination with target:** After `avahi_string_list_find` locates a node in the list from `avahi_string_list_parse`, call `get_pair` to split "key=value"; free `*key`/`*value` after use. avahi-common/strlst-test.c:103-110,115-122; also strlst.c:552-556 (`get_service_cookie`) shows the find→get_pair(key=NULL)→use value→avahi_free(value) pattern.
**Evidence:** avahi-common/strlst.c:486-530.

### avahi_string_list_copy
**Protocol:** `AvahiStringList *avahi_string_list_copy(const AvahiStringList *l)`. Takes any list (including NULL → returns NULL per loop not executing and `avahi_string_list_reverse(NULL)` returning NULL). Builds an entirely new list via the same `avahi_string_list_add_arbitrary` core path used internally by `parse`; on OOM mid-copy it frees the partial new list (`avahi_string_list_free(r)`, strlst.c:359) and returns NULL — caller must check for NULL and not assume original `l` was altered (it isn't touched, `copy` takes `const AvahiStringList *l`).
**Combination with target:** Call on the list produced by `avahi_string_list_parse` to get an independent copy for comparison (`avahi_string_list_equal`) or for destructive operations elsewhere without affecting the original; free the copy independently via `avahi_string_list_free`. avahi-common/strlst-test.c:92-98,125.
**Evidence:** avahi-common/strlst.c:354-366.

### Core helpers (not separate entries, used internally — confirm existence only)
**Claim:** `avahi_string_list_add_anonymous`, `avahi_string_list_add_arbitrary`, `avahi_string_list_free`, `avahi_string_list_reverse` all exist and are the internal building blocks that `avahi_string_list_parse`, `avahi_string_list_copy`, `avahi_string_list_to_string` and `avahi_string_list_serialize` call; they are declared in the public header (so technically callable directly) but the task's Logic Group treats them as Core, not Entries — the harness is expected to drive them only indirectly through the Entries listed above.
**Evidence:** avahi-common/strlst.c:34-47 (`add_anonymous`), 49-61 (`add_arbitrary`), 108-116 (`free`), 118-129 (`reverse`); called from `avahi_string_list_parse` at strlst.c:89 (`add_arbitrary`) and strlst.c:104 (`free`), from `avahi_string_list_copy` at strlst.c:358,365, from `avahi_string_list_to_string` at strlst.c:193,222 (`reverse`), from `avahi_string_list_serialize` at strlst.c:236,262 (`reverse`).

## Build notes
**Claim:** Fuzz targets for this project are plain C, compiled individually per `fuzz/fuzz-*.c` file with `-I.` (project root) so headers are included as `#include "avahi-common/strlst.h"` or `#include "strlst.h"` (internal relative include, as strlst.c itself does), then linked against the prebuilt static libraries `avahi-core/.libs/libavahi-core.a` and `avahi-common/.libs/libavahi-common.a` plus the fuzzing engine. An existing (currently stubbed) harness file `fuzz/fuzz-strlst.c` is the designated target file name for this feature. Related real harness `fuzz/fuzz-consume-record.c` shows the idiom: `#include "avahi-common/malloc.h"`, `#include "avahi-core/dns.h"`, etc., with `LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)`, optional `#ifdef HAVE_NALLOCFUZZ` instrumentation, and `goto finish` / cleanup-on-every-path style matching the library's own pointer-ownership discipline.
**Evidence:**
> ossfuzz_view/avahi-c1/build.sh:18: `fuzz/oss-fuzz.sh` (delegates entirely to this script).
> avahi-c1/fuzz/oss-fuzz.sh:121-145:
> ```
> for f in fuzz/fuzz-*.c; do
>     fuzz_target=$(basename "$f" .c)
>     $CC -c $CFLAGS -I. "fuzz/$fuzz_target.c" -o "$fuzz_target.o"
>     ...
>     "${LINK_WITH:-$CXX}" $CXXFLAGS "$fuzz_target.o" -o "$OUT/$fuzz_target" $LIB_FUZZING_ENGINE $additional_obj_files \
>         "avahi-core/.libs/libavahi-core.a" "avahi-common/.libs/libavahi-common.a"
> done
> ```
> avahi-c1/fuzz/fuzz-strlst.c:1-11 — current stub: plain C, `#include <stddef.h>`/`<stdint.h>`, `extern "C"` guard for C++ linkage of `LLVMFuzzerTestOneInput`, to be replaced by the real harness exercising `avahi_string_list_parse` and the rest of this Logic Group.
> avahi-c1/fuzz/fuzz-consume-record.c:20-26: `#include "avahi-common/malloc.h"` / `#include "avahi-core/dns.h"` style — confirms headers are referenced with the `avahi-common/...` / `avahi-core/...` prefix relative to the `-I.` project-root include path, i.e. the target's header should be included as `#include "avahi-common/strlst.h"`.
> avahi-common/strlst.c:30-32: internal file itself includes `"strlst.h"`, `"malloc.h"`, `"defs.h"` (relative, since it compiles from within `avahi-common/`) — a harness placed in `fuzz/` must instead use the `avahi-common/`-prefixed include form shown in fuzz-consume-record.c.