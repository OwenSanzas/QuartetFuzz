## target: asn1_der_decoding

### P2.1 Init Sequence
**Claim:** Before calling `asn1_der_decoding`, the caller must (1) build an ASN.1 definitions tree once with `asn1_array2tree` (or `asn1_parser2tree`), and (2) instantiate a fresh element node of the desired type from that definitions tree with `asn1_create_element`. `asn1_der_decoding` is then called on that fresh element together with the raw DER/BER bytes.
**Evidence:**
> lib/decoding.c:1611-1613 (doc of asn1_der_decoding): "Fill the structure *@element with values of a DER encoding string. The structure must just be created with function asn1_create_element()."
> fuzz/libtasn1_pkix_der_fuzzer.c:537: `res = asn1_array2tree (pkix_asn1_tab, &_gnutls_pkix1_asn, NULL);` (done once, guarded by `static int first`)
> fuzz/libtasn1_pkix_der_fuzzer.c:543-544: `asn1_create_element (_gnutls_pkix1_asn, "PKIX1.Certificate", &dn)` immediately before
> fuzz/libtasn1_pkix_der_fuzzer.c:550: `res = asn1_der_decoding (&dn, data, size, NULL);`
> examples/CertificateExample.c:576-578: `asn1_create_element (cert_def, "PKIX1Implicit88.Certificate", &cert2); result = asn1_der_decoding (&cert2, der, der_len, errorDescription);`

### P2.2 Parameter Construction
**Claim:** `element` is `asn1_node *` — address of a pointer previously set by `asn1_create_element` (must not be NULL-dereferenced; `*element` may legitimately be the created node, and on error the node is deleted and `*element` is left dangling/freed). `ider` is a `const void *` raw byte buffer (attacker-controlled bytes, no required encoding beyond DER/BER itself), `ider_len` is `int`, the number of valid bytes in `ider` (must be ≥ 0 and match the actual buffer size — internally cast to `*max_ider_len` for `asn1_der_decoding2`). `errorDescription` is optional (`char *`, may be `NULL`); if non-NULL it must point to at least `ASN1_MAX_ERROR_DESCRIPTION_SIZE` (128) bytes, per the type's documented buffer contract.
**Evidence:**
> libtasn1.h.in:377-379: `asn1_der_decoding (asn1_node * element, const void *ider, int ider_len, char *errorDescription);`
> lib/decoding.c:1624-1628: `asn1_der_decoding (asn1_node *element, const void *ider, int ider_len, char *errorDescription) { return asn1_der_decoding2 (element, ider, &ider_len, 0, errorDescription); }`
> lib/decoding.c:936-940: `if (errorDescription != NULL) errorDescription[0] = 0; if (node == NULL) return ASN1_ELEMENT_NOT_FOUND;`
> libtasn1.h.in:314: `# define ASN1_MAX_ERROR_DESCRIPTION_SIZE 128` ("Maximum number of characters of a description message")

### P2.3 Object Lifecycle
**Claim:** Create (asn1_array2tree → definitions; asn1_create_element → element) → decode (asn1_der_decoding/asn1_der_decoding2, fills the same element in place) → use (asn1_read_value, asn1_der_decoding_startEnd, asn1_expand_any_defined_by, asn1_der_coding, asn1_print_structure on the filled element) → destroy (asn1_delete_structure on element; asn1_delete_structure on the definitions tree only after all elements created from it are done).
**Evidence:**
> fuzz/libtasn1_pkix_der_fuzzer.c:537,543-544,550,558,566: array2tree once → create_element → der_decoding → der_decoding_startEnd → asn1_delete_structure(&dn) (element only; the static `_gnutls_pkix1_asn` is leaked/reused across fuzzer invocations deliberately, process-lifetime definitions tree)
> examples/CertificateExample.c:646(array2tree) → 576(create_element) → 578(der_decoding) → 588/591(get_Name_type reads) → 624(delete_structure(&cert2)) → 670(delete_structure(&PKIX1Implicit88) at program exit)

### P2.4 Return Value Handling
**Claim:** Return 0 (`ASN1_SUCCESS`) means decoding succeeded and `*element` is populated and safe to traverse/read. Non-zero means failure; the most relevant codes are `ASN1_ELEMENT_NOT_FOUND` (2, `*element`/`node` was NULL), `ASN1_DER_ERROR` (4, malformed/truncated/trailing-data DER), `ASN1_TAG_ERROR` (8, tag mismatch vs schema). **On any non-success return, `*element` has already been destroyed internally** (`asn1_delete_structure` was called on it) — callers must not call `asn1_delete_structure` again on it nor otherwise dereference it, only reassign/forget the pointer.
**Evidence:**
> lib/decoding.c:1618-1621 (doc): "Returns: ASN1_SUCCESS if DER encoding OK, ASN1_ELEMENT_NOT_FOUND if ELEMENT is NULL, and ASN1_TAG_ERROR or ASN1_DER_ERROR if the der encoding doesn't match the structure name (*ELEMENT deleted)."
> lib/decoding.c:1597-1599 (asn1_der_decoding2 cleanup path): `cleanup: asn1_delete_structure (element); return result;`
> lib/includes/libtasn1.h.in:115-123: `ASN1_SUCCESS 0`, `ASN1_ELEMENT_NOT_FOUND 2`, `ASN1_DER_ERROR 4`, `ASN1_GENERIC_ERROR 6`, `ASN1_TAG_ERROR 8`
> examples/CertificateExample.c:580-584: `if (result != ASN1_SUCCESS) { printf("Problems with DER encoding\n"); return; }` (no further delete_structure call on cert2 in that path — consistent with internal auto-delete)

### P2.5 Cleanup Sequence
**Claim:** On success, caller must eventually call `asn1_delete_structure(&element)` (or `asn1_delete_structure2` with `ASN1_DELETE_FLAG_ZEROIZE` to wipe sensitive values) exactly once. On failure from `asn1_der_decoding`/`asn1_der_decoding2`, the element was already deleted internally — do NOT call delete again. The definitions tree (from `asn1_array2tree`) is destroyed separately, once, after all elements derived from it are no longer needed (typically at program/fuzzer-process end), also via `asn1_delete_structure`.
**Evidence:**
> lib/decoding.c:1598: `asn1_delete_structure (element);` inside `cleanup:` of asn1_der_decoding2 — single internal free path for all failures.
> fuzz/libtasn1_pkix_der_fuzzer.c:565-566: `out: asn1_delete_structure (&dn);` called unconditionally after the goto-on-error pattern — safe because `dn` was either untouched (create_element failed, dn==NULL-ish/undefined... see note below) or already deleted by der_decoding on failure, or valid on success; `asn1_delete_structure` tolerates `*structure == NULL`.
> lib/structure.c:296-300: `asn1_delete_structure (asn1_node *structure) { return _asn1_delete_structure (NULL, structure, 0); }` — doc elsewhere states it returns `ASN1_ELEMENT_NOT_FOUND` if `*structure` was NULL (safe no-op call).
> examples/CertificateExample.c:670: `asn1_delete_structure (&PKIX1Implicit88);` at very end of main, after all per-certificate elements derived from it were already deleted.

### P2.6 API Existence
**Claim:** All functions referenced below (asn1_der_decoding, asn1_der_decoding2, asn1_der_decoding_element, asn1_der_decoding_startEnd, asn1_expand_any_defined_by, asn1_array2tree, asn1_create_element, asn1_delete_structure, asn1_delete_structure2, asn1_der_coding, asn1_read_value, asn1_find_node) exist in the current codebase with matching signatures.
**Evidence:**
> lib/decoding.c:918 (asn1_der_decoding2), 1624 (asn1_der_decoding), 1655 (asn1_der_decoding_element), 1690 (asn1_der_decoding_startEnd), 1753 (asn1_expand_any_defined_by)
> lib/structure.c:180 (asn1_array2tree), 715 (asn1_create_element), 297 (asn1_delete_structure), 314 (asn1_delete_structure2)
> lib/coding.c:1075 (asn1_der_coding)
> lib/includes/libtasn1.h.in:330,338,341,343,350-359,369,373,378,383,390,396,452 — all declared as `extern ASN1_API` in the public header template.

### P2.7 Co-call Constraints
**Claim:** Paired lifecycle APIs: `asn1_create_element` ↔ `asn1_delete_structure`/`asn1_delete_structure2` (one delete per created element, unless decoding already auto-deleted it on error). `asn1_array2tree` ↔ `asn1_delete_structure` on the definitions pointer (once, at the end, after no element derived from it is used anymore). `asn1_der_decoding` is NOT itself paired with an explicit "end" call — the companion/dual operation is `asn1_der_coding` (encode) used for round-trip testing, not a required cleanup step.
**Evidence:**
> lib/structure.c:297-300,314-317: delete_structure/delete_structure2 are the only structure-destructors; nothing else releases an asn1_node.
> fuzz/libtasn1_pkix_der_fuzzer.c:524,537 vs 566: definitions tree created once (`static`, line 524/537) and never deleted in the fuzzer (long-lived for the process), while the per-iteration element `dn` is deleted every iteration (line 566).
> examples/CertificateExample.c:646/649 (array2tree/parser2tree) paired with 670 (delete_structure) at the very end of main; each per-certificate `asn1_create_element` (576, 121, 146, 171, 225, 268, 294, 323, 373, 399, 427, 449, 486) paired with a corresponding `asn1_delete_structure` (624,135,160,186,272,300,330,377,405,434,458,492).

### P2.8 Prerequisite State
**Claim:** A non-NULL, fully-expanded definitions tree (built by `asn1_array2tree`, which internally resolves identifiers via `_asn1_check_identifier`/`_asn1_expand_object_id`) must exist before `asn1_create_element` is called with a `source_name` string that must match a type name present in that tree (e.g. `"PKIX1.Certificate"`); otherwise `asn1_create_element` fails with `ASN1_ELEMENT_NOT_FOUND` and there is nothing valid to pass to `asn1_der_decoding`. No global/library init call exists in libtasn1 besides building this tree once.
**Evidence:**
> lib/structure.c:193-194: `if (*definitions != NULL) return ASN1_ELEMENT_NOT_EMPTY;` — array2tree requires `*definitions` to start NULL.
> lib/structure.c:251-256: `result = _asn1_check_identifier (*definitions); ... result = _asn1_expand_object_id (&e_list, *definitions);` — identifier resolution happens inside array2tree, must succeed (ASN1_SUCCESS) before using the tree.
> lib/structure.c:721-724: `dest_node = _asn1_copy_structure2 (definitions, source_name); if (dest_node == NULL) return ASN1_ELEMENT_NOT_FOUND;` — create_element needs a valid, already-built `definitions` and a `source_name` found within it.
> lib/decoding.c:939-940: `if (node == NULL) return ASN1_ELEMENT_NOT_FOUND;` inside asn1_der_decoding2 — target itself requires `*element != NULL`, i.e. create_element must have succeeded first.

### P2.9 Result Usage
**Claim:** After a successful `asn1_der_decoding`, real callers pass the filled element to: (a) `asn1_read_value` to extract field values/strings by dotted path name, (b) `asn1_der_decoding_startEnd` to find byte offsets of a named sub-element within the original DER buffer (e.g. for signature verification), (c) `asn1_expand_any_defined_by` to recursively re-decode ANY-typed fields into concrete types using the OID sibling, (d) `asn1_der_coding` to re-encode (round-trip), (e) `asn1_print_structure`/`asn1_visit_tree` to dump for debugging, and finally (f) `asn1_delete_structure` to free it.
**Evidence:**
> examples/CertificateExample.c:587-592: `get_Name_type (cert_def, cert2, "tbsCertificate.issuer", str);` and `...subject...` — internally calls `asn1_read_value (cert, name, str, &len)` (line 92) on the decoded `cert2`.
> examples/CertificateExample.c:597-612: `asn1_read_value (cert2, "signatureAlgorithm.algorithm", ...)`; then `asn1_der_decoding_startEnd (cert2, der, der_len, "tbsCertificate", &start, &end);` then `asn1_read_value (cert2, "signature", str, &len);` — classic "decode, locate signed range by name, read signature" sequence.
> fuzz/libtasn1_pkix_der_fuzzer.c:550-559: decoded `dn` passed straight into `asn1_der_decoding_startEnd (dn, data, size, "tbsCertificate.issuer", &start, &end);`
> tests/Test_tree.c:601-602: `case ACT_EXPAND_ANY: result = asn1_expand_any_defined_by (definitions, &asn1_element); break;` — invoked on an element populated by the preceding `ACT_DECODING` case (line 587-589) in the same test driver.
> tests/Test_tree.c:577-586: `asn1_der_coding (asn1_element, ...)` used in the same switch/test harness alongside `ACT_DECODING`, exercising decode-then-encode round trips.

### P2.10 Behaviour Controls
**Claim:** `asn1_der_decoding` always calls `asn1_der_decoding2` with `flags=0`, i.e. BER-tolerant mode without padding allowance and without strict-DER enforcement. To exercise other paths the harness must call `asn1_der_decoding2` directly with `ASN1_DECODE_FLAG_ALLOW_PADDING` (tolerate trailing bytes) and/or `ASN1_DECODE_FLAG_STRICT_DER` (reject BER constructs, force flat DER length/value reads) and/or `ASN1_DECODE_FLAG_ALLOW_INCORRECT_TIME`. Inside decoding, the ASN.1 type of each schema node (`ASN1_ETYPE_SEQUENCE/SET/SET_OF/CHOICE/...`, see type_field dispatch) and the tag's `ASN1_CLASS_STRUCTURED` bit (constructed vs primitive, enabling indefinite-length/BER recursive decode of OCTET STRING) select different internal code paths; which schema is used (hence which branches are reachable) is controlled by the `source_name` argument to `asn1_create_element` and the `definitions` array passed to `asn1_array2tree`.
**Evidence:**
> lib/decoding.c:1624-1628: `asn1_der_decoding(...) { return asn1_der_decoding2 (element, ider, &ider_len, 0, errorDescription); }` — flags hardcoded to 0.
> lib/includes/libtasn1.h.in:256-278: `ASN1_DECODE_FLAG_ALLOW_PADDING`(1), `ASN1_DECODE_FLAG_STRICT_DER`(1<<1), `ASN1_DECODE_FLAG_ALLOW_INCORRECT_TIME`(1<<2), all documented "Used by: asn1_der_decoding2()".
> lib/decoding.c:1585-1591 (asn1_der_decoding2): `if ((ider_len < 0) || (!(flags & ASN1_DECODE_FLAG_ALLOW_PADDING) && (ider_len != 0))) { ... result = ASN1_DER_ERROR; }` — padding flag directly changes trailing-byte tolerance.
> examples/CertificateExample.c:543-544/576: `asn1_create_element (cert_def, "PKIX1Implicit88.X520countryName", &value)` vs `asn1_create_element (cert_def, "PKIX1Implicit88.Certificate", &cert2)` — different `source_name` selects a wholly different sub-schema/branch set decoded from the same kind of DER bytes.

### P2.11 Input Expectations
**Claim:** `ider` is expected to be a BER/DER tag-length-value encoded buffer matching (in structure/tag/class) the schema of `*element`; `ider_len` must equal (or, with `ASN1_DECODE_FLAG_ALLOW_PADDING`, be ≥) the number of bytes actually consumed by the outermost DER object, and must be ≥ 0. There is no required trailing NUL/terminator; the function tracks remaining length via an internal `DECR_LEN` macro and fails with `ASN1_DER_ERROR` if the buffer is truncated (length goes negative) or has unconsumed trailing bytes when padding is not allowed. Malformed/ambiguous tag or length fields (as parsed by `asn1_get_tag_der`/`asn1_get_length_der`) cause early `ASN1_DER_ERROR`/`ASN1_TAG_ERROR` returns before most of the tree is touched.
**Evidence:**
> lib/decoding.c:1606-1607 (doc): "@ider: vector that contains the DER encoding. @ider_len: number of bytes of *@ider: @ider[0]..@ider[len-1]."
> lib/decoding.c:928: `int indefinite, result, total_len = *max_ider_len, ider_len = *max_ider_len;` then length is decremented throughout the main decode loop (e.g. lines 851,862,867 `DECR_LEN(der_len, ...)`), underflow triggers error via the `DECR_LEN` macro (defined near top of decoding.c) returning `ASN1_DER_ERROR`.
> lib/decoding.c:1585-1591: trailing-bytes / negative-length check quoted above (padding-flag gated).
> lib/decoding.c:854-856: `len2 = asn1_get_length_der (der + counter, der_len, &len3); if (len2 < -1) return ASN1_DER_ERROR;` — malformed length field rejected immediately.

## Other entries: call protocol and combination with the target

### asn1_der_decoding2 (variant of the target)
**Claim:** Same prerequisites as `asn1_der_decoding` (element from `asn1_create_element`). Differs by: `max_ider_len` is `int *` — in (max buffer size) / out (actual bytes consumed) — and `flags` selects `ASN1_DECODE_FLAG_ALLOW_PADDING` / `ASN1_DECODE_FLAG_STRICT_DER` / `ASN1_DECODE_FLAG_ALLOW_INCORRECT_TIME`. Return codes and cleanup-on-error identical to the target (internal `asn1_delete_structure(element)` on failure, lib/decoding.c:1598). A harness exercising BER/indefinite-length/strict-DER paths should call this directly instead of (or in addition to) `asn1_der_decoding`.
**Evidence:** lib/decoding.c:891-920 (doc+signature), 1585-1599 (padding/cleanup logic), libtasn1.h.in:372-375 (declaration), tests/strict-der.c and tests/Test_indefinite.c reference `asn1_der_decoding2`-style flag usage (grep hit in files list above).

### asn1_array2tree (source/init, precursor)
**Claim:** Needs: `*definitions` pointer initialized to `NULL`, a static `asn1_static_node` array (schema). Returns `ASN1_SUCCESS`, `ASN1_ELEMENT_NOT_EMPTY` (if `*definitions != NULL`), `ASN1_IDENTIFIER_NOT_FOUND`, or `ASN1_ARRAY_ERROR`. Must be called exactly once per array/definitions pointer before any `asn1_create_element` using it. Cleanup: `asn1_delete_structure(&definitions)` once, after all elements derived from it are freed/unused.
**Evidence:** lib/structure.c:163-262 (doc + body, quoted above in P2.8); fuzz/libtasn1_pkix_der_fuzzer.c:532-539 (called once guarded by `static int first`).

### asn1_create_element (lifecycle precursor)
**Claim:** Needs a successfully built `definitions` tree and a `source_name` string naming a type inside it (e.g. `"PKIX1.Certificate"`). Returns `ASN1_SUCCESS` or `ASN1_ELEMENT_NOT_FOUND` if `source_name` unknown. On success, `*element` is a freshly allocated node tree (a copy of the definition's subtree) ready to be passed to `asn1_der_decoding`/`asn1_der_decoding2`. Different `source_name` values select different schemas/branches of the same definitions tree to decode the same bytes against — a key additional fuzzing dimension. Cleanup: `asn1_delete_structure(&element)` (unless decoding already deleted it on failure).
**Evidence:** lib/structure.c:699-734 (doc+body, quoted above); examples/CertificateExample.c:576 and 121/146/171/225/268/294/323/373/399/427/449/486 (many distinct `source_name` values on the same `cert_def` definitions tree).

### asn1_der_decoding_element (deprecated thin wrapper)
**Claim:** Deprecated alias; simply forwards to `asn1_der_decoding` ignoring `elementName` beyond documentation intent. Same pre/post conditions as the target. The header marks it `_ASN1_GCC_ATTR_DEPRECATED`; new harnesses should prefer the target directly.
**Evidence:** lib/decoding.c:1654-1659: `asn1_der_decoding_element (...) { return asn1_der_decoding (structure, ider, len, errorDescription); }`; libtasn1.h.in:381-387 ("Do not use. Use asn1_der_decoding() instead.").

### asn1_der_decoding_startEnd (consumer of the target's result)
**Claim:** Needs an `element` that is ideally already decoded (`asn1_der_decoding` already run); if the target node's `start`/`end` are both 0 (not yet decoded) and `ider`/`ider_len` are supplied, it calls `asn1_der_decoding` itself internally first. Looks up `name_element` via `asn1_find_node`. Returns `ASN1_SUCCESS`, `ASN1_ELEMENT_NOT_FOUND` (element/name missing), or `ASN1_GENERIC_ERROR` (end < start, or ider missing when decode-on-demand is needed). Outputs `*start`/`*end` byte offsets into the original `ider` buffer, used by callers to e.g. hash/verify a `tbsCertificate` range. No separate free needed (does not allocate); the owning `element` is still freed via `asn1_delete_structure` by the caller.
**Evidence:** lib/decoding.c:1661-1734 (full body, quoted above); fuzz/libtasn1_pkix_der_fuzzer.c:556-563 (called right after the target succeeds, on the same `dn`/`data`/`size`); examples/CertificateExample.c:606-612 (same pattern with `cert2`).

### asn1_expand_any_defined_by (post-processing consumer)
**Claim:** Needs the `definitions` tree (same one `element` was created from) and an already-decoded `*element` (doc: "Expands every ANY DEFINED BY element of a structure created from a DER decoding process (asn1_der_decoding function)"). Walks the tree for `ASN1_ETYPE_ANY` nodes with `CONST_DEFINED_BY`, matches a sibling `ASN1_ETYPE_OBJECT_ID` value against OIDs known in `definitions`, and internally re-invokes the DER decoder on the embedded bytes to replace the ANY node with a concretely-typed decoded subtree. Returns `ASN1_SUCCESS`, `ASN1_ERROR_TYPE_ANY` (no OID match / structural problem), or other decode error codes. No special cleanup beyond the normal `asn1_delete_structure(&element)` later (it mutates the existing tree in place).
**Evidence:** lib/decoding.c:1736-1753+ (doc+signature, quoted above); tests/Test_tree.c:601-602 (`ACT_EXPAND_ANY` invoked after `ACT_DECODING` in the same test-driver switch, on the already-decoded `asn1_element`).

### asn1_der_coding (inverse operation / round-trip consumer)
**Claim:** Needs an `element` tree with values set — either freshly decoded by the target or built by hand via `asn1_write_value`. `name` selects the sub-element to encode (via internal `asn1_find_node`). `ider`/`len` is caller-allocated output buffer with `*len` initially = buffer capacity; on `ASN1_MEM_ERROR` `*len` is updated to the required size (classic "query size, allocate, re-call" pattern). Returns `ASN1_SUCCESS`, `ASN1_ELEMENT_NOT_FOUND` (bad name), `ASN1_VALUE_NOT_FOUND` (missing required field value), `ASN1_MEM_ERROR` (buffer too small). Does not free or consume `element`; it internally works on a private copy (`_asn1_copy_structure3`) so the original `element` is untouched and still must be freed by the caller.
**Evidence:** lib/coding.c:1053-1098 (doc+body, quoted above); tests/Test_tree.c:577-586 (`ACT_ENCODING`/`ACT_ENCODING_LENGTH` cases) used alongside `ACT_DECODING` in the same driver for round-trip tests.

## Build notes
**Claim:** Fuzzers are plain C, compiled/linked against the project's own built static lib (`lib/libtasn1.la`) and gnulib helper (`lib/gl/libgnu.la`), with the public header found via `-I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes` (where `libtasn1.h` is generated from `lib/includes/libtasn1.h.in`). The OSS-Fuzz `build.sh` runs `./bootstrap && ./configure --enable-fuzzing ... && make -C lib && make -C src && cd fuzz && make oss-fuzz`, and the `oss-fuzz` target compiles each `*_fuzzer.c` with `$CXX $CXXFLAGS` using the same include paths plus `-I$(top_srcdir)` and links with libFuzzer (`-fsanitize=fuzzer` from `AM_LDFLAGS` in fuzzing mode).
**Evidence:**
> ossfuzz_view/libtasn1-c4/build.sh:18-27: `./bootstrap`; `./configure --enable-static --disable-shared ... --enable-fuzzing`; `make -j... -C lib`; `make -j... -C src`; `cd fuzz; make oss-fuzz`.
> libtasn1/fuzz/Makefile.am:24-28: `AM_CPPFLAGS = -I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(srcdir) -I$(top_builddir)/lib/gl -I$(top_srcdir)/lib/gl ...`; `LDADD = ../lib/gl/libgnu.la ../lib/libtasn1.la $(FUZZ_LIBS)`.
> libtasn1/fuzz/Makefile.am:41-44: `if FUZZING ... AM_LDFLAGS += -fsanitize=fuzzer`.
> libtasn1/fuzz/Makefile.am:73-80 (`oss-fuzz:` target): `$$CXX $$CXXFLAGS -I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(top_srcdir) "$${fuzzer}.c" -o ...` — confirms C sources compiled with the C++ driver/libFuzzer toolchain, header reachable via the two `lib/includes` paths.
> libtasn1/fuzz/libtasn1_pkix_der_fuzzer.c:29: `#include "libtasn1.h"` — the public installed-style header, found via the include paths above.