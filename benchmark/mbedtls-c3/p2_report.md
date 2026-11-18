## target: mbedtls_x509_crt_parse

### P2.1 Init Sequence
**Claim:** The caller must first allocate a `mbedtls_x509_crt` object (usually on the stack or heap) and zero-initialize it with `mbedtls_x509_crt_init()` before any call to `mbedtls_x509_crt_parse` (and its variants `_der`, `_der_nocopy`, `_der_with_ext_cb`). No other library-wide init is strictly required for the parse itself, but if `MBEDTLS_USE_PSA_CRYPTO` is enabled, `psa_crypto_init()` must be called first (documented, not conditionally checked at runtime).
**Evidence:**
> include/mbedtls/x509_crt.h:511-513: "\note If #MBEDTLS_USE_PSA_CRYPTO is enabled, the PSA crypto subsystem must have been initialized by calling psa_crypto_init() before calling this function."
> library/x509_crt.c:3229-3232: `void mbedtls_x509_crt_init(mbedtls_x509_crt *crt) { memset(crt, 0, sizeof(mbedtls_x509_crt)); }`
> programs/fuzz/fuzz_client.c:50-51: `mbedtls_x509_crt_init(&cacert); if (mbedtls_x509_crt_parse(&cacert, ...`
> library/x509_crt.c:1335-1353: `mbedtls_x509_crt_parse_der_internal` walks `chain` expecting `crt->version` to be 0 for an unused/uninitialized slot (what `mbedtls_x509_crt_init` guarantees via the memset), and allocates+inits a new tail node with `mbedtls_x509_crt_init(crt->next)` when the slot is already used — confirming `init()` establishes the required "version==0 means empty slot" invariant.

### P2.2 Parameter Construction
**Claim:** `chain` is a non-NULL pointer to an initialized (zeroed) `mbedtls_x509_crt`; `buf` is a non-NULL pointer to the certificate bytes (attacker-controlled fuzz data is fine here); `buflen` is the exact byte length of `buf`. For PEM input, `buflen` must include the terminating NUL byte, and `buf[buflen-1]` must actually be `'\0'`, otherwise the function silently treats the data as DER.
**Evidence:**
> library/x509_crt.c:1411-1413: `if (chain == NULL || buf == NULL) { return MBEDTLS_ERR_X509_BAD_INPUT_DATA; }`
> library/x509_crt.c:1420-1423: `if (buflen != 0 && buf[buflen - 1] == '\0' && strstr((const char *) buf, "-----BEGIN CERTIFICATE-----") != NULL) { buf_format = MBEDTLS_X509_FORMAT_PEM; }`
> include/mbedtls/x509_crt.h:520-521: "\param buflen The size of \p buf, including the terminating \c NULL byte in case of PEM encoded data."

### P2.3 Object Lifecycle
**Claim:** Create (`mbedtls_x509_crt_init`) → parse one-or-more times into the same chain (`mbedtls_x509_crt_parse` / `_der` / `_der_nocopy` / `_der_with_ext_cb`, which may be called repeatedly to append more certs) → use (`mbedtls_x509_crt_info`, `mbedtls_x509_crt_verify_with_profile`, key-usage checks, DN walking, etc.) → destroy (`mbedtls_x509_crt_free`). `mbedtls_x509_crt_free` must be called exactly once on the *head* of the chain; it walks and frees all linked nodes.
**Evidence:**
> library/x509_crt.c:3237-3267: `mbedtls_x509_crt_free` loop: `while (cert_cur != NULL) { mbedtls_pk_free(...); ...; cert_cur = cert_cur->next; ... if (cert_prv != crt) mbedtls_free(cert_prv); }` — frees internal buffers for every node and `free()`s every node except the head (caller owns the head storage).
> library/x509_crt.c:1335-1353: parse appends new nodes to the tail of an existing chain, supporting repeated parse calls on the same `chain` object (multi-cert loading pattern).
> programs/fuzz/fuzz_client.c:50 and tests/src/test_helpers/ssl_helpers.c:643-700: real callers call `mbedtls_x509_crt_init` once, then one or more `mbedtls_x509_crt_parse` calls, before use/verify, and free once at teardown (see ssl_helpers / client teardown code, not shown here but following this convention throughout the codebase).

### P2.4 Return Value Handling
**Claim:** `mbedtls_x509_crt_parse` returns 0 if every (PEM) cert parsed OK; for PEM a **positive** value is the count of certs that failed while at least one succeeded (not a fatal error — chain still usable for the succeeded ones); a **negative** value is a hard error (bad input, alloc failure, or "no certificate could be parsed", or `MBEDTLS_ERR_X509_CERT_UNKNOWN_FORMAT` when nothing looked like PEM or DER). `mbedtls_x509_crt_parse_der*` return 0 or a negative error only (single cert, no "partial success" concept).
**Evidence:**
> include/mbedtls/x509_crt.h:523-526: "\return 0 if all certificates were parsed successfully. \return The (positive) number of certificates that couldn't be parsed... \return A negative X509 or PEM error code otherwise."
> library/x509_crt.c:1499-1505: `if (success) { return total_failed; } else if (first_error) { return first_error; } else { return MBEDTLS_ERR_X509_CERT_UNKNOWN_FORMAT; }`
> library/x509_crt.c:1479-1485: on a per-cert parse error inside the PEM loop, `if (ret == MBEDTLS_ERR_X509_ALLOC_FAILED) { return ret; }` (abort immediately on OOM) else accumulate and continue.
> library/x509_crt.c:1356-1366 (`mbedtls_x509_crt_parse_der_internal`): on failure, unlinks the half-built tail node and frees it, returning the negative `ret` from `x509_crt_parse_der_core`.

### P2.5 Cleanup Sequence
**Claim:** On success: eventually call `mbedtls_x509_crt_free(&chain_head)` once to release every appended node's `pk`, `sig_opts`, DN/extension linked lists, and `raw.p` buffer (if `own_buffer`). On failure inside the core parser, the failing node is already freed internally by `x509_crt_parse_der_core`/`mbedtls_x509_crt_parse_der_internal` (via `mbedtls_x509_crt_free(crt)` on that node and detaching it from `prev->next`); the caller still must free whatever nodes *did* succeed earlier in the chain/`chain` head. There is no separate "free on error only" step the caller must perform beyond the normal, single `mbedtls_x509_crt_free(chain)` call, because the library keeps the chain structurally consistent after any parse error.
**Evidence:**
> library/x509_crt.c:1113-1116, 1143-1146, 1161-1309 (repeated pattern throughout `x509_crt_parse_der_core`): `mbedtls_x509_crt_free(crt); return ret;` on every ASN.1 parse failure for that node.
> library/x509_crt.c:1356-1365: `if (ret != 0) { if (prev) prev->next = NULL; if (crt != chain) mbedtls_free(crt); return ret; }` — detaches and frees the failed tail node, leaving `chain` (and any previously successful nodes) intact and still owned by caller.
> library/x509_crt.c:3237-3267: single `mbedtls_x509_crt_free` call walks the whole chain.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current source tree (`library/x509_crt.c`, `include/mbedtls/x509_crt.h`).
**Evidence:**
> library/x509_crt.c:1371 `mbedtls_x509_crt_parse_der_nocopy`
> library/x509_crt.c:1378 `mbedtls_x509_crt_parse_der_with_ext_cb`
> library/x509_crt.c:1388 `mbedtls_x509_crt_parse_der`
> library/x509_crt.c:1399 `mbedtls_x509_crt_parse` (target)
> library/x509_crt.c:1513 `mbedtls_x509_crt_parse_file`
> library/x509_crt.c:1756 `mbedtls_x509_crt_info`
> library/x509_crt.c:3176 `mbedtls_x509_crt_verify_with_profile`
> library/x509_crt.c:3229 `mbedtls_x509_crt_init`
> library/x509_crt.c:3237 `mbedtls_x509_crt_free`
> library/x509_crt.c:1078 `x509_crt_parse_der_core` (static, internal core)

### P2.7 Co-call Constraints
**Claim:** `mbedtls_x509_crt_init` **must** pair with exactly one terminal `mbedtls_x509_crt_free` on the same head pointer (init/free pair, like open/close). `mbedtls_x509_crt_parse*` calls do not need an individual matching "end" call — they may be invoked multiple times on the same chain to append more certs before the single final `free`. `mbedtls_x509_crt_verify_with_profile` requires a non-NULL `profile` argument (checked) and pairs logically with having previously parsed both the end-entity `crt` and a `trust_ca` chain (also parsed via `mbedtls_x509_crt_parse`).
**Evidence:**
> library/x509_crt.c:3229-3232 vs 3237-3267: init zeroes one struct; free walks/frees a whole chain — asymmetric but intentional (init called once per node is not needed; only the head needs explicit `init`, subsequent nodes are `mbedtls_calloc`+`mbedtls_x509_crt_init`'d internally, see line 1344-1351).
> library/x509_crt.c:3086-3089: `if (profile == NULL) { ret = MBEDTLS_ERR_X509_BAD_INPUT_DATA; goto exit; }` inside `x509_crt_verify_restartable_ca_cb` (called by `mbedtls_x509_crt_verify_with_profile`).

### P2.8 Prerequisite State
**Claim:** Before `mbedtls_x509_crt_parse`, only an initialized `chain` object is required (no network/TLS context, no RNG). Before `mbedtls_x509_crt_verify_with_profile`, both the `crt` to verify and (optionally) a `trust_ca` chain must already be parsed via `mbedtls_x509_crt_parse`/`_der`, and a `mbedtls_x509_crt_profile` must exist (the library provides `mbedtls_x509_crt_profile_default`, `_default_full`, `_next`, `_suiteb` globals, or caller can build a custom one). Before `mbedtls_x509_crt_info`, `crt` must be a successfully-parsed (or at least initialized) certificate; the function tolerates `crt == NULL` by printing "Certificate is uninitialised!" and returning.
**Evidence:**
> library/x509_crt.c:1767-1772: `if (NULL == crt) { ret = mbedtls_snprintf(p, n, "\nCertificate is uninitialised!\n"); ...; return (int)(size - n); }`
> library/x509_crt.c:3086-3089: profile NULL check as above.
> library/x509_crt.c:3159-3171 (`mbedtls_x509_crt_verify`) and 3176-3188 (`mbedtls_x509_crt_verify_with_profile`): both take `crt` and `trust_ca` (mbedtls_x509_crt chains), consistent with "parse end-entity and CA chain first, then verify."

### P2.9 Result Usage
**Claim:** The filled `mbedtls_x509_crt *chain` / its fields are consumed by: (a) `mbedtls_x509_crt_info` to print human-readable info (DN strings via `mbedtls_x509_dn_gets`, serial via `mbedtls_x509_serial_gets`, extension flags via `crt->ext_types`); (b) `mbedtls_x509_crt_verify_with_profile` / `mbedtls_x509_crt_verify` (chain building/signature checking via internal `x509_crt_verify_chain`, key/usage checks via `mbedtls_x509_crt_check_key_usage`/`check_extended_key_usage`); (c) `mbedtls_ssl_conf_ca_chain` / `mbedtls_ssl_conf_own_cert` in the TLS layer consume the parsed chain as CA store or own certificate (seen in fuzz_client.c / ssl_helpers.c); (d) eventually `mbedtls_x509_crt_free` to release it.
**Evidence:**
> library/x509_crt.c:1786-1791 (`mbedtls_x509_crt_info`): `mbedtls_x509_dn_gets(p, n, &crt->issuer); ... mbedtls_x509_dn_gets(p, n, &crt->subject);`
> library/x509_crt.c:1829-1838: `if (crt->ext_types & MBEDTLS_X509_EXT_BASIC_CONSTRAINTS) { ... crt->ca_istrue ... crt->max_pathlen ... }`
> library/x509_crt.c:3097-3110 (`x509_crt_verify_restartable_ca_cb`, called by `mbedtls_x509_crt_verify_with_profile`): `pk_type = mbedtls_pk_get_type(&crt->pk); ... ret = x509_crt_verify_chain(crt, trust_ca, ca_crl, ...);` — directly consumes the `pk` and chain structure produced by the parse.
> programs/fuzz/fuzz_client.c:50-51 and tests/src/test_helpers/ssl_helpers.c:651-655: parsed `cacert`/`cert->cert` objects are subsequently handed to SSL config functions (`mbedtls_ssl_conf_ca_chain`, `mbedtls_ssl_conf_own_cert`, not shown in excerpt but standard pattern in this codebase) as the next consumers.

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting parameters/flags:
- `mbedtls_x509_crt_parse` auto-detects PEM vs DER from the trailing NUL + `"-----BEGIN CERTIFICATE-----"` substring (not a caller flag, but attacker-controlled via buffer content) — selects the PEM loop vs direct DER path.
- `make_copy` (1 in `_der`, 0 in `_der_nocopy`, caller-chosen in `_der_with_ext_cb`) selects whether `crt->raw.p` is a private heap copy (`own_buffer=1`, freed by `mbedtls_x509_crt_free`) or points directly into the caller's `buf` (`own_buffer=0`, **not** freed, caller must keep `buf` alive for the chain's lifetime).
- `cb`/`p_ctx` (ext callback) in `_der_with_ext_cb` lets caller intercept OIDs unknown to `mbedtls_oid_get_x509_ext_type` inside `x509_get_crt_ext`, instead of the default "ignore if non-critical / error if critical" behaviour.
- `crt->version` (parsed 0/1/2 mapped to v1/v2/v3, rejecting any other raw value) gates whether issuer/subject unique IDs and the extensions block are permitted/parsed at all.
- For `mbedtls_x509_crt_verify_with_profile`: the `profile` argument (`mbedtls_x509_crt_profile_default`, `_default_full`, `_next`, `_suiteb`, or custom) selects allowed hash/PK/curve/RSA-bitlen policies; `cn` (non-NULL) enables hostname/name matching; `f_vrfy`/`p_vrfy` callback can override/inspect per-cert flags.
**Evidence:**
> library/x509_crt.c:1420-1429: PEM/DER auto-detection as quoted in P2.2.
> library/x509_crt.c:1120-1135: `if (make_copy != 0) { crt->raw.p = p = mbedtls_calloc(...); ...; crt->own_buffer = 1; } else { crt->raw.p = (unsigned char *) buf; crt->own_buffer = 0; }`
> library/x509_crt.c:1371-1392 (nocopy/der/with_ext_cb wrappers) showing the differing `make_copy`/`cb` arguments passed to `mbedtls_x509_crt_parse_der_internal`.
> library/x509_crt.c:1166-1171: `if (crt->version < 0 || crt->version > 2) { mbedtls_x509_crt_free(crt); return MBEDTLS_ERR_X509_UNKNOWN_VERSION; } crt->version++;`
> library/x509_crt.c:3086-3110: `profile` and `cn` parameters of `x509_crt_verify_restartable_ca_cb` used for policy checks and name verification.

### P2.11 Input Expectations
**Claim:** Input is either (a) a single DER-encoded `Certificate ::= SEQUENCE { tbsCertificate, signatureAlgorithm, signatureValue }` ASN.1 blob with `buflen` exactly spanning it, or (b) one-or-more PEM blocks delimited by `-----BEGIN CERTIFICATE-----`/`-----END CERTIFICATE-----`, concatenated and NUL-terminated, with `buflen` including the final NUL. Trailing garbage after a single DER cert is accepted only if it exactly matches the outer SEQUENCE length (checked via ASN.1 tag/length parsing); core parser enforces `version` in {0,1,2}, and every nested TLV (serial, alg id, Names, dates, SPKI, unique IDs, extensions) is walked with explicit `mbedtls_asn1_get_tag`/length checks that bound-check against `end`.
**Evidence:**
> library/x509_crt.c:1106-1116: `/* Certificate ::= SEQUENCE { tbsCertificate ..., signatureAlgorithm ..., signatureValue BIT STRING } */ if ((ret = mbedtls_asn1_get_tag(&p, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE)) != 0) { mbedtls_x509_crt_free(crt); return MBEDTLS_ERR_X509_INVALID_FORMAT; }`
> library/x509_crt.c:1166-1169: version range check as above.
> library/x509_crt.c:1305-1309: `if (p != end) { mbedtls_x509_crt_free(crt); return MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_FORMAT, MBEDTLS_ERR_ASN1_LENGTH_MISMATCH); }` — enforces exact consumption of the outer SEQUENCE.
> include/mbedtls/x509_crt.h:517-521: PEM may be a concatenation; DER "buffer must comprise exactly one certificate."
> library/x509_crt.c:1438-1473: PEM loop re-invokes `mbedtls_pem_read_buffer` repeatedly, advancing `buf`/`buflen` by `use_len` each iteration, stopping at `MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT` or `buflen <= 1`.

## Other entries: call protocol and combination with the target

### mbedtls_x509_crt_parse_der
**Needs before it:** an initialized `mbedtls_x509_crt *chain` (same as target).
**Parameters/flags:** `buf`/`buflen` must be exactly one DER `Certificate` SEQUENCE (no PEM auto-detect, no loop). Internally forces `make_copy=1`.
**Return:** 0 or negative error only (library/x509_crt.c:1388-1393).
**Cleanup:** same `mbedtls_x509_crt_free(chain)`.
**Combination with target:** `mbedtls_x509_crt_parse` itself calls `mbedtls_x509_crt_parse_der` once per extracted PEM block (library/x509_crt.c:1475) or directly when the whole buffer is DER (line 1426/1429); a harness can call the target directly rather than this lower-level variant to exercise both DER and PEM paths through one entry point.
**Evidence:** library/x509_crt.c:1388-1393.

### mbedtls_x509_crt_parse_der_nocopy
**Needs before it:** initialized `chain`; **caller must keep `buf` alive and unmodified for the entire lifetime of the resulting chain node**, since `crt->raw.p` aliases it (`own_buffer=0`, not freed by `mbedtls_x509_crt_free`).
**Parameters/flags:** same DER-only input requirement as `_der`; no PEM support.
**Return:** 0 or negative (library/x509_crt.c:1371-1376, delegates to `mbedtls_x509_crt_parse_der_internal(..., make_copy=0, ...)`).
**Cleanup:** `mbedtls_x509_crt_free(chain)` still frees `pk`/DN lists/etc. but **not** `raw.p` (library/x509_crt.c:3256-3258: `if (cert_cur->raw.p != NULL && cert_cur->own_buffer) { mbedtls_zeroize_and_free(...); }`) — harness must separately manage/free the original `buf`.
**Combination with target:** independent alternate entry exercising the "no copy" lifetime path; not called by the target itself. A harness exercising it must allocate `buf` on the heap (not a stack buffer that goes out of scope) so it outlives the chain's use.

### mbedtls_x509_crt_parse_der_with_ext_cb
**Needs before it:** initialized `chain`; caller supplies `cb` (`mbedtls_x509_crt_ext_cb_t`) and `p_ctx`, plus `make_copy` flag chosen by caller.
**Parameters/flags:** Same DER-only input; `cb` is invoked from `x509_get_crt_ext` for extension OIDs not recognized by `mbedtls_oid_get_x509_ext_type`, letting the harness supply custom handling (accept/reject unknown critical extensions) instead of the library default.
**Return:** 0 or negative (library/x509_crt.c:1378-1386).
**Cleanup:** depends on `make_copy` as above.
**Combination with target:** independent alternate entry; not invoked by the target. Useful to exercise the custom-extension dispatch branch of the core parser (`x509_get_crt_ext`) that the plain target cannot reach.

### mbedtls_x509_crt_parse_file
**Needs before it:** initialized `chain`; `MBEDTLS_FS_IO` must be enabled (library/x509_crt.c:1509 `#if defined(MBEDTLS_FS_IO)`); a filesystem path.
**Parameters/flags:** `path` is a filename; internally reads via `mbedtls_pk_load_file` then calls the target `mbedtls_x509_crt_parse(chain, buf, n)` (library/x509_crt.c:1519-1523).
**Return:** same contract as `mbedtls_x509_crt_parse` (0 / positive failed-count / negative error), plus propagates `mbedtls_pk_load_file` errors.
**Cleanup:** zeroizes and frees its internal `buf` itself (`mbedtls_zeroize_and_free(buf, n)` at line 1525); caller still must `mbedtls_x509_crt_free(chain)` eventually.
**Combination with target:** thin file-reading front end to the target; **not relevant for a buffer-based fuzz harness** since OSS-Fuzz libFuzzer input is an in-memory buffer, not a file path — a harness would need to write fuzz data to a temp file to exercise this, which is usually out of scope/unnecessary when the target itself is already covered.

### mbedtls_x509_crt_info
**Needs before it:** a `crt` that is at least `mbedtls_x509_crt_init`'d (tolerates NULL specially) — typically the result of a prior `mbedtls_x509_crt_parse*` call; a caller-provided `buf`/`size` output buffer and `prefix` string.
**Parameters/flags:** `size` bounds output; function uses `MBEDTLS_X509_SAFE_SNPRINTF` macro internally to avoid overflow and returns early (negative) if the buffer is too small.
**Return:** `(int)(size - n)` bytes written on success, or a negative/propagated error (e.g., from `mbedtls_x509_key_size_helper`) on failure (library/x509_crt.c:1816-1819).
**Cleanup:** none (read-only consumer); does not free `crt`.
**Combination with target:** typical harness flow is `mbedtls_x509_crt_init` → `mbedtls_x509_crt_parse(chain, data, size)` → if `ret >= 0`, call `mbedtls_x509_crt_info(info_buf, sizeof(info_buf), "", chain)` to exercise the field-printing/consumer code (DN, serial, extensions, key size) → `mbedtls_x509_crt_free(chain)`.
**Evidence:** library/x509_crt.c:1756-1823.

### mbedtls_x509_crt_verify_with_profile
**Needs before it:** a parsed end-entity `crt` (via target), optionally a parsed `trust_ca` chain (via target again, on a second `mbedtls_x509_crt` object) and optionally a `mbedtls_x509_crl *ca_crl`; a non-NULL `profile` (e.g. `&mbedtls_x509_crt_profile_default`, declared in x509_crt.h); an output `uint32_t *flags`.
**Parameters/flags:** `cn` (char*, may be NULL to skip name check) selects whether hostname matching (`x509_crt_verify_name`) runs; `f_vrfy`/`p_vrfy` optional callback to inspect/override flags per certificate in the built chain.
**Return:** 0 on success with `*flags==0`; on any check failure `*flags` holds `MBEDTLS_X509_BADCERT_*`/`MBEDTLS_X509_BADCRL_*` bits and return is `MBEDTLS_ERR_X509_CERT_VERIFY_FAILED` or a fatal negative error (`*flags` forced to `(uint32_t)-1` in that case) (library/x509_crt.c:3139-3152).
**Cleanup:** internally frees its own scratch structures (`ver_chain.trust_ca_cb_result`, restart context) at `exit:` (library/x509_crt.c:3122-3134); does **not** free `crt`/`trust_ca` — those remain caller-owned and must still be `mbedtls_x509_crt_free`'d by the harness.
**Combination with target:** natural pipeline is: target-parse end-entity cert into `crt`, target-parse CA cert(s) into `trust_ca`, then call `mbedtls_x509_crt_verify_with_profile(crt, trust_ca, NULL, profile, cn_or_NULL, &flags, NULL, NULL)` to exercise `x509_crt_verify_chain`/`mbedtls_x509_dn_gets`/key-usage checks downstream of the parsed fields, then free both chains.
**Evidence:** library/x509_crt.c:3176-3188 (wrapper), 3080-3153 (`x509_crt_verify_restartable_ca_cb` core logic it delegates to).

## Build notes
**Claim:** The project already ships a dedicated libFuzzer harness `programs/fuzz/fuzz_x509crt.c` for this exact target, but in this checkout it has been replaced by an empty stub that only keeps the symbol linked via `__asm__` aliasing — it performs no actual parsing. The real OSS-Fuzz build (`build.sh`) compiles all `programs/fuzz/fuzz_*.c` files as part of the CMake build (language: C, linked against the full mbedtls static libs built with `scripts/config.py full`), then copies the resulting binaries (`cp programs/fuzz/fuzz_* $OUT/`) plus seed corpora/options. A real harness should `#include "mbedtls/x509_crt.h"` (and `mbedtls/x509_crt.h` is found via the standard `include/` path already used by the in-tree build system/CMake, no custom include path needed since `programs/fuzz/*` is built inside the same CMake project).
**Evidence:**
> programs/fuzz/fuzz_x509crt.c:1-16 (current stub, with the `qf_target_0` asm-alias trick to keep `mbedtls_x509_crt_parse` linked without calling it — this is a placeholder, NOT real harness logic for the downstream generator to copy).
> ossfuzz_view/mbedtls-c3/build.sh:25-30: `mkdir build; cd build; cmake -DENABLE_TESTING=ON ..; make -j$(nproc) all; cp programs/fuzz/fuzz_* $OUT/`
> ossfuzz_view/mbedtls-c3/build.sh:40: `zip -r fuzz/fuzz_x509crt_seed_corpus.zip ../framework/data_files/*.crt ../framework/data_files/dir*/*.crt fuzz/corpuses/x509/` — confirms a seed corpus of real `.crt` DER/PEM files is provided specifically for this fuzzer name.
> ossfuzz_view/mbedtls-c3/build.sh:18-24: `scripts/config.py full` plus explicit `unset MBEDTLS_USE_PSA_CRYPTO` for the primary build, and a second build variant (`build-usepsa`) with `MBEDTLS_USE_PSA_CRYPTO` set — two ABI/behavior configurations are both exercised (`usepsa-fuzz_*` binaries), relevant since `MBEDTLS_USE_PSA_CRYPTO` affects some X.509 crypto backends per the function's own doc note (P2.1).