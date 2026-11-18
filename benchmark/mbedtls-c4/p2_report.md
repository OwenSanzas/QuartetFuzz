## target: mbedtls_x509_csr_parse

### Overview
`mbedtls_x509_csr_parse` is declared in `include/mbedtls/x509_csr.h:172` and defined in `library/x509_csr.c:463-507`. It is the PEM/DER-auto-detecting entry point for parsing a PKCS#10 Certificate Signing Request. Internally it tries PEM decapsulation first (if `MBEDTLS_PEM_PARSE_C` is enabled and the buffer is NUL-terminated), then always falls through to `mbedtls_x509_csr_parse_der`, which calls the static core `mbedtls_x509_csr_parse_der_internal` (`library/x509_csr.c:274`).

### P2.1 Init Sequence
**Claim:** The CSR structure must be zero-initialized with `mbedtls_x509_csr_init(&csr)` before calling `mbedtls_x509_csr_parse`. If `MBEDTLS_USE_PSA_CRYPTO` is enabled, `psa_crypto_init()` must be called once before any parse call (library-wide, not per-CSR). No other mbedtls-wide init is required for the X.509 CSR module itself.
**Evidence:**
> include/mbedtls/x509_csr.h:82-84: "\note If #MBEDTLS_USE_PSA_CRYPTO is enabled, the PSA crypto subsystem must have been initialized by calling psa_crypto_init() before calling this function." (same note repeated for parse_der, parse_der_with_ext_cb, parse)
> programs/x509/req_app.c:59-68: `mbedtls_x509_csr_init(&csr);` then `#if defined(MBEDTLS_USE_PSA_CRYPTO) psa_status_t status = psa_crypto_init(); ...`
> tests/suites/test_suite_x509parse.function:1299-1300: `mbedtls_x509_csr_init(&csr); USE_PSA_INIT();` precedes `mbedtls_x509_csr_parse_der`.
> library/x509_csr.c:293: the internal DER parser itself calls `mbedtls_x509_csr_init(csr)` again at the very start, so even if the caller forgot, the struct gets zeroed before fields are written — but a caller must still not pass an already-populated/undestroyed csr (would leak memory, see P2.5).

### P2.2 Parameter Construction
**Claim:** `csr` must be a pointer to a valid (ideally freshly-initialized) `mbedtls_x509_csr`; `buf` is a byte buffer that may hold PEM text (NUL-terminated) or raw DER bytes; `buflen` is its length "including the terminating null byte for PEM data". All three are checked for NULL/0 at function entry (inside the DER core) and will return `MBEDTLS_ERR_X509_BAD_INPUT_DATA`.
**Evidence:**
> include/mbedtls/x509_csr.h:165-168: "\param buf buffer holding the CRL data \param buflen size of the buffer (including the terminating null byte for PEM data)"
> library/x509_csr.c:289-291 (in `mbedtls_x509_csr_parse_der_internal`): `if (csr == NULL || buf == NULL || buflen == 0) { return MBEDTLS_ERR_X509_BAD_INPUT_DATA; }`
> library/x509_csr.c:474-476 (in `mbedtls_x509_csr_parse` itself): identical NULL/0 checks before PEM/DER dispatch.
> library/x509_csr.c:480: `if (buf[buflen - 1] == '\0') { ... mbedtls_pem_read_buffer(...) }` — PEM path is only attempted when the last byte is NUL; otherwise it falls straight to `mbedtls_x509_csr_parse_der(csr, buf, buflen)` at line 506.

### P2.3 Object Lifecycle
**Claim:** Create (`mbedtls_x509_csr_init`) → parse (`mbedtls_x509_csr_parse`/`_der`/`_der_with_ext_cb`/`_file`) → consume (`mbedtls_x509_csr_info`, or read fields directly) → destroy (`mbedtls_x509_csr_free`). The parse functions themselves call `mbedtls_x509_csr_init` again internally before filling fields, and call `mbedtls_x509_csr_free` internally on every error path, so a `csr` passed to parse is always left in a freeable (safe) state, success or failure — callers must still call `mbedtls_x509_csr_free` exactly once after use regardless of return code (it's a no-op/safe on an all-zero struct).
**Evidence:**
> library/x509_csr.c:293: `mbedtls_x509_csr_init(csr);` at the start of `mbedtls_x509_csr_parse_der_internal`.
> library/x509_csr.c:319,324,336,347,352,365,370,380,397,402,415,422,427,432: every error return in the core calls `mbedtls_x509_csr_free(csr);` first.
> programs/x509/req_app.c:59,98,102,115,124: init → parse_file → (on error) free+goto exit; (on success) info → free at `exit:` label — single free at the end on both paths.
> tests/suites/test_suite_x509parse.function:1299,1304,1314: init → parse_der → free (always executed via `exit:` label even when TEST_EQUAL fails).

### P2.4 Return Value Handling
**Claim:** Return `0` on success; negative `MBEDTLS_ERR_X509_*` / `MBEDTLS_ERR_PEM_*` / `MBEDTLS_ERR_ASN1_*` (composed via `MBEDTLS_ERROR_ADD`) codes on failure. Callers must check `ret != 0` and treat the csr as unusable (though internally it's already been freed/zeroed by the core on failure); the only valid version is v1 (encoded as `0`), otherwise `MBEDTLS_ERR_X509_UNKNOWN_VERSION`.
**Evidence:**
> include/mbedtls/x509_csr.h:170: "\return 0 if successful, or a specific X509 or PEM error code"
> library/x509_csr.c:351-354: `if (csr->version != 0) { mbedtls_x509_csr_free(csr); return MBEDTLS_ERR_X509_UNKNOWN_VERSION; }`
> library/x509_csr.c:423: `return MBEDTLS_ERR_X509_UNKNOWN_SIG_ALG;` on bad signature algorithm.
> programs/x509/req_app.c:98-104: `ret = mbedtls_x509_csr_parse_file(...); if (ret != 0) { printf(...); mbedtls_x509_csr_free(&csr); goto exit; }`
> tests/suites/test_suite_x509parse.function:1304-1305: `my_ret = mbedtls_x509_csr_parse_der(...); TEST_EQUAL(my_ret, ref_ret);` — tests exercise both 0 and various negative `ref_ret` values driven by malformed DER fixtures.

### P2.5 Cleanup Sequence
**Claim:** Only `mbedtls_x509_csr_free(&csr)` is needed, exactly once, after the csr is no longer needed (whether parse succeeded or failed). It frees the embedded `pk` context, optional `sig_opts` (RSASSA-PSS params), the `subject` named-data list, the `subject_alt_names` sequence list, and the raw DER copy, then zeroizes the whole struct. If `MBEDTLS_USE_PSA_CRYPTO` was initialized, the caller should also call `mbedtls_psa_crypto_free()` once at program/fuzzer teardown (not per-CSR).
**Evidence:**
> library/x509_csr.c:626-646: `mbedtls_x509_csr_free` body — `mbedtls_pk_free(&csr->pk); #if RSASSA_PSS mbedtls_free(csr->sig_opts); mbedtls_asn1_free_named_data_list_shallow(csr->subject.next); mbedtls_asn1_sequence_free(csr->subject_alt_names.next); if (csr->raw.p) mbedtls_zeroize_and_free(csr->raw.p, csr->raw.len); mbedtls_platform_zeroize(csr, sizeof(*csr));`
> programs/x509/req_app.c:124-127: `exit: mbedtls_x509_csr_free(&csr); #if MBEDTLS_USE_PSA_CRYPTO mbedtls_psa_crypto_free(); #endif`

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited locations.
**Evidence:**
> include/mbedtls/x509_csr.h:172 `mbedtls_x509_csr_parse`; :92 `mbedtls_x509_csr_parse_der`; :151 `mbedtls_x509_csr_parse_der_with_ext_cb`; :185 `mbedtls_x509_csr_parse_file`; :201 `mbedtls_x509_csr_info`; :210 `mbedtls_x509_csr_init`; :217 `mbedtls_x509_csr_free`.
> library/x509_csr.c:274 `mbedtls_x509_csr_parse_der_internal` (static core); :43 `x509_csr_get_version`; :199 `x509_csr_parse_attributes`; :64 `x509_csr_parse_extensions`.
> include/mbedtls/pk.h:326 `mbedtls_pk_free`; include/mbedtls/asn1.h:426 `mbedtls_asn1_sequence_free`, :631 `mbedtls_asn1_free_named_data_list_shallow`; include/mbedtls/pem.h:64 `mbedtls_pem_init`, :92 `mbedtls_pem_read_buffer`, :121 `mbedtls_pem_free`.

### P2.7 Co-call Constraints
**Claim:** `mbedtls_x509_csr_init` / `mbedtls_x509_csr_free` are a mandatory create/destroy pair around any parse call (init before, free after — exactly one free per init, even on error, since internal error paths already free). `mbedtls_pem_init`/`mbedtls_pem_read_buffer`/`mbedtls_pem_free` is an internal pairing used only inside `mbedtls_x509_csr_parse` itself (not caller-visible). If `psa_crypto_init()` is used, it should be paired with one `mbedtls_psa_crypto_free()` at teardown (library-wide, not per CSR).
**Evidence:**
> library/x509_csr.c:481,500: `mbedtls_pem_init(&pem);` ... `mbedtls_pem_free(&pem);` inside `mbedtls_x509_csr_parse` — self-contained, no caller action needed.
> programs/x509/req_app.c:59-127: single init/free pair spanning the whole CSR lifetime, single psa_crypto_init/mbedtls_psa_crypto_free pair.
> tests/suites/test_suite_x509parse.function:1299/1314, 1326/1344, 1356+ (parse_file case): each test case does exactly one init + one free, with `USE_PSA_INIT()/USE_PSA_DONE()` bracketing it.

### P2.8 Prerequisite State
**Claim:** No connection/context objects are required besides PSA crypto init when `MBEDTLS_USE_PSA_CRYPTO` is compiled in. The CSR module depends on `MBEDTLS_X509_CSR_PARSE_C` being defined (the whole file is guarded by it); `MBEDTLS_FS_IO` gates `mbedtls_x509_csr_parse_file`; `MBEDTLS_PEM_PARSE_C` gates the PEM-detection branch inside `mbedtls_x509_csr_parse` (if undefined, `mbedtls_x509_csr_parse` behaves exactly like `mbedtls_x509_csr_parse_der`); `!MBEDTLS_X509_REMOVE_INFO` gates `mbedtls_x509_csr_info`.
**Evidence:**
> library/x509_csr.c:20: `#if defined(MBEDTLS_X509_CSR_PARSE_C)` wraps the entire file.
> library/x509_csr.c:30-32,465,478,505: `#if defined(MBEDTLS_PEM_PARSE_C)` guards the pem_context/variables and the PEM branch in `mbedtls_x509_csr_parse`.
> library/x509_csr.c:509: `#if defined(MBEDTLS_FS_IO)` guards `mbedtls_x509_csr_parse_file`.
> library/x509_csr.c:531: `#if !defined(MBEDTLS_X509_REMOVE_INFO)` guards `mbedtls_x509_csr_info`.
> ossfuzz build.sh:19-24: `scripts/config.py full` then explicitly `scripts/config.py unset MBEDTLS_USE_PSA_CRYPTO` for the default fuzz build (so psa_crypto_init is compiled out / not required in that configuration), while a second build variant (`build-usepsa`, lines 54-66) sets `MBEDTLS_USE_PSA_CRYPTO` and requires `psa_crypto_init()`.

### P2.9 Result Usage
**Claim:** After a successful parse, the filled `mbedtls_x509_csr` is consumed by `mbedtls_x509_csr_info()` to produce a human-readable summary (subject DN, signature algorithm, public key info, key usage, Netscape cert type), by direct field access (`csr.version`, `csr.subject`, `csr.pk`, `csr.key_usage`, `csr.ns_cert_type`, `csr.subject_alt_names`, `csr.sig_oid`), and by `mbedtls_pk_get_name`/`mbedtls_x509_key_size_helper`/`mbedtls_x509_sig_alg_gets`/`mbedtls_x509_dn_gets` internally inside `mbedtls_x509_csr_info`. Real tools (cert_write, req_app) then typically use `csr.subject` / `csr.pk` to build a certificate (not shown exercised by a fuzz target, but is the "write" counterpart).
**Evidence:**
> library/x509_csr.c:537-565 (`mbedtls_x509_csr_info` body): `mbedtls_x509_dn_gets(p, n, &csr->subject)`, `mbedtls_x509_sig_alg_gets(p, n, &csr->sig_oid, csr->sig_pk, csr->sig_md, csr->sig_opts)`, `mbedtls_x509_key_size_helper(key_size_str, ..., mbedtls_pk_get_name(&csr->pk))` (line ~565, cut off in read but visible at offset 564).
> programs/x509/req_app.c:98-119: `mbedtls_x509_csr_parse_file(&csr, opt.filename)` then `mbedtls_x509_csr_info((char*)buf, sizeof(buf)-1, "      ", &csr)` then `mbedtls_printf("%s\n", buf)`.
> programs/x509/cert_write.c:759-766: `mbedtls_x509_csr_parse_file(&csr, opt.request_file)` then `mbedtls_x509_dn_gets(subject_name, sizeof(subject_name), &csr.subject)` directly reading the parsed `subject` field to build a new certificate (cert_write.c reuses the parsed subject/pk to write a cert — the "result consumer" outside info()).
> tests/suites/test_suite_x509parse.function:1304-1310: after successful parse, `mbedtls_x509_csr_info(my_out, sizeof(my_out), "", &csr)` output is compared byte-for-byte against a reference string — the standard pattern for consuming parse results in tests.

### P2.10 Behaviour Controls
**Claim:** The top-level `mbedtls_x509_csr_parse` auto-selects PEM vs DER based on whether `buf[buflen-1]=='\0'` and whether a `"-----BEGIN CERTIFICATE REQUEST-----"`/`"-----BEGIN NEW CERTIFICATE REQUEST-----"` header is found; `mbedtls_x509_csr_parse_der_with_ext_cb` differs from `mbedtls_x509_csr_parse_der` only in the `cb`/`p_ctx` parameters, which change how unknown/unsupported extensions are handled (callback invoked instead of unconditional skip-if-noncritical/fail-if-critical). Inside the DER core, the CSR `version` field selects accept (0→v1) vs reject (anything else → `MBEDTLS_ERR_X509_UNKNOWN_VERSION`); the PKCS#9 attribute OID selects whether `x509_csr_parse_extensions` runs at all (only `MBEDTLS_OID_PKCS9_CSR_EXT_REQ` is recognized, others fully skipped); within extensions, the extension OID (`MBEDTLS_X509_EXT_KEY_USAGE`/`SUBJECT_ALT_NAME`/`NS_CERT_TYPE`) selects a dedicated sub-parser, and the `critical` BOOLEAN combined with "unknown extension" selects reject-vs-skip (or callback result when using `_with_ext_cb`).
**Evidence:**
> library/x509_csr.c:480-491: dual PEM header/footer attempts (`"-----BEGIN CERTIFICATE REQUEST-----"` then `"-----BEGIN NEW CERTIFICATE REQUEST-----"`), falling back to DER at line 506.
> library/x509_csr.c:443-458: `mbedtls_x509_csr_parse_der` calls the internal core with `cb=NULL,p_ctx=NULL`; `mbedtls_x509_csr_parse_der_with_ext_cb` calls the same internal core with caller-supplied `cb,p_ctx` — only the extension-callback path differs.
> library/x509_csr.c:351-356: version check: `if (csr->version != 0) {... return MBEDTLS_ERR_X509_UNKNOWN_VERSION;} csr->version++;`
> library/x509_csr.c:229-253: `if (MBEDTLS_OID_CMP(MBEDTLS_OID_PKCS9_CSR_EXT_REQ, &attr_oid) == 0) { ... x509_csr_parse_extensions(...) }` — other attribute OIDs are silently skipped (fallthrough to `*p = end_attr_data;` at line 260).
> library/x509_csr.c:150-185: `switch(ext_type)` over `MBEDTLS_X509_EXT_KEY_USAGE`/`SUBJECT_ALT_NAME`/`NS_CERT_TYPE`, `default:` branch does `if (is_critical) return MBEDTLS_ERR_X509_FEATURE_UNAVAILABLE; else *p = end_ext_octet;` (lines 174-184) — this is the branch that `mbedtls_x509_csr_parse_der_with_ext_cb`'s callback replaces (per header doc at x509_csr.h:96-121).

### P2.11 Input Expectations
**Claim:** Input is either PEM text (ASCII, must be NUL-terminated per `buflen` including the trailing NUL, with recognized BEGIN/END CERTIFICATE REQUEST armor) or raw BER/DER bytes encoding a PKCS#10 `CertificationRequest ::= SEQUENCE { certificationRequestInfo CertificationRequestInfo, signatureAlgorithm AlgorithmIdentifier, signature BIT STRING }`. The DER core enforces: outer SEQUENCE length must exactly match remaining buffer (`len != (size_t)(end-p)` → `MBEDTLS_ERR_X509_INVALID_FORMAT`+`ASN1_LENGTH_MISMATCH`); CertificationRequestInfo is itself a SEQUENCE; version is an INTEGER (absence tolerated as default 0 per `x509_csr_get_version`); subject is a Name SEQUENCE parsed by `mbedtls_x509_get_name`; SubjectPublicKeyInfo parsed by `mbedtls_pk_parse_subpubkey`; attributes are `[0]` CONTEXT-SPECIFIC CONSTRUCTED; trailing data after signature triggers length-mismatch error; buflen==0 or buf==NULL is rejected up front.
**Evidence:**
> library/x509_csr.c:310-316: ASN.1 comment documenting the `CertificationRequest`/`CertificationRequestInfo`/`AlgorithmIdentifier`/BIT STRING signature grammar.
> library/x509_csr.c:317-327: outer tag must be `CONSTRUCTED|SEQUENCE` and `len == end-p` exactly, else `MBEDTLS_ERR_X509_INVALID_FORMAT` (+`ASN1_LENGTH_MISMATCH`).
> library/x509_csr.c:43-59 (`x509_csr_get_version`): `mbedtls_asn1_get_int`; if tag doesn't match (`MBEDTLS_ERR_ASN1_UNEXPECTED_TAG`) defaults `*ver=0` and returns success — version field is OPTIONAL-like via DEFAULT-0 handling.
> library/x509_csr.c:394-399: attributes tag must be `MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_CONTEXT_SPECIFIC` (i.e. `[0]`).
> library/x509_csr.c:431-435: `if (p != end) {...ASN1_LENGTH_MISMATCH}` — no trailing bytes allowed after the signature BIT STRING.
> include/mbedtls/x509_csr.h:167-168: "buflen size of the buffer (including the terminating null byte for PEM data)".
> library/x509_csr.c:289-291: `buflen == 0` or NULL pointers rejected with `MBEDTLS_ERR_X509_BAD_INPUT_DATA`.

## Other entries (call protocol and combination with the target)

### mbedtls_x509_csr_parse_der (entry, variant)
**Declaration:** include/mbedtls/x509_csr.h:92. **Definition:** library/x509_csr.c:443-447, thin wrapper calling `mbedtls_x509_csr_parse_der_internal(csr, buf, buflen, NULL, NULL)`.
- Needs before it: `mbedtls_x509_csr_init(&csr)` (though internally re-done), and `psa_crypto_init()` if `MBEDTLS_USE_PSA_CRYPTO` (header note, x509_csr.h:82-84).
- Parameters: `csr` (out param, struct pointer), `buf`/`buflen` — raw DER bytes only, no PEM auto-detection (unlike the target). Same NULL/0 checks as the target (library/x509_csr.c:289-291).
- Return: 0 success, negative `MBEDTLS_ERR_X509_*`/`MBEDTLS_ERR_ASN1_*` on failure — identical error surface to the target since it shares the same core.
- Cleanup: `mbedtls_x509_csr_free(&csr)` once, success or failure (core already frees internally on error, library/x509_csr.c:319 etc.).
- Combination with target: this is literally what `mbedtls_x509_csr_parse` falls back to for non-PEM input (library/x509_csr.c:506); a harness normally calls only the target (`mbedtls_x509_csr_parse`) which already covers this path for DER-only fuzz inputs that don't end in `\0` or don't match the PEM header, so calling both back-to-back on the same struct is redundant/would need separate `csr` instances or an intervening free+init.
- Evidence: tests/suites/test_suite_x509parse.function:1304 `my_ret = mbedtls_x509_csr_parse_der(&csr, csr_der->x, csr_der->len);` directly follows `mbedtls_x509_csr_init(&csr); USE_PSA_INIT();` and is followed by `mbedtls_x509_csr_info` (line 1308) then `mbedtls_x509_csr_free` (line 1314).

### mbedtls_x509_csr_parse_der_with_ext_cb (entry, variant)
**Declaration:** include/mbedtls/x509_csr.h:151-154. **Definition:** library/x509_csr.c:452-458, wrapper calling the same internal core with caller-supplied `cb`/`p_ctx`.
- Needs before it: identical to `mbedtls_x509_csr_parse_der` (init csr, optional psa_crypto_init), plus the caller must supply a valid callback function pointer of type `mbedtls_x509_csr_ext_cb_t` (include/mbedtls/x509_csr.h:123-128): `int cb(void *p_ctx, mbedtls_x509_csr const *csr, mbedtls_x509_buf const *oid, int critical, const unsigned char *p, const unsigned char *end)`. `p_ctx` may be NULL if the callback doesn't need it.
- Parameters/flags selecting different path: the callback itself decides accept/reject of unsupported extensions; per the doc comment (x509_csr.h:112-118) "the callback must fail and return a negative error code if it can not parse ... When the callback fails to parse a critical extension, parsing also fails; when non-critical, the extension is simply skipped." This changes the `default:` branch behavior in `x509_csr_parse_extensions` (library/x509_csr.c:174-184) from pure reject-if-critical to invoke-callback.
- Return: same 0/negative convention; test harness uses two concrete callbacks, `parse_csr_ext_accept_cb` (always returns 0) and `parse_csr_ext_reject_cb` (always returns an error), to drive both branches (tests/suites/test_suite_x509parse.function:425-447, invoked at line 1331-1334).
- Cleanup: same `mbedtls_x509_csr_free` as other variants.
- Combination with target: not called by `mbedtls_x509_csr_parse` (the target never passes a non-NULL callback); it is an independent public entry point sharing the same internal core (`mbedtls_x509_csr_parse_der_internal`, library/x509_csr.c:274). A harness exercising the target's extension-parsing logic with a custom callback must call this entry directly on a separate (or re-initialized) `mbedtls_x509_csr` rather than going through the target.
- Evidence: tests/suites/test_suite_x509parse.function:1319-1346 (`mbedtls_x509_csr_parse_with_ext_cb` test case).

### mbedtls_x509_csr_parse_file (entry, variant/source)
**Declaration:** include/mbedtls/x509_csr.h:185 (guarded by `MBEDTLS_FS_IO`). **Definition:** library/x509_csr.c:513-528.
- Needs before it: `mbedtls_x509_csr_init(&csr)`; filesystem access to `path` (reads an entire file via `mbedtls_pk_load_file`).
- Parameters: `csr` out param; `path` a NUL-terminated C string filename. No `buflen`/`buf` — the function loads the file itself: `mbedtls_pk_load_file(path, &buf, &n)` (library/x509_csr.c:519) then calls `mbedtls_x509_csr_parse(csr, buf, n)` (line 523) — i.e. it is a thin file-to-buffer wrapper around the target itself, inheriting the target's PEM/DER auto-detection.
- Return: propagates `mbedtls_pk_load_file`'s error if the file can't be read/loaded, else propagates `mbedtls_x509_csr_parse`'s return value.
- Cleanup: internally frees its own temporary buffer via `mbedtls_zeroize_and_free(buf, n)` (line 525) regardless of parse outcome; caller must still separately call `mbedtls_x509_csr_free(&csr)` on the csr itself.
- Combination with target: calls the target function directly (library/x509_csr.c:523 `ret = mbedtls_x509_csr_parse(csr, buf, n);`), so using this entry plus the target in the same harness is redundant unless the harness specifically wants to test file-loading logic (`mbedtls_pk_load_file`) — note libFuzzer harnesses normally feed in-memory buffers, so this entry is a poor fit for byte-buffer-driven fuzzing but documented here since the Logic Group lists it. A harness could write fuzz data to a temp file and call this to additionally exercise `mbedtls_pk_load_file`, but the ASN.1 parsing coverage is identical to calling the target directly.
- Evidence: programs/x509/req_app.c:98 and programs/x509/cert_write.c:759 both call it directly on files, then use `mbedtls_x509_csr_info`/`mbedtls_x509_dn_gets` on the result and `mbedtls_x509_csr_free` at the end (req_app.c:124; cert_write.c also frees csr at its own exit, per standard pattern in that tool).

### mbedtls_x509_csr_info (entry, consumer of the target's result)
**Declaration:** include/mbedtls/x509_csr.h:201-202 (guarded by `!MBEDTLS_X509_REMOVE_INFO`). **Definition:** library/x509_csr.c:537-~609.
- Needs before it: a `csr` that was **successfully** parsed by the target (or `_der`/`_der_with_ext_cb`/`_file`) — it reads `csr->version`, `csr->subject`, `csr->sig_oid`, `csr->sig_pk`, `csr->sig_md`, `csr->sig_opts`, `csr->pk`, (and further down, key usage / subject alt name / ns_cert_type fields per the Logic Group description). Calling it on an un-parsed (all-zero, just-`init`'d) csr is not an error per se (fields are zero) but produces meaningless/empty output; calling it on a csr that failed to parse (and was thus `mbedtls_x509_csr_free`'d back to zero by the core) is likewise just zero-field output, not a crash — both tests and programs gate the info() call on `ret == 0` from the parse.
- Parameters: `buf`/`size` output buffer and its capacity (caller-allocated, e.g. `unsigned char buf[100000]` in req_app.c:51, or `char my_out[1000]` in tests); `prefix` a NUL-terminated string prepended to each line; `csr` the parsed structure (const).
- Return: length of the string written (not including the NUL) on success, or a negative error code (e.g. `MBEDTLS_ERR_X509_BUFFER_TOO_SMALL` propagated via `MBEDTLS_X509_SAFE_SNPRINTF`) on failure such as buffer too small.
- Cleanup: none owned by this call (it doesn't allocate); `csr` is still owned/freed by the caller via `mbedtls_x509_csr_free`.
- Combination with target: standard pattern is `mbedtls_x509_csr_parse(&csr, data, size)` → `if (ret == 0) mbedtls_x509_csr_info(buf, sizeof(buf)-1, prefix, &csr)` → `mbedtls_x509_csr_free(&csr)`. This mirrors the sibling `fuzz_x509crt.c` harness pattern exactly (programs/fuzz/fuzz_x509crt.c:19-22: `mbedtls_x509_crt_parse` then conditionally `mbedtls_x509_crt_info`), which is the template the (now-stubbed) `fuzz_x509csr.c` originally followed.
- Evidence: programs/x509/req_app.c:111-119 (`mbedtls_x509_csr_info((char*)buf, sizeof(buf)-1, "      ", &csr)` after successful `parse_file`); tests/suites/test_suite_x509parse.function:1307-1310 (`if (ref_ret == 0) { ... mbedtls_x509_csr_info(my_out, sizeof(my_out), "", &csr); TEST_EQUAL(...) }`).

## Build notes
**Claim:** The project's existing fuzz harness for this exact function, `programs/fuzz/fuzz_x509csr.c`, is a plain C file built via CMake as part of `programs/fuzz/CMakeLists.txt`'s `executables_no_common_c` list, linked against the main `mbedtls` static libs (+ pthread) and either `FuzzingEngine` (OSS-Fuzz, forcing C++ link via `LINKER_LANGUAGE CXX`) or `onefile.c`'s local `main()` for standalone builds. The target's header is included simply as `#include "mbedtls/x509_csr.h"` (system/project include path, no special path needed beyond the normal `include/` dir mbedtls exposes), as seen in the structurally-identical sibling `fuzz_x509crt.c`. The OSS-Fuzz `build.sh` builds two configurations (`MBEDTLS_USE_PSA_CRYPTO` unset, and set) and copies `programs/fuzz/fuzz_*` binaries plus `.options` files and seed corpora (`fuzz_x509csr_seed_corpus.zip`, built from `*.csr`/`*.req.*` files under `framework/data_files`) to `$OUT`.
**Evidence:**
> programs/fuzz/CMakeLists.txt:11-17: `set(executables_no_common_c fuzz_pubkey fuzz_x509crl fuzz_x509crt fuzz_x509csr fuzz_pkcs7)`
> programs/fuzz/CMakeLists.txt:31-51: `set(exe_sources ${exe}.c $<TARGET_OBJECTS:mbedtls_test>)`; `target_link_libraries(${exe} ${libs})` or `${libs} FuzzingEngine` with `LINKER_LANGUAGE CXX` when `FUZZINGENGINE_LIB` is found.
> programs/fuzz/fuzz_x509crt.c:2: `#include "mbedtls/x509_crt.h"` — plain project-relative include, same convention expected for `#include "mbedtls/x509_csr.h"` in `fuzz_x509csr.c`.
> ${QF_WORK}/ossfuzz_view/mbedtls-c4/build.sh:19,24,27-30: `scripts/config.py full`; `scripts/config.py unset MBEDTLS_USE_PSA_CRYPTO`; `cmake -DENABLE_TESTING=ON ..`; `make -j$(nproc) all`; `cp programs/fuzz/fuzz_* $OUT/`.
> build.sh:41: `zip -r fuzz/fuzz_x509csr_seed_corpus.zip ../framework/data_files/*.csr ../framework/data_files/*.req.*` — seed corpus sourced from real CSR/REQ sample files shipped with the project.
> build.sh:54-66: second pass sets `MBEDTLS_USE_PSA_CRYPTO`, rebuilds only `programs/fuzz`, copies binaries as `usepsa-fuzz_*`.
> programs/fuzz/fuzz_x509csr.c:1,10,12-16 (current state): the harness source has been replaced by a stub that only keeps the symbol `mbedtls_x509_csr_parse` linked via `__asm__` aliasing and an empty `LLVMFuzzerTestOneInput`; no live example of the original CSR-specific harness body remains in-tree, so the per-call-site protocol above was reconstructed from the structurally identical, non-stubbed sibling `fuzz_x509crt.c` and from the `programs/x509/req_app.c` tool and `tests/suites/test_suite_x509parse.function` test cases.
> programs/fuzz/fuzz_x509csr.options:1-2: `[libfuzzer]` / `max_len = 65535` — libFuzzer input-size cap configured for this target.