## target: idn2_to_unicode_8z8z

### P2.1 Init Sequence
**Claim:** No library-wide init/context object is required. `idn2_to_unicode_8z8z` can be called directly with a NUL-terminated UTF-8 C string; there is no `idn2_context`/handle to create first. The only "prerequisite" is that `input` be a valid pointer or NULL (NULL short-circuits to `IDN2_OK`).
**Evidence:**
> libidn2/lib/decode.c:304-313:
> ```
> int idn2_to_unicode_8z8z (const char *input, char **output, int flags)
> {
>   ...
>   rc = idn2_to_unicode_8z4z (input, &output_u32, flags);
>   if (rc != IDN2_OK || !input)
>     return rc;
> ```
> libidn2/lib/decode.c:68-73 (idn2_to_unicode_8z4z, called first): `if (!input) { if (output) *output = NULL; return IDN2_OK; }` — shows NULL input is an explicit, documented fast path, not a precondition violation.
> libidn2/examples/example-tounicode.c:40: `rc = idn2_to_unicode_8z8z (buf, &p, 0);` called with no other libidn2 setup calls beforehand.

### P2.2 Parameter Construction
**Claim:** `input`: `const char *`, may be NULL or a NUL-terminated byte string (UTF-8 expected, but the function itself does no UTF-8 validation beyond what the per-label path requires — see P2.11). `output`: `char **`, may be NULL (lookup-only / no allocation) or a valid pointer to receive a newly `malloc`'d NUL-terminated UTF-8 string that the caller must free. `flags`: `int`, documented as "Currently unused" for this ToUnicode path — any value is accepted and ignored.
**Evidence:**
> libidn2/lib/decode.c:283-286 (doc comment): "`@input`: Input zero-terminated UTF-8 string. `@output`: Newly allocated UTF-8 output string. `@flags`: Currently unused." / "`@output` may be NULL to test lookup of `@input` without allocating memory."
> libidn2/lib/decode.c:326-329: `if (output) *output = (char *) output_u8; else free (output_u8);` — output==NULL is valid and just discards the buffer after it was computed.
> libidn2/lib/decode.c:62-64 (idn2_to_unicode_8z4z, same convention): `(void) flags;` — flags parameter explicitly ignored in the core.
> tests/test-tounicode.c:501-504: calls with NULL input, NULL output, and both, confirming these combinations are supported/tested.

### P2.3 Object Lifecycle
**Claim:** There is no persistent object — lifecycle is a single call: allocate nothing beforehand → call `idn2_to_unicode_8z8z(input, &output, flags)` → use `*output` → `free(*output)` (or `idn2_free`). No "destroy" call pairs with the target other than freeing the output buffer.
**Evidence:**
> libidn2/examples/example-tounicode.c:40,49: `rc = idn2_to_unicode_8z8z (buf, &p, 0); ... free (p); /* or idn2_free() */`
> libidn2/lib/decode.c:326-329: allocation happens inside the call via `u32_to_u8`; ownership transfers to caller through `*output`.
> libidn2/lib/idn2.h.in:326: `extern _IDN2_API void idn2_free (void *ptr);` — dedicated free wrapper provided as an alternative to `free()`.

### P2.4 Return Value Handling
**Claim:** Return is an `int` from the `idn2_rc` enum. `IDN2_OK` (0) means success and, if `output!=NULL`, `*output` now holds an allocated buffer. Any nonzero value is an error and `*output` is left untouched (not allocated) — the caller must not free it in that case. Errors propagate unchanged from `idn2_to_unicode_8z4z` (label split / punycode decode) and from the final `u32_to_u8` UTF-32→UTF-8 conversion.
**Evidence:**
> libidn2/lib/decode.c:294-299 (doc): "Returns: %IDN2_OK ... %IDN2_TOO_BIG_DOMAIN ... %IDN2_TOO_BIG_LABEL ... %IDN2_ENCODING_ERROR ... %IDN2_MALLOC".
> libidn2/lib/decode.c:311-313: `rc = idn2_to_unicode_8z4z (...); if (rc != IDN2_OK || !input) return rc;` — early return on any core error, output untouched.
> libidn2/lib/decode.c:319-324: `if (!output_u8) { if (errno == ENOMEM) return IDN2_MALLOC; return IDN2_ENCODING_ERROR; }`
> libidn2/lib/idn2.h.in:250-260: enum idn2_rc values `IDN2_OK=0, IDN2_MALLOC=-100, ..., IDN2_ENCODING_ERROR=-200, ..., IDN2_TOO_BIG_DOMAIN=-205, IDN2_TOO_BIG_LABEL=-206`.
> libidn2/lib/idn2.h.in:318-320: `extern _IDN2_API const char *idn2_strerror (int rc)` — used by callers (e.g. example-tounicode.c:43) to render the error.

### P2.5 Cleanup Sequence
**Claim:** On success with non-NULL `output`, the caller frees exactly one buffer: `*output` (via `free()` or `idn2_free()`). On success with `output==NULL`, the function itself frees the internal `output_u8` buffer (decode.c:329) — nothing for the caller to do. On error, nothing is allocated for the caller to free (the function frees its own intermediate `output_u32` at decode.c:317 before returning, and frees `output_u8` internally in the `!output` case). There is no other handle/state to tear down.
**Evidence:**
> libidn2/lib/decode.c:315-317: `output_u8 = u32_to_u8 (output_u32, ..., &length); free (output_u32);` — the intermediate UTF-32 buffer is always freed by the target itself, never exposed to the caller.
> libidn2/lib/decode.c:326-329: `if (output) *output = (char *) output_u8; else free (output_u8);`
> libidn2/examples/example-tounicode.c:49: `free (p);` after checking `rc == IDNA_SUCCESS`.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current source tree (verified by direct `Grep`/`Read`).
**Evidence:**
> libidn2/lib/decode.c:304 `idn2_to_unicode_8z8z`; :62 `idn2_to_unicode_8z4z`; :181 `idn2_to_unicode_4z4z`; :244 `idn2_to_unicode_44i`; :357 `idn2_to_unicode_8zlz`; :419 `idn2_to_unicode_lzlz`.
> libidn2/lib/lookup.c:904 `idn2_to_ascii_8z`; :530 `idn2_lookup_u8`.
> libidn2/lib/punycode.c:296 `idn2_punycode_decode`; :102 `decode_digit` (static); :120 `adapt` (static).
> libidn2/lib/idn2.h.in:318 `idn2_strerror`; :326 `idn2_free`.
> libidn2/lib/free.c:46 `idn2_free`.

### P2.7 Co-call Constraints
**Claim:** No lock/unlock or begin/commit pairing exists for this function. The only pairing is allocate-on-success / free-by-caller (or NULL-output meaning "don't allocate, I'll just check the return code"). `idn2_to_unicode_8z8z` and `idn2_to_ascii_8z` (ToASCII, via `idn2_lookup_u8`) are an inverse pair useful for round-trip fuzzing (decode(encode(x)) or encode(decode(x))), but calling one is never required before the other.
**Evidence:**
> libidn2/lib/lookup.c:904-907: `idn2_to_ascii_8z` defined as a thin wrapper over `idn2_lookup_u8`, the encode-side counterpart to the decode-side `idn2_to_unicode_*` family.
> libidn2/lib/punycode.c:143-147 (doc on idn2_punycode_encode, the encode analog of idn2_punycode_decode): "you should consider idn2_to_ascii_8z(), idn2_to_ascii_lz(), idn2_to_ascii_4z() etc." mirroring the ToUnicode doc at decode.c:285-287 pointing at idn2_to_unicode_8z8z()/idn2_to_unicode_lzlz().

### P2.8 Prerequisite State
**Claim:** No global/process state (locale, config file, network) must exist before calling `idn2_to_unicode_8z8z` itself. The locale only matters for the *other* entries `idn2_to_unicode_8zlz`/`idn2_to_unicode_lzlz`, which call `locale_charset()` and iconv-based conversion routines after/before calling the 8z8z/8z4z core.
**Evidence:**
> libidn2/lib/decode.c:357-393 (`idn2_to_unicode_8zlz`): `encoding = locale_charset (); output_l8 = (uint8_t*) u8_strconv_to_encoding (output_u8, encoding, iconveh_error);` — locale dependency only here, not in the target.
> libidn2/lib/decode.c:304-332 (`idn2_to_unicode_8z8z` body): no locale/env/file access, only `idn2_to_unicode_8z4z` + `u32_to_u8`.

### P2.9 Result Usage
**Claim:** The allocated `*output` UTF-8 string is typically: (a) printed/logged, (b) freed, or (c) fed into further charset conversion by `idn2_to_unicode_8zlz` (locale conversion) which itself calls `idn2_to_unicode_8z8z` internally and then `u8_strconv_to_encoding`. Tests re-encode the result back to UTF-32 with `u8_to_u32` purely for comparison against expected vectors — that is a gnulib unistring function, not a libidn2 API, but shows the expected consumption pattern.
**Evidence:**
> libidn2/lib/decode.c:357,363,367-369 (`idn2_to_unicode_8zlz`): `rc = idn2_to_unicode_8z8z (input, (char **) &output_u8, flags); ... encoding = locale_charset (); output_l8 = (uint8_t*) u8_strconv_to_encoding (output_u8, encoding, iconveh_error);` — direct downstream consumer of the target's output buffer.
> libidn2/tests/test-tounicode.c:433-439: `rc = idn2_to_unicode_8z8z (t->punycode, (char **) &utf8, 0); if (rc == IDN2_OK) { ucs4 = u8_to_u32 (utf8, u8_strlen (utf8) + 1, NULL, &outlen); free (utf8); } _check_4z (t, rc, ucs4, "idn2_to_unicode_8z8z");`
> libidn2/examples/example-tounicode.c:40,47,49: result is printed with `printf ("ACE label (length %ld): '%s'\n", ...)` then freed.

### P2.10 Behaviour Controls
**Claim:** For `idn2_to_unicode_8z8z`/`idn2_to_unicode_8z4z`, `flags` has no effect (explicitly `(void) flags;`/documented "Currently unused"); the only real behavioural selector in the decode path is the per-label "xn--"/"XN--"/mixed-case 4-byte ACE prefix check, which routes a label to `idn2_punycode_decode` instead of straight UTF-8→UTF-32 conversion (`u8_to_u32`). `output == NULL` selects a "validate only, don't allocate" mode at every level of the family. For the sibling encode entry `idn2_to_ascii_8z` (via `idn2_lookup_u8`), `flags` IS meaningful: `IDN2_NFC_INPUT`, `IDN2_TRANSITIONAL`/`IDN2_NONTRANSITIONAL`, `IDN2_ALABEL_ROUNDTRIP`/`IDN2_NO_ALABEL_ROUNDTRIP`, `IDN2_USE_STD3_ASCII_RULES` select different TR46/NFC/roundtrip processing paths — a harness exercising the family (per the other-entries table below) should vary these for the ascii-side calls.
**Evidence:**
> libidn2/lib/decode.c:64: `(void) flags;` in `idn2_to_unicode_8z4z`.
> libidn2/lib/decode.c:88-96: `if (e - s >= 4 && (s[0]=='x'||s[0]=='X') && (s[1]=='n'||s[1]=='N') && s[2]=='-' && s[3]=='-') { s += 4; rc = idn2_punycode_decode ((char*) s, e - s, label_u32, &label_len); ...}` — case-insensitive 4-char ACE-prefix detection controls the path.
> libidn2/lib/idn2.h.in:165-186: flag enum `IDN2_NFC_INPUT=1, IDN2_ALABEL_ROUNDTRIP=2, IDN2_TRANSITIONAL=4, IDN2_NONTRANSITIONAL=8, IDN2_USE_STD3_ASCII_RULES=32` — used by `idn2_lookup_u8`/`idn2_to_ascii_8z`, not by the decode path.
> libidn2/lib/lookup.c:497-517 (doc on idn2_lookup_u8): details how each flag changes behavior for the ASCII/encode side.

### P2.11 Input Expectations
**Claim:** `input` is expected to be a NUL-terminated byte string, dot-separated into labels; each label is independently either (i) ACE (starts with case-insensitive "xn--", remainder decoded by `idn2_punycode_decode`, which requires all remaining chars be "basic" ASCII `[A-Za-z0-9_-]`, rejects empty input, and rejects a leading/trailing bare delimiter `-`) or (ii) plain UTF-8 (converted via `u8_to_u32`, which can fail with `IDN2_ENCODING_ERROR` on malformed UTF-8). Hard size bounds: each decoded label must be `<= IDN2_LABEL_MAX_LENGTH` (63) code points, and the whole domain's decoded length must be `<= IDN2_DOMAIN_MAX_LENGTH` (255) code points, enforced while concatenating into the fixed stack array `out_u32[IDN2_DOMAIN_MAX_LENGTH + 1]`. The Punycode decoder additionally performs explicit overflow checks on the generalized variable-length integer (returns `IDN2_PUNYCODE_OVERFLOW`), range checks on decoded code points (`n > 0x10FFFF` or surrogate range → `IDN2_PUNYCODE_BAD_INPUT`), and an output-capacity check (`IDN2_PUNYCODE_BIG_OUTPUT`). NULL `input` is explicitly accepted and treated as success with no output.
**Evidence:**
> libidn2/lib/decode.c:76,82-83: `uint32_t out_u32[IDN2_DOMAIN_MAX_LENGTH + 1]; ... uint32_t label_u32[IDN2_LABEL_MAX_LENGTH]; size_t label_len = IDN2_LABEL_MAX_LENGTH;`
> libidn2/lib/decode.c:88-99,115-125: bounds checks returning `IDN2_TOO_BIG_LABEL`/`IDN2_TOO_BIG_DOMAIN`.
> libidn2/lib/punycode.c:303-304: `if (!input_length) return IDN2_PUNYCODE_BAD_INPUT;`
> libidn2/lib/punycode.c:306-313: `for (j = 0; j < input_length; ++j) { if (!decode_basic (input[j])) return IDN2_PUNYCODE_BAD_INPUT; if (input[j] == delimiter) b = j; }`
> libidn2/lib/punycode.c:318-325: rejects leading (`!b`) or trailing (`b == input_length - 1`) delimiter.
> libidn2/lib/punycode.c:362-363,369-371,378-379: three distinct `IDN2_PUNYCODE_OVERFLOW` checks in the generalized-VLI decode loop.
> libidn2/lib/punycode.c:381-382: `if (n > 0x10FFFF || (n >= 0xD800 && n <= 0xDBFF)) return IDN2_PUNYCODE_BAD_INPUT;`
> libidn2/lib/punycode.c:389-390: `if (out >= max_out) return IDN2_PUNYCODE_BIG_OUTPUT;`
> libidn2/lib/decode.c:68-73: `if (!input) { if (output) *output = NULL; return IDN2_OK; }` (in idn2_to_unicode_8z4z, reached from the target when input==NULL, decode.c:312 `if (rc != IDN2_OK || !input) return rc;`).

## Other entries of the Logic Group (call protocol + combination with the target)

### idn2_to_unicode_8z4z (UTF-8 → UTF-32 core, called by the target)
**Signature:** `int idn2_to_unicode_8z4z (const char *input, uint32_t **output, int flags)` — libidn2/lib/decode.c:62.
**Needs before it:** nothing (no init). **Params:** `input` NUL-terminated UTF-8 C string or NULL; `output` may be NULL (lookup-only); `flags` unused (`(void) flags;` decode.c:64).
**Paths selected:** per-label "xn--"/"XN--" (case-insensitive) prefix → `idn2_punycode_decode`; else `u8_to_u32`. Same bounds as the target (IDN2_LABEL_MAX_LENGTH/IDN2_DOMAIN_MAX_LENGTH).
**Return:** `IDN2_OK` / `IDN2_TOO_BIG_DOMAIN` / `IDN2_TOO_BIG_LABEL` / `IDN2_ENCODING_ERROR` / `IDN2_MALLOC` (decode.c:52-57).
**What to free:** on success with non-NULL `output`, caller frees `*output` (a `uint32_t*` from `u32_cpy_alloc`, decode.c:145,153). On error nothing is allocated for the caller.
**Combination with target:** this is literally the first call inside `idn2_to_unicode_8z8z` (decode.c:311); a harness normally does not need to call it directly unless it wants to test the UTF-32 intermediate result directly, as tests/test-tounicode.c:411 does, comparing it against `idn2_to_unicode_8z8z`'s result (decode.c:433) for cross-checking.

### idn2_to_unicode_4z4z (UTF-32 input variant)
**Signature:** `int idn2_to_unicode_4z4z (const uint32_t *input, uint32_t **output, int flags)` — libidn2/lib/decode.c:181.
**Needs before it:** nothing. **Params:** `input` a NUL-terminated (0-code-point-terminated) `uint32_t` array or NULL; `output` may be NULL.
**Internals:** converts `input` to UTF-8 via `u32_to_u8` (decode.c:195), calls `idn2_to_unicode_8z4z` (decode.c:203), frees the temporary UTF-8 buffer (decode.c:204).
**Return:** same rc set as idn2_to_unicode_8z4z, plus `IDN2_MALLOC`/`IDN2_ENCODING_ERROR` from the initial `u32_to_u8` (decode.c:196-201).
**What to free:** caller frees `*output` (`uint32_t*`) on success if non-NULL was requested; function frees its own `output_u32` internally if the caller passed `output==NULL` (decode.c:206-212).
**Combination:** independent entry point exercising the same core from UTF-32 input; tests/test-tounicode.c:420 calls it directly on a `u8_to_u32`-converted version of the same punycode string also fed to the target, as a cross-check (`_check_4z`).

### idn2_to_unicode_44i (raw UTF-32 array, explicit lengths, no NUL dependency)
**Signature:** `int idn2_to_unicode_44i (const uint32_t *in, size_t inlen, uint32_t *out, size_t *outlen, int flags)` — libidn2/lib/decode.c:244.
**Needs before it:** nothing. **Params:** `in`/`inlen` describe a (possibly non-NUL-terminated) code-point buffer; `out` is caller-provided buffer of capacity `*outlen` (may be NULL); `outlen` in/out: on input max capacity (ignored if `out` is NULL), on output actual decoded length.
**Internals:** heap-copies `in` into a NUL-terminated buffer (decode.c:259-264), calls `idn2_to_unicode_4z4z` (decode.c:266), copies up to `*outlen` code points into `out` (decode.c:271-273), then always frees its own `output_u32` (decode.c:274) — note: `out`/`outlen` truncation is silent, caller must check returned `*outlen` vs. the `out` capacity it supplied.
**Return:** `IDN2_OK` or propagated error from idn2_to_unicode_4z4z, or `IDN2_MALLOC` if the initial `malloc` fails (decode.c:259-261).
**What to free:** caller allocates/frees `in`/`out` itself (plain buffers, not library-owned); nothing from this call needs explicit idn2_free. tests/test-tounicode.c:424-427 shows typical use: fixed-size `q[]` passed as `out`, result copied into a freshly `u32_cpy_alloc`'d buffer for comparison.
**Combination:** alternate "no NUL termination assumption" entry into the same core; good for a harness that wants to pass attacker bytes directly as a `uint32_t[]` without needing a NUL terminator baked in.

### idn2_to_unicode_8zlz (locale-output variant, consumes the target's result)
**Signature:** `int idn2_to_unicode_8zlz (const char *input, char **output, int flags)` — libidn2/lib/decode.c:357.
**Needs before it:** nothing extra, but its behavior depends on the active process locale (`locale_charset()`, decode.c:367).
**Internals:** calls `idn2_to_unicode_8z8z (input, (char **) &output_u8, flags)` (decode.c:363) — i.e. it literally wraps the target — then converts `output_u8` (UTF-8) to the locale charset with `u8_strconv_to_encoding` (decode.c:368-369), frees `output_u8` unconditionally (decode.c:390).
**Return:** `IDN2_OK` / target's errors / `IDN2_MALLOC` / `IDN2_ENCODING_ERROR` from the iconv step (decode.c:371-376).
**What to free:** caller frees `*output` (locale-encoded string) on success if non-NULL requested (decode.c:382-385); the intermediate UTF-8 buffer is always freed internally regardless of path (decode.c:390).
**Combination with target:** direct caller of `idn2_to_unicode_8z8z`; a harness exercising this entry automatically also exercises the target. Use in addition to direct target calls to cover the iconv/locale output path; test skip logic (tests/test-tounicode.c:400-401 `skip_lz`) shows this path is only meaningfully different when locale encoding != UTF-8.

### idn2_to_unicode_lzlz (locale-input + locale-output variant)
**Signature:** `int idn2_to_unicode_lzlz (const char *input, char **output, int flags)` — libidn2/lib/decode.c:419.
**Needs before it:** nothing extra; depends on locale like 8zlz.
**Internals:** NULL input short-circuits to `IDN2_OK`/`*output=NULL` (decode.c:425-430); else converts `input` from locale charset to UTF-8 via `u8_strconv_from_encoding` (decode.c:432-433) — can fail with `IDN2_ICONV_FAIL` (decode.c:437-439, distinct from 8zlz's `IDN2_ENCODING_ERROR`) — then calls `idn2_to_unicode_8zlz` (decode.c:442), frees the input conversion buffer (decode.c:443).
**Return:** `IDN2_OK` / `IDN2_MALLOC` / `IDN2_ICONV_FAIL` / propagated errors from idn2_to_unicode_8zlz (which itself wraps the target).
**What to free:** caller frees `*output` on success as with 8zlz; internal `input_l8` always freed by the function itself.
**Combination:** two layers above the target (lzlz → 8zlz → 8z8z); exercises an input-side charset-conversion path in front of the same core. Feeding raw fuzz bytes here additionally stresses `u8_strconv_from_encoding`/iconv error handling before reaching the target.

### idn2_to_ascii_8z (inverse ToASCII operation — round-trip partner, not part of the ToUnicode core but same feature family)
**Signature:** `int idn2_to_ascii_8z (const char *input, char **output, int flags)` — libidn2/lib/lookup.c:904, thin wrapper: `return idn2_lookup_u8 ((const uint8_t *) input, (uint8_t **) output, flags);` (lookup.c:906).
**Needs before it:** nothing; it is independent of the target (encode direction).
**Params:** `input` UTF-8 string expected in Unicode NFC form unless `IDN2_NFC_INPUT` is passed (lookup.c:489,497-499); `output` may be NULL (test-only mode, per idn2_lookup_u8 doc, lookup.c:519-520); `flags` meaningfully selects behavior here: `IDN2_NFC_INPUT`, `IDN2_ALABEL_ROUNDTRIP`/`IDN2_NO_ALABEL_ROUNDTRIP` (default since 2.2), `IDN2_TRANSITIONAL`/`IDN2_NONTRANSITIONAL`, `IDN2_USE_STD3_ASCII_RULES` (lookup.c:497-517; idn2.h.in:165-186).
**Return:** `IDN2_OK` or error (`IDN2_TOO_BIG_DOMAIN`/`IDN2_TOO_BIG_LABEL`/etc., lookup.c:522-525).
**What to free:** caller frees `*output` (ASCII/ACE string) on success, same convention as the target.
**Combination with target:** round-trip harness pattern — feed fuzz bytes to `idn2_to_ascii_8z` to get an ACE string, then feed that ACE string into `idn2_to_unicode_8z8z` (decode direction, the target) to exercise both the punycode encoder and decoder on related inputs; or feed `idn2_to_unicode_8z8z`'s decoded output back into `idn2_to_ascii_8z` to check round-trip stability. Real project fuzzers keep these as two *separate* fuzz targets (`fuzz/libidn2_to_ascii_8z_fuzzer.c` and `fuzz/libidn2_to_unicode_8z8z_fuzzer.c`), not chained in one call — see Build notes.

### idn2_punycode_decode (core, lowest level, same file-family as the target's dispatch)
**Signature:** `int idn2_punycode_decode (const char *input, size_t input_length, uint32_t *output, size_t *output_length)` — libidn2/lib/punycode.c:296.
**Not a public entry in idn2.h.in's family list requested by harness; it is "Core" per the Logic Group description, exported (`_IDN2_API`) but documented as rarely needed directly** ("Warning: Be aware that is rare for applications to need to perform Punycode operations, and you should consider idn2_to_unicode_8z8z()...", punycode.c:285-287). Called internally from `idn2_to_unicode_8z4z` once per "xn--"-prefixed label (decode.c:93-94). A harness could call it directly to target the punycode state machine bypassing label splitting/bounds, but that is a *lower-level* internal than the requested target, not a sibling "entry" of the ToUnicode family in idn2.h's public-facing sense — it does still carry `_IDN2_API` visibility (punycode.c has no static qualifier) so it's technically public ABI, but it's not declared in `idn2.h.in`'s synopsis of idn2_to_unicode_* functions and is only documented as an internal helper referenced by its warning comment.

## Build notes

**Language:** C (library and examples are plain C; fuzz drivers are compiled as C/C++ depending on `$CXX`/engine, but `LLVMFuzzerTestOneInput` signature is `extern "C"`-compatible via `fuzzer.h`).
**Existing gold/empty fuzzer for this target:** `fuzz/libidn2_to_unicode_8z8z_fuzzer.c` in the current tree is a stub ("Gold harness replaced by an empty stub for baseline generation") that only force-links the symbol via `__asm__("idn2_to_unicode_8z8z")` and returns 0 from `LLVMFuzzerTestOneInput` without calling it — libidn2/fuzz/libidn2_to_unicode_8z8z_fuzzer.c:1-16. A real harness must replace this with actual calls into the ToUnicode family.
**Sibling real fuzzer for reference (ToASCII side):** `fuzz/libidn2_to_ascii_8z_fuzzer.c` shows the expected pattern: build a NUL-terminated `domain` from fuzz bytes, exercise NULL-input/NULL-output edge cases when `size==0`, then loop over an array of representative `flags` values calling `idn2_to_ascii_8z`/`idn2_to_ascii_lz`/`idn2_to_ascii_4i`/`idn2_to_ascii_4z`, freeing successful outputs with `idn2_free` — libidn2/fuzz/libidn2_to_ascii_8z_fuzzer.c:27-119.
**Header include:** `#include "idn2.h"` (local/build-tree copy of the generated `lib/idn2.h` from `lib/idn2.h.in`), e.g. libidn2/fuzz/fuzzer.h:24, libidn2/examples/example-tounicode.c:9 (`#include <idn2.h> /* idn2_to_unicode_8z8z() */`).
**Compile flags for fuzz sources (autotools path):** `AM_CPPFLAGS = -I$(top_srcdir)/lib -I../lib -I$(top_srcdir)/gl -I../gl -DSRCDIR=... -DTEST_RUN`, linked against `../lib/libidn2.la ../gl/libgnu.la $(LTLIBUNISTRING)` — libidn2/fuzz/Makefile.am:21-25.
**OSS-Fuzz compile rule:** `fuzz/Makefile.am`'s `oss-fuzz` target: for each `*_fuzzer.c`, `$CXX $CXXFLAGS -I$(top_srcdir)/lib -I$(top_srcdir) "$fuzzer.c" -o "$fuzzer" ../lib/.libs/libidn2.a $LIB_FUZZING_ENGINE -Wl,-Bstatic -Wl,-Bdynamic` — libidn2/fuzz/Makefile.am:49-58, i.e. statically links `libidn2.a` (built by `make check` beforehand) plus the OSS-Fuzz engine.
**Top-level OSS-Fuzz build script:** `./bootstrap; ASAN_OPTIONS=detect_leaks=0 ./configure --enable-static --disable-shared --disable-doc --disable-gcc-warnings; make clean; make -j$(nproc) check VERBOSE=t; cd fuzz; make oss-fuzz; find . -name '*_fuzzer' -exec cp -v '{}' $OUT ';'` — ${QF_WORK}/src/libidn2-c1/build.sh:18-27. Note `--enable-static --disable-shared`, so the fuzzer links the static `libidn2.a`.
**Libraries:** gnulib's `libgnu.la` and libunistring (`$LTLIBUNISTRING`) are required dependencies (used for `u8_to_u32`/`u32_to_u8`/`u32_cpy`/`u8_strconv_*` etc. called throughout decode.c), confirmed by `#include <unistr.h>`, `<uniconv.h>`, `<unitypes.h>` at libidn2/lib/decode.c:36-38 and the `LDADD` line in fuzz/Makefile.am:25.