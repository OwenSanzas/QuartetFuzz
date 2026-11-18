## target: idn2_to_ascii_8z

### P2.1 Init Sequence
**Claim:** No library-level init object is required. `idn2_to_ascii_8z` is a thin, stateless wrapper directly forwarding to `idn2_lookup_u8`; the only "init" is preparing a NUL-terminated UTF-8 `input` buffer and an `int flags` value before the call. There is no `idn2_init()`/handle-creation API in this library.
**Evidence:**
> lib/lookup.c:903-907:
> ```
> int
> idn2_to_ascii_8z (const char *input, char **output, int flags)
> {
>   return idn2_lookup_u8 ((const uint8_t *) input, (uint8_t **) output, flags);
> }
> ```
> examples/example-toascii.c:30-40: reads a line into `buf`, NUL-terminates it, then immediately calls `idn2_to_ascii_8z (buf, &p, IDN2_NONTRANSITIONAL)` — no other libidn2 call precedes it.

### P2.2 Parameter Construction
**Claim:** `input` is `const char *`: a NUL-terminated UTF-8 byte string (may be `NULL`, handled specially downstream). `output` is `char **`: an out-parameter pointer that will receive a newly `malloc`-ed NUL-terminated ASCII string on success; may be `NULL` if the caller only wants validation ("probe" mode, no allocation). `flags` is an `int` built by OR-ing `idn2_flags` enum values (`IDN2_NFC_INPUT=1, IDN2_ALABEL_ROUNDTRIP=2, IDN2_TRANSITIONAL=4, IDN2_NONTRANSITIONAL=8, IDN2_ALLOW_UNASSIGNED=16, IDN2_USE_STD3_ASCII_RULES=32, IDN2_NO_TR46=64, IDN2_NO_ALABEL_ROUNDTRIP=128`); `0` selects default behaviour. `IDN2_TRANSITIONAL` and `IDN2_NONTRANSITIONAL` are mutually exclusive, as are `IDN2_ALABEL_ROUNDTRIP`/`IDN2_NO_ALABEL_ROUNDTRIP`, and combining `(IDN2_TRANSITIONAL|IDN2_NONTRANSITIONAL)` with `IDN2_NO_TR46` is also invalid.
**Evidence:**
> lib/idn2.h.in:290-291: `extern _IDN2_API int idn2_to_ascii_8z (const char *input, char **output, int flags);`
> lib/idn2.h.in:179-189: enum `idn2_flags` definition with the bit values above.
> lib/lookup.c:104-122 `set_default_flags()`: returns `IDN2_INVALID_FLAGS` for the mutually-exclusive combinations described above, and defaults to `IDN2_NONTRANSITIONAL` when neither `IDN2_NO_TR46` nor `IDN2_TRANSITIONAL` is set (line 118-119).
> lib/lookup.c:537-542: `if (src == NULL) { if (lookupname) *lookupname = NULL; return IDN2_OK; }` — NULL input is legal and short-circuits.
> lib/lookup.c:604-613: `lookupname`/`output` may be NULL; only allocated when non-NULL.

### P2.3 Object Lifecycle
**Claim:** There is no persistent object/handle — each call is a one-shot "create output → use it → free it" cycle. Caller allocates/owns `input`; library allocates `*output` (if requested) with `malloc`; caller must `free()` (or `idn2_free()`) `*output` after use. No "configure" or "destroy" API exists beyond that.
**Evidence:**
> lib/lookup.c:606-612 (`idn2_lookup_u8`, which `idn2_to_ascii_8z` calls): `uint8_t *tmp = (uint8_t *) malloc (lookupnamelen + 1); ... *lookupname = tmp;` — output buffer is heap-allocated by the library.
> examples/example-toascii.c:40,49: `rc = idn2_to_ascii_8z (buf, &p, IDN2_NONTRANSITIONAL); ... free (p); /* or idn2_free() */` — create via call, use `p`, then free.

### P2.4 Return Value Handling
**Claim:** Return value is an `int` from the `idn2_rc` enum. `IDN2_OK` (0) means success and, if `output != NULL`, `*output` now holds a valid allocated string that must be freed. Any non-zero / negative code (e.g. `IDN2_MALLOC`, `IDN2_ENCODING_ERROR`, `IDN2_TOO_BIG_DOMAIN`, `IDN2_TOO_BIG_LABEL`, `IDN2_DISALLOWED`, `IDN2_BIDI`, `IDN2_NOT_NFC`, `IDN2_ALABEL_ROUNDTRIP_FAILED`, `IDN2_INVALID_FLAGS`, ...) means failure; on failure `*output` is NOT set/allocated by `idn2_to_ascii_8z`/`idn2_lookup_u8` (no partial buffer to free). Use `idn2_strerror(rc)` / `idn2_strerror_name(rc)` to get a human string.
**Evidence:**
> lib/idn2.h.in:209-279: `idn2_rc` enum with `IDN2_OK=0` and the negative error codes listed.
> lib/lookup.c:569-573,578-580,587-591,608-609: every error path `return`s immediately without writing to `*lookupname`.
> examples/example-toascii.c:40-45: `rc = idn2_to_ascii_8z(...); if (rc != IDNA_SUCCESS) { printf(...); return EXIT_FAILURE; }` before touching `p`.
> lib/idn2.h.in:318-321: `idn2_strerror`/`idn2_strerror_name` declared for mapping `rc` to text.

### P2.5 Cleanup Sequence
**Claim:** On success, the caller must free exactly one buffer: `*output`, via `free()` or `idn2_free()` (equivalent, `idn2_free` is just a `free()` wrapper for cross-DLL-heap cases). On error, nothing was allocated by `idn2_to_ascii_8z` itself for the caller to free (internal temporaries from `_tr46`/`label` are freed internally before any error return). No other libidn2 resource needs releasing since no persistent handle exists.
**Evidence:**
> lib/free.c:45-48: `void idn2_free (void *ptr) { free (ptr); }`
> lib/lookup.c:570-573 (inside `idn2_lookup_u8`): `free (src_allocated); return rc;` — internal TR46 buffer freed on every label-processing error path.
> tests/test-lookup.c:934-935: `if (rc == IDN2_OK) idn2_free (out);` — only frees on success.

### P2.6 API Existence
**Claim:** All functions named below were found as actual declarations/definitions in the current libidn2 source tree (not fabricated).
**Evidence:**
> `idn2_to_ascii_8z`: lib/idn2.h.in:290, defined lib/lookup.c:903-907.
> `idn2_lookup_u8`: lib/idn2.h.in:193-194, defined lib/lookup.c:529-616.
> `idn2_to_ascii_4z`: lib/idn2.h.in:288-289, defined lib/lookup.c:855-881.
> `idn2_to_ascii_lz`: lib/idn2.h.in:292-293, defined lib/lookup.c:930-934.
> `idn2_lookup_ul`: lib/idn2.h.in:202-203, defined lib/lookup.c:654-680.
> `idn2_to_unicode_8z8z`: lib/idn2.h.in:302-303, defined lib/decode.c:303-332.
> `idn2_to_unicode_8z4z`: lib/idn2.h.in:295-296, defined lib/decode.c:61-... (used by 8z8z).
> `idn2_register_u8`: lib/idn2.h.in:196-198, defined lib/register.c:71-204.
> `idn2_register_ul`: lib/idn2.h.in:205-207, defined lib/register.c (declared ~206-221).
> `idn2_free`: lib/idn2.h.in:326, defined lib/free.c:45-48.
> `idn2_punycode_encode`/`idn2_punycode_decode`: lib/idn2.h.in:309-316, used in lib/lookup.c:150-151,212 and lib/register.c:105-106,187.
> `idn2_strerror`/`idn2_strerror_name`: lib/idn2.h.in:318-321.
> `idn2_check_version`: lib/idn2.h.in:323-324, used in fuzz/libidn2_to_unicode_8z8z_fuzzer.c:49,96.
> Internal core helpers `label`, `_tr46`, `_idn2_label_test`, `_idn2_ascii_p`, `_idn2_u8_to_u32_nfc`: lib/lookup.c:124-230 (`label`), declared lib/idna.h:61-67 (`_idn2_u8_to_u32_nfc`, `_idn2_ascii_p`, `_idn2_label_test`); `_tr46` referenced lib/lookup.c:548-557 (defined elsewhere in lookup.c, not shown but called at that line).

### P2.7 Co-call Constraints
**Claim:** The only mandatory pairing is "successful call → free(*output)". There is no lock/unlock, begin/commit, or open/close pairing because the API is stateless per-call. `idn2_to_ascii_8z`'s mutually exclusive flag pairs (`IDN2_TRANSITIONAL` vs `IDN2_NONTRANSITIONAL`; `IDN2_ALABEL_ROUNDTRIP` vs `IDN2_NO_ALABEL_ROUNDTRIP`) act as a soft "co-call constraint" in that combining both members of a pair yields `IDN2_INVALID_FLAGS` rather than undefined behaviour.
**Evidence:**
> lib/lookup.c:107-116 (`set_default_flags`): explicit checks returning `IDN2_INVALID_FLAGS` for the conflicting flag combinations.
> lib/free.c:45-48 and tests/test-lookup.c:934-935,963-964,993- (pattern: `if (rc==IDN2_OK) idn2_free(out);`).

### P2.8 Prerequisite State
**Claim:** No external context/connection/config object must exist first. The function depends only on process-global static Unicode tables compiled into the library (TR46 mapping table `tr46map.h`, IDNA2008 property tables used by `_idn2_label_test`), which require no runtime initialization call by the user. Locale-dependent variants (`idn2_to_ascii_lz`/`idn2_lookup_ul`) additionally depend on the process locale (`locale_charset()`), but `idn2_to_ascii_8z` itself (pure UTF-8 path) has no such dependency.
**Evidence:**
> lib/lookup.c:42-43: `#include "idna.h" ... #include "tr46map.h"` — static, compiled-in tables, no init call.
> lib/lookup.c:662: `const char *encoding = locale_charset ();` only in `idn2_lookup_ul` (used by `idn2_to_ascii_lz`), not in the `idn2_to_ascii_8z`/`idn2_lookup_u8` path.

### P2.9 Result Usage
**Claim:** The `*output` ASCII (xn--) string produced by `idn2_to_ascii_8z`/`idn2_lookup_u8` is typically: (a) printed/used directly as a DNS lookup name by the caller (it is not re-fed into another libidn2 "consumer" API in this library's own code — consumption happens in DNS resolver code outside libidn2); (b) in the library's own self-tests, immediately compared against an expected string and then freed; (c) round-tripped through `idn2_to_unicode_8z8z`/`idn2_to_unicode_8z4z`/punycode-decode machinery to validate correctness (its own internal `label()` function does this automatically when `IDN2_ALABEL_ROUNDTRIP` is active and the label looks like "xn--...", calling `idn2_punycode_decode` on the just-encoded buffer). The returned buffer is otherwise just a NUL-terminated C string the caller owns and frees.
**Evidence:**
> examples/example-toascii.c:40-49: `rc = idn2_to_ascii_8z (buf, &p, IDN2_NONTRANSITIONAL); ... printf ("ACE label (length %ld): '%s'\n", (long)strlen(p), p); free(p);` — printed then freed, no further libidn2 call.
> tests/test-lookup.c:913,926,935: `rc = idn2_lookup_u8(...,&out,...); ... strcmp((char*)out, idna[i].out); ... idn2_free(out);` — compared to expected, then freed.
> lib/lookup.c:219-226 (inside `label()`, used by both `idn2_lookup_u8` and `idn2_register_u8`): after `idn2_punycode_encode` produces `dst`, if `check_roundtrip`, compares `src_org`/`dst` (the just-produced A-label is validated by having been decoded via `idn2_punycode_decode` earlier at line 150-151) — i.e., the encode output is round-trip-checked against the decode of the original A-label input.
> lib/decode.c:304-332 `idn2_to_unicode_8z8z`: inverse operation; a harness exercising both target and this function can feed the target's `*output` into `idn2_to_unicode_8z8z`'s `input` to validate round-trip, though no in-tree caller currently chains them directly.

### P2.10 Behaviour Controls
**Claim:** `flags` selects divergent code paths as follows (verified in `idn2_lookup_u8`/`label`/`_tr46`, all reached from the target):
- `IDN2_NO_TR46` (64): skips the `_tr46()` mapping/validation stage entirely — labels are tested with raw `_idn2_label_test` only (lib/lookup.c:548-557, "`if (!(flags & IDN2_NO_TR46)) { rc = _tr46(...); }`").
- `IDN2_TRANSITIONAL` (4) vs `IDN2_NONTRANSITIONAL` (8) (mutually exclusive, default is `IDN2_NONTRANSITIONAL`, see P2.2): select `TR46_TRANSITIONAL_CHECK` vs `TR46_NONTRANSITIONAL_CHECK` test flags and different deviation-character mapping (lib/lookup.c:186-204, label(): `if (!(flags & IDN2_TRANSITIONAL)) { rc = _idn2_label_test(... (flags & IDN2_NONTRANSITIONAL) ? TEST_NONTRANSITIONAL : 0 ...) }`).
- `IDN2_USE_STD3_ASCII_RULES` (32): toggles `TEST_ALLOW_STD3_DISALLOWED` bit passed to `_idn2_label_test`, and changes how `_tr46`'s DISALLOWED_STD3_* map classes are treated (lib/lookup.c:379-389, 444-445,456-457, 196-199).
- `IDN2_NFC_INPUT` (1): forces NFC normalization of the raw label before testing, via `_idn2_u8_to_u32_nfc(..., flags & IDN2_NFC_INPUT)` (lib/lookup.c:182).
- `IDN2_ALABEL_ROUNDTRIP` (2, default)/`IDN2_NO_ALABEL_ROUNDTRIP` (128): controls whether a label starting with "xn--" is decoded and the decode→re-encode result is compared back to the original (`check_roundtrip` logic) (lib/lookup.c:137-168, 219-226).
- `IDN2_ALLOW_UNASSIGNED` (16): documented as a libidn-compatibility no-op flag (lib/idn2.h.in:171, "Libidn compatibility flag, unused").
- Input-driven branching: whether a label is pure ASCII (`_idn2_ascii_p`, lib/lookup.c:137) selects a copy-through fast path vs full Unicode pipeline; whether a label begins with case-insensitive "xn--" selects punycode-decode+roundtrip path (lib/lookup.c:139-140, 413-415 in `_tr46`); label/domain length vs `IDN2_LABEL_MAX_LENGTH`(63)/`IDN2_DOMAIN_MAX_LENGTH`(255) selects `IDN2_TOO_BIG_LABEL`/`IDN2_TOO_BIG_DOMAIN` errors (lib/lookup.c:171-174, 575-580, 587-591).
**Evidence:** line references above (all in lib/lookup.c).

### P2.11 Input Expectations
**Claim:** `input` must be a NUL-terminated byte string that the library treats as UTF-8 (`idn2_lookup_u8`'s doc explicitly says "input string must be encoded in UTF-8 and be in Unicode NFC form", with `IDN2_NFC_INPUT` offered to normalize non-NFC input). `input == NULL` is explicitly supported and short-circuits to `IDN2_OK` with `*output = NULL` (if `output` non-NULL). The string is split on ASCII '.' into labels; each label independently must not exceed `IDN2_LABEL_MAX_LENGTH` (63) output bytes, and the whole assembled domain must not exceed `IDN2_DOMAIN_MAX_LENGTH` (255) bytes, else `IDN2_TOO_BIG_LABEL`/`IDN2_TOO_BIG_DOMAIN` is returned. No other explicit size cap on the raw input length is enforced before processing (bounded indirectly by `IDN2_LABEL_MAX_LENGTH`/`IDN2_DOMAIN_MAX_LENGTH` checks and internal fixed-size stack buffers such as `uint8_t _lookupname[IDN2_DOMAIN_MAX_LENGTH+1]` and `uint32_t label_u32[IDN2_LABEL_MAX_LENGTH]`).
**Evidence:**
> lib/lookup.c:489,493-495 (doc comment on `idn2_lookup_u8`, the function the target forwards to): "@src: input zero-terminated UTF-8 string in Unicode NFC normalized form. ... Note that the input string must be encoded in UTF-8 and be in Unicode NFC form."
> lib/lookup.c:537-542: NULL-input short-circuit.
> lib/lookup.c:561-564: `const uint8_t *end = (uint8_t *) strchrnul ((const char *) src, '.'); size_t labellen = end - src;` — label splitting on '.'.
> lib/lookup.c:533: `uint8_t _lookupname[IDN2_DOMAIN_MAX_LENGTH + 1];` fixed-size assembly buffer.
> lib/lookup.c:171-174, 575-580, 587-591: label/domain length checks returning `IDN2_TOO_BIG_LABEL`/`IDN2_TOO_BIG_DOMAIN`.

## Other Logic-Group entries: call protocol and combination with the target

### idn2_lookup_u8 (core driver the target forwards to)
**Signature:** `int idn2_lookup_u8 (const uint8_t *src, uint8_t **lookupname, int flags);` — lib/idn2.h.in:193-194, defined lib/lookup.c:529-616.
**Protocol:** Identical to the target except the parameter type is `const uint8_t *` instead of `const char *` (the target just casts and forwards, lib/lookup.c:906). No init needed. `src` may be `NULL` → returns `IDN2_OK`, `*lookupname = NULL` if non-NULL (lib/lookup.c:537-542). `lookupname` may be `NULL` to "probe" (validate without allocating) — this is the size-probing path mentioned in the task description (lib/lookup.c:604-613: allocation only happens `if (lookupname)`). `flags`: same `idn2_flags` bitmask as the target, validated by `set_default_flags()` (lib/lookup.c:104-122).
**Return/cleanup:** Same as target — `IDN2_OK` + optional allocated `*lookupname` to `free()`/`idn2_free()`; any other `idn2_rc` value on error, nothing allocated.
**Combination with target:** `idn2_to_ascii_8z` **is** `idn2_lookup_u8` (direct 1:1 forward, lib/lookup.c:903-907). A harness calling both only duplicates coverage of the UTF-8 encoding entry; calling `idn2_lookup_u8` with `lookupname == NULL` additionally reaches the size-probing branch (`if (lookupname) {...}` skipped) that `idn2_to_ascii_8z` callers rarely use directly (since `idn2_to_ascii_8z`'s `output` is normally non-NULL), so it is useful to call `idn2_lookup_u8` once with `lookupname = NULL` to cover that branch.
**Evidence:** lib/lookup.c:529-616; tests/test-lookup.c:1002-1022 exercises `idn2_lookup_u8(NULL,NULL,0)` and `idn2_lookup_u8(NULL,&out,0)` specifically to cover these NULL-combinations.

### idn2_to_ascii_4z (UCS-4 input variant)
**Signature:** `int idn2_to_ascii_4z (const uint32_t *input, char **output, int flags);` — lib/idn2.h.in:288-289, defined lib/lookup.c:855-881.
**Protocol:** `input` is a zero-terminated UCS-4 (`uint32_t`) array (one code point per element, 0-terminated), not raw bytes — a harness must convert fuzz bytes into a `uint32_t[]` array explicitly zero-terminated (e.g. by casting/aligning 4-byte groups and appending a `0`). `input == NULL` → `IDN2_OK`, `*output=NULL` (lib/lookup.c:862-867). Internally: `u32_to_u8()` converts the UCS-4 array to UTF-8 (lib/lookup.c:869), then calls `idn2_lookup_u8()` (lib/lookup.c:877) — i.e. it funnels into the exact same core as the target after an encoding conversion. `output` out-param behaves like the target's.
**Return/cleanup:** `IDN2_OK`/error exactly like the target; on success free `*output`. Internal `input_u8` temporary is freed before returning (lib/lookup.c:878).
**Combination with target:** Different *input encoding* path into the same `idn2_lookup_u8` core; a harness can derive the UCS-4 array from the same fuzz bytes used for the target's UTF-8 string (e.g., by decoding the UTF-8 bytes to codepoints, or by reinterpreting raw bytes as `uint32_t` code points) to hit `u32_to_u8` conversion-failure paths (`IDN2_ENCODING_ERROR`/`IDN2_MALLOC`, lib/lookup.c:870-875) that `idn2_to_ascii_8z` cannot reach directly.
**Evidence:** lib/lookup.c:855-881.

### idn2_to_ascii_lz (locale-encoded input variant)
**Signature:** `int idn2_to_ascii_lz (const char *input, char **output, int flags);` — lib/idn2.h.in:292-293, defined lib/lookup.c:930-934, which calls `idn2_lookup_ul` (lib/lookup.c:933, 654-680).
**Protocol:** `input` is a NUL-terminated string in the **process locale's encoding** (not necessarily UTF-8). Before any IDNA processing, `idn2_lookup_ul` calls `locale_charset()` and `u8_strconv_from_encoding(src, encoding, iconveh_error)` to transcode to UTF-8 (lib/lookup.c:662-671); on iconv failure returns `IDN2_ICONV_FAIL` (or `IDN2_MALLOC` on OOM) *before* ever reaching the TR46/IDNA core. After successful transcoding it force-adds `IDN2_NFC_INPUT` and calls `idn2_lookup_u8(utf8src, lookupname, flags|IDN2_NFC_INPUT)` (lib/lookup.c:674-675) — same core as the target. `src == NULL` is allowed (`utf8src` stays NULL, forwarded to `idn2_lookup_u8(NULL,...)` which is itself NULL-safe).
**Return/cleanup:** Same `idn2_rc` codes as target, plus `IDN2_ICONV_FAIL` specific to this variant. `utf8src` intermediate buffer is always freed (lib/lookup.c:677) regardless of outcome. On `IDN2_OK`, free `*output`.
**Combination with target:** Exercises the locale/iconv conversion error paths in addition to the IDNA core; since fuzzing normally runs in the "C"/POSIX locale, `locale_charset()` typically returns ASCII/UTF-8-compatible encoding, so most inputs pass through to the same core as the target — still useful to include to cover the iconv call path itself (and `IDN2_ICONV_FAIL`) for bytes that are invalid in the current locale encoding.
**Evidence:** lib/lookup.c:654-680, 930-934; tests/test-locale.c:37-45 calls `idn2_lookup_ul("abc", NULL, 0)` and checks for `IDN2_ICONV_FAIL` as an acceptable outcome depending on test-environment locale.

### idn2_to_unicode_8z8z (inverse/decode direction)
**Signature:** `int idn2_to_unicode_8z8z (const char *input, char **output, int flags);` — lib/idn2.h.in:302-303, defined lib/decode.c:304-332.
**Protocol:** `input` is a NUL-terminated UTF-8 string, possibly containing ACE ("xn--") labels, i.e. it is meant to consume what `idn2_to_ascii_8z` produces. `flags` is currently documented as unused by this function ("@flags: Currently unused.", lib/decode.c:286) — it is forwarded to `idn2_to_unicode_8z4z` but that function also marks it unused (`(void) flags;`, lib/decode.c:64). Internally calls `idn2_to_unicode_8z4z(input, &output_u32, flags)` (lib/decode.c:311) which splits on '.' (lib/decode.c:80-116), detects "xn--"/"XN--" (case-insensitive) prefix per label and calls `idn2_punycode_decode()` on the suffix (lib/decode.c:88-96), otherwise UTF-8→UTF-32 decodes the label directly (lib/decode.c:103-113); then converts the resulting UTF-32 domain back to UTF-8 via `u32_to_u8` (lib/decode.c:315-316). `input == NULL` → `IDN2_OK`, no allocation (lib/decode.c:311-313, early return skips encoding step). `output == NULL` allowed (just skips storing, frees the temporary, lib/decode.c:326-329).
**Return/cleanup:** `IDN2_OK`/`IDN2_TOO_BIG_DOMAIN`/`IDN2_TOO_BIG_LABEL`/`IDN2_ENCODING_ERROR`/`IDN2_MALLOC` (plus whatever `idn2_punycode_decode` returns, e.g. `IDN2_PUNYCODE_BAD_INPUT`). On success with `output != NULL`, caller must `free()`/`idn2_free()` `*output`.
**Combination with target:** Natural round-trip partner — feed the target's `*output` (an ASCII/ACE string) into `idn2_to_unicode_8z8z`'s `input` to validate `ToASCII` then `ToUnicode` consistency, and independently feed raw fuzz bytes directly into `idn2_to_unicode_8z8z` to exercise `idn2_punycode_decode` on attacker-controlled "xn--" suffixes without going through the encode direction at all. The project's own fuzzer (`fuzz/libidn2_to_unicode_8z8z_fuzzer.c`) does exactly the latter, calling `idn2_to_unicode_lzlz`/`idn2_to_unicode_4z4z`/`idn2_to_unicode_44i`/`idn2_to_unicode_8z4z` family functions on raw fuzz bytes (lines 61-118), never chaining from `idn2_to_ascii_8z`'s output — a harness combining both entries would go further than the in-tree fuzzers.
**Evidence:** lib/decode.c:61-120 (`idn2_to_unicode_8z4z`), 283-332 (`idn2_to_unicode_8z8z`); fuzz/libidn2_to_unicode_8z8z_fuzzer.c:61-118.

### idn2_register_u8 (registration/consumer sharing label core)
**Signature:** `int idn2_register_u8 (const uint8_t *ulabel, const uint8_t *alabel, uint8_t **insertname, int flags);` — lib/idn2.h.in:196-198, defined lib/register.c:71-204.
**Protocol:** Operates on a single **label** (not a full dotted domain) per RFC 5891 §4, unlike the target which handles a full domain. `ulabel` (UTF-8 NFC Unicode form) and/or `alabel` (ACE "xn--" form) may each be `NULL`, but not both (`ulabel==NULL && alabel==NULL` → `IDN2_OK`, `*insertname=NULL`, lib/register.c:77-82). If `alabel` given: checked for `xn--` prefix and ASCII-only (lib/register.c:93-103), length ≤ `IDN2_LABEL_MAX_LENGTH` else `IDN2_TOO_BIG_LABEL`; punycode-decoded (lib/register.c:105-106); if `ulabel` also given, decoded result is `strcmp`-compared against `ulabel` → `IDN2_UALABEL_MISMATCH` on mismatch (lib/register.c:115-119); then it recursively calls **`idn2_register_u8(u8, NULL, &tmp, 0)`** on the decoded ulabel (lib/register.c:121) to re-derive the alabel and compares it back to the input alabel (round-trip check, lib/register.c:125-128) — a second, internal call to the same function. If only `ulabel` given: runs `_idn2_u8_to_u32_nfc` + `_idn2_label_test` (TEST_NFC|TEST_DISALLOWED|TEST_UNASSIGNED|TEST_2HYPHEN|TEST_HYPHEN_STARTEND|TEST_LEADING_COMBINING|TEST_CONTEXTJ_RULE|TEST_CONTEXTO_RULE|TEST_BIDI, lib/register.c:162-174) then `idn2_punycode_encode` (lib/register.c:187) producing the alabel. `flags`: only `IDN2_NFC_INPUT` is meaningfully consulted here (lib/register.c:163); the TR46/transitional flags used by the target are **not** applied in this function (it is pure IDNA2008 label testing, no TR46 mapping stage).
**Return/cleanup:** `IDN2_OK` / `IDN2_TOO_BIG_LABEL` / `IDN2_INVALID_ALABEL` / `IDN2_UALABEL_MISMATCH` / `IDN2_ENCODING_ERROR` / `IDN2_MALLOC` / label-test errors (`IDN2_DISALLOWED`, `IDN2_BIDI`, etc). On `IDN2_OK` with `insertname != NULL`, `*insertname` is a `strdup`-allocated string the caller must `free()`/`idn2_free()` (lib/register.c:130-137, 152-158, 194-199).
**Combination with target:** Shares the identical per-label machinery (`_idn2_u8_to_u32_nfc`, `_idn2_label_test`, `idn2_punycode_encode`/`decode`, `_idn2_ascii_p`) that the target's internal `label()` function uses, but operates outside the TR46 mapping stage and on a single label instead of a dotted domain — a harness including it alongside the target broadens coverage of the IDNA2008 label-test/punycode core to the registration code path (RFC 5891 §4) in addition to the lookup path (§5) the target exercises. The project's own `fuzz/libidn2_register_fuzzer.c` drives this through `idn2_register_ul` (locale variant) rather than `idn2_register_u8` directly (lines 47-70), feeding raw fuzz bytes as `ulabel` and as `"xn--"+bytes` as `alabel`.
**Evidence:** lib/register.c:71-204; fuzz/libidn2_register_fuzzer.c:27-75.

## Build notes

**Claim:** Fuzzers for this project are plain C (compiled with `$CXX`/`$CXXFLAGS` per the `oss-fuzz` Makefile target, but the existing harness sources themselves are `.c` files, `extern "C"`-compatible, included via `#include "idn2.h"` with `-I$(top_srcdir)/lib`), statically linked against `../lib/.libs/libidn2.a` plus the fuzzing engine. `build.sh` runs `./bootstrap`, `./configure --enable-static --disable-shared ...`, `make check`, then `cd fuzz && make oss-fuzz`, and copies every `*_fuzzer` binary plus `.dict`/`.options`/seed-corpus zips to `$OUT`.
**Evidence:**
> build.sh:18-33 (full content): `./bootstrap`; `ASAN_OPTIONS=detect_leaks=0 ./configure --enable-static --disable-shared --disable-doc --disable-gcc-warnings`; `make clean`; `make -j$(nproc) check VERBOSE=t`; `cd fuzz`; `make oss-fuzz`; `find . -name '*_fuzzer' -exec cp -v '{}' $OUT ';'` (+ dict/options/seed-corpus steps).
> fuzz/Makefile.am:49-58 (`oss-fuzz` target): 
> ```
> oss-fuzz:
>     if test "$$OUT" != ""; then \
>     for ccfile in *_fuzzer.c; do \
>     fuzzer=$$(basename $$ccfile .c); \
>     $$CXX $$CXXFLAGS -I$(top_srcdir)/lib -I$(top_srcdir) \
>     "$${fuzzer}.c" -o "$${fuzzer}" \
>     ../lib/.libs/libidn2.a $${LIB_FUZZING_ENGINE} -Wl,-Bstatic \
>     -Wl,-Bdynamic; \
>     done; \
>     fi
> ```
>   i.e. header include path is `-I$(top_srcdir)/lib` (where the generated `lib/idn2.h` — built from `lib/idn2.h.in` by configure — lives) and `-I$(top_srcdir)`; link is against the static archive `../lib/.libs/libidn2.a` plus `$LIB_FUZZING_ENGINE`.
> fuzz/Makefile.am:22,25 (the `make check`/autotools build of the same sources, for local/non-ossfuzz builds): `AM_CPPFLAGS = -I$(top_srcdir)/lib -I../lib -I$(top_srcdir)/gl -I../gl ...`; `LDADD = ../lib/libidn2.la ../gl/libgnu.la $(LTLIBUNISTRING)` — also shows gnulib (`gl`) and libunistring are link dependencies of the real build (used transitively by `lib/lookup.c`'s `#include <uninorm.h>`, `<unistr.h>`, `<uniconv.h>` from libunistring, see lib/lookup.c:37-40).
> examples/example-toascii.c:9,14: `#include <idn2.h>` and recommended compile line `cc -o example-toascii example-toascii.c $(pkg-config --cflags --libs libidn2)` — confirms the installed public header is `idn2.h` (generated from `lib/idn2.h.in`) and the installed library is `libidn2`.
> Note: the current in-tree `fuzz/libidn2_to_ascii_8z_fuzzer.c` (lib/.../fuzz/libidn2_to_ascii_8z_fuzzer.c:1-16) is a stub replaced for this task ("Gold harness replaced by an empty stub for baseline generation") that only keeps the `idn2_to_ascii_8z` symbol linked via an `__asm__` alias trick and does not actually call it — the downstream harness generator must write a real call, following the patterns documented above (e.g. examples/example-toascii.c and tests/test-lookup.c) rather than copy this stub.