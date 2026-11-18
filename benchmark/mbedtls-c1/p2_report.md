## target: mbedtls_pkcs7_parse_der

### P2.1 Init Sequence
**Claim:** Caller must zero-initialize the `mbedtls_pkcs7` structure with `mbedtls_pkcs7_init()` before calling `mbedtls_pkcs7_parse_der()`. No other global/library init is required by the function itself (no PSA/crypto init needed for parsing, only for verify path in some builds).
**Evidence:**
> include/mbedtls/pkcs7.h:151-155: "`void mbedtls_pkcs7_init(mbedtls_pkcs7 *pkcs7);` /* Initialize mbedtls_pkcs7 structure. */"
> library/pkcs7.c:35-38: `void mbedtls_pkcs7_init(mbedtls_pkcs7 *pkcs7) { memset(pkcs7, 0, sizeof(*pkcs7)); }`
> tests/suites/test_suite_pkcs7.function:20-30: helper `pkcs7_parse_buffer()` calls `mbedtls_pkcs7_init(&pkcs7);` then `mbedtls_pkcs7_parse_der(&pkcs7, pkcs7_buf, buflen);` then `mbedtls_pkcs7_free(&pkcs7);`
> tests/suites/test_suite_pkcs7.function:72-92 (`pkcs7_parse_reuse`): shows `mbedtls_pkcs7_init(&pkcs7)` once, then repeated parse/free/parse cycles on the same struct — init is only needed once, but struct must be zero (or freed) before a fresh parse.
> Internally, `mbedtls_x509_crt_init(&signed_data->certs)` is also called by the parser itself before parsing certificates (library/pkcs7.c:518), so the caller does not need to pre-init the embedded x509 struct.

### P2.2 Parameter Construction
**Claim:** `pkcs7` is a pointer to a caller-allocated (stack or heap) `mbedtls_pkcs7` structure, must be non-NULL (checked) and should have been zero-initialized via `mbedtls_pkcs7_init`. `buf` is a pointer to the DER bytes (read-only, attacker controlled, any content); it is copied internally so caller may free/reuse it after the call. `buflen` is the exact length of the DER-encoded content — the function allocates `buflen` bytes with `mbedtls_calloc(1, buflen)` and treats buf[0..buflen) as the full outer SEQUENCE; it must not contain trailing garbage (checked against ASN.1 length).
**Evidence:**
> library/pkcs7.c:561-563: `if (pkcs7 == NULL) { return MBEDTLS_ERR_PKCS7_BAD_INPUT_DATA; }`
> library/pkcs7.c:565-572: `pkcs7->raw.p = p = mbedtls_calloc(1, buflen); ... memcpy(p, buf, buflen); pkcs7->raw.len = buflen;`
> include/mbedtls/pkcs7.h:161-167: "\param buf The buffer holding only the DER encoded PKCS #7 content. \param buflen The size in bytes of \p buf. The size must be exactly the length of the DER encoded PKCS #7 content. \note This function makes an internal copy of the PKCS #7 buffer \p buf... may be destroyed or reused after this call returns."
> Note: if `buflen==0`, `mbedtls_calloc(1,0)` may return NULL on some allocators causing `MBEDTLS_ERR_PKCS7_ALLOC_FAILED` rather than a parse attempt (library/pkcs7.c:566-570).

### P2.3 Object Lifecycle
**Claim:** Create (stack-allocate struct) → `mbedtls_pkcs7_init()` → `mbedtls_pkcs7_parse_der()` (fills `pkcs7->signed_data`, allocates `pkcs7->raw.p`, embedded cert(s) in `pkcs7->signed_data.certs`, and a linked list at `pkcs7->signed_data.signers`) → optionally use results (`mbedtls_pkcs7_signed_data_verify` / `mbedtls_pkcs7_signed_hash_verify`, or direct field reads) → `mbedtls_pkcs7_free()`. The structure may be reused for another `parse_der` call after a `free`, since free zeroizes the whole struct.
**Evidence:**
> library/pkcs7.c:784: `mbedtls_platform_zeroize(pkcs7, sizeof(*pkcs7));` at the end of `mbedtls_pkcs7_free`, making the struct safe to re-use for a subsequent `parse_der`/`init`.
> tests/suites/test_suite_pkcs7.function:85-94 (`pkcs7_parse_reuse`): parse → free → assert `pkcs7.raw.p == NULL` and `pkcs7.signed_data.signers.next == NULL` → parse again on the *same* struct (no re-`init` call needed because `free` zeroized it).
> library/pkcs7.c:635-637: on parse failure (`ret < 0`), the parser itself calls `mbedtls_pkcs7_free(pkcs7)` before returning — caller must not call free again expecting non-reentrant behavior issues, but a second free is safe because `raw.p==NULL` guard exists (line 766).

### P2.4 Return Value Handling
**Claim:** On success, returns the positive enum value `MBEDTLS_PKCS7_SIGNED_DATA` (value 2, from `mbedtls_pkcs7_type`). Any negative value is an error code (e.g. `MBEDTLS_ERR_PKCS7_INVALID_FORMAT`, `MBEDTLS_ERR_PKCS7_INVALID_VERSION`, `MBEDTLS_ERR_PKCS7_INVALID_ALG`, `MBEDTLS_ERR_PKCS7_INVALID_CERT`, `MBEDTLS_ERR_PKCS7_INVALID_SIGNER_INFO`, `MBEDTLS_ERR_PKCS7_FEATURE_UNAVAILABLE`, `MBEDTLS_ERR_PKCS7_BAD_INPUT_DATA`, `MBEDTLS_ERR_PKCS7_ALLOC_FAILED`). Caller must check `ret == MBEDTLS_PKCS7_SIGNED_DATA` (or `ret < 0`) before trusting `pkcs7->signed_data` fields / calling verify.
**Evidence:**
> library/pkcs7.c:632: `ret = MBEDTLS_PKCS7_SIGNED_DATA;` on success path after `pkcs7_get_signed_data` succeeds.
> include/mbedtls/pkcs7.h:170-171: "\return The \c mbedtls_pkcs7_type of \p buf, if successful. \return A negative error code on failure."
> tests/suites/test_suite_pkcs7.function:85-87, 92-94, 158-159: `TEST_EQUAL(mbedtls_pkcs7_parse_der(...), MBEDTLS_PKCS7_SIGNED_DATA);` and `TEST_EQUAL(..., res_expect)` for negative/error cases.
> library/pkcs7.c:635-637: `if (ret < 0) { mbedtls_pkcs7_free(pkcs7); }` — on any error the function already cleans up partial allocations (certs, signer list, raw buffer) internally before returning; the struct is left zeroed.

### P2.5 Cleanup Sequence
**Claim:** Caller must always call `mbedtls_pkcs7_free(&pkcs7)` exactly once after use (success or failure), which frees `raw.p` (the internal DER copy), the embedded x509 cert chain (`mbedtls_x509_crt_free`), the (empty/no-op) CRL (`mbedtls_x509_crl_free`), and walks + frees the `signers` linked list (each node's `issuer` name list via `pkcs7_free_signer_info`, then the node itself for all but the first, which is embedded in the struct). Calling free on an already-freed/zeroed struct is a safe no-op (guarded by `raw.p == NULL` check).
**Evidence:**
> library/pkcs7.c:761-785: `mbedtls_pkcs7_free()` body — `if (pkcs7 == NULL || pkcs7->raw.p == NULL) return;` then `mbedtls_free(pkcs7->raw.p); mbedtls_x509_crt_free(&pkcs7->signed_data.certs); mbedtls_x509_crl_free(&pkcs7->signed_data.crl);` then walks `signers.next` list freeing each with `pkcs7_free_signer_info` + `mbedtls_free`.
> include/mbedtls/pkcs7.h:241-242: "Unallocate all PKCS #7 data... It doesn't free \p pkcs7 itself. This should be done by the caller." — the struct itself (if heap-allocated) must be freed separately by the harness.
> library/pkcs7.c:635-637: internal error-path already calls `mbedtls_pkcs7_free` — a harness that also unconditionally calls `mbedtls_pkcs7_free` after `parse_der` returns exercises this double-free-safe guard (important given task description's focus on UAF/double-free in signer-list cleanup).

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase at the stated locations.
**Evidence:**
> include/mbedtls/pkcs7.h:155 `mbedtls_pkcs7_init`; :173 `mbedtls_pkcs7_parse_der`; :204 `mbedtls_pkcs7_signed_data_verify`; :236 `mbedtls_pkcs7_signed_hash_verify`; :246 `mbedtls_pkcs7_free`.
> library/pkcs7.c:35 `mbedtls_pkcs7_init`; :553 `mbedtls_pkcs7_parse_der`; :736 `mbedtls_pkcs7_signed_data_verify`; :747 `mbedtls_pkcs7_signed_hash_verify`; :761 `mbedtls_pkcs7_free`; :642 `mbedtls_pkcs7_data_or_hash_verify` (static, shared core); :451 `pkcs7_get_signed_data`; :170 `pkcs7_get_certificates`; :377 `pkcs7_get_signers_info_set`; :277 `pkcs7_get_signer_info`; :133 `pkcs7_get_digest_algorithm_set`; :84 `pkcs7_get_content_info_type` (all static, internal core, confirmed present).

### P2.7 Co-call Constraints
**Claim:** `mbedtls_pkcs7_init` / `mbedtls_pkcs7_free` is the only strict pair (init before any parse, free after use regardless of parse outcome). `mbedtls_pkcs7_parse_der` and `mbedtls_pkcs7_free` are also effectively paired because a successful parse allocates resources (raw buffer copy, cert, signer list) that only `free` releases; however `parse_der` itself invokes `free` internally on error, so the harness-level free call after an error is a redundant-but-safe second call. There is no explicit "begin/commit" transaction API; `signed_data_verify`/`signed_hash_verify` are read-only consumers, not paired lifecycle calls, and can be called 0+ times on the same parsed structure before `free`.
**Evidence:**
> library/pkcs7.c:766: `if (pkcs7 == NULL || pkcs7->raw.p == NULL) { return; }` — makes a second/no-op free safe, supporting repeated parse/free cycles as in `pkcs7_parse_reuse` test.
> tests/suites/test_suite_pkcs7.function:188-199: `mbedtls_pkcs7_signed_hash_verify`/`mbedtls_pkcs7_signed_data_verify` called in a loop over multiple certs against the *same* parsed `pkcs7`, showing verify is callable multiple times without re-parsing.

### P2.8 Prerequisite State
**Claim:** No external context/connection prerequisites beyond the zero-initialized `mbedtls_pkcs7` struct and the DER buffer. For the verify entries, a valid `mbedtls_x509_crt` (`cert`) must already be parsed/initialized (e.g. via `mbedtls_x509_crt_parse_der`/`_file`) and must have non-expired `valid_to`/`valid_from` fields relative to current time, since `mbedtls_pkcs7_data_or_hash_verify` checks `mbedtls_x509_time_is_past(&cert->valid_to)` / `mbedtls_x509_time_is_future(&cert->valid_from)`. The target `pkcs7` struct must already hold a successful parse result (`no_of_signers > 0`), otherwise verify returns `MBEDTLS_ERR_PKCS7_INVALID_CERT` immediately.
**Evidence:**
> library/pkcs7.c:655-657: `if (pkcs7->signed_data.no_of_signers == 0) { return MBEDTLS_ERR_PKCS7_INVALID_CERT; }`
> library/pkcs7.c:659-662: `if (mbedtls_x509_time_is_past(&cert->valid_to) || mbedtls_x509_time_is_future(&cert->valid_from)) { return MBEDTLS_ERR_PKCS7_CERT_DATE_INVALID; }`
> tests/suites/test_suite_pkcs7.function:163-166: certs are parsed via `mbedtls_x509_crt_parse_file(crts[i], crt_files_arr[i])` (an independently-supplied certificate, NOT the one embedded in the parsed pkcs7 struct) before calling verify — confirms `cert` is caller-supplied, separate from `pkcs7->signed_data.certs`.

### P2.9 Result Usage
**Claim:** After a successful `mbedtls_pkcs7_parse_der`, callers read fields of `pkcs7->signed_data` (e.g. `no_of_signers`, `signers` list, `digest_alg_identifiers`, `certs`) and/or pass the whole `pkcs7` struct plus an externally-supplied certificate and data/hash into `mbedtls_pkcs7_signed_data_verify` or `mbedtls_pkcs7_signed_hash_verify`. These verify functions iterate the `signers` linked list calling `mbedtls_pk_verify` on each signer's `sig` field until one succeeds.
**Evidence:**
> tests/suites/test_suite_pkcs7.function:161: `TEST_EQUAL(pkcs7.signed_data.no_of_signers, n_crts);` — direct field read after parse.
> tests/suites/test_suite_pkcs7.function:188-199: loop calling `mbedtls_pkcs7_signed_hash_verify(&pkcs7, crts[i], hash, ...)` / `mbedtls_pkcs7_signed_data_verify(&pkcs7, crts[i], data, datalen)` using the parsed `pkcs7` object.
> library/pkcs7.c:721-729 (`mbedtls_pkcs7_data_or_hash_verify`): `for (signer = &pkcs7->signed_data.signers; signer; signer = signer->next) { ret = mbedtls_pk_verify(&pk_cxt, md_alg, hash, ..., signer->sig.p, signer->sig.len); if (ret == 0) break; }` — consumes the signer linked list and `sig` buffer built by the parser.
> library/pkcs7.c:664: `mbedtls_oid_get_md_alg(&pkcs7->signed_data.digest_alg_identifiers, &md_alg);` — consumes the digest OID field filled during parse.

### P2.10 Behaviour Controls
**Claim:** Processing path is entirely driven by the DER content of `buf`, not by explicit function flags (no options struct). Key branch points: (1) presence of an OID wrapper vs bare SignedData SEQUENCE (`try_data` fallback on `MBEDTLS_ERR_ASN1_UNEXPECTED_TAG`); (2) the contentType OID value — only `MBEDTLS_OID_PKCS7_SIGNED_DATA` proceeds to `pkcs7_get_signed_data`, other known PKCS7 OIDs return `MBEDTLS_ERR_PKCS7_FEATURE_UNAVAILABLE`, unknown OIDs return `MBEDTLS_ERR_PKCS7_BAD_INPUT_DATA`; (3) `version` field must equal `MBEDTLS_PKCS7_SUPPORTED_VERSION` (1); (4) number of certificates in the optional `[0]` field: 0 is allowed (skipped), exactly 1 SEQUENCE parsed via `mbedtls_x509_crt_parse_der`, more than one (`end_cert != end_set`) → `MBEDTLS_ERR_PKCS7_FEATURE_UNAVAILABLE`; (5) number of SignerInfo entries: 0 allowed (`no_of_signers==0`), 1 parsed inline, >1 triggers the heap-allocated linked-list path in `pkcs7_get_signers_info_set`; (6) for verify: `is_data_hash` flag (0=`signed_data_verify` hashes `data` via `mbedtls_md`, 1=`signed_hash_verify` treats input as pre-hashed and requires `datalen == mbedtls_md_get_size(md_info)`); (7) compile-time `MBEDTLS_PKCS7_ALLOW_WEAK_SIGNATURES` gates whether `mbedtls_x509_profile_check_md_alg`/`check_pk_alg` against `mbedtls_x509_crt_profile_default` are enforced during verify.
**Evidence:**
> library/pkcs7.c:588-611: OID branch logic; :592-594 `try_data:` fallback for bare SignedData.
> library/pkcs7.c:71-73: `if (*ver != MBEDTLS_PKCS7_SUPPORTED_VERSION) { ret = MBEDTLS_ERR_PKCS7_INVALID_VERSION; }`
> library/pkcs7.c:180-182, 203-205: `0` certs returns via `MBEDTLS_ERR_ASN1_UNEXPECTED_TAG → return 0`; `end_cert != end_set → MBEDTLS_ERR_PKCS7_FEATURE_UNAVAILABLE`.
> library/pkcs7.c:392-395, 405-422: `len == 0` → `return 0` signers; else loop allocating extra `mbedtls_pkcs7_signer_info` nodes with `mbedtls_calloc`.
> library/pkcs7.c:694-702: `if (is_data_hash) { ... memcpy(hash, data, datalen) ... } else { ret = mbedtls_md(md_info, data, datalen, hash); }`
> library/pkcs7.c:669-681: `#if !defined(MBEDTLS_PKCS7_ALLOW_WEAK_SIGNATURES)` guarded profile checks.

### P2.11 Input Expectations
**Claim:** Input must be a DER-encoded (not BER) buffer whose length (`buflen`) exactly matches the content (no trailing/missing bytes: multiple explicit length-mismatch checks). It must start with a `SEQUENCE` tag (outer ContentInfo or bare SignedData). If a contentType OID is present, it must be followed by an EXPLICIT `[0]` context-tag whose length exactly spans to `end`. Inside SignedData: version INTEGER must decode and equal 1; DigestAlgorithmIdentifiers is a SET expected to contain exactly one AlgorithmIdentifier (trailing bytes → `MBEDTLS_ERR_PKCS7_FEATURE_UNAVAILABLE`); the recognized digest OID must map via `mbedtls_oid_get_md_alg`; contentInfo content tag (if any non-empty `p != end_content_info`) is treated as unsupported; optional certificates field is `[0] IMPLICIT` constructed context-specific tag wrapping exactly one certificate SEQUENCE consumed entirely (`end_cert == end_set`); SignerInfos is a SET (possibly empty, length 0) of SEQUENCEs, each containing version INTEGER, issuerAndSerialNumber SEQUENCE (name + serial), digestAlgorithm (must byte-for-byte match the SignedData-level digest alg), encryptionAlgorithm, and an OCTET STRING signature; no unauthenticated/authenticated attributes are permitted (any leftover bytes after signature cause `MBEDTLS_ERR_PKCS7_INVALID_SIGNER_INFO`); after SignedData, no trailing bytes are permitted in the outer buffer.
**Evidence:**
> include/mbedtls/pkcs7.h:19-20: "The RFC specifies support for BER. This implementation is limited to DER only."
> library/pkcs7.c:582-586, 467-470, 546-548: three separate `(end - p) != len` / `p != end` length-mismatch checks (`MBEDTLS_ERR_ASN1_LENGTH_MISMATCH`).
> library/pkcs7.c:61-76 (`pkcs7_get_version`): INTEGER parse + equality check to `MBEDTLS_PKCS7_SUPPORTED_VERSION`.
> library/pkcs7.c:140-158 (`pkcs7_get_digest_algorithm_set`): SET tag, one `AlgorithmIdentifier`, then `if (*p != end) return MBEDTLS_ERR_PKCS7_FEATURE_UNAVAILABLE;`.
> library/pkcs7.c:500-514: contentInfo non-empty-content rejection path.
> library/pkcs7.c:337-343: digest-alg-identifier-must-match check in `pkcs7_get_signer_info`.
> library/pkcs7.c:356-359: "Do not permit any unauthenticated attributes" — `*p != end_signer` check.

## Other entries (combined with the target in one harness)

### mbedtls_pkcs7_init
- **Call protocol:** No prerequisites; takes a pointer to a (typically stack) `mbedtls_pkcs7` struct and zero-fills it (`memset`). Must be called exactly once before the first `parse_der` on a struct that was not already zero/freed. Returns `void`.
- **Evidence:** library/pkcs7.c:35-38; include/mbedtls/pkcs7.h:150-155.
- **Combination:** `mbedtls_pkcs7_init(&pkcs7);` then feed fuzz buffer to `mbedtls_pkcs7_parse_der(&pkcs7, data, size);`.

### mbedtls_pkcs7_signed_data_verify
- **Call protocol:** Requires (a) `pkcs7` already populated by a *successful* `mbedtls_pkcs7_parse_der` call (return value `MBEDTLS_PKCS7_SIGNED_DATA`) — calling it on a struct that failed to parse or was only `init`'d returns `MBEDTLS_ERR_PKCS7_INVALID_CERT` because `no_of_signers==0`; (b) a separately-parsed, non-NULL `mbedtls_x509_crt *cert` (e.g. via `mbedtls_x509_crt_parse_der`), with `valid_from`/`valid_to` fields such that current time falls within them (relevant since `MBEDTLS_PLATFORM_TIME_ALT` is set in build.sh for repeatable time); (c) non-NULL `data` (checked, else `MBEDTLS_ERR_PKCS7_BAD_INPUT_DATA`). Internally hashes `data` with `mbedtls_md` using the OID-selected digest algorithm, then calls `mbedtls_pk_verify` per signer. Returns 0 on any-signer success, else `MBEDTLS_ERR_PKCS7_VERIFY_FAIL`/other negative codes. Allocates a transient `hash` buffer internally and frees it itself before returning — no caller cleanup needed for that buffer.
- **Evidence:** library/pkcs7.c:736-745, 642-733 (shared core `mbedtls_pkcs7_data_or_hash_verify`); include/mbedtls/pkcs7.h:176-207; tests/suites/test_suite_pkcs7.function:155-199.
- **Combination:** After `parse_der` succeeds and a cert is available (harness must construct/parse a cert, e.g. via `mbedtls_x509_crt_parse_der` on fuzz-derived or fixed bytes), call `mbedtls_pkcs7_signed_data_verify(&pkcs7, cert, data, datalen)`. No separate free needed for its own internals; `pkcs7` is freed later via `mbedtls_pkcs7_free`.

### mbedtls_pkcs7_signed_hash_verify
- **Call protocol:** Same prerequisites as `signed_data_verify`, except `hash` must be non-NULL (checked) and its length `hashlen` must exactly equal `mbedtls_md_get_size(md_info)` for the digest algorithm encoded in the parsed structure, otherwise `MBEDTLS_ERR_PKCS7_VERIFY_FAIL` (library/pkcs7.c:695-699). It is an alternative to `signed_data_verify`, not meant to be called in sequence with it on the same input semantics, though both can be called on the same parsed `pkcs7`/`cert` in a harness (as the project's own test does for a mode flag).
- **Evidence:** library/pkcs7.c:747-756, 694-699; tests/suites/test_suite_pkcs7.function:182-193 (`do_hash_alg` flag selects between the two verify calls).
- **Combination:** Mutually exclusive-by-design alternative call after a successful parse; harness may call either or both using the same `pkcs7`/`cert`.

### mbedtls_pkcs7_free
- **Call protocol:** Must be called exactly once per "session" of a parsed/attempted struct (extra calls are safe no-ops due to the `raw.p == NULL` guard). Must be called even if `parse_der` failed, although the parser already self-invokes it internally on failure (library/pkcs7.c:635-637) — a harness should still call it unconditionally after use to mirror real usage and to exercise any error-path cleanup bugs (double-free/UAF) as noted in the task description. Takes only the `pkcs7` pointer; returns `void`.
- **Evidence:** library/pkcs7.c:761-785; tests/suites/test_suite_pkcs7.function:27, 89, 99, 208.
- **Combination:** Always the last call in the harness per iteration: `mbedtls_pkcs7_free(&pkcs7);` after parse (+ optional verify calls).

## Build notes

**Claim:** Fuzzer sources for this project are plain C, compiled/linked via the project's CMake build (`programs/fuzz/CMakeLists.txt`), including the target's header via the normal installed include path `mbedtls/pkcs7.h` (resolved through the project's `include/` directory added by the top-level CMake include dirs), and linked against the `mbedtls`/`mbedtlsx509`/`mbedtlscrypto` static libraries (via the `mbedtls_target` CMake alias) plus libFuzzer/`FuzzingEngine` for the OSS-Fuzz build.

**Evidence:**
> programs/fuzz/CMakeLists.txt:1-4: `set(libs ${mbedtls_target} ${CMAKE_THREAD_LIBS_INIT})`
> programs/fuzz/CMakeLists.txt:11-17: `fuzz_pkcs7` listed in `executables_no_common_c` (built as plain C with `exe_sources = ${exe}.c ...` and `onefile.c` when not using an external fuzzing-engine lib).
> programs/fuzz/CMakeLists.txt:42-51: `add_executable(${exe} ${exe_sources})`; links `${libs}` or `${libs} FuzzingEngine` and sets `LINKER_LANGUAGE CXX` when `FuzzingEngine` is found (OSS-Fuzz build links libFuzzer as C++ driver even though the harness source itself is `.c`).
> programs/fuzz/CMakeLists.txt:43-44: include dirs added are only test-helper dirs (`framework/tests/include`, `tests/include`); the library's public headers (`mbedtls/pkcs7.h`) are made available through the top-level project's global include directories set by the main `CMakeLists.txt` (standard `include/` of the mbedtls source tree), consistent with `#include "mbedtls/pkcs7.h"` used throughout `library/pkcs7.c:9` and `tests/suites/test_suite_pkcs7.function:3`.
> ${QF_WORK}/ossfuzz_view/mbedtls-c1/build.sh:18-30: `scripts/config.py full; ... cmake -DENABLE_TESTING=ON ..; make -j$(nproc) all; cp programs/fuzz/fuzz_* $OUT/` — confirms the OSS-Fuzz `build.sh` builds the fuzz binaries (including `fuzz_pkcs7`) via the project's own CMake build and copies the resulting executables to `$OUT`.
> ${QF_WORK}/ossfuzz_view/mbedtls-c1/Dockerfile:27: clones `mbedtls-3.6` branch, confirming the API surface matches this checked-out source tree (`mbedtls_pkcs7_parse_der` etc. all present as verified above).
> Current repository's `programs/fuzz/fuzz_pkcs7.c` is a stub ("Gold harness replaced by an empty stub for baseline generation") that only keeps the symbol `mbedtls_pkcs7_parse_der` linked via an `__asm__` alias and does not call it — the downstream harness generator must write the real `LLVMFuzzerTestOneInput` body following the P2 sequence documented above (init → parse_der(data,size) → optional verify → free).