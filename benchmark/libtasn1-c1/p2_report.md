## target: asn1_decode_simple_ber

### P2.1 Init Sequence
**Claim:** `asn1_decode_simple_ber` requires NO library-global init call and no object/handle setup. It is a stateless, self-contained decoding function operating directly on a caller-supplied byte buffer and an `etype` constant. The only "setup" is choosing a valid `etype` and preparing the `der`/`_der_len` buffer; no `asn1_node`, no `asn1_create_element`, no `asn1_array2tree` are needed to call it directly.
**Evidence:**
> lib/decoding.c:2501-2508:
> ```
> int
> asn1_decode_simple_ber (unsigned int etype, const unsigned char *der,
>             unsigned int _der_len, unsigned char **str,
>             unsigned int *str_len, unsigned int *ber_len)
> {
>   return _asn1_decode_simple_ber (etype, der, _der_len, str, str_len, ber_len,
>                   DECODE_FLAG_HAVE_TAG);
> }
> ```
> tests/Test_strings.c:136-138: called directly with a static `ber[i]` table entry, no prior init other than filling the struct.
> tests/octet-string.c:187-191: called directly after building `tv[i].der_str`/`tv[i].der_len` with no other API calls before it.
> fuzz/asn1_decode_simple_ber_fuzzer.c (current stub) just references the symbol; the project also has `fuzz/asn1_decode_simple_der_fuzzer.c` (a related entry) calling the sibling function directly with no init (lines 36-50).

By contrast, when the *library* reaches this function internally (as part of a schema-based decode), the init sequence is: `asn1_array2tree`/`asn1_parser2tree` → `asn1_create_element` → `asn1_der_decoding`/`asn1_der_decoding2`, which then calls `_asn1_decode_simple_ber` on an OCTET STRING node — see lib/decoding.c:1248-1251. This path is optional for a harness that calls the target directly; it is required only if the harness wants to exercise `asn1_der_decoding2`'s BER OCTET STRING branch (see "other entries" below).

### P2.2 Parameter Construction
**Claim:**
- `etype` (unsigned int): must be one of the `ASN1_ETYPE_*` constants satisfying `ETYPE_OK(etype)` (non-`ASN1_ETYPE_INVALID`, `< _asn1_tags_size`, has a description) AND `ETYPE_CLASS(etype) == ASN1_CLASS_UNIVERSAL`; otherwise the function returns `ASN1_VALUE_NOT_VALID` immediately. Any `etype` value, valid or garbage, is safe to pass — it is range/table-checked before use.
- `der` (`const unsigned char *`): caller-owned buffer, must be non-NULL; may contain attacker-controlled bytes of any content. Not required to be NUL-terminated (length-driven, not string-driven).
- `_der_len` (`unsigned int`): must be > 0 (checked as `der_len == 0` after assignment to a signed `int der_len`; note `_der_len` is cast to `int`, so values above `INT_MAX` wrap negative and are routed through the internal signed-length arithmetic — a harness should therefore keep `_der_len <= INT_MAX`, ideally `<= actual buffer size`, i.e., `_der_len == size of der buffer the harness allocated`).
- `str` (`unsigned char **`, out): non-NULL pointer required by convention (not NULL-checked in the function, dereferenced unconditionally at `*str = total;`) — caller must pass a valid `unsigned char *` variable address.
- `str_len` (`unsigned int *`, out): same — dereferenced unconditionally at `*str_len = total_size;`, must be non-NULL.
- `ber_len` (`unsigned int *`, out, may be NULL): explicitly checked with `if (ber_len) *ber_len = 0;` and other `if (ber_len)` guards — this is the only pointer the API explicitly documents/allows as NULL.
**Evidence:**
> lib/decoding.c:2250-2271 (inside `_asn1_decode_simple_ber`):
> ```
> if (ber_len) *ber_len = 0;
> if (der == NULL || der_len == 0) { ... return ASN1_VALUE_NOT_VALID; }
> if (ETYPE_OK (etype) == 0) { ... return ASN1_VALUE_NOT_VALID; }
> class = ETYPE_CLASS (etype);
> if (class != ASN1_CLASS_UNIVERSAL) { ... return ASN1_VALUE_NOT_VALID; }
> ```
> lib/decoding.c:2476-2477: `*str = total; *str_len = total_size;` — unconditional dereference, no NULL check on `str`/`str_len`.
> lib/includes/libtasn1.h.in:440-442 (declaration comment block, "ber_len: the total length occupied by BER (may be %NULL)") confirms only `ber_len` is nullable.
> lib/int.h:94-96: `ETYPE_OK` macro definition used for validation.

### P2.3 Object Lifecycle
**Claim:** The target is not an object-lifecycle API itself (no create/destroy pair for a handle). Its lifecycle is: (1) caller prepares `der`/`_der_len` and a valid `etype`; (2) call `asn1_decode_simple_ber`; (3) on `ASN1_SUCCESS`, `*str` points to a freshly `malloc`/`realloc`-backed buffer of `*str_len` bytes that the **caller must `free()`**; on failure, `*str`/`*str_len` are left untouched (not guaranteed to be set) and nothing needs freeing.
**Evidence:**
> lib/decoding.c:2201-2214 `append()` uses `_asn1_realloc` to grow the output buffer that becomes `total`/`*str`.
> lib/decoding.c:2476 `*str = total;` — `total` is heap-allocated via `append`→`_asn1_realloc` (ultimately `realloc`), so caller owns it.
> lib/decoding.c:2480-2483 (cleanup label): `free (out); free (total); return result;` — on any error path only internal temporaries are freed; `*str` is never written in these paths, so caller must NOT call `free()` on an uninitialized `str` if the call failed.
> tests/Test_strings.c:137-153: calls target, checks `ret != ASN1_SUCCESS`, and on success does `free (b);` (b is the `str` out-param) — demonstrating required caller-side `free`.
> tests/octet-string.c:226-227: `free (tmp); tmp = NULL;` after successful decode — same pattern.

### P2.4 Return Value Handling
**Claim:** Return type `int`. `ASN1_SUCCESS` (0) means `*str`/`*str_len` (and `*ber_len` if non-NULL) are valid and must eventually be freed (`*str`). Non-zero error codes include `ASN1_VALUE_NOT_VALID` (bad etype/class/NULL der/zero len), `ASN1_DER_ERROR` (malformed tag/length/EOC/tag mismatch), `ASN1_MEM_ALLOC_ERROR` (realloc failure in `append`), and whatever `asn1_get_tag_der`/`asn1_get_length_ber` propagate. On any non-success return, the out-params should be treated as not holding an allocation that needs freeing (the function frees its own internal temporaries on the `cleanup:` path and never assigns `*str`).
**Evidence:**
> lib/decoding.c:2253-2271: returns `ASN1_VALUE_NOT_VALID` for NULL/zero-length `der`, bad `etype`, or non-`ASN1_CLASS_UNIVERSAL` class.
> lib/decoding.c:2277-2288: returns result of `asn1_get_tag_der` directly (e.g. `ASN1_DER_ERROR`) or `ASN1_DER_ERROR` on tag mismatch.
> lib/decoding.c:2348-2353, 2426-2431, 2462-2467: propagate `append()`'s `ASN1_MEM_ALLOC_ERROR` via `goto cleanup`.
> lib/decoding.c:2480-2483: `cleanup:` frees `out`/`total` and returns the stored `result` (non-success) without touching caller's `*str`.
> tests/Test_strings.c:139-144 / tests/octet-string.c:192-199: real callers check `ret != ASN1_SUCCESS` (or compare to an `expected_error`) before touching `*str`/`*str_len`.

### P2.5 Cleanup Sequence
**Claim:** On success, caller must `free(*str)` exactly once after use; `*str_len`/`*ber_len` are plain integers needing no cleanup. On failure, nothing allocated by this call needs freeing (internal `out`/`total` already freed in `cleanup:`). There is no explicit "close" or paired teardown API for the target itself.
**Evidence:**
> lib/decoding.c:2480-2483 cleanup block frees internal temporaries only.
> tests/octet-string.c:226: `free (tmp); tmp = NULL;` right after use, before loop continues (repeated per-iteration pattern), confirming single `free` per successful call.
> tests/Test_strings.c:153: `free (b);` once per successful BER decode in the loop.

### P2.6 API Existence
**Claim:** All functions mentioned in this report exist in the current source tree:
- `asn1_decode_simple_ber` — lib/decoding.c:2501; declared lib/includes/libtasn1.h.in:441.
- `asn1_decode_simple_der` — lib/decoding.c:2192; declared lib/includes/libtasn1.h.in:435.
- `asn1_encode_simple_der` — lib/coding.c:203; declared lib/includes/libtasn1.h.in:447 (grep match).
- `asn1_der_decoding2` — lib/decoding.c:918; declared lib/includes/libtasn1.h.in:373.
- `asn1_der_decoding` — lib/decoding.c:1624; declared lib/includes/libtasn1.h.in:378.
- `asn1_create_element` — lib/structure.c:715 (signature match at line 700 comment); declared lib/includes/libtasn1.h.in:338.
- `asn1_delete_structure` — lib/structure.c:297; declared lib/includes/libtasn1.h.in:341.
- `asn1_array2tree` — lib/structure.c:180; declared lib/includes/libtasn1.h.in:330.
- `asn1_get_tag_der`, `asn1_get_length_der`, `asn1_get_length_ber`, `asn1_length_der` — all in lib/decoding.c (179, 112, 250) and lib/coding.c (73) respectively, internal/core helpers also publicly exported per libtasn1.h.in (grep hits above).
- Internal-only (static, not part of public API, confirmed by `static` qualifier): `_asn1_decode_simple_ber` (lib/decoding.c:2233), `_asn1_decode_simple_der` (lib/decoding.c:2122), `append` (lib/decoding.c:2202).
**Evidence:** see file:line citations above; `static` keyword confirmed at lib/decoding.c:77-81 (forward decl) and lib/decoding.c:2232-2236 (definition) for `_asn1_decode_simple_ber`.

### P2.7 Co-call Constraints
**Claim:** No lock/unlock-style pairing for the target itself (stateless call). The only true pairing involving this feature's entries is: `asn1_create_element()` (allocates an `asn1_node`) must be paired with `asn1_delete_structure()`/`asn1_delete_structure2()` when using `asn1_der_decoding`/`asn1_der_decoding2` (which internally may call the target). `malloc`'d output of `asn1_decode_simple_ber` must be paired with `free()`.
**Evidence:**
> lib/structure.c:297-299 `asn1_delete_structure` / lib/includes/libtasn1.h.in:338-344 create/delete pair.
> fuzz/libtasn1_pkix_der_fuzzer.c:542-566: `asn1_create_element(...,&dn)` ... `asn1_der_decoding(&dn,...)` ... `out: asn1_delete_structure(&dn);` — canonical create/decode/delete pairing (even on error, via `goto out`).
> tests/octet-string.c:226 / tests/Test_strings.c:153: `free()` pairing with the target's allocation.

### P2.8 Prerequisite State
**Claim:** No global/process state is required before calling `asn1_decode_simple_ber` directly — it reads only from the static internal table `_asn1_tags[]` (compiled into the library) via the `ETYPE_*` macros, not from any user-initialized context. No connections, config objects, or file handles are needed. (This differs from the schema-based entry points `asn1_der_decoding`/`asn1_der_decoding2`, which DO require a prerequisite `asn1_node` tree built via `asn1_array2tree`/`asn1_parser2tree` + `asn1_create_element`.)
**Evidence:**
> lib/int.h:92-96, 105-106: `_asn1_tags[]` / `_asn1_tags_size` are `extern` globals defined at link time in the library (no init call needed, static data).
> lib/decoding.c:2253-2271: validation functions reference only `etype`-table macros and function parameters, no global init call.

### P2.9 Result Usage
**Claim:** Callers of `asn1_decode_simple_ber` use its outputs in two ways observed in the codebase:
1. Direct test/fuzz usage: compare `*str`/`*str_len` to expected plaintext bytes via `memcmp`, then `free(*str)`.
2. Internal structure-decode usage (`asn1_der_decoding2`): the returned heap buffer `ptmp`/`vlen` is immediately handed to `_asn1_set_value_lv(p, ptmp, vlen)` which **takes ownership** of the buffer and stores it as the node's value (the comment pattern and the subsequent `free(ptmp)` on line 1263 show the library frees its OWN local copy — because `_asn1_set_value_lv` apparently copies/does not take ownership; read exactly as written below), after which decoding continues via `counter += ber_len;` to advance the parse cursor using the companion `ber_len` out-parameter.
**Evidence:**
> lib/decoding.c:1248-1264:
> ```
> result = _asn1_decode_simple_ber (type_field (p->type),
>                  der + counter, ider_len, &ptmp,
>                  &vlen, &ber_len, dflags);
> if (result != ASN1_SUCCESS) { warn (); goto cleanup; }
> DECR_LEN (ider_len, ber_len);
> _asn1_set_value_lv (p, ptmp, vlen);
> counter += ber_len;
> free (ptmp);
> ```
> This shows: (a) `ber_len` out-param is used to advance the outer parser cursor (`counter += ber_len`) and to decrement the remaining buffer length (`DECR_LEN(ider_len, ber_len)`); (b) `str`/`*str_len` (`ptmp`/`vlen`) are passed to `_asn1_set_value_lv` to store into the ASN.1 node; (c) `ptmp` is `free()`d right after, confirming the target's returned buffer is caller-owned and must be freed once no longer needed.
> tests/Test_strings.c:136-153: `str_len` compared via `memcmp(b, ber[i].str, str_len)`, consumed purely for validation then freed.
> tests/octet-string.c:187-227: `der_len` (the `ber_len` out param) compared against expected total BER length `tv[i].der_len`, and `tmp`/`str_size` (the `str`/`str_len` out params) compared against expected decoded plaintext — i.e. callers validate BOTH outputs (decoded value AND total consumed BER length) independently.

### P2.10 Behaviour Controls
**Claim:** Behaviour of `asn1_decode_simple_ber` (and its internal worker) is controlled by:
1. `etype`'s class/tag (from `_asn1_tags[]`): selects which DER tag is expected (`ETYPE_TAG`) and gates further class checks.
2. The leading byte of `der` at the tag position: bit `ASN1_CLASS_STRUCTURED` (0x20, combined into the class nibble read by `asn1_get_tag_der`) chosen by the input selects the constructed/indefinite-or-definite recursive path (`append()`-based concatenation) vs. the plain non-constructed DER path (delegated to `_asn1_decode_simple_der`).
3. First length byte after tag: `0x80` selects BER *indefinite length* (loop until 2-byte `00 00` EOC) vs. any other value selects BER *definite-length constructed* (`asn1_get_length_ber` returns a concrete length, loop consumes exactly that many bytes across recursive calls).
4. Internal recursion-depth flags `DECODE_FLAG_LEVEL1/2/3`: not caller-settable (there is no public flags parameter on `asn1_decode_simple_ber`), but internally capped: the constructed-string branch is skipped once `DECODE_FLAG_LEVEL3` is set, forcing termination at 3 nested levels (`!(dflags & DECODE_FLAG_LEVEL3)` condition) — this bounds recursion regardless of how the attacker nests constructed strings.
5. `ETYPE_IS_STRING(etype)`: only string-type ETYPEs (OCTET STRING, BIT STRING, IA5, UTF8, etc.) take the constructed/indefinite path at all; non-string universal types always go to `_asn1_decode_simple_der`.
6. `ber_len` being NULL or non-NULL: toggles whether the total consumed-length bookkeeping is computed/returned at all (guarded with `if (ber_len)` throughout) — does not change parsing outcome, only whether the caller receives the consumed length.
**Evidence:**
> lib/decoding.c:2299-2300: `if ((((dflags & DECODE_FLAG_CONSTRUCTED) || class == ASN1_CLASS_STRUCTURED) && ETYPE_IS_STRING (etype)) && !(dflags & DECODE_FLAG_LEVEL3))` — the combined gating condition.
> lib/decoding.c:2309 `if (der_len > 0 && p[0] == 0x80) /* indefinite */` vs. the `else /* constructed */` branch at line 2377 using `asn1_get_length_ber`.
> lib/decoding.c:2322-2329 / 2401-2408: level-flag escalation `LEVEL1 -> LEVEL2 -> LEVEL3` on each recursive call, capping recursion (recursion stops being attempted once LEVEL3 is already set, per the guard at line 2300).
> lib/decoding.c:2438 `else if (class == ETYPE_CLASS (etype))` — the definite/non-constructed fallback path into `_asn1_decode_simple_der`.
> lib/decoding.c:2250, 2294, 2315, 2360, 2394, 2423, 2440, 2449: every `ber_len` write is guarded by `if (ber_len)`.

### P2.11 Input Expectations
**Claim:** The target expects:
- `der_len` (`_der_len` cast to signed `int`) to be nonzero; no explicit upper bound check other than implicit `int` overflow avoidance via `DECR_LEN` macro which detects negative lengths and errors out (`ASN1_DER_ERROR`) rather than reading out of bounds.
- When `DECODE_FLAG_HAVE_TAG` is set (always true for the public `asn1_decode_simple_ber` entry), the buffer must begin with a valid BER/DER tag matching `ETYPE_TAG(etype)` exactly (class+tag byte(s) decoded by `asn1_get_tag_der`, which itself requires `der_len >= 2` for multi-byte tags, or allows 1 byte for short-form tags — see `der_len < 2` check only guards NULL/too-short inputs generically; short form tags need just 1 byte).
- No fixed overall size limit beyond what fits in an `unsigned int`/`int`; lengths are bounds-checked against the remaining buffer at every step (`DECR_LEN`, `asn1_get_length_der`'s internal `sum > der_len` check at lib/decoding.c:161-162).
- For indefinite-length constructed strings: expects a terminating 2-byte EOC marker `00 00`; if absent, the parser treats it as "no EOC" and extends consumption (`der_len += 2;` at line 2366) and errors with `ASN1_DER_ERROR` if that leaves `der_len == 2` (line 2368-2373), avoiding infinite loop but relying on the recursive call itself eventually failing on truncated input.
- No requirement for NUL-termination; this is a length-delimited binary format, not a C string.
- Recursion is naturally bounded to 3 levels of nested constructed/indefinite strings via the `DECODE_FLAG_LEVEL1/2/3` flags (see P2.10), protecting against unbounded stack recursion from attacker-supplied nesting.
**Evidence:**
> lib/decoding.c:2253-2257: `if (der == NULL || der_len == 0) { ... return ASN1_VALUE_NOT_VALID; }`
> lib/decoding.c:64-71 `DECR_LEN` macro: `l -= s; if (l < 0) { ...; result = ASN1_DER_ERROR; goto cleanup; }` — the core bounds-check used throughout to prevent reading past the declared length.
> lib/decoding.c:186-187 (`asn1_get_tag_der`): `if (der == NULL || der_len < 2 || len == NULL) return ASN1_DER_ERROR;` (note: rejects buffers shorter than 2 bytes even for short-form single-byte tags — effective minimum any tag parse needs is checked conservatively here).
> lib/decoding.c:112-165 (`asn1_get_length_der`): bounds-checks decoded length against `der_len`, returns `-4` if `sum > der_len`, `-2` on arithmetic overflow (using `INT_MULTIPLY_OVERFLOW`/`INT_ADD_OVERFLOW` from intprops.h).
> lib/decoding.c:2298-2376: indefinite-length EOC handling and its own loop-termination/error-on-truncation logic.
> lib/decoding.c:2300: `!(dflags & DECODE_FLAG_LEVEL3)` caps recursion at 3 levels.

## Other entries in this feature's API surface

### Entry: asn1_decode_simple_der
**Role:** Sibling entry decoding the SAME `ASN1_ETYPE_*` universal string types but only the plain (non-constructed) DER form; no allocation — returns a pointer *into* the caller's `der` buffer.
**Call protocol:**
- **Needs before it:** nothing (stateless), same as the target. No init call.
- **Parameters:** `etype` (same `ETYPE_OK`+`ASN1_CLASS_UNIVERSAL` validation, PLUS additionally requires `ETYPE_IS_STRING(etype)==1` — rejects non-string universal types, unlike the target which allows non-string types by delegating to this very function); `der`/`_der_len` same constraints as target; `str` is `const unsigned char **` (output is an alias into `der`, NOT allocated — caller must NOT free it); `str_len` is `unsigned int *` out.
- **Return value:** `int`, `ASN1_SUCCESS` or `ASN1_VALUE_NOT_VALID`/`ASN1_DER_ERROR` (same error family as target). On success `*str` points inside the input buffer at offset `tag_len+len_len`; caller must keep the original `der` buffer alive at least as long as `*str` is used.
- **What to free:** Nothing — `*str` is not heap-allocated (it aliases into `der`). Confirmed by signature using `const unsigned char **str` and by the body simply doing `*str = p;` where `p` points inside the input.
- **Behaviour controls:** Only has `DECODE_FLAG_HAVE_TAG` set when called via the public wrapper; does not recurse or support constructed/indefinite forms at all — if `der` encodes a constructed/indefinite OCTET STRING, callers must use the target (`asn1_decode_simple_ber`) instead, which internally calls `_asn1_decode_simple_der` for the definite/non-constructed fallback branch (lib/decoding.c:2452-2455).
- **Combination with the target in one harness:** Both entries can be fed the SAME `(etype, der, _der_len)` triple fuzzer input to compare behavior: for a definite-length non-constructed encoding they should agree (target delegates to this one internally and wraps/copies the result), while for BER constructed/indefinite encodings only the target succeeds. A harness can call both and cross-check results/`str_len` when both report `ASN1_SUCCESS` and the encoding is definite/non-constructed (round-trip/differential testing), as the project's own `fuzz/asn1_decode_simple_der_fuzzer.c` does for the sibling function alone.
**Evidence:**
> lib/decoding.c:2121-2177 (`_asn1_decode_simple_der` definition), especially line 2136: `if (ETYPE_OK (etype) == 0 || ETYPE_IS_STRING (etype) == 0) return ASN1_VALUE_NOT_VALID;`
> lib/decoding.c:2173-2174: `*str_len = ret; *str = p;` — pointer-into-buffer, no allocation.
> lib/decoding.c:2192-2199 (public wrapper `asn1_decode_simple_der`).
> lib/decoding.c:2452-2455: target's call into `_asn1_decode_simple_der` for the definite non-constructed fallback.
> fuzz/asn1_decode_simple_der_fuzzer.c:36-50: project's own usage pattern — calls with two different `etype`s on the same input, checks `ret_len <= size` via `assert`, no `free()` call (confirming non-owning output).

### Entry: asn1_encode_simple_der
**Role:** Inverse of simple DER decoding — encodes a flat `(etype, str, str_len)` into a tag+length header `tl`/`tl_len` (the value bytes `str` are NOT copied into `tl`; caller is expected to append `str` itself after `tl`).
**Call protocol:**
- **Needs before it:** Nothing; caller must pre-allocate the `tl` output buffer with at least `ASN1_MAX_TL_SIZE` bytes and set `*tl_len` to that buffer's capacity (checked against via `if (*tl_len < tlen) return ASN1_MEM_ERROR;`).
- **Parameters:** `etype` (must pass `ETYPE_OK` and `ETYPE_CLASS(etype)==ASN1_CLASS_UNIVERSAL`, same family of checks, but NOT required to be a string type here); `str` must be non-NULL (`if (str == NULL) return ASN1_VALUE_NOT_VALID;`) — note `str_len==0` is allowed (NULL check only, no length check); `tl`/`tl_len` as above.
- **Return value:** `ASN1_SUCCESS`, `ASN1_VALUE_NOT_VALID` (bad etype/class/NULL str/zero tag_len or len_len), or `ASN1_MEM_ERROR` (tl buffer too small).
- **What to free:** Nothing — `tl` is caller-stack/caller-allocated, no heap allocation performed inside.
- **Behaviour controls:** `asn1_length_der()` internally picks short-form vs. long-form length encoding based on `str_len` magnitude (see lib/coding.c:73 area) — this selects which length-encoding edge cases (single byte vs multi-byte length) get exercised.
- **Combination with the target in one harness:** Used for round-trip/differential fuzzing: harness picks fuzzer-controlled `(etype, bytes)`, calls `asn1_encode_simple_der` to build `tl`, concatenates `tl + bytes` into a synthetic DER buffer, then feeds that buffer into `asn1_decode_simple_ber` (or `asn1_decode_simple_der`) and checks the decoded value equals the original `bytes` — exactly the pattern in tests/Test_strings.c:92-130 (for the plain-DER pair) which could be extended/mirrored for the BER target.
**Evidence:**
> lib/coding.c:202-243 (`asn1_encode_simple_der` definition); line 213-221 for the validation checks; line 232-233 `if (*tl_len < tlen) return ASN1_MEM_ERROR;`.
> tests/Test_strings.c:92-110: real caller builds `tl`, computes `der_len = tl_len + tv[i].str_len`, compares against expected full DER bytes — demonstrating the "tl + str" concatenation contract.
> lib/includes/libtasn1.h.in (ASN1_MAX_TL_SIZE referenced around line 420-422) defines the minimum buffer size callers must provide for `tl`.

### Entries: asn1_der_decoding2 / asn1_der_decoding
**Role:** Top-level, schema-driven structure decoders. `asn1_der_decoding` is a thin wrapper around `asn1_der_decoding2` with `flags=0`. These are the "real" consumer context that invokes the static `_asn1_decode_simple_ber` core internally when decoding an `ASN1_ETYPE_OCTET_STRING` node whose leading tag byte indicates BER constructed encoding.
**Call protocol:**
- **Needs before it (mandatory prerequisite chain):**
  1. Build an ASN.1 definitions tree: `asn1_array2tree(array, &definitions, errDesc)` (from a static `asn1_static_node` table) or `asn1_parser2tree(file, &definitions, errDesc)`.
  2. `asn1_create_element(definitions, "Module.TypeName", &element)` to allocate the target `asn1_node *element` to be filled.
  3. Only then call `asn1_der_decoding(&element, ider, ider_len, errorDescription)` or `asn1_der_decoding2(&element, ider, &max_ider_len, flags, errorDescription)`.
- **Parameters:** `element` must be a non-NULL `asn1_node*` whose pointee was produced by `asn1_create_element` (checked: `if (node == NULL) return ASN1_ELEMENT_NOT_FOUND;` and `if (node->type & CONST_OPTION) ... ASN1_GENERIC_ERROR`); `ider`/`ider_len` (or `*max_ider_len`) is the attacker-controlled DER/BER buffer and its length (signed `int`, so negative/huge values should be avoided by a harness — cap to `INT_MAX` and to actual buffer size); `flags` only meaningful for `asn1_der_decoding2` — `ASN1_DECODE_FLAG_ALLOW_PADDING`, `ASN1_DECODE_FLAG_STRICT_DER` (disables BER decoding, forcing errors on constructed OCTET STRING it would otherwise hand to the target — see `IS_ERR` macro / line 1218 check `(flags & ASN1_DECODE_FLAG_STRICT_DER) || !(ptag[0] & ASN1_CLASS_STRUCTURED)`), `ASN1_DECODE_FLAG_ALLOW_INCORRECT_TIME`.
- **Return value:** `ASN1_SUCCESS`, `ASN1_ELEMENT_NOT_FOUND` (NULL element), `ASN1_TAG_ERROR`/`ASN1_DER_ERROR` (malformed encoding — "*@ELEMENT deleted" per doc comment, meaning on tag/DER errors the library itself destroys the node and the caller's `*element` becomes invalid/NULL — must NOT double-free).
- **What to free:** On success, caller must eventually call `asn1_delete_structure(&element)` (or `asn1_delete_structure2` with flags) to release the filled node tree. On `ASN1_TAG_ERROR`/`ASN1_DER_ERROR`, per the function's own doc comment the element is already deleted internally — caller should NOT call delete again (would double-free); verify via `goto cleanup` path behavior.
- **Behaviour controls affecting the target:** Within the per-field switch for `ASN1_ETYPE_OCTET_STRING` (lib/decoding.c:1209-1266), the decoder inspects `ptag[0] & ASN1_CLASS_STRUCTURED` of the field's leading tag byte in the actual input: if NOT structured (or `ASN1_DECODE_FLAG_STRICT_DER` is set), it decodes as plain DER directly (not via the target); if the byte IS structured and STRICT_DER is not set, it sets `dflags |= DECODE_FLAG_CONSTRUCTED` and calls `_asn1_decode_simple_ber(type_field(p->type), der+counter, ider_len, &ptmp, &vlen, &ber_len, dflags)` — i.e. the harness can steer execution into the target's core by crafting an OCTET STRING field in the schema whose DER/BER bytes have the constructed bit set in the input stream, while `ASN1_DECODE_FLAG_STRICT_DER` (passed to `asn1_der_decoding2`) disables this path entirely.
- **Combination with the target in one harness:** A harness exercising both the direct target AND the schema path would: (a) build a minimal schema via `asn1_array2tree`/`asn1_create_element` containing at least one `OCTET STRING` field, (b) feed fuzzer bytes through `asn1_der_decoding`/`asn1_der_decoding2`, (c) separately/also call `asn1_decode_simple_ber` directly on fuzzer-chosen `(etype, der, der_len)` slices — both exercise the same `_asn1_decode_simple_ber` core but via different entry surfaces, and (d) always balance with `asn1_delete_structure`/`free()` as documented above.
**Evidence:**
> lib/decoding.c:917-920 (`asn1_der_decoding2` signature) and lib/decoding.c:1623-1628 (`asn1_der_decoding` wrapper calling `asn1_der_decoding2(element, ider, &ider_len, 0, errorDescription)`).
> lib/decoding.c:939-940: `if (node == NULL) return ASN1_ELEMENT_NOT_FOUND;`
> lib/decoding.c:1209-1266: the `ASN1_ETYPE_OCTET_STRING` case, including the `_asn1_decode_simple_ber` call at lines 1248-1251 and the STRICT_DER/structured-bit branch at 1218-1240.
> lib/includes/libtasn1.h.in:256-278: `ASN1_DECODE_FLAG_ALLOW_PADDING`, `ASN1_DECODE_FLAG_STRICT_DER`, `ASN1_DECODE_FLAG_ALLOW_INCORRECT_TIME` flag docs, "Used by: asn1_der_decoding2()".
> lib/decoding.c:912-915 (doc comment): "%ASN1_TAG_ERROR or %ASN1_DER_ERROR if the der encoding doesn't match the structure name (*@ELEMENT deleted)." — documents the delete-on-error behavior.
> fuzz/libtasn1_pkix_der_fuzzer.c:541-566: real usage — `asn1_create_element` → `asn1_der_decoding` → (consumer `asn1_der_decoding_startEnd`) → `out: asn1_delete_structure(&dn);` on both success and failure paths (single `goto out` covers all error returns), demonstrating the correct create/decode/delete lifecycle with a single cleanup point.

## Build notes

**Claim:** Fuzzer sources for this project are plain C, compiled/linked as part of the `fuzz/` autotools subdir, against the static library `lib/libtasn1.a` built by `./configure --enable-fuzzing` + `make -C lib`, with the target's header reached simply via `#include "libtasn1.h"` using an include path pointing at `lib/includes` (where `libtasn1.h.in` is instantiated into `libtasn1.h` by configure).

**Evidence:**
> ossfuzz_view/libtasn1-c1/build.sh:18-24:
> ```
> ./bootstrap
> ASAN_OPTIONS=detect_leaks=0 ./configure --enable-static --disable-shared --disable-doc --disable-gcc-warnings --enable-fuzzing
> make clean
> make -j$(nproc) -C lib
> make -j$(nproc) -C src
> make -j$(nproc) -C tests check
> ```
> ossfuzz_view/libtasn1-c1/build.sh:26-28: `cd fuzz; make oss-fuzz; find . -name '*_fuzzer' -exec cp -v '{}' $OUT ';'` — builds all fuzzers listed in `fuzz/Makefile.am`'s `oss-fuzz` target and copies binaries to `$OUT`.
> libtasn1/fuzz/Makefile.am:73-83 (`oss-fuzz:` target):
> ```
> for ccfile in *_fuzzer.c; do
>   fuzzer=$(basename $ccfile .c)
>   $CXX $CXXFLAGS -I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(top_srcdir) \
>     "${fuzzer}.c" -o "${fuzzer}" \
>     ../lib/.libs/libtasn1.a $LIB_FUZZING_ENGINE \
>     -Wl,-Bstatic $XLIBS -Wl,-Bdynamic
> done
> ```
> Note: invoked with `$CXX`/`$CXXFLAGS` (compiled as C++ via the C++ compiler driver even though sources are `.c`, consistent with OSS-Fuzz convention of using the C++ linker for `LIB_FUZZING_ENGINE`); links statically against `../lib/.libs/libtasn1.a` (the library built by `make -C lib`), with no other external library dependency (`XLIBS=""`).
> libtasn1/fuzz/asn1_decode_simple_ber_fuzzer.c:1 ("Gold harness replaced by an empty stub") and line 10 `__asm__("asn1_decode_simple_ber")` — the pipeline's current fuzzer stub for this exact target, confirming it is built via this same `fuzz/Makefile.am` infrastructure (listed as `asn1_decode_simple_ber_fuzzer` in `FUZZERS =` at Makefile.am:38).
> libtasn1/fuzz/Makefile.am:24-25 (AM_CPPFLAGS, used for non-oss-fuzz builds via automake, same include layout as the oss-fuzz target):
> `-I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(srcdir) -I$(top_builddir)/lib/gl -I$(top_srcdir)/lib/gl` — confirms `lib/includes` (holding `libtasn1.h.in` → generated `libtasn1.h`) is the include path used to reach the target's declaration via `#include "libtasn1.h"`.
> libtasn1/fuzz/asn1_decode_simple_der_fuzzer.c:23,26,29-30: `#include <config.h>` then `#include "libtasn1.h"` then `#include "fuzzer.h"` — standard include pattern used by all fuzzers in this directory including the target's.
> libtasn1/lib/includes/libtasn1.h.in: source template from which `lib/includes/libtasn1.h` is generated by `./configure` (confirmed existence via Glob, and declarations for all investigated functions found at lines 320-454 as cited above).