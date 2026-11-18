## target: asn1_decode_simple_der

### P2.1 Init Sequence
**Claim:** No library-level init object is required. `asn1_decode_simple_der` is a stateless, allocation-free function; it can be called directly with a raw buffer. The only "setup" is constructing valid/invalid `der`/`_der_len` inputs (optionally produced by `asn1_encode_simple_der`, see below) and choosing a valid `etype`.
**Evidence:**
> libtasn1/lib/includes/libtasn1.h.in:434-438: declaration, no context/handle parameter.
> libtasn1/lib/decoding.c:2192-2199: `asn1_decode_simple_der` just forwards to `_asn1_decode_simple_der(etype, der, _der_len, str, str_len, DECODE_FLAG_HAVE_TAG)` — no global state touched.
> libtasn1/tests/Test_strings.c:94-121: test calls `asn1_encode_simple_der` first (to build a de/re-encoded `der` buffer) then calls `asn1_decode_simple_der` directly — no other init call in between.

### P2.2 Parameter Construction
**Claim:**
- `etype` (unsigned int): must be one of the `ASN1_ETYPE_*` string types of `ASN1_CLASS_UNIVERSAL` class; validated internally by `ETYPE_OK()` and `ETYPE_IS_STRING()` and class check. Non-string / non-UNIVERSAL / out-of-range values are rejected with `ASN1_VALUE_NOT_VALID`.
- `der` (const unsigned char *): must be non-NULL; raw buffer containing tag+length+value bytes (attacker data).
- `_der_len` (unsigned int): real number of readable bytes in `der`; must be > 0, it is internally cast to a signed `int der_len` (`int der_len = _der_len;`), so values > INT_MAX will wrap to negative and be rejected by the `der_len==0`/`der_len<=0` checks, but very large unsigned values close to INT_MAX should be avoided or tested deliberately for overflow.
- `str` (const unsigned char **): output-only, caller-supplied pointer-to-pointer; on success set to point **inside** `der` (no new allocation) — caller must NOT free `*str`.
- `str_len` (unsigned int *): output-only, set to the decoded content length.
**Evidence:**
> libtasn1/lib/decoding.c:2122-2137: `if (der == NULL || der_len == 0) return ASN1_VALUE_NOT_VALID;` and `if (ETYPE_OK (etype) == 0 || ETYPE_IS_STRING (etype) == 0) return ASN1_VALUE_NOT_VALID;`
> libtasn1/lib/decoding.c:2140-2142: `class = ETYPE_CLASS (etype); if (class != ASN1_CLASS_UNIVERSAL) return ASN1_VALUE_NOT_VALID;`
> libtasn1/lib/decoding.c:2128: `int der_len = _der_len;` — unsigned-to-signed conversion of the length parameter.
> libtasn1/lib/decoding.c:2173-2174: `*str_len = ret; *str = p;` — `p` is a pointer within the original `der` buffer, no malloc.
> libtasn1/lib/int.h:94-103: `ETYPE_OK`/`ETYPE_IS_STRING` macros defining the allowed etype set (GENERALSTRING, NUMERIC_STRING, IA5_STRING, TELETEX_STRING, PRINTABLE_STRING, UNIVERSAL_STRING, BMP_STRING, UTF8_STRING, VISIBLE_STRING, OCTET_STRING).

### P2.3 Object Lifecycle
**Claim:** There is no object/handle to create or destroy for this function; lifecycle is simply: build `der`+`_der_len` → call `asn1_decode_simple_der` → read `*str`/`*str_len` (which alias into `der`) → nothing to free. The result pointer is only valid as long as the original `der` buffer is valid.
**Evidence:**
> libtasn1/lib/decoding.c:2173-2176: no allocation, `*str` returned is `p` (offset into input `der`), function ends with `return ASN1_SUCCESS;` without any resource creation.
> libtasn1/tests/Test_strings.c:112-129: after the call, test only reads `str`/`str_len` and compares with `memcmp`; no `free(str)` call anywhere for this function (contrast with `asn1_decode_simple_ber` below, where `free(b)` is used).

### P2.4 Return Value Handling
**Claim:** Return is an `int` from the `ASN1_*` error-code namespace. `ASN1_SUCCESS` (0) means `*str`/`*str_len` are valid; any other value (`ASN1_VALUE_NOT_VALID`=7, `ASN1_DER_ERROR`=4, or a tag/length error code propagated from `asn1_get_tag_der`/`asn1_get_length_der`) means `*str`/`*str_len` must NOT be used — they may be left unset in several failure paths (e.g., the NULL/zero-length and etype checks happen before `*str`/`*str_len` would be assigned).
**Evidence:**
> libtasn1/lib/decoding.c:2133-2137,2149-2150,2155-2156,2160-2161,2165-2166,2170-2171: all failure returns (`ASN1_VALUE_NOT_VALID`, propagated `ret` from `asn1_get_tag_der`, `ASN1_DER_ERROR`) occur before `*str_len`/`*str` are written.
> libtasn1/lib/includes/libtasn1.h.in:115,119,122: `ASN1_SUCCESS=0`, `ASN1_DER_ERROR=4`, `ASN1_VALUE_NOT_VALID=7`.
> libtasn1/tests/Test_strings.c:116-121: `if (ret != ASN1_SUCCESS) { fprintf(...asn1_strerror(ret)...); return 1; }` — canonical caller pattern: check against `ASN1_SUCCESS` before touching `str`/`str_len`.

### P2.5 Cleanup Sequence
**Claim:** Nothing to free on success or error for `asn1_decode_simple_der` itself (no heap allocation occurs in this function). The only cleanup owed by the harness is freeing the `der` input buffer if the harness itself allocated it (e.g., when built via `asn1_encode_simple_der`+manual concatenation), which is unrelated to the target's own contract.
**Evidence:**
> libtasn1/lib/decoding.c:2121-2177: no `malloc`/`_asn1_realloc`/`append` call anywhere in `_asn1_decode_simple_der`.
> libtasn1/tests/Test_strings.c: no `free()` call associated with the `asn1_decode_simple_der` result (lines 112-129), confirming no caller-owned allocation is produced.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current source tree.
**Evidence:**
> libtasn1/lib/decoding.c:2122 `_asn1_decode_simple_der` (static, core)
> libtasn1/lib/decoding.c:2192 `asn1_decode_simple_der` (target, public)
> libtasn1/lib/decoding.c:2233 `_asn1_decode_simple_ber` (static, core)
> libtasn1/lib/decoding.c:2501-2507 `asn1_decode_simple_ber` (public)
> libtasn1/lib/decoding.c:2202 `append` (static, core)
> libtasn1/lib/decoding.c:112 `asn1_get_length_der` (public)
> libtasn1/lib/decoding.c:250 `asn1_get_length_ber` (public)
> libtasn1/lib/decoding.c:180 `asn1_get_tag_der` (public)
> libtasn1/lib/coding.c:203 `asn1_encode_simple_der` (public)
> all also re-declared in libtasn1/lib/includes/libtasn1.h.in:423-464.

### P2.7 Co-call Constraints
**Claim:** No lock/unlock or begin/commit pairing exists for `asn1_decode_simple_der`. The natural pairing in tests is encode/decode round-trip: `asn1_encode_simple_der` (produces tag+length) is concatenated with the raw string bytes to form a `der` buffer, which is then fed to `asn1_decode_simple_der` (or `asn1_decode_simple_ber`) to verify round-trip equality. For `asn1_decode_simple_ber`, every successful call must be paired with `free(*str)` by the caller (allocation/free pairing), unlike the DER target.
**Evidence:**
> libtasn1/tests/Test_strings.c:94-121: encode then decode pairing (no resource pairing, just logical round trip).
> libtasn1/tests/Test_strings.c:136-153: `asn1_decode_simple_ber(...)` followed by `free (b);` on success — allocation/free pairing specific to the BER variant.
> libtasn1/fuzz/libtasn1_gnutls_der_fuzzer.c:37-53: `asn1_decode_simple_ber` result freed with `free(str)` inside an `if (ret == ASN1_SUCCESS)` block, confirming the free-only-on-success convention.

### P2.8 Prerequisite State
**Claim:** No global/library state, context, connection, or configuration object must exist before calling `asn1_decode_simple_der`; it is a pure function over its arguments. The only "state" that matters is the static internal `_asn1_tags[]` table used by the `ETYPE_*` macros, which is a compile-time constant array, not something the caller must initialize.
**Evidence:**
> libtasn1/lib/int.h:92-106: `ETYPE_TAG`/`ETYPE_CLASS`/`ETYPE_OK` macros reference `extern const tag_and_class_st _asn1_tags[];` — a fixed table, no init function found for it (`Grep` for `_asn1_tags_size =` shows it is a static array defined in generated/static source, not runtime-initialized).

### P2.9 Result Usage
**Claim:** Callers use `*str`/`*str_len` as a read-only view into the original `der` buffer, typically for `memcmp`/content comparison, further BER-level decoding (`_asn1_decode_simple_ber` reuses `_asn1_decode_simple_der`'s output `cout`/`out_len` and immediately feeds it into `append()` to build up a reassembled buffer), or simple printing. No project API takes the `(str, str_len)` pair as an opaque handle requiring a dedicated destructor; it is consumed in place.
**Evidence:**
> libtasn1/tests/Test_strings.c:123-128: `memcmp (str, tv[i].str, str_len)` — direct content comparison, no further decode.
> libtasn1/lib/decoding.c:2452-2467 (in `_asn1_decode_simple_ber`): `_asn1_decode_simple_der (etype, der, _der_len, &cout, &out_len, dflags); ... append (&total, &total_size, cout, out_len);` — the DER target's output is copied via `append()` into a newly-malloc'd buffer that the BER function (and ultimately `asn1_decode_simple_ber`) owns and returns to its own caller.
> libtasn1/lib/decoding.c:1240-1260 area (general `asn1_der_decoding` internals) also calls `_asn1_decode_simple_ber` on parsed nodes (line ~1249) feeding results into the node's `value`/`value_len` fields of an `asn1_node` tree — i.e. in the full schema-based decoder, results become node values (not directly relevant to this standalone-API harness but shows the same str/len convention project-wide).

### P2.10 Behaviour Controls
**Claim:**
- `etype` selects which `ASN1_ETYPE_*` tag/class is expected; mismatched tag/class vs. `etype` causes `ASN1_DER_ERROR`.
- The internal `dflags` parameter of `_asn1_decode_simple_der`/`_asn1_decode_simple_ber` (not exposed to the public API) controls whether a tag is expected (`DECODE_FLAG_HAVE_TAG`, always set by both public wrappers with `have_tag` effectively hard-coded true for `asn1_decode_simple_der`), whether input is constructed/indefinite (`DECODE_FLAG_CONSTRUCTED`), and recursion depth (`DECODE_FLAG_LEVEL1/2/3`, capped at 3 to bound recursion in `asn1_decode_simple_ber`'s indefinite-length branch).
- `asn1_decode_simple_ber`'s extra `ber_len` out-parameter (may be NULL) toggles whether the function also computes/report total consumed BER length.
- The buffer content itself selects the path: short-form length (bit7=0) vs. long-form (bit7=1, k nonzero) vs. indefinite form (`0x80`, only meaningful in the BER path) vs. high-tag-number form (low 5 tag bits all 1) in `asn1_get_tag_der`.
**Evidence:**
> libtasn1/lib/decoding.c:58-62: `#define DECODE_FLAG_HAVE_TAG 1 / DECODE_FLAG_CONSTRUCTED (1<<1) / DECODE_FLAG_LEVEL1 (1<<2) / DECODE_FLAG_LEVEL2 (1<<3) / DECODE_FLAG_LEVEL3 (1<<4)`.
> libtasn1/lib/decoding.c:2197-2198: `asn1_decode_simple_der` always passes `DECODE_FLAG_HAVE_TAG`.
> libtasn1/lib/decoding.c:2299-2300: branch guarded by `((dflags & DECODE_FLAG_CONSTRUCTED) || class == ASN1_CLASS_STRUCTURED) && ETYPE_IS_STRING(etype) && !(dflags & DECODE_FLAG_LEVEL3)` — recursion stops once LEVEL3 is reached.
> libtasn1/lib/decoding.c:2309 vs 2377: `if (der_len > 0 && p[0] == 0x80) /* indefinite */ ... else /* constructed */` — selects indefinite-EOC-terminated vs definite-length constructed sub-decoding.
> libtasn1/lib/decoding.c:121-154 (`asn1_get_length_der`): short form (`!(der[0]&128)`) vs long form (`der[0]&0x7F` nonzero) vs indefinite (`k==0`, returns -1).
> libtasn1/lib/decoding.c:190-227 (`asn1_get_tag_der`): short tag form (`(der[0]&0x1F)!=0x1F`) vs long/high-tag-number form.
> libtasn1/lib/decoding.c:2250-2251,2440-2450: `ber_len` is optional (`if (ber_len) *ber_len = 0; ... if (ber_len) *ber_len += ...`).

### P2.11 Input Expectations
**Claim:** `der` is expected to start with a DER tag octet (class+tag, short or high-tag-number form) immediately followed by a DER length octet (short/definite long form only — `asn1_decode_simple_der` rejects indefinite length since `asn1_get_length_der` returns -1 for that and the caller treats `ret < 0` as `ASN1_DER_ERROR`), followed by that many content bytes, all within `_der_len` bytes total. No NUL-termination is required (lengths are explicit). `_der_len` must be large enough for tag+length+content; otherwise `ASN1_DER_ERROR`/`ASN1_VALUE_NOT_VALID` is returned. Oversized/overflowing lengths are checked in `asn1_get_length_der` via `INT_MULTIPLY_OVERFLOW`/`INT_ADD_OVERFLOW` and the `sum > der_len` bound check.
**Evidence:**
> libtasn1/lib/decoding.c:2133-2134: `if (der == NULL || der_len == 0) return ASN1_VALUE_NOT_VALID;`
> libtasn1/lib/decoding.c:2146-2162: tag parsing requires `der_len` to have room (`asn1_get_tag_der` itself checks `der_len < 2` at decoding.c:186 for the no-short-tag fallback), then `der_len -= tag_len; if (der_len <= 0) return ASN1_DER_ERROR;`
> libtasn1/lib/decoding.c:2164-2171: `ret = asn1_get_length_der (p, der_len, &len_len); if (ret < 0) return ASN1_DER_ERROR;` then `der_len -= len_len; if (der_len <= 0) return ASN1_DER_ERROR;` — note this only checks there is at least 1 content byte room left, but does not re-check `ret <= der_len` here (that bound check already happened inside `asn1_get_length_der`'s `sum > der_len` test at decoding.c:161-162, which returns -4 → caught by `ret < 0`).
> libtasn1/lib/decoding.c:137-144,157-158: `INT_MULTIPLY_OVERFLOW (ans, 256)` / `INT_ADD_OVERFLOW (ans, der[punt])` / `INT_ADD_OVERFLOW (sum, *len)` guard against integer overflow while accumulating a long-form length value.

## Other entries of the Logic Group

### Entry: asn1_decode_simple_ber (BER variant, recursive/allocating)
**Protocol:**
- Prerequisite: none beyond a valid `der`/`_der_len` buffer and valid `etype` (same `ETYPE_OK` check, but note it does NOT require `ETYPE_IS_STRING`; non-string/definite types fall through to calling `_asn1_decode_simple_der` internally). Must be `ASN1_CLASS_UNIVERSAL`.
  Evidence: libtasn1/lib/decoding.c:2259-2271.
- Parameters: `etype` (unsigned int, universal etype), `der`/`_der_len` (input buffer, same unsigned→signed cast caveat as the target: `int der_len = _der_len;` at decoding.c:2240), `str` (`unsigned char **`, **output is heap-allocated** via `_asn1_realloc`/`append`, caller must `free(*str)` on success), `str_len` (`unsigned int *`), `ber_len` (`unsigned int *`, **optional, may be NULL**, reports total bytes of BER consumed).
  Evidence: libtasn1/lib/decoding.c:2233-2236 (signature), 2250-2251 (`if (ber_len) *ber_len = 0;`).
- Return: `ASN1_SUCCESS` (0) on success — then `*str` is a freshly malloc'd buffer (even if the content is empty? note `append()` only reallocs when `src_size>0`, so `*str` could remain NULL with `*str_len==0` if no fragments were appended — harness must `free()` a possibly-NULL pointer, which is safe). Any other value is an error; `cleanup:` label frees internal temporaries (`out`, `total`) before returning, so the caller must NOT free anything on error (nothing was returned to it).
  Evidence: libtasn1/lib/decoding.c:2476-2484 (`*str = total; *str_len = total_size; return ASN1_SUCCESS; cleanup: free(out); free(total); return result;`).
- Recursion/paths: indefinite-length constructed strings (leading length octet `0x80`) are decoded by recursively calling `_asn1_decode_simple_ber` with `DECODE_FLAG_LEVEL1`→`LEVEL2`→`LEVEL3` to cap recursion at 3 levels, each fragment's result appended via `append()` and then `free(out)`'d; definite-length constructed strings use a `while (const_len > 0)` loop instead of recursion depth tracking beyond the same LEVEL flags; non-string/definite classes call `_asn1_decode_simple_der` once and `append()` its output.
  Evidence: libtasn1/lib/decoding.c:2309-2436 (both indefinite `do{...}while(1)` and constructed `while(const_len>0)` branches), 2438-2468 (fallback to `_asn1_decode_simple_der`).
- Lifecycle in harness: call `asn1_decode_simple_ber(etype, data, size, &str, &str_len, &ber_len_or_NULL)`; on `ASN1_SUCCESS`, use `str`/`str_len`, then `free(str)`.
  Evidence: libtasn1/tests/Test_strings.c:136-153; libtasn1/fuzz/libtasn1_gnutls_der_fuzzer.c:36-53 (`assert(ret_len <= size); free(str);` pattern, called twice with `ASN1_ETYPE_BIT_STRING` and `ASN1_ETYPE_OCTET_STRING`).
- Combination with target in one harness: a harness can call BOTH `asn1_decode_simple_der` (plain DER path, no alloc) and `asn1_decode_simple_ber` (BER/indefinite path, allocates) on the same or related `data`/`size`, mirroring `libtasn1_gnutls_der_fuzzer.c` and `tests/Test_strings.c` which exercise both; each call is independent (no shared state), but `asn1_decode_simple_ber`'s successful result must be freed while `asn1_decode_simple_der`'s must not.

### Entry: asn1_encode_simple_der (encoder — not consumed by target, but can produce its input)
**Protocol:**
- Prerequisite: none. Pure function.
- Parameters: `etype` (same `ETYPE_OK`+`ASN1_CLASS_UNIVERSAL` check, but no `ETYPE_IS_STRING` restriction here), `str`/`str_len` (input content bytes; `str` must be non-NULL even if `str_len==0`), `tl` (caller-allocated output buffer, recommended size `ASN1_MAX_TL_SIZE` = `ASN1_MAX_TAG_SIZE(4)+ASN1_MAX_LENGTH_SIZE(9)` = 13 bytes), `tl_len` (in/out: caller sets it to `sizeof(tl)` before the call, function sets it to actual bytes used on success).
  Evidence: libtasn1/lib/coding.c:203-243; libtasn1/lib/includes/libtasn1.h.in:420-422 (`ASN1_MAX_TAG_SIZE 4`, `ASN1_MAX_LENGTH_SIZE 9`, `ASN1_MAX_TL_SIZE`).
- Return: `ASN1_SUCCESS` on success (`*tl`/`*tl_len` filled with tag+length encoding only — the content bytes `str` are NOT copied into `tl`; caller must separately append `str` after `tl` to form full DER). `ASN1_VALUE_NOT_VALID` if `str==NULL`, bad `etype`, or non-UNIVERSAL class; `ASN1_MEM_ERROR` if `*tl_len` (input capacity) is smaller than the required `tag_len+len_len`.
  Evidence: libtasn1/lib/coding.c:213-233.
- Lifecycle/combination with target: used to build a well-formed `der` buffer to feed into `asn1_decode_simple_der`/`asn1_decode_simple_ber` for round-trip testing: `tl_len = sizeof(tl); asn1_encode_simple_der(etype, str, str_len, tl, &tl_len);` then concatenate `tl[0..tl_len)` + `str[0..str_len)` into one buffer of length `tl_len+str_len`, and decode that buffer.
  Evidence: libtasn1/tests/Test_strings.c:92-121 (exact pattern: encode into `tl`, compare full concatenation against expected `der`, then decode `tv[i].der` directly — note test uses a separately pre-encoded `tv[i].der` constant for decode, not `tl`+`str` concatenation, but `Test_simple.c:100` shows `asn1_encode_simple_der` used standalone for round-trip-style testing too).
- Nothing to free: `tl` is caller-stack-allocated (`unsigned char tl[ASN1_MAX_TL_SIZE];` in Test_strings.c:82), no heap allocation inside the function.

### Entry: asn1_get_length_der (low-level DER length parser, called internally by the target)
**Protocol:**
- Parameters: `der` (pointer to the length field, i.e., buffer already advanced past the tag), `der_len` (signed `int`, remaining bytes from `der`), `len` (output: number of bytes the length field itself occupies).
- Return: the decoded length value (`long`, ≥0) on success; `-1` if indefinite length (0x80 leading byte with no following octets consumed as length -- `k==0`); `-2` if the value overflows an `int`; `-4` if `decoded_length + len_len > der_len` (declared length exceeds buffer).
  Evidence: libtasn1/lib/decoding.c:100-110 (doc comment), 112-165 (implementation), confirmed return codes `-1`/`-2`/`-4` all present at lines 150,138/142/157,162.
- Used standalone: `fuzz/asn1_get_length_der_fuzzer.c:30-41` calls it directly on raw fuzz data (`size` capped at `MAXDATASIZE=512`), discarding the return value — a minimal/no-crash check fuzzer, independent of `asn1_decode_simple_der`.
- In the target's call chain: `_asn1_decode_simple_der` calls it at decoding.c:2164 right after consuming the tag, and treats `ret < 0` (any negative: -1/-2/-4) uniformly as `ASN1_DER_ERROR`.
  Evidence: libtasn1/lib/decoding.c:2164-2171.
- Nothing to free (no allocation); no init needed.

### Entry: asn1_get_length_ber (BER length parser, extends DER one with indefinite-length resolution)
**Protocol:**
- Parameters: same shape as `asn1_get_length_der`.
- Return: same as `asn1_get_length_der`, except when the DER parser returns `-1` (indefinite) **and** `ber_len>1`, it calls `_asn1_get_indefinite_length_string(ber+1, ber_len-1, &ret)` to scan forward for the terminating EOC and returns the *total* implied length, or `-3` if that scan fails.
  Evidence: libtasn1/lib/decoding.c:234-265.
- Used standalone: `fuzz/asn1_get_length_ber_fuzzer.c:30-41`, same minimal pattern as the DER one, independent of the target.
- In `asn1_decode_simple_ber`'s call chain: used at decoding.c:2381 inside the "constructed, not indefinite" branch to get the definite content length of a constructed wrapper before recursing.
- Nothing to free; no init needed.

### Entry: asn1_get_tag_der (low-level tag parser, called internally by the target when DECODE_FLAG_HAVE_TAG is set)
**Protocol:**
- Parameters: `der`, `der_len` (signed int, must be ≥2 or returns `ASN1_DER_ERROR` immediately), `cls` (output class byte), `len` (output: bytes occupied by the tag field, must be non-NULL or `ASN1_DER_ERROR`), `tag` (output decoded tag number, may be NULL).
- Return: `ASN1_SUCCESS` or `ASN1_DER_ERROR` (including on tag-number overflow via `INT_MULTIPLY_OVERFLOW`/`INT_ADD_OVERFLOW`, or when the high-tag-number continuation runs past `der_len`).
  Evidence: libtasn1/lib/decoding.c:167-232.
- In the target's call chain: `_asn1_decode_simple_der` calls it first (decoding.c:2148) when `DECODE_FLAG_HAVE_TAG` is set (always true for the public `asn1_decode_simple_der`), then checks `class != ETYPE_CLASS(etype) || tag != ETYPE_TAG(etype)` and fails with `ASN1_DER_ERROR` on mismatch (decoding.c:2152-2156).
- Not found as a standalone fuzz entry in `fuzz/` (only `asn1_get_length_der_fuzzer.c`/`asn1_get_length_ber_fuzzer.c`/`asn1_get_object_id_der_fuzzer.c` exist for low-level parsers; no `asn1_get_tag_der_fuzzer.c`), so in a harness it is only exercised indirectly via `asn1_decode_simple_der`/`asn1_decode_simple_ber`.
  Evidence: Glob of `fuzz/*.c` listed 11 files, none named `asn1_get_tag_der_fuzzer.c`.
- Nothing to free; no init needed.

### Core function: append (internal helper used by asn1_decode_simple_ber, not a public entry)
**Claim:** `append` is `static` (not declared in `libtasn1.h.in`), so it is NOT part of the public API surface and cannot/should not be called directly by a harness. It is documented here only because it governs the allocation behavior that the harness must account for when calling `asn1_decode_simple_ber` (i.e., when and why `*str` is heap-allocated and must be freed).
**Evidence:**
> libtasn1/lib/decoding.c:2201-2214: `static int append (uint8_t **dst, unsigned *dst_size, const unsigned char *src, unsigned src_size) { ... *dst = _asn1_realloc(*dst, *dst_size + src_size); ... memcpy(...); ... }` — not present in libtasn1.h.in (confirmed by earlier grep of the header, which lists only the public entries).

## Build notes

**Claim:** Fuzzers for this project are plain C, compiled/linked via the `fuzz/` autotools target or, for OSS-Fuzz, via the `oss-fuzz` Makefile rule which invokes `$CXX`/`$CC`-provided fuzzer engine directly; the target's header `libtasn1.h` is reached via `-I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes` (the generated/installed header lives at `lib/includes/libtasn1.h.in` → built into `lib/includes/libtasn1.h`); the library is statically linked (`--enable-static --disable-shared`) via `../lib/libtasn1.la`.

**Evidence:**
> ossfuzz_view/libtasn1-c2/build.sh:20: `./configure --enable-static --disable-shared --disable-doc --disable-gcc-warnings --enable-fuzzing`
> ossfuzz_view/libtasn1-c2/build.sh:26-28: `cd fuzz && make oss-fuzz && find . -name '*_fuzzer' -exec cp -v '{}' $OUT ';'`
> libtasn1/fuzz/Makefile.am:24-25: `AM_CPPFLAGS = -I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(srcdir) -I$(top_builddir)/lib/gl -I$(top_srcdir)/lib/gl ...`
> libtasn1/fuzz/Makefile.am:28: `LDADD = ../lib/gl/libgnu.la ../lib/libtasn1.la $(FUZZ_LIBS)`
> libtasn1/fuzz/Makefile.am:30-39: existing fuzzer target list includes `asn1_decode_simple_ber_fuzzer` and `asn1_decode_simple_der_fuzzer` already defined as first-class OSS-Fuzz targets (`asn1_decode_simple_der_fuzzer_SOURCES = asn1_decode_simple_der_fuzzer.c $(MAIN)` at line 63), confirming the pipeline's existing convention of pairing one fuzzer source file per target entry.
> libtasn1/fuzz/Makefile.am:73-79 (`oss-fuzz:` rule): `for ccfile in *_fuzzer.c; do ... $$CXX $$CXXFLAGS -I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(top_srcdir) "$${fuzzer}.c" -o "$${...` — confirms OSS-Fuzz build compiles each `*_fuzzer.c` with C++ compiler (libFuzzer convention) but the fuzzer sources themselves are plain C (`#include <config.h>` / `#include "libtasn1.h"`), e.g. libtasn1/fuzz/asn1_decode_simple_ber_fuzzer.c:23-29.
> libtasn1/fuzz/asn1_decode_simple_der_fuzzer.c:1-16: current repository state shows this specific fuzzer stubbed out to an empty `LLVMFuzzerTestOneInput` that does nothing but keep the symbol linked (`__asm__("asn1_decode_simple_der")`) — i.e., the "gold" harness for this exact target currently in-tree is a placeholder and the downstream Harness Generator is expected to produce the real body; `asn1_decode_simple_ber_fuzzer.c` (lines 30-56) remains a full, working example of the sibling BER entry and is the best concrete template for calling conventions (etype selection, success check, `free`, `assert(ret_len<=size)`).