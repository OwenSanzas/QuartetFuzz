## target: mbedtls_x509_crl_parse

### P2.1 Init Sequence
**Claim:** The chain object `mbedtls_x509_crl *chain` must be initialized with `mbedtls_x509_crl_init()` (a plain `memset(0)`) before the first call to `mbedtls_x509_crl_parse`/`mbedtls_x509_crl_parse_der`/`mbedtls_x509_crl_parse_file`. If `MBEDTLS_USE_PSA_CRYPTO` is enabled, `psa_crypto_init()` must have been called first (documented, not enforced by the function itself). No other library-wide init is required for the plain (non-PSA) build.
**Evidence:**
> include/mbedtls/x509_crl.h:98-100: "\note If #MBEDTLS_USE_PSA_CRYPTO is enabled, the PSA crypto subsystem must have been initialized by calling psa_crypto_init() before calling this function."
> library/x509_crl.c:664-667: `void mbedtls_x509_crl_init(mbedtls_x509_crl *crl) { memset(crl, 0, sizeof(mbedtls_x509_crl)); }`
> programs/x509/crl_app.c:59: `mbedtls_x509_crl_init(&crl);` called before `mbedtls_x509_crl_parse_file` at line 98.
> tests/suites/test_suite_x509parse.function:1270-1271: `mbedtls_x509_crl_init(&crl); USE_PSA_INIT();` precedes `mbedtls_x509_crl_parse` at line 1276.

### P2.2 Parameter Construction
**Claim:** `chain` must be a non-NULL pointer to a zero-initialized (or previously-parsed, chainable) `mbedtls_x509_crl`. `buf` must be a non-NULL pointer to at least `buflen` readable bytes; `buflen` is the exact byte length of the buffer, and for PEM input it must include the terminating NUL byte (the function checks `buf[buflen-1] != '\0'` to decide PEM vs raw DER). Passing `buflen == 0` is rejected deeper in `mbedtls_x509_crl_parse_der`. No other flags select behaviour — PEM/DER dispatch is purely content/termination driven.
**Evidence:**
> library/x509_crl.c:513-515: `if (chain == NULL || buf == NULL) { return MBEDTLS_ERR_X509_BAD_INPUT_DATA; }`
> library/x509_crl.c:522-523: `if (buflen == 0 || buf[buflen - 1] != '\0') { ret = MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT; }` — only non-PEM path tried if buffer isn't NUL-terminated.
> include/mbedtls/x509_crl.h:121-123: "\param buflen size of the buffer (including the terminating null byte for PEM data)"
> library/x509_crl.c:325-327 (inside `mbedtls_x509_crl_parse_der`): `if (buflen == 0) { return MBEDTLS_ERR_X509_INVALID_FORMAT; }`

### P2.3 Object Lifecycle
**Claim:** Lifecycle is create (`mbedtls_x509_crl_init`) → parse one-or-more times via `mbedtls_x509_crl_parse`/`_der`/`_file` (each successful call appends a new node to the `next` linked list; a failing call leaves the chain unusable and already-partially-filled, so it must still be freed) → consume (`mbedtls_x509_crl_info`, or direct field access such as `crl.issuer`, `crl.entry`, used e.g. by `mbedtls_x509_crt_verify`'s revocation-checking machinery) → destroy (`mbedtls_x509_crl_free`). The chain can be reused for repeated parse calls (concatenated CRLs); `mbedtls_x509_crl_free` must be called exactly once at the very end regardless of success/failure.
**Evidence:**
> library/x509_crl.c:306-320: on each call, `mbedtls_x509_crl_parse_der` walks to the end of chain (`while (crl->version != 0 && crl->next != NULL) crl = crl->next;`) and `mbedtls_calloc`s+`mbedtls_x509_crl_init`s a new node if the last one is already populated, before parsing into it.
> programs/x509/crl_app.c:59,98,102,124: init → parse_file → (on error) free+exit, else → info → free at `exit:` label.
> tests/suites/test_suite_x509parse.function:1270,1276,1287: init → parse → (conditionally) info → free.

### P2.4 Return Value Handling
**Claim:** Return 0 means success (at least one CRL successfully appended/chained). Nonzero is a negative-style X.509/PEM/ASN.1 error code (combined via `MBEDTLS_ERROR_ADD`), e.g. `MBEDTLS_ERR_X509_BAD_INPUT_DATA`, `MBEDTLS_ERR_X509_INVALID_FORMAT`, `MBEDTLS_ERR_X509_UNKNOWN_VERSION`, `MBEDTLS_ERR_X509_SIG_MISMATCH`, `MBEDTLS_ERR_X509_ALLOC_FAILED`, `MBEDTLS_ERR_PEM_*`. On any error path the already-allocated chain state must still be released via `mbedtls_x509_crl_free` (the internal code itself calls `mbedtls_x509_crl_free(crl)` on every parse error inside `_der`, but the caller/harness must still call `mbedtls_x509_crl_free` on the outer `chain` head once, because `mbedtls_x509_crl_free` walks the whole chain and is idempotent-safe since fields are either valid pointers or zero after `_init`/zeroing).
**Evidence:**
> library/x509_crl.c:505-561 (mbedtls_x509_crl_parse): returns `ret` from `mbedtls_pem_read_buffer`/`mbedtls_x509_crl_parse_der` on PEM-block failure, `0` after exhausting all PEM blocks, or the raw `mbedtls_x509_crl_parse_der` return for non-PEM buffers.
> library/x509_crl.c:349-499: every failing branch in `mbedtls_x509_crl_parse_der` does `mbedtls_x509_crl_free(crl); return <err>;` before returning.
> tests/suites/test_suite_x509parse.function:1276: `TEST_EQUAL(mbedtls_x509_crl_parse(&crl, buf->x, buf->len), (result));` — test data drives expected positive/negative return codes (see test_suite_x509parse.data for the matrix of expected codes).

### P2.5 Cleanup Sequence
**Claim:** Call `mbedtls_x509_crl_free(&chain)` exactly once after all parsing/consumption is finished, on both success and error paths. It frees, for every node in the `next` chain: `sig_opts` (if `MBEDTLS_X509_RSASSA_PSS_SUPPORT`), the shallow `issuer.next` named-data list, every `entry.next` revoked-certificate-entry node, the raw DER buffer copy (`raw.p`), then zeroizes and (except for the head element, which the caller owns) frees the node itself.
**Evidence:**
> library/x509_crl.c:672-706 (`mbedtls_x509_crl_free`): walks `crl_cur = crl; while (crl_cur != NULL) { ... free entry.next list ... free raw.p ... crl_cur = crl_cur->next; mbedtls_platform_zeroize(crl_prv, ...); if (crl_prv != crl) mbedtls_free(crl_prv); }` — note the head object itself is zeroized but NOT `mbedtls_free`d (it's caller-owned, e.g. stack variable).
> programs/x509/crl_app.c:102,115,124: `mbedtls_x509_crl_free(&crl);` called on every exit path (error and success), only once per `crl`.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited locations.
**Evidence:**
> include/mbedtls/x509_crl.h:109-110,127,144,159,168,175: declares `mbedtls_x509_crl_parse_der`, `mbedtls_x509_crl_parse`, `mbedtls_x509_crl_parse_file`, `mbedtls_x509_crl_info`, `mbedtls_x509_crl_init`, `mbedtls_x509_crl_free`.
> library/x509_crl.c:283,505,567,589,664,672: defines the same six functions.
> library/x509_internal.h:25-44: declares `mbedtls_x509_get_name`, `mbedtls_x509_get_alg`, `mbedtls_x509_get_sig`, `mbedtls_x509_get_sig_alg`, `mbedtls_x509_get_time`, `mbedtls_x509_get_serial`, `mbedtls_x509_get_ext` (used internally by the parser).
> include/mbedtls/x509.h:323,374: declares `mbedtls_x509_dn_gets`, `mbedtls_x509_serial_gets` used by `mbedtls_x509_crl_info`.

### P2.7 Co-call Constraints
**Claim:** `mbedtls_x509_crl_init` / `mbedtls_x509_crl_free` is the mandatory create/destroy pair (unpaired init with no matching free leaks memory; free without prior init on stack garbage is undefined — `_init` zero-fills so `_free` can safely detect "nothing to free" via NULL/0-length fields). `mbedtls_x509_crl_parse_file` internally pairs `mbedtls_pk_load_file` (allocates buffer) with `mbedtls_zeroize_and_free` (frees it) around the call to `mbedtls_x509_crl_parse` — this is internal and the harness doesn't need to replicate it when calling `_file`. If `MBEDTLS_USE_PSA_CRYPTO` is on, `psa_crypto_init()`/`mbedtls_psa_crypto_free()` form another required pair around any parse calls (see crl_app.c).
**Evidence:**
> library/x509_crl.c:573-581 (`mbedtls_x509_crl_parse_file`): `mbedtls_pk_load_file(path, &buf, &n)` → `mbedtls_x509_crl_parse(chain, buf, n)` → `mbedtls_zeroize_and_free(buf, n)`.
> programs/x509/crl_app.c:62-67,125-127: `psa_crypto_init()` near the top, `mbedtls_psa_crypto_free()` at `exit:`.
> library/x509_crl.c:664-667 vs 672-706: init zero-fills; free walks from zero-filled fields without crashing (`raw.p != NULL` guard at line 694, etc.).

### P2.8 Prerequisite State
**Claim:** No network/file/connection state is required for `mbedtls_x509_crl_parse` itself (pure in-memory parser). `mbedtls_x509_crl_parse_file` additionally needs a readable file at `path` (uses `mbedtls_pk_load_file`, requires `MBEDTLS_FS_IO`). No prior certificate/key context is required to *parse* a CRL; a CA certificate/public key is only needed later if the caller wants to *verify* the CRL's signature (not done by `mbedtls_x509_crl_parse*`, which only checks that the inner and outer `signatureAlgorithm` OIDs/params match, not the signature bytes against a key).
**Evidence:**
> library/x509_crl.c:479-486: consistency check is only `crl->sig_oid` vs `sig_oid2` (and params) — `MBEDTLS_ERR_X509_SIG_MISMATCH` on mismatch; no cryptographic verification occurs in this file.
> library/x509_crl.c:567-575: `mbedtls_x509_crl_parse_file` requires `MBEDTLS_FS_IO`-guarded `mbedtls_pk_load_file`.
> programs/x509/cert_app.c:252-261: CRL is loaded independently of CA cert loading (lines 229-248) merely to be added to the verification chain information, confirming CRL parsing has no certificate prerequisite.

### P2.9 Result Usage
**Claim:** After a successful parse, real callers pass `&crl` (or `crl.*` fields) to: (a) `mbedtls_x509_crl_info()` to render a human-readable report (issuer DN via `mbedtls_x509_dn_gets`, serials via `mbedtls_x509_serial_gets`, signature algorithm via `mbedtls_x509_sig_alg_gets`); (b) higher-level verification code (in `programs/x509/cert_app.c`, the parsed `cacrl` is attached to the verification context for `mbedtls_x509_crt_verify`-family calls, consuming `crl->issuer`, `crl->entry` list, `crl->this_update`/`next_update` for revocation checks — not shown directly in x509_crl.c but is the documented purpose); (c) `mbedtls_x509_crl_free()` to release it. The returned/filled `chain->entry` linked list (`mbedtls_x509_crl_entry`) and `chain->next` linked list (additional CRLs) are walked by callers using the public `next` pointers.
**Evidence:**
> library/x509_crl.c:589-658 (`mbedtls_x509_crl_info`): iterates `entry = &crl->entry; while (entry != NULL && entry->raw.len != 0) { ...mbedtls_x509_serial_gets(p, n, &entry->serial)...; entry = entry->next; }` and calls `mbedtls_x509_dn_gets(p, n, &crl->issuer)` (line 606) and `mbedtls_x509_sig_alg_gets(p, n, &crl->sig_oid, crl->sig_pk, crl->sig_md, crl->sig_opts)` (line 650).
> programs/x509/crl_app.c:112: `ret = mbedtls_x509_crl_info((char *) buf, sizeof(buf) - 1, "      ", &crl);` directly after a successful `mbedtls_x509_crl_parse_file`.
> tests/suites/test_suite_x509parse.function:1277-1283: `if ((result) == 0) { res = mbedtls_x509_crl_info(...); ... }` — info is only called when parse succeeded (result==0), i.e. P2.4-compliant gating.
> programs/x509/cert_app.c:146,252-261,444: `mbedtls_x509_crl_init(&cacrl)` then (conditionally) `mbedtls_x509_crl_parse_file(&cacrl, opt.crl_file)`, used later in the verify flow, then `mbedtls_x509_crl_free(&cacrl)` at cleanup.

### P2.10 Behaviour Controls
**Claim:** The only "mode" controls are implicit, derived from the input bytes, not explicit API flags:
1. PEM vs. DER selection in `mbedtls_x509_crl_parse`: buffer must be NUL-terminated AND match PEM markers "-----BEGIN X509 CRL-----"/"-----END X509 CRL-----" to take the PEM loop; otherwise falls through to `mbedtls_x509_crl_parse_der` on the raw bytes.
2. Multiple concatenated PEM blocks are looped over (`do { ... } while (is_pem && buflen > 1)`), each appended to the chain.
3. CRL version field (ASN.1 INTEGER, optional) selects v1 (no version present or 0) vs v2 (1); any other value returns `MBEDTLS_ERR_X509_UNKNOWN_VERSION`. Only `version == 2` triggers parsing of `crlExtensions` via `x509_get_crl_ext`.
4. `nextUpdate` is OPTIONAL Time — absence (signalled by `MBEDTLS_ERR_ASN1_UNEXPECTED_TAG`/`MBEDTLS_ERR_ASN1_OUT_OF_DATA` wrapped in `MBEDTLS_ERR_X509_INVALID_DATE`) is tolerated; any other time-parse error is fatal.
5. Per-entry `crlEntryExtensions` are OPTIONAL and only well-formedness (tag/length) is checked — content is ignored; a *critical* top-level CRL extension (`is_critical` bit set) is rejected regardless of extension OID (extension OIDs/content are not otherwise distinguished).
**Evidence:**
> library/x509_crl.c:522-529,540-554 (PEM loop/termination check).
> library/x509_crl.c:51-67 (`x509_crl_get_version`): `MBEDTLS_ERR_ASN1_UNEXPECTED_TAG` → `*ver = 0` (v1 implied).
> library/x509_crl.c:385-388,453-460: `if (crl->version < 0 || crl->version > 1) return MBEDTLS_ERR_X509_UNKNOWN_VERSION;` and `if (crl->version == 2) { ret = x509_get_crl_ext(...); }`.
> library/x509_crl.c:426-434: nextUpdate optional-handling block.
> library/x509_crl.c:142-146 (`x509_get_crl_ext`): `if (is_critical) { return MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_EXTENSIONS, MBEDTLS_ERR_ASN1_UNEXPECTED_TAG); }`.

### P2.11 Input Expectations
**Claim:** Input is either (a) one or more PEM blocks delimited by "-----BEGIN X509 CRL-----"/"-----END X509 CRL-----", NUL-terminated as a whole C-string (`buflen` includes the final `\0`), or (b) a single raw DER `CertificateList ::= SEQUENCE { tbsCertList TBSCertList, signatureAlgorithm AlgorithmIdentifier, signatureValue BIT STRING }` structure. DER length of the outer SEQUENCE must exactly span to `buf+buflen` (`len != (size_t)(end - p)` → `MBEDTLS_ERR_X509_INVALID_FORMAT`+`MBEDTLS_ERR_ASN1_LENGTH_MISMATCH`); any leftover/trailing bytes after fully parsing TBSCertList or after the signature are rejected as length mismatches (lines 462-466, 493-497). `buflen` must be > 0; the DER path copies `buf` into a heap buffer of exactly `buflen` bytes via `mbedtls_calloc`+`memcpy`, so OOB reads beyond `buflen` are not expected inside the parser (bugs would show as reads past the calloc'd copy). No minimum size beyond what ASN.1 tag/length decoding requires (`mbedtls_asn1_get_tag` itself checks bounds against `end`).
**Evidence:**
> library/x509_crl.c:325-339: `if (buflen == 0) return MBEDTLS_ERR_X509_INVALID_FORMAT; p = mbedtls_calloc(1, buflen); ... memcpy(p, buf, buflen); crl->raw.p = p; crl->raw.len = buflen; end = p + buflen;`
> library/x509_crl.c:347-357: outer SEQUENCE tag/length check with exact-length requirement.
> library/x509_crl.c:462-466,493-497: trailing-byte checks (`if (p != end) { ... MBEDTLS_ERR_ASN1_LENGTH_MISMATCH }`).
> include/mbedtls/x509_crl.h:121-123: buflen must include PEM NUL terminator.
> library/x509_crl.c:522: `if (buflen == 0 || buf[buflen - 1] != '\0') { ret = MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT; }` — reading `buf[buflen-1]` assumes `buflen >= 1` and `buf` has at least `buflen` valid bytes.

## Other Entries (API surface) and how they combine with the target in one harness

### mbedtls_x509_crl_parse_der
**Role:** Core DER-only variant, internally invoked by `mbedtls_x509_crl_parse` for each decoded PEM block and directly usable by a harness to drive pure-DER fuzzing paths (bypassing the PEM envelope logic).
**Prerequisites:** `chain` must be `mbedtls_x509_crl_init`-ed (or a valid existing chain to append to); `buf`/`buflen` as in target (no NUL-termination requirement here, pure DER, `buflen` must be > 0).
**Parameters/paths:** same chain-walk/append logic as described in P2.3/P2.1; version field and extension-presence flags select the same branches as in the target (`x509_crl_get_version`, `x509_get_entries`, `x509_get_crl_ext` gated by `version==2`).
**Return value:** identical error-code domain to the target (0 on success, negative X509/ASN1 combined codes on failure); on failure it calls `mbedtls_x509_crl_free(crl)` itself before returning (library/x509_crl.c:349-497), so the harness need not call free twice, just once more at teardown (idempotent-safe because the struct is zeroized by free).
**What to free:** same as target — `mbedtls_x509_crl_free(&chain)` once at the end.
**Combination with target:** a harness may call `mbedtls_x509_crl_parse_der` directly on raw fuzz bytes (DER-only fuzzing) OR call `mbedtls_x509_crl_parse` (which itself calls `_der` after PEM-stripping) on the same input to additionally exercise the PEM-detection path; both share one `mbedtls_x509_crl_init`/`mbedtls_x509_crl_free` pair around them.
**Evidence:**
> include/mbedtls/x509_crl.h:109-110: declaration.
> library/x509_crl.c:283-500: definition, called from library/x509_crl.c:540 inside `mbedtls_x509_crl_parse`'s PEM loop, and from library/x509_crl.c:560 as the final DER fallback.

### mbedtls_x509_crl_parse_file
**Role:** File-based wrapper source channel; loads file bytes then calls the target `mbedtls_x509_crl_parse`. Not itself the attack-surface entry for in-memory fuzzing (requires filesystem access, `MBEDTLS_FS_IO`), but documented here as it's in the Logic Group.
**Prerequisites:** `chain` initialized; `path` must name a readable file (PEM or DER CRL). Requires `MBEDTLS_FS_IO` compiled in.
**Parameters/paths:** no additional flags; dispatches entirely to `mbedtls_x509_crl_parse` after `mbedtls_pk_load_file` reads the whole file into a heap buffer sized to the file (NUL-terminated by `mbedtls_pk_load_file`, enabling the PEM path).
**Return value:** forwards `mbedtls_x509_crl_parse`'s return code, or `mbedtls_pk_load_file`'s error if the file can't be read/loaded.
**What to free:** internally frees its own load buffer via `mbedtls_zeroize_and_free(buf, n)` (library/x509_crl.c:579); caller is still responsible for `mbedtls_x509_crl_free(chain)` at the end as usual.
**Combination with target:** typically NOT combined with the target in the same harness call for in-memory fuzzing (a libFuzzer-style in-memory harness would call `mbedtls_x509_crl_parse` directly on the fuzz buffer rather than writing it to a temp file and calling `_parse_file`); `_parse_file` is better suited to a corpus-replay/CLI tool (see `programs/x509/crl_app.c`) rather than the fuzz entry itself.
**Evidence:**
> library/x509_crl.c:567-582: definition; calls `mbedtls_pk_load_file` then `mbedtls_x509_crl_parse`, frees buffer.
> include/mbedtls/x509_crl.h:129-145: declaration, guarded by `#if defined(MBEDTLS_FS_IO)`.
> programs/x509/crl_app.c:98: real caller.

### mbedtls_x509_crl_info
**Role:** Consumer of the target's output — formats the parsed chain's fields (issuer, dates, entries, sig alg) into a caller-supplied text buffer. This is the natural "use the result" step after a successful parse, giving the fuzz harness a way to additionally exercise the formatting/printing code path over whatever structure the parser produced (good for finding OOB in the string-building helpers it calls).
**Prerequisites:** `crl` must point to a `mbedtls_x509_crl` that was successfully parsed (`mbedtls_x509_crl_parse*` returned 0); `buf` is caller-allocated output buffer of `size` bytes; must only be called on a *successfully parsed* CRL — test suite guards this with `if ((result) == 0)` before calling it (tests/suites/test_suite_x509parse.function:1277).
**Parameters/paths:** `prefix` is an arbitrary string prepended to each line; no other mode flags. Internally walks `crl->entry` linked list and calls `mbedtls_x509_dn_gets`, `mbedtls_x509_serial_gets`, `mbedtls_x509_sig_alg_gets` — these all re-traverse structures the parser filled in, so malformed-but-"successfully parsed" edge cases (e.g. many chained entries) exercise this code too.
**Return value:** length of the written string (excluding NUL) on success, or a negative error if the destination buffer was too small (`MBEDTLS_ERR_X509_BUFFER_TOO_SMALL`-style, surfaced via the `MBEDTLS_X509_SAFE_SNPRINTF` macro causing early return of `-1`/`-2`, as the test checks `res != -1 && res != -2`). Not a function that itself allocates — caller owns the output buffer.
**What to free:** nothing owned by `_info` itself; `crl` is freed later via `mbedtls_x509_crl_free`, and the caller's output `buf` is caller-managed (often a stack array, as in crl_app.c:51 `unsigned char buf[100000];`).
**Combination with target:** harness calls `mbedtls_x509_crl_init(&crl)` → `mbedtls_x509_crl_parse(&crl, data, size)` → if return==0, call `mbedtls_x509_crl_info(outbuf, outsize, "", &crl)` to also fuzz the info/formatting path → `mbedtls_x509_crl_free(&crl)` at the end regardless.
**Evidence:**
> include/mbedtls/x509_crl.h:147-161: declaration, guarded by `#if !defined(MBEDTLS_X509_REMOVE_INFO)`.
> library/x509_crl.c:589-658: definition.
> tests/suites/test_suite_x509parse.function:1264-1289 (`x509parse_crl` test case): canonical init→parse→(conditional) info→free sequence, directly analogous to what the harness should do.
> programs/x509/crl_app.c:98-124: CLI analogue using `_parse_file` instead of `_parse`.

### mbedtls_x509_crl_init / mbedtls_x509_crl_free
**Role:** Mandatory lifecycle bookends for every `mbedtls_x509_crl` object used with any of the above entries. Already covered in detail in P2.1/P2.3/P2.5/P2.7 for the target; apply identically regardless of which parse entry (`_parse`, `_parse_der`, `_parse_file`) is used, since all three operate on / allocate onto the same `mbedtls_x509_crl` struct and its `next`/`entry.next` linked lists.
**Evidence:**
> library/x509_crl.c:664-667 and 672-706 (definitions, already quoted above).
> include/mbedtls/x509_crl.h:163-175 (declarations).

## Build notes
**Claim:** Fuzz targets for this project are plain C, built via CMake as part of `programs/fuzz/`, with `make -j$(nproc) all` then fuzzer binaries copied to `$OUT`. Headers are included via the project's standard public include path `include/` (i.e. `#include "mbedtls/x509_crl.h"`), no special include path hacking needed. The project's own `fuzz_x509crl.c` normally calls `mbedtls_x509_crl_parse` directly on the libFuzzer `data`/`size` buffer (the stub currently present in this checkout is a placeholder/no-op for baseline generation and not representative of the real logic, but confirms the target symbol and linkage point). Libraries linked: the project's own static libs (crypto/x509/tls) built by the same CMake project; libFuzzer/sanitizer flags are supplied by the OSS-Fuzz `CC`/`CFLAGS`/`LIB_FUZZING_ENGINE` environment (standard OSS-Fuzz CMake integration, not overridden in `build.sh`).
**Evidence:**
> ossfuzz_view/mbedtls-c2/build.sh:25-30: `mkdir build; cd build; cmake -DENABLE_TESTING=ON ..; make -j$(nproc) all; cp programs/fuzz/fuzz_* $OUT/`
> ossfuzz_view/mbedtls-c2/build.sh:33-51: corpus packaging referencing `fuzz/fuzz_x509crl_seed_corpus.zip` and `fuzz/*.options`, confirming `fuzz_x509crl` is a recognized/built fuzz binary name for this target.
> programs/fuzz/fuzz_x509crl.c:1,10: "/* Gold harness replaced by an empty stub for baseline generation. */" and `QF_EXTERN void qf_target_0(void) __asm__("mbedtls_x509_crl_parse");` — confirms the real (non-stub) fuzz entry is meant to call `mbedtls_x509_crl_parse`, and that it's a C translation unit (`#include <stddef.h>`, `#include <stdint.h>`, `extern "C"` guard for C++ compatibility) linked against the library build.
> programs/fuzz/fuzz_x509crl.options:1-2: `[libfuzzer] max_len = 65535` — libFuzzer-style harness, confirming `LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)` entry convention matches the target's `(buf, buflen)` signature directly.
> include/mbedtls/x509_crl.h:16: `#include "mbedtls/x509.h"` — the public header chain a harness needs is simply `#include "mbedtls/x509_crl.h"` from the `include/` root, matching CMake's exported include directories for `programs/fuzz/*`.