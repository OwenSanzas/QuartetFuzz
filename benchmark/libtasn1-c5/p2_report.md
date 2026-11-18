## target: asn1_der_decoding

### P2.1 Init Sequence
**Claim:** Before calling `asn1_der_decoding`, the caller must (1) build an ASN.1 definitions tree with `asn1_array2tree` (from a static `asn1_static_node` array) or `asn1_parser2tree` (from a `.asn1` text file), and (2) instantiate an empty element from that tree with `asn1_create_element`. `asn1_der_decoding` only fills an already-created, empty `asn1_node`; it does not create one.
**Evidence:**
> lib/includes/libtasn1.h.in:338-339: `asn1_create_element (asn1_node_const definitions, const char *source_name, asn1_node * element);`
> lib/decoding.c:902-903 (doc comment on asn1_der_decoding2): "Fill the structure *@element with values of a DER encoding string. The structure must just be created with function asn1_create_element()."
> fuzz/libtasn1_gnutls_der_fuzzer.c:146-156: `asn1_array2tree(gnutls_asn1_tab,&_gnutls_gnutls_asn,NULL)` done once (static/`first`), then per-input `asn1_create_element(_gnutls_gnutls_asn,"GNUTLS.DHParameter",&dn)` followed by `asn1_der_decoding(&dn,data,size,NULL)`.
> tests/coding-decoding2.c:43-50: `asn1_parser2tree(choicefile,&definitions,errorDescription)` then `asn1_create_element(definitions,"TEST.Main",&node1)` before any decode/encode use.
> examples/CrlExample.c:448-450: `asn1_create_element(cert_def,"PKIX1Implicit88.CertificateList",&crl2)` immediately followed by `asn1_der_decoding(&crl2, der, der_len, errorDescription)`.

### P2.2 Parameter Construction
**Claim:** `element` is `asn1_node *` — address of a non-NULL `asn1_node` previously produced by `asn1_create_element` (must itself be non-NULL, i.e. element creation must have succeeded). `ider` is a `const void *` pointing to at least `ider_len` readable bytes (the raw DER/BER blob); `ider_len` is a non-negative `int` byte count of that buffer. `errorDescription` may be `NULL`, or must point to a caller-allocated buffer of at least `ASN1_MAX_ERROR_DESCRIPTION_SIZE` bytes (library zeroes `errorDescription[0]` first).
**Evidence:**
> lib/includes/libtasn1.h.in:377-379: `asn1_der_decoding (asn1_node * element, const void *ider, int ider_len, char *errorDescription);`
> lib/decoding.c:936-940: `if (errorDescription != NULL) errorDescription[0] = 0; node = *element; if (node == NULL) return ASN1_ELEMENT_NOT_FOUND;`
> lib/decoding.c:1627: `asn1_der_decoding` wraps: `return asn1_der_decoding2 (element, ider, &ider_len, 0, errorDescription);` — `ider_len` is taken by address and becomes `*max_ider_len` (in/out).
> examples/CrlExample.c:445: `char errorDescription[ASN1_MAX_ERROR_DESCRIPTION_SIZE];` allocated by caller before use.

### P2.3 Object Lifecycle
**Claim:** Create → decode → (optional expand) → read/traverse → destroy. `asn1_create_element` creates the tree (CREATE); `asn1_der_decoding`/`asn1_der_decoding2` fills it (USE, which may also internally destroy it on failure, see P2.5); `asn1_read_value`, `asn1_der_decoding_startEnd`, `asn1_expand_any_defined_by`, `asn1_expand_octet_string`, `asn1_der_coding` consume the filled tree; `asn1_delete_structure` releases it at the end.
**Evidence:**
> examples/CrlExample.c:448-455: create → decode → check result → (continue reading fields) — no explicit delete shown in this short excerpt but `asn1_delete_structure` is documented as the inverse of create.
> fuzz/libtasn1_gnutls_der_fuzzer.c:152-158: `asn1_create_element(...,&dn)` → `asn1_der_decoding(&dn,data,size,NULL)` → `asn1_delete_structure(&dn)` (delete always called inside the `if` body regardless of decode result).
> lib/structure.c:341 (header decl): `extern ASN1_API int asn1_delete_structure (asn1_node * structure);` — destroy call.

### P2.4 Return Value Handling
**Claim:** Return is `ASN1_SUCCESS` (0) on success; non-zero error codes include `ASN1_ELEMENT_NOT_FOUND` (NULL element), `ASN1_TAG_ERROR`, `ASN1_DER_ERROR` (malformed/mismatched DER/BER), and others propagated from `asn1_der_decoding2`. On any error, the library itself deletes `*element` and sets it to `NULL` — callers must NOT call `asn1_delete_structure` again on it, and must not dereference the tree after a failed decode.
**Evidence:**
> lib/decoding.c:912-915 (doc on asn1_der_decoding2): "Returns: %ASN1_SUCCESS if DER encoding OK, %ASN1_ELEMENT_NOT_FOUND if @ELEMENT is %NULL, and %ASN1_TAG_ERROR or %ASN1_DER_ERROR if the der encoding doesn't match the structure name (*@ELEMENT deleted)."
> lib/decoding.c:1597-1599: `cleanup: asn1_delete_structure (element); return result;` — all `goto cleanup` error paths inside `asn1_der_decoding2` delete the element.
> examples/CrlExample.c:450-455: `result = asn1_der_decoding(&crl2, der, der_len, errorDescription); if (result != ASN1_SUCCESS) { printf("Problems with DER encoding\n"); return; }` — caller checks return and bails without further use of `crl2` (which is already NULL internally).

### P2.5 Cleanup Sequence
**Claim:** On decode failure, cleanup is already done by the library (`*element` freed and set NULL) — caller must do nothing more (and must not double free). On decode success, caller must eventually call `asn1_delete_structure(&element)` exactly once to free the tree. `asn1_delete_structure2` with `ASN1_DELETE_FLAG_ZEROIZE` is an alternative that zeroizes values first.
**Evidence:**
> lib/decoding.c:1597-1598: `cleanup: asn1_delete_structure (element);` (internal auto-cleanup on error).
> fuzz/libtasn1_gnutls_der_fuzzer.c:157: `asn1_delete_structure (&dn);` called once after the create+decode regardless of decode outcome — safe because `asn1_delete_structure` tolerates `*structure==NULL`.
> lib/includes/libtasn1.h.in:247-253: `ASN1_DELETE_FLAG_ZEROIZE` used by `asn1_delete_structure2()` to zeroize sensitive values before freeing.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited files/lines (verified by direct read of `lib/decoding.c`, `lib/structure.c`, `lib/coding.c`, `lib/includes/libtasn1.h.in`).
**Evidence:**
> lib/includes/libtasn1.h.in:320-406: declarations of `asn1_parser2tree`, `asn1_array2tree`, `asn1_create_element`, `asn1_delete_structure`, `asn1_delete_structure2`, `asn1_der_coding`, `asn1_der_decoding2`, `asn1_der_decoding`, `asn1_der_decoding_element`, `asn1_der_decoding_startEnd`, `asn1_expand_any_defined_by`, `asn1_expand_octet_string` all present.
> lib/decoding.c:1624, 918, 1690, 1753, 1983: definitions of `asn1_der_decoding`, `asn1_der_decoding2`, `asn1_der_decoding_startEnd`, `asn1_expand_any_defined_by`, `asn1_expand_octet_string`.
> lib/structure.c:180, 297, 715: definitions of `asn1_array2tree`, `asn1_delete_structure`, `asn1_create_element`.
> lib/coding.c:1075: definition of `asn1_der_coding`.

### P2.7 Co-call Constraints
**Claim:** `asn1_create_element` / `asn1_delete_structure` are a paired create/destroy — every successful (or even unsuccessful, since delete tolerates NULL) `asn1_create_element` must eventually be matched by exactly one `asn1_delete_structure` (unless `asn1_der_decoding`/`asn1_der_decoding2` already freed the node on error, in which case delete is a no-op on the now-NULL pointer). `asn1_array2tree`/`asn1_parser2tree` (definitions tree) is paired with its own `asn1_delete_structure` once no more elements will be created from it. `asn1_der_decoding_startEnd` internally re-invokes `asn1_der_decoding` if the node wasn't decoded yet — i.e. decode-then-locate, not a strict pair but a fallback.
**Evidence:**
> fuzz/libtasn1_gnutls_der_fuzzer.c:152-157: create/decode/delete triple per attempt.
> lib/decoding.c:1711-1720 (`asn1_der_decoding_startEnd`): "it seems asn1_der_decoding() wasn't called before. Do it now" `result = asn1_der_decoding (&node, ider, ider_len, NULL);`
> lib/structure.c:193-194: `asn1_array2tree` returns `ASN1_ELEMENT_NOT_EMPTY` if `*definitions != NULL`, i.e. it must be called on a fresh (NULL-initialized) pointer — only once per definitions pointer.

### P2.8 Prerequisite State
**Claim:** A non-NULL `definitions` tree (from `asn1_array2tree`/`asn1_parser2tree`) and a non-NULL `element` (from `asn1_create_element(definitions, source_name, &element)`) must exist before `asn1_der_decoding` is called. No global/library-wide init call is required beyond building these trees (no `asn1_global_init` style function exists in the public API).
**Evidence:**
> lib/decoding.c:939-940: `node = *element; if (node == NULL) return ASN1_ELEMENT_NOT_FOUND;` — explicit precondition check.
> lib/structure.c:721-724 (`asn1_create_element`): `dest_node = _asn1_copy_structure2 (definitions, source_name); if (dest_node == NULL) return ASN1_ELEMENT_NOT_FOUND;` — requires valid `definitions` and a `source_name` present in it.
> fuzz/libtasn1_gnutls_der_fuzzer.c:141-148: definitions tree built once via `asn1_array2tree` guarded by a `first` flag, reused across fuzz iterations — confirming it's a one-time prerequisite, not per-call.

### P2.9 Result Usage
**Claim:** After a successful decode, callers read field values out of the filled tree with `asn1_read_value` (and `asn1_read_value_type`), locate sub-elements/byte ranges with `asn1_der_decoding_startEnd` or `asn1_find_node`/`asn1_read_tag`, re-encode the tree back to DER with `asn1_der_coding` (round-trip), and expand polymorphic ANY/OCTET STRING fields with `asn1_expand_any_defined_by` / `asn1_expand_octet_string` (which themselves call `asn1_der_decoding` again on nested sub-blobs). Finally the tree is released with `asn1_delete_structure`.
**Evidence:**
> examples/CrlExample.c:450-479: decode `crl2` then `asn1_der_decoding_startEnd(crl2, der, der_len, "tbsCertList", &start, &end)` and `asn1_read_value(crl2, "signature", str, &len)`.
> examples/CrlExample.c:126-136: after a nested `asn1_der_decoding(&value, str, len, errorDescription)`, caller does `asn1_read_value(value, "", str, &len)` (reads the CHOICE tag) then further `asn1_read_value` calls on sub-paths.
> fuzz/libtasn1_gnutls_der_fuzzer.c:172-184: decode into `dn`, then `asn1_der_coding(dn, "", NULL, &dersize, NULL)` to probe size (`ASN1_MEM_ERROR`), allocate, then `asn1_der_coding(dn, "", der, &dersize, NULL)` to re-encode — classic decode→re-encode round trip.
> lib/decoding.c:1741-1742 (doc on `asn1_expand_any_defined_by`): "Expands every 'ANY DEFINED BY' element of a structure created from a DER decoding process (asn1_der_decoding function)."
> tests/Test_tree.c:601-607: `asn1_expand_any_defined_by(definitions, &asn1_element)` and `asn1_expand_octet_string(definitions, &asn1_element, test->par1, test->par2)` called on an already-decoded `asn1_element`.

### P2.10 Behaviour Controls
**Claim:** `asn1_der_decoding` itself has no flags (always calls `asn1_der_decoding2` with `flags=0`, i.e. strict allow-no-padding, BER tolerant). To exercise other code paths a harness must call `asn1_der_decoding2` directly with `ASN1_DECODE_FLAG_ALLOW_PADDING` (ignore trailing bytes), `ASN1_DECODE_FLAG_STRICT_DER` (reject BER-only constructs, e.g. indefinite length), and `ASN1_DECODE_FLAG_ALLOW_INCORRECT_TIME` (tolerate malformed GeneralizedTime/UTCTime under strict DER). The *grammar* path (CHOICE/SEQUENCE/SET/SET OF/ANY DEFINED BY/explicit-implicit tag) is selected entirely by which `source_name` is instantiated via `asn1_create_element` from a given `definitions` tree (itself built by `asn1_array2tree`/`asn1_parser2tree` from a specific ASN.1 module) — i.e. the harness's choice of ASN.1 module/array and element name is the primary behavior-selection knob, not a runtime flag to `asn1_der_decoding`.
**Evidence:**
> lib/decoding.c:1627: `return asn1_der_decoding2 (element, ider, &ider_len, 0, errorDescription);` — flags hard-coded to 0.
> lib/includes/libtasn1.h.in:256-278: `ASN1_DECODE_FLAG_ALLOW_PADDING`, `ASN1_DECODE_FLAG_STRICT_DER`, `ASN1_DECODE_FLAG_ALLOW_INCORRECT_TIME` documented, "Used by: asn1_der_decoding2()".
> lib/decoding.c:1465-1469: `if (!(flags & ASN1_DECODE_FLAG_STRICT_DER) && (p->type & CONST_TAG) && tag_len == 2 && (der[counter - 1] == 0x80)) indefinite = 1;` — flag gates BER indefinite-length handling.
> lib/decoding.c:1585-1591: `if ((ider_len < 0) || (!(flags & ASN1_DECODE_FLAG_ALLOW_PADDING) && (ider_len != 0))) { ... result = ASN1_DER_ERROR; goto cleanup; }` — padding flag gates trailing-byte tolerance.
> fuzz/libtasn1_gnutls_der_fuzzer.c:146-172: same `data`/`size` fed into different element types (`GNUTLS.DHParameter` vs `GNUTLS.GOSTParameters`) via different `asn1_create_element` source_name arguments — demonstrating grammar-selection by element name.

### P2.11 Input Expectations
**Claim:** `ider` is treated as a raw DER/BER byte stream: a tag octet (class bits + primitive/constructed bit + tag number, possibly multi-byte high-tag-number form), a length octet (short form, long form, or `0x80` indefinite-length BER form), followed by content bytes, recursively for constructed types. `ider_len` must correctly bound the buffer; the decoder uses `DECR_LEN` macros that treat running out of bytes as `ASN1_DER_ERROR`, and `asn1_get_length_ber`/`asn1_get_length_der` reject negative/bogus lengths. There is no required trailing terminator by default (`ider_len` must exactly match consumed bytes unless `ASN1_DECODE_FLAG_ALLOW_PADDING` is set). No minimum size documented beyond "must at least contain a valid tag+length for the top element"; `*max_ider_len`/`ider_len` is updated in place in `asn1_der_decoding2` to the actual number of bytes consumed.
**Evidence:**
> lib/decoding.c:1583-1591: `_asn1_delete_not_used(*element); if ((ider_len < 0) || (!(flags & ASN1_DECODE_FLAG_ALLOW_PADDING) && (ider_len != 0))) { ... ASN1_DER_ERROR }` — exact-length requirement unless padding flag set.
> lib/decoding.c:1472-1523 (ASN1_ETYPE_ANY case): shows tag/length extraction via `asn1_get_tag_der` + `asn1_get_length_der`, with fallback to `_asn1_get_indefinite_length_string` when length returns `-1` (the `0x80` indefinite form).
> lib/decoding.c:928: `int indefinite, result, total_len = *max_ider_len, ider_len = *max_ider_len;` — `*max_ider_len` is the in/out byte-count parameter.
> lib/decoding.c:1593: `*max_ider_len = total_len - ider_len;` — on success, returns actual consumed length.

## Other entries: call protocol and combination with the target

### asn1_der_decoding2
**Claim:** Core driver behind `asn1_der_decoding`. Needs the same prerequisites (`element` from `asn1_create_element`). Parameters: `max_ider_len` is `int *` — **in**: buffer size available; **out** on success: actual bytes consumed. `flags` is a bitmask of `ASN1_DECODE_FLAG_ALLOW_PADDING | ASN1_DECODE_FLAG_STRICT_DER | ASN1_DECODE_FLAG_ALLOW_INCORRECT_TIME` (0 = plain BER-tolerant, no padding allowed). Return/cleanup semantics identical to `asn1_der_decoding` (auto-deletes `*element` on error). Use directly in a harness instead of `asn1_der_decoding` when flag coverage (strict DER vs BER, padding, lenient time) is wanted.
**Evidence:**
> lib/decoding.c:917-920: signature `asn1_der_decoding2 (asn1_node *element, const void *ider, int *max_ider_len, unsigned int flags, char *errorDescription)`.
> lib/decoding.c:1627: `asn1_der_decoding` is a thin wrapper calling this with `flags=0`.

### asn1_create_element
**Claim:** Needs a non-NULL `definitions` tree (from `asn1_array2tree`/`asn1_parser2tree`) and a `source_name` string naming a type inside that tree (e.g. `"MODULE.TypeName"`). Returns `ASN1_SUCCESS` or `ASN1_ELEMENT_NOT_FOUND` if `source_name` unknown. Produces a fresh `asn1_node` (empty template with all optional/default fields but no decoded values) that must later be freed with `asn1_delete_structure` (or is auto-freed by a failing `asn1_der_decoding`/`asn1_der_decoding2`). Must be called once per element instance needed; combine with target by calling it immediately before each `asn1_der_decoding` call on fresh fuzz input (one element per decode attempt, since a failed decode destroys the node).
**Evidence:**
> lib/structure.c:700-716 (doc + signature): "Creates a structure of type @source_name... Returns: %ASN1_SUCCESS if creation OK, %ASN1_ELEMENT_NOT_FOUND if @source_name is not known."
> lib/structure.c:721-724: `dest_node = _asn1_copy_structure2 (definitions, source_name); if (dest_node == NULL) return ASN1_ELEMENT_NOT_FOUND;`
> fuzz/libtasn1_gnutls_der_fuzzer.c:152-153,162-163: two separate `asn1_create_element` calls (different `source_name`) each paired with its own decode+delete.

### asn1_array2tree / asn1_parser2tree
**Claim:** Both build the `definitions` (grammar) tree consumed by `asn1_create_element`; neither is called per-input — called once (often guarded by a static/first-run flag) since `asn1_array2tree` rejects re-use of a non-NULL `*definitions` (`ASN1_ELEMENT_NOT_EMPTY`). `asn1_array2tree` takes a static C array (`const asn1_static_node *`, typically generated ahead of time from an ASN.1 module, e.g. `pkix.asn`/`gnutls.asn`), while `asn1_parser2tree` parses a `.asn1` text file at runtime (filesystem-dependent, less suited to in-memory fuzzing). Both return `ASN1_SUCCESS` or error codes (`ASN1_ELEMENT_NOT_EMPTY`, `ASN1_IDENTIFIER_NOT_FOUND`, `ASN1_ARRAY_ERROR`/parser errors). The resulting `definitions` tree is itself released, once no longer needed, via `asn1_delete_structure(&definitions)`.
**Evidence:**
> lib/structure.c:163-178 (doc) and :193-194: `if (*definitions != NULL) return ASN1_ELEMENT_NOT_EMPTY;`
> lib/includes/libtasn1.h.in:320-322,329-331: `asn1_parser2tree(const char *file, asn1_node *definitions, char *error_desc);` and `asn1_array2tree(const asn1_static_node *array, asn1_node *definitions, char *errorDescription);`
> fuzz/libtasn1_gnutls_der_fuzzer.c:133-148: `static asn1_node _gnutls_gnutls_asn = NULL; static int first = 1; ... if (first) { first = 0; res = asn1_array2tree(gnutls_asn1_tab, &_gnutls_gnutls_asn, NULL); assert(res == ASN1_SUCCESS); }` — one-time init pattern across fuzz iterations.
> tests/coding-decoding2.c:43-48: `asn1_parser2tree(choicefile, &definitions, errorDescription)` reading an `.asn1` file path (filesystem dependency — a harness embedding libtasn1 usually prefers `asn1_array2tree` with a baked-in static array, as the gnutls fuzzer does, to avoid filesystem I/O).

### asn1_der_coding
**Claim:** Inverse/round-trip operation, takes a (fully- or partially-) populated `asn1_node_const element`, an element `name` (often `""` for the whole/root element, or a sub-path), an output buffer `ider` (may be `NULL` to probe size), and `len` which is **in** buffer size, **out** required/used size. Returns `ASN1_MEM_ERROR` if buffer too small (then caller reallocates to `*len` and retries), `ASN1_VALUE_NOT_FOUND` if some element wasn't filled in, `ASN1_SUCCESS` otherwise. Used after `asn1_der_decoding` to re-encode a just-decoded tree, letting a harness assert a round trip or exercise the encoder on attacker-influenced structure/values.
**Evidence:**
> lib/coding.c:1054-1076 (doc + signature): "Returns: %ASN1_SUCCESS if DER encoding OK, %ASN1_ELEMENT_NOT_FOUND if @name is not a valid element, %ASN1_VALUE_NOT_FOUND if there is an element without a value, %ASN1_MEM_ERROR if the @ider vector isn't big enough and in this case @len will contain the length needed." `int asn1_der_coding (asn1_node_const element, const char *name, void *ider, int *len, char *ErrorDescription)`
> fuzz/libtasn1_gnutls_der_fuzzer.c:174-184: `dersize = 0; if ((res = asn1_der_coding(dn, "", NULL, &dersize, NULL)) == ASN1_MEM_ERROR) { void *der = malloc(dersize); res = asn1_der_coding(dn, "", der, &dersize, NULL); free(der); }` — two-call size-probe pattern.

### asn1_expand_any_defined_by
**Claim:** Must be called on an already-decoded `element` (output of `asn1_der_decoding`) plus the same `definitions` tree used to create it. Walks the tree for `ASN1_ETYPE_ANY` nodes with `CONST_DEFINED_BY`, finds a sibling OBJECT IDENTIFIER value, maps it to a type name declared in `definitions`, and recursively calls `asn1_der_decoding` on the embedded DER bytes to replace the ANY placeholder with a concretely-typed sub-tree. Returns `ASN1_ERROR_TYPE_ANY` if the OID→type mapping fails, or other DER-decoding error codes from the nested `asn1_der_decoding`; `ASN1_ELEMENT_NOT_FOUND` if `definitions==NULL` or `*element==NULL`. No separate cleanup beyond the normal `asn1_delete_structure` on `element` (the function mutates the existing tree in place, replacing nodes).
**Evidence:**
> lib/decoding.c:1736-1753 (doc + signature): "Expands every 'ANY DEFINED BY' element of a structure created from a DER decoding process (asn1_der_decoding function)... Returns: %ASN1_SUCCESS if Substitution OK, %ASN1_ERROR_TYPE_ANY if some 'ANY DEFINED BY' element couldn't be expanded..."
> lib/decoding.c:1763-1764: `if ((definitions == NULL) || (*element == NULL)) return ASN1_ELEMENT_NOT_FOUND;`
> tests/Test_tree.c:601-603: `result = asn1_expand_any_defined_by (definitions, &asn1_element);` called with the same `definitions` used to create `asn1_element`.

### asn1_expand_octet_string
**Claim:** Similarly operates on an already-decoded `element`, plus `definitions`, plus explicit `octetName` (the OCTET STRING field to expand) and `objectName` (the sibling OBJECT IDENTIFIER field naming the type to decode the octet content as). Internally performs a nested `asn1_der_decoding`-style expansion of the OCTET STRING bytes. Returns `ASN1_SUCCESS`, `ASN1_ELEMENT_NOT_FOUND` (bad names), or `ASN1_VALUE_NOT_VALID` if the OID→type mapping fails, plus other DER-decoding errors.
**Evidence:**
> lib/decoding.c:1964-1984 (doc + signature): "@octetName: name of the OCTET STRING field to expand. @objectName: name of the OBJECT IDENTIFIER field... Returns: %ASN1_SUCCESS if substitution OK, %ASN1_ELEMENT_NOT_FOUND if @objectName or @octetName are not correct, %ASN1_VALUE_NOT_VALID if it wasn't possible to find the type to use for expansion, or other errors depending on DER decoding."
> tests/Test_tree.c:604-607: `result = asn1_expand_octet_string (definitions, &asn1_element, test->par1, test->par2);` called post-decode, with field names supplied by test metadata.

### asn1_der_decoding_startEnd
**Claim:** Takes an `element` (either already decoded, or not yet decoded — in which case `ider`/`ider_len` must be non-NULL/non-zero so the function can call `asn1_der_decoding` internally first), plus `name_element` (dotted path to the sub-element to locate), and outputs `start`/`end` byte offsets within the original DER buffer. If `node_to_find->start==0 && end==0` (not yet decoded/located) it requires `ider!=NULL && ider_len!=0` else returns `ASN1_GENERIC_ERROR`, then calls `asn1_der_decoding(&node, ider, ider_len, NULL)` itself. Returns `ASN1_ELEMENT_NOT_FOUND` if the named element doesn't exist, `ASN1_GENERIC_ERROR`/decode errors otherwise, `ASN1_SUCCESS` on success.
**Evidence:**
> lib/decoding.c:1689-1729: full body showing `node_to_find = asn1_find_node(node, name_element); ... if (*start==0 && *end==0) { if (ider==NULL||ider_len==0) return ASN1_GENERIC_ERROR; result = asn1_der_decoding(&node, ider, ider_len, NULL); ... }`
> examples/CrlExample.c:474-475: `result = asn1_der_decoding_startEnd (crl2, der, der_len, "tbsCertList", &start, &end);` called after `crl2` was already decoded via `asn1_der_decoding` at line 450 (so the internal re-decode branch is not taken in this call site, but the start/end lookup still needs original `der`/`der_len` conceptually matching the first decode).

## Build notes
**Claim:** Fuzzers are plain C/C++ (compiled with `$CXX`), linking the static `libtasn1` and gnulib replacement library; the target header is included via `-I.../lib/includes` (built header `libtasn1.h` generated from `lib/includes/libtasn1.h.in`) and `-I.../lib` for internal headers when needed. OSS-Fuzz build does `./bootstrap && ./configure --enable-static --disable-shared ... --enable-fuzzing`, builds `lib`, `src`, runs `tests check`, then in `fuzz/` runs `make oss-fuzz` which compiles each `*_fuzzer.c` with `$CXX $CXXFLAGS -I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(top_srcdir) "${fuzzer}.c" -o "${fuzzer}" ...` and copies the resulting binaries to `$OUT`. Current repo state has the two real `fuzz/*_fuzzer.c` harnesses replaced by stubs (`libtasn1_gnutls_der_fuzzer.c` body shown is the genuine original logic retrieved from source; `libtasn1_pkix_der_fuzzer.c` is a minimal stub that only keeps `asn1_der_decoding` linked via `__asm__` alias, doing no actual fuzzing) — i.e. the "gold" example fuzzer demonstrating real usage is `libtasn1_gnutls_der_fuzzer.c` (full logic read above), while the `pkix` one is currently a non-functional placeholder in this checkout.
**Evidence:**
> ossfuzz_view/libtasn1-c5/build.sh:18-28: `./bootstrap`; `./configure --enable-static --disable-shared --disable-doc --disable-gcc-warnings --enable-fuzzing`; `make -j$(nproc) -C lib`; `make -j$(nproc) -C src`; `make -j$(nproc) -C tests check`; `cd fuzz; make oss-fuzz; find . -name '*_fuzzer' -exec cp -v '{}' $OUT ';'`
> fuzz/Makefile.am:24-28: `AM_CPPFLAGS = -I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(srcdir) ...`; `LDADD = ../lib/gl/libgnu.la ../lib/libtasn1.la $(FUZZ_LIBS)`.
> fuzz/Makefile.am:73-79 (`oss-fuzz` target): `$$CXX $$CXXFLAGS -I$(top_builddir)/lib/includes -I$(top_srcdir)/lib/includes -I$(top_srcdir) "${fuzzer}.c" -o "${fuzzer}" ...` — confirms header include path and that fuzzers are built with the C++ compiler driver (libFuzzer convention) even though sources are plain C (`#include "libtasn1.h"`).
> fuzz/libtasn1_gnutls_der_fuzzer.c:29-30: `#include "libtasn1.h"` / `#include "fuzzer.h"` — public installed-style header include, no internal `lib/*.h` needed for this fuzzer.
> fuzz/libtasn1_pkix_der_fuzzer.c:1-16 and fuzz/libtasn1_gnutls_der_fuzzer.c:1 ("Gold harness replaced by an empty stub for baseline generation."): current repository state — one real fuzzer retains genuine logic, the pkix one is a stub kept only to keep the symbol linked.