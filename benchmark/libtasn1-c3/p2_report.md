## target: asn1_get_object_id_der

### P2.1 Init Sequence
**Claim:** No library-global init call is required before `asn1_get_object_id_der`. It is a pure, stateless decoding primitive that only needs its input buffer and an output buffer prepared by the caller. It is called either stand-alone (as in the test program) or as one step inside `asn1_der_decoding2`'s node-walking loop, in which case it is preceded only by tag parsing of the enclosing element (not an init of the function itself).
**Evidence:**
> lib/decoding.c:410-412: `int asn1_get_object_id_der (const unsigned char *der, int der_len, int *ret_len, char *str, int str_size)` — no handle/context parameter.
> tests/object-id-decoding.c:85-91: called directly in a loop with only a stack buffer `char str[128]` prepared, no prior library init.
> lib/decoding.c:1169-1172: inside `asn1_der_decoding2`, called after the generic tag-decoding logic (`DECR_LEN`, tag/class checks) has advanced `counter`/`ider_len`, i.e. it consumes the OID content bytes with tag already stripped (per its own doc comment at lib/decoding.c:406).

### P2.2 Parameter Construction
**Claim:**
- `der`: pointer to the raw content octets of a BER/DER `OBJECT IDENTIFIER` *without* the tag byte (caller strips tag first). Must be non-NULL with at least `der_len` readable bytes.
- `der_len`: `int`, the number of available bytes at `der`; must be `> 0` or the function returns `ASN1_GENERIC_ERROR` immediately.
- `ret_len`: `int*`, non-NULL output; is always initialized to 0 at function entry even on error paths.
- `str`: pre-allocated output buffer, must be non-NULL or function returns `ASN1_GENERIC_ERROR`; function writes `str[0]=0` first if `str_size>0`.
- `str_size`: size of `str` in bytes; may be small — `_asn1_str_cpy`/`_asn1_str_cat` (called internally) are the truncation-safe primitives, so no overflow occurs even if `str_size` is small, but the string may be truncated.
**Evidence:**
> lib/decoding.c:419-424:
> ```
> *ret_len = 0;
> if (str && str_size > 0) str[0] = 0;
> if (str == NULL || der_len <= 0) return ASN1_GENERIC_ERROR;
> ```
> lib/gstr.h:24-26: `unsigned int _asn1_str_cpy(char *dest, size_t dest_tot_size, ...)` and `_asn1_str_cat(char *dest, size_t dest_tot_size, const char *src)` — bounded copy/cat used for every piece of OID text written into `str`.

### P2.3 Object Lifecycle
**Claim:** Not an object/handle API — no create/destroy. It is a single-call, stateless transform: caller supplies `der`/`der_len` and a pre-allocated `str` buffer, gets back a status code plus filled `str` and `*ret_len`. No resources to construct or release beyond the caller's own buffers.
**Evidence:**
> lib/decoding.c:410-502: entire function body allocates no heap memory and stores no persistent state; only a local `temp[LTOSTR_MAX_SIZE]` stack buffer (line 416) is used.

### P2.4 Return Value Handling
**Claim:** Return is one of `ASN1_SUCCESS` (0), `ASN1_GENERIC_ERROR` (6, bad args), or `ASN1_DER_ERROR` (4, malformed length/content: length<=0, length+len_len>der_len, leading 0x80 byte at outer or inner sub-identifier, base-128 shift overflow, add-overflow on `len+len_len`). Callers MUST check the return code before trusting `str`/`*ret_len`; `str` is left as an empty string (`str[0]=0`) on early error, but may contain a *partial* OID string if error is detected mid-loop (the `leading`/overflow checks happen after some `_asn1_str_cat` calls already wrote arcs — e.g. the X/Y header is written at line 466-468 before the loop that can fail at line 477/482).
**Evidence:**
> lib/decoding.c:428-429: `if (len <= 0 || len + len_len > der_len) return ASN1_DER_ERROR;`
> lib/decoding.c:432-433: `if (der[len_len] == 0x80) return ASN1_DER_ERROR;` (leading-octet rule)
> lib/decoding.c:439-440, 481-482: `INT_LEFT_SHIFT_OVERFLOW` checks returning `ASN1_DER_ERROR` on 64-bit overflow of accumulators `val0`/`val`.
> lib/decoding.c:496-497: `if (INT_ADD_OVERFLOW (len, len_len)) return ASN1_DER_ERROR;` guards the final `*ret_len` computation.
> lib/decoding.c:1173-1177 (caller `asn1_der_decoding2`): `if (result != ASN1_SUCCESS) { warn(); goto cleanup; }` — real caller checks the return value and aborts/destroys the whole tree on failure.
> tests/object-id-decoding.c:92-99: test harness compares `ret` against `expected_error` and reports mismatch via `asn1_strerror(ret)`.

### P2.5 Cleanup Sequence
**Claim:** The target itself needs no cleanup (no allocation). When used inside `asn1_der_decoding2`, failure of the target triggers cleanup of the *enclosing* ASN.1 node tree via `asn1_delete_structure`, not of anything owned by the target.
**Evidence:**
> lib/decoding.c:1173-1176: on OID decode failure, `goto cleanup;` inside `asn1_der_decoding2`.
> lib/decoding.c:1597-1599 (`asn1_der_decoding_element`) and analogous `cleanup:` labels in `asn1_der_decoding2` call `asn1_delete_structure (element)` on any decode failure, including the `ASN1_ETYPE_OBJECT_ID` case.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current source tree as verified by direct reads/greps.
**Evidence:**
> lib/decoding.c:410 `asn1_get_object_id_der` definition.
> lib/decoding.c:112 `asn1_get_length_der` definition.
> lib/coding.c:444 `asn1_object_id_der` definition (wraps static `_asn1_object_id_der` at coding.c:356).
> lib/decoding.c:918 `asn1_der_decoding2` definition; lib/decoding.c:1624 `asn1_der_decoding` (thin wrapper calling `asn1_der_decoding2`).
> lib/includes/libtasn1.h.in:330-379 declares `asn1_array2tree`, `asn1_create_element`, `asn1_delete_structure`, `asn1_write_value`, `asn1_der_decoding2`, `asn1_der_decoding` — all present and matching their .c definitions.
> lib/parser_aux.c:624 `_asn1_ltostr` definition; lib/gstr.h:24,26 `_asn1_str_cpy`/`_asn1_str_cat` declarations (defined in gstr.c, used throughout decoding.c/coding.c).

### P2.7 Co-call Constraints
**Claim:** The target has no paired "begin/end" API of its own. Inside the bigger pipeline, the relevant pairing is `asn1_create_element` ↔ `asn1_delete_structure` around any `asn1_der_decoding`/`asn1_der_decoding2` call that may reach `ASN1_ETYPE_OBJECT_ID` and thus the target; and `asn1_array2tree` (build the definitions tree once) paired with `asn1_delete_structure` on that same definitions tree when the fuzzer/program exits.
**Evidence:**
> fuzz/libtasn1_gnutls_der_fuzzer.c:146-157: `asn1_array2tree(...)` once, then per-iteration `asn1_create_element(... , &dn)` → `asn1_der_decoding (&dn, data, size, NULL)` → `asn1_delete_structure (&dn)`.
> fuzz/libtasn1_array2tree_fuzzer.c:87-90: `asn1_array2tree(tab, &node, errorDescription)` then, only `if (result == ASN1_SUCCESS)`, `asn1_delete_structure(&node)`.

### P2.8 Prerequisite State
**Claim:** For the direct, stand-alone call (as in `tests/object-id-decoding.c` and the dedicated fuzzer), the only prerequisite state is: a readable `der` buffer holding OID content octets with the tag already stripped, and a writable `str` buffer of size `str_size`. No ASN.1 definitions tree, no node, no flags are required. For the `asn1_der_decoding2` pipeline route, prerequisite state is an `asn1_node` tree created via `asn1_array2tree`/`asn1_parser2tree` plus `asn1_create_element`, whose current node's `type_field(...) == ASN1_ETYPE_OBJECT_ID`.
**Evidence:**
> tests/object-id-decoding.c:81-91: `char str[128]; int ret, ret_len;` then direct call with `tv[i].der+1` (tag stripped by `+1`) and `tv[i].der_len-1`.
> lib/decoding.c:1169-1172: target reached only when `type_field (p->type)` of the current tree node equals `ASN1_ETYPE_OBJECT_ID`, i.e. the structure definition for that node must declare an OBJECT IDENTIFIER field.

### P2.9 Result Usage
**Claim:** The target's two outputs — the dotted-decimal C string in `str` and the consumed-length in `*ret_len` — are used differently depending on caller:
1. In the direct-call test, `str` is compared with `strcmp` against an expected OID string, and `ret_len` is compared against the expected consumed length.
2. In `asn1_der_decoding2`, `ret_len` (`len2`) is used to advance the parse cursor (`DECR_LEN(ider_len, len2); counter += len2;`), and the string `temp` (copy destination for `str` argument) is stored into the tree node's value via `_asn1_set_value(p, temp, tlen+1)` if non-empty, becoming the structure's queryable field value (later retrievable with `asn1_read_value`).
**Evidence:**
> tests/object-id-decoding.c:104-116: `if (ret_len != tv[i].der_len - 1) { ... }` and `if (strcmp (tv[i].oid, str) != 0) { ... }`.
> lib/decoding.c:1169-1186:
> ```
> result = asn1_get_object_id_der (der + counter, ider_len, &len2, temp, sizeof (temp));
> if (result != ASN1_SUCCESS) { warn(); goto cleanup; }
> DECR_LEN (ider_len, len2);
> tlen = strlen (temp);
> if (tlen > 0) _asn1_set_value (p, temp, tlen + 1);
> counter += len2;
> move = RIGHT;
> ```
> lib/includes/libtasn1.h.in:354-355: `asn1_read_value (asn1_node_const root, const char *name, void *ivalue, int *len);` — the documented way downstream code later reads the OID text out of the tree node that `asn1_der_decoding2` populated.

### P2.10 Behaviour Controls
**Claim:** The target itself has no flags; its behavior is entirely data-driven by the byte patterns in `der`:
- Leading byte `0x80` (outer sub-id or any inner sub-id after a terminator byte) → forced `ASN1_DER_ERROR` (X.690 rule).
- Continuation bit (`0x80`) in each byte selects whether the base-128 group continues; absence of it (`!(byte & 0x80)`) ends the current arc and emits `.N` via `_asn1_str_cat`.
- Accumulated `val0` bucket (`<=39`, `40..79`, `>=80`) selects the X={0,1,2} / Y split (lib/decoding.c:449-464).
- `INT_LEFT_SHIFT_OVERFLOW` on the 7-bit shift accumulation aborts decoding for any arc whose value would overflow 64 bits.
Around the target, `asn1_der_decoding2`'s `flags` parameter (`ASN1_DECODE_FLAG_ALLOW_PADDING`, `ASN1_DECODE_FLAG_STRICT_DER`, `ASN1_DECODE_FLAG_ALLOW_INCORRECT_TIME`) control the *surrounding* parse (padding tolerance, BER vs strict DER, time tolerance) but do not change how `ASN1_ETYPE_OBJECT_ID` is itself parsed — the OBJECT_ID case in the switch calls the target unconditionally with no flag checks.
**Evidence:**
> lib/decoding.c:432-433, 476-477: leading `0x80` checks.
> lib/decoding.c:444, 487: `if (!(der[len_len + k] & 0x80)) { ... }` continuation-bit tests.
> lib/decoding.c:449-464: X/Y bucket derivation from `val0`.
> lib/decoding.c:439-440, 481-482: overflow guards.
> lib/includes/libtasn1.h.in:256-270: `ASN1_DECODE_FLAG_ALLOW_PADDING`/`ASN1_DECODE_FLAG_STRICT_DER` definitions, "Used by: asn1_der_decoding2()".
> lib/decoding.c:1169-1177: the `ASN1_ETYPE_OBJECT_ID` switch case does not read `flags` at all before calling the target.

### P2.11 Input Expectations
**Claim:** The target expects `der`/`der_len` to be the **content octets only** (no tag byte) of a BER/DER `OBJECT IDENTIFIER`, beginning with its own length field (short or long form, decoded via `asn1_get_length_der`). Constraints enforced in code:
- `der_len > 0` (else `ASN1_GENERIC_ERROR`).
- Decoded `len` (content length) must be `> 0` and `len + len_len <= der_len` (else `ASN1_DER_ERROR`) — i.e. length field must not claim more bytes than are available.
- First content byte (`der[len_len]`) must not be `0x80`.
- No explicit terminator is required beyond `len` bytes of content; the last group in `len` bytes must have its continuation bit clear implicitly by reaching `k==len-1` (the loop at 437-446 just runs for `k<len`; if the final byte still has the high bit set the arc would be left dangling, but the loop naturally stops at `len`).
- Each inner arc after the first two also forbids a leading `0x80` byte once `leading` is reset at the start of a new arc (lib/decoding.c:476-477).
**Evidence:**
> lib/decoding.c:423-424, 428-429, 432-433 (quoted above).
> lib/decoding.c:406: doc comment "function expects the DER object identifier without the tag."
> lib/decoding.c:437-447: length-field-bounded loop building the first 2-arc value `val0`.
> lib/decoding.c:472-494: loop building subsequent arcs for `k` in `[parsed, len)`.

## Other entries in the Logic Group

### asn1_get_length_der
**Call protocol:** Stateless, no init needed. Takes `der`/`der_len` (raw bytes starting at the length octet) and an output `int *len` (number of bytes the length field itself occupies). Returns the decoded length (>=0) on success, or a negative sentinel: `-1` indefinite length, `-2` overflow/too-big, `-4` decoded length + len exceeds `der_len`. No resources to free. It is both an independently fuzzable public primitive and the first thing `asn1_get_object_id_der` calls internally (lib/decoding.c:426) to find where the sub-identifier bytes start and how many there are.
**Evidence:**
> lib/decoding.c:99-165 (doc comment + body): "Returns: Return the decoded length value, or -1 on indefinite length, or -2 when the value was too big ... or -4 when the decoded length value plus @len would exceed @der_len."
> fuzz/asn1_get_length_der_fuzzer.c:30-41: direct, standalone call `asn1_get_length_der (data, size, &ret_len);` with no setup/teardown, bounded by `MAXDATASIZE 512`.
> lib/decoding.c:426-429: `asn1_get_object_id_der` calls it first and immediately validates `len`/`len_len` before proceeding — in a combined harness, feeding the same buffer to both functions exercises shared length-decoding edge cases (short/long form, overflow, truncation) consistently.
**How it combines with the target:** Call `asn1_get_length_der` directly on a byte slice to explore length-encoding edge cases, and/or let `asn1_get_object_id_der` call it internally when decoding full OID DER blobs — both consume the same `der_len` semantics, so corpus entries valid for one partially stress the other.

### asn1_object_id_der (and static core _asn1_object_id_der)
**Call protocol:** `asn1_object_id_der(str, der, der_len, flags)`:
- `str`: NUL-terminated OID in dotted-decimal text, e.g. `"1.3.6.1.4.1"`. First two arcs are range-checked: first arc 0-2; if first arc is 0 or 1, second arc must be <=39 (else `ASN1_VALUE_NOT_VALID`).
- `der`: output buffer, may be `NULL` (then `*der_len` must be the required size query — on `NULL` der with `max_len>0` the static core returns `ASN1_VALUE_NOT_VALID`; typical "size probe" pattern is `der=NULL` with `*der_len=0`, check for `ASN1_MEM_ERROR`, then allocate and re-call).
- `der_len`: in/out — in: buffer capacity; out: required/used length.
- `flags`: documented as "must be zero" (lib/includes/libtasn1.h.in / coding.c:435).
- Returns `ASN1_SUCCESS`, `ASN1_VALUE_NOT_VALID` (bad OID string), or `ASN1_MEM_ERROR` (buffer too small, `*der_len` then holds the needed size).
- No allocation/handle to free; `der` buffer ownership stays with caller.
**Evidence:**
> lib/coding.c:430-443 (doc comment): "@flags: must be zero", "Returns: %ASN1_SUCCESS ... %ASN1_VALUE_NOT_VALID if @str is not a valid OID, %ASN1_MEM_ERROR if the @der vector isn't big enough".
> lib/coding.c:393-404 (`_asn1_object_id_der`): range checks `if (val1 > 2) return ASN1_VALUE_NOT_VALID;` and `else if ((val1==0||val1==1) && val>39) return ASN1_VALUE_NOT_VALID;`.
> tests/object-id-encoding.c:91-106: `der_len = sizeof(der); ret = asn1_object_id_der (tv[i].oid, der, &der_len, 0);` then checks `ret` against expected, and on success compares resulting bytes.
**How it combines with the target (round trip):** Use `asn1_object_id_der` to synthesize valid (or edge-case, e.g. very large arc values, boundary X/Y like `1.39`) DER-encoded OIDs from an attacker/fuzzer-controlled textual OID (itself derived from fuzz bytes), including the tag+length framing; then strip tag (first byte + its length bytes) and feed the remaining content bytes into `asn1_get_object_id_der` to check the round trip and reach decoder branches that purely-random bytes rarely hit (e.g. valid continuation-bit sequences, X=2 bucket, multi-byte 64-bit-near-overflow arcs). Both tests/object-id-decoding.c and tests/object-id-encoding.c use the *same* literal DER/OID table (tv[]), confirming the two are meant to be exact inverses for compliant inputs.

### asn1_der_decoding (and asn1_der_decoding2)
**Call protocol:**
- Prerequisite: an `asn1_node` definitions tree built by `asn1_array2tree` (from an `asn1_static_node` array) or `asn1_parser2tree` (from a `.asn1` text file), then `asn1_create_element(definitions, "Module.Type", &element)` to instantiate one element/node to decode into.
- `asn1_der_decoding(&element, ider, ider_len, errorDescription)` is a thin wrapper: `return asn1_der_decoding2(element, ider, &ider_len, 0, errorDescription);` (lib/decoding.c:1623-1628) — always flags=0.
- `asn1_der_decoding2(&element, ider, &max_ider_len, flags, errorDescription)` lets the caller pass `ASN1_DECODE_FLAG_ALLOW_PADDING`, `ASN1_DECODE_FLAG_STRICT_DER`, `ASN1_DECODE_FLAG_ALLOW_INCORRECT_TIME`.
- Returns `ASN1_SUCCESS`, `ASN1_ELEMENT_NOT_FOUND` (`element`/`*element` NULL), `ASN1_TAG_ERROR`, or `ASN1_DER_ERROR` for malformed content; on any of the latter, `*element` is destroyed internally (`asn1_delete_structure` called in the `cleanup:` path) and the errorDescription buffer (if non-NULL) is filled.
- On success the tree `element` holds the decoded values, readable via `asn1_read_value`; caller must eventually call `asn1_delete_structure(&element)` to free the tree node itself (idempotent-safe check: on decode failure the node is already deleted by the function, so the caller's own cleanup for the failure path should not double-free — see below).
**Evidence:**
> lib/includes/libtasn1.h.in:372-379: declarations of `asn1_der_decoding2`/`asn1_der_decoding`.
> lib/decoding.c:1611-1621 (doc comment): "The structure must just be created with function asn1_create_element()", "Returns: %ASN1_SUCCESS ... %ASN1_TAG_ERROR or %ASN1_DER_ERROR if the der encoding doesn't match the structure name (*@ELEMENT deleted)."
> lib/decoding.c:1623-1628: wrapper body.
> fuzz/libtasn1_gnutls_der_fuzzer.c:150-158: `asn1_create_element(... , &dn)`; `if (res == ASN1_SUCCESS) { res = asn1_der_decoding (&dn, data, size, NULL); asn1_delete_structure (&dn); }` — note: `asn1_delete_structure` is called unconditionally after `asn1_der_decoding` here (regardless of its return), which is safe because the library documents `*element` as deleted-and-nulled internally on error, and `asn1_delete_structure` tolerates an already-NULL/consistent state — see structure.c.
> lib/decoding.c:1169-1177: the `ASN1_ETYPE_OBJECT_ID` case is reached only when the definitions tree contains an OBJECT IDENTIFIER-typed field for the element being decoded (i.e. a `.asn1` grammar or `asn1_static_node` table with such a field is a prerequisite to exercise the target through this path).
**How it combines with the target:** Build (once) a definitions tree with at least one `OBJECT IDENTIFIER` field (e.g. reuse the GNUTLS table from fuzz/libtasn1_gnutls_der_fuzzer.c, or a minimal custom one), `asn1_create_element` a fresh element per iteration, feed fuzz bytes as `ider`/`ider_len` to `asn1_der_decoding`/`asn1_der_decoding2`, and always call `asn1_delete_structure(&element)` afterward. This drives the target with realistic tag/length framing supplied by the generic decoder rather than hand-stripped, reaching the OID case only when the fuzz bytes align with that field's tag in the grammar.

## Build notes
**Claim:** Fuzzer sources are plain C (compiled with `$CXX`/`$CC` per OSS-Fuzz convention but the project's own fuzzers are C files with `.c` extension using `libFuzzer`'s `LLVMFuzzerTestOneInput`), built via the project's own `fuzz/Makefile.am` `oss-fuzz` target when `$OUT` is set, which compiles each `*_fuzzer.c` against the in-tree header `lib/includes/libtasn1.h` (built from `libtasn1.h.in`) using `-I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(top_srcdir)`, and links against the already-built static `../lib/libtasn1.la` (plus gnulib's `libgnu.la`) rather than reaching into internal object files. OSS-Fuzz driver script `build.sh` runs `./bootstrap && ./configure --enable-static --disable-shared ... --enable-fuzzing`, builds `lib`, `src`, `tests`, then `cd fuzz && make oss-fuzz`, and copies every `*_fuzzer` binary plus `.dict`/`.options` to `$OUT`.
**Evidence:**
> ossfuzz_view/libtasn1-c3/build.sh:18-30: `./bootstrap`; `./configure --enable-static --disable-shared --disable-doc --disable-gcc-warnings --enable-fuzzing`; `make -j... -C lib`; `make -j... -C src`; `make -j... -C tests check`; `cd fuzz; make oss-fuzz`; `find . -name '*_fuzzer' -exec cp -v '{}' $OUT ';'`.
> ossfuzz_view/libtasn1-c3/Dockerfile:17-27: `FROM gcr.io/oss-fuzz-base/base-builder`, clones `gnulib` and `libtasn1.git`, `WORKDIR libtasn1`.
> lib/includes/libtasn1.h.in (project header template; installed as `libtasn1.h`) contains all declarations used above (`asn1_get_object_id_der` at line 483-486, `asn1_get_length_der` at 423-424, `asn1_object_id_der` at 488-490, `asn1_der_decoding`/`asn1_der_decoding2` at 372-379).
> fuzz/Makefile.am:24-28: `AM_CPPFLAGS = ... -I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(srcdir) ...`; `LDADD = ../lib/gl/libgnu.la ../lib/libtasn1.la $(FUZZ_LIBS)`.
> fuzz/Makefile.am:73-79 (`oss-fuzz:` target): compiles each `*_fuzzer.c` with `-I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(top_srcdir)` and links against the built library archive, i.e. the public installed header + public library symbols only (P3 public-API boundary respected by the project's own fuzzers).
> fuzz/asn1_get_object_id_der_fuzzer.c:1-16 — note: in this checkout the file has been replaced by a trivial stub (`qf_target_0` aliasing via `__asm__` to keep the symbol linked, empty `LLVMFuzzerTestOneInput`) for "baseline generation"; the real decode logic and call pattern must instead be taken from `tests/object-id-decoding.c` (verified above) and from the `ASN1_ETYPE_OBJECT_ID` case inside `asn1_der_decoding2` (lib/decoding.c:1169-1186), both of which show the authentic call protocol.