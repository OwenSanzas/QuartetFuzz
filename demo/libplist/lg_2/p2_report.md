# P2 Report: `plist_to_bin` and `plist_from_bin`

## target: plist_from_bin

### P2.1 Init Sequence
**Claim:** No global initialization is required. The function is self-contained — just call it directly with valid arguments.
**Evidence:**
> include/plist/plist.h:1016-1024: Declaration shows no precondition beyond valid arguments.
> test/plist_btest.c:80: `plist_from_bin(plist_bin, size_in, &root_node1);` — called directly, no prior init.
> test/plist_test.c:96: `plist_from_bin(plist_bin, size_out, &root_node2);` — called directly.

### P2.2 Parameter Construction
**Claim:** Signature is `plist_err_t plist_from_bin(const char *plist_bin, uint32_t length, plist_t *plist)`.
- `plist_bin`: pointer to binary plist data buffer (const, not modified).
- `length`: size of the buffer in bytes (uint32_t).
- `plist`: output pointer; must be a valid `plist_t*` (will be set to NULL initially by the function).
**Evidence:**
> include/plist/plist.h:1024: `PLIST_API plist_err_t plist_from_bin(const char *plist_bin, uint32_t length, plist_t * plist);`
> src/bplist.c:917-923: NULL checks: returns `PLIST_ERR_INVALID_ARG` if `plist` is NULL, or if `plist_bin` is NULL or `length == 0`. Sets `*plist = NULL` at entry.

### P2.3 Object Lifecycle
**Claim:** The function allocates a plist tree internally. On success, `*plist` is set to the root node. The caller must free it with `plist_free()`.
**Evidence:**
> src/bplist.c:920: `*plist = NULL;` — output initialized to NULL.
> test/plist_btest.c:120: `plist_free(root_node1);` — cleanup after use.
> test/plist_test.c:120: `plist_free(root_node1);` — same pattern.

### P2.4 Return Value Handling
**Claim:** Returns `plist_err_t`. Check for `PLIST_ERR_SUCCESS` (0). On error, `*plist` remains NULL. Possible errors: `PLIST_ERR_INVALID_ARG` (NULL args or zero length), `PLIST_ERR_PARSE` (malformed data — bad magic, bad version, bad trailer, bad offsets), `PLIST_ERR_NO_MEM`.
**Evidence:**
> src/bplist.c:917-918: `if (!plist) { return PLIST_ERR_INVALID_ARG; }`
> src/bplist.c:921-923: `if (!plist_bin || length == 0) { return PLIST_ERR_INVALID_ARG; }`
> src/bplist.c:926-928: `if (!(length >= BPLIST_MAGIC_SIZE + ...)) { return PLIST_ERR_PARSE; }`
> src/bplist.c:931-933: magic mismatch → `PLIST_ERR_PARSE`
> src/bplist.c:936-938: version mismatch → `PLIST_ERR_PARSE`

### P2.5 Cleanup Sequence
**Claim:** On success, free the resulting plist tree with `plist_free(*plist)`. On error, `*plist` is NULL, so no cleanup needed for the output. The input buffer is not modified or freed by the function.
**Evidence:**
> test/plist_btest.c:120: `plist_free(root_node1);`
> src/bplist.c:920: `*plist = NULL;` at entry ensures NULL on error paths.

### P2.6 API Existence
**Claim:** Confirmed to exist.
**Evidence:**
> include/plist/plist.h:1024: Declaration present.
> src/bplist.c:905: Definition present.

### P2.7 Co-call Constraints
**Claim:** No paired API required. The function is standalone for parsing. The output plist can be used with any plist API (plist_to_bin, plist_to_xml, etc.) and must eventually be freed with `plist_free()`.
**Evidence:**
> test/plist_btest.c:80-120: Full usage cycle: parse → use → free.

### P2.8 Prerequisite State
**Claim:** No prerequisite state. No context, config, or connection objects needed. The function only requires a valid binary plist buffer.
**Evidence:**
> src/bplist.c:905-923: Function only checks its direct arguments.

---

## target: plist_to_bin

### P2.1 Init Sequence
**Claim:** No global initialization required. The only prerequisite is a valid `plist_t` node (typically obtained from `plist_from_bin`, `plist_from_xml`, or constructed via `plist_new_*` APIs).
**Evidence:**
> include/plist/plist.h:920-930: Declaration with no documented preconditions beyond valid args.
> test/plist_btest.c:104: `plist_to_bin(root_node2, &plist_bin2, &size_out2);` — called with a plist obtained from `plist_from_xml`.
> test/plist_test.c:88: `plist_to_bin(root_node1, &plist_bin, &size_out);` — called with a plist obtained from `plist_from_xml`.

### P2.2 Parameter Construction
**Claim:** Signature is `plist_err_t plist_to_bin(plist_t plist, char **plist_bin, uint32_t *length)`.
- `plist`: a valid plist_t root node (must not be NULL).
- `plist_bin`: pointer to a `char*` that will receive the allocated buffer. Must not be NULL.
- `length`: pointer to a `uint32_t` that will receive the output size. Must not be NULL.
**Evidence:**
> include/plist/plist.h:930: `PLIST_API plist_err_t plist_to_bin(plist_t plist, char **plist_bin, uint32_t * length);`
> src/bplist.c:1378-1381: `if (!plist || !plist_bin || !length) { return PLIST_ERR_INVALID_ARG; }`

### P2.3 Object Lifecycle
**Claim:** The function allocates a binary buffer internally. On success, `*plist_bin` points to the allocated buffer and `*length` contains its size. The caller must free the buffer with `plist_mem_free()`. The input `plist` is NOT consumed or modified.
**Evidence:**
> include/plist/plist.h:928: `@note Use plist_mem_free() to free the allocated memory.`
> include/plist/plist.h:1420-1431: `plist_mem_free()` documentation lists `plist_to_bin()` as one of the functions whose output it frees.
> test/plist_btest.c:104,124: `plist_to_bin(root_node2, &plist_bin2, &size_out2);` ... `free(plist_bin2);` (Note: test uses `free()` but the API doc says `plist_mem_free()` — use `plist_mem_free()` for correctness).

### P2.4 Return Value Handling
**Claim:** Returns `plist_err_t`. Check for `PLIST_ERR_SUCCESS`. Possible errors: `PLIST_ERR_INVALID_ARG` (NULL args), `PLIST_ERR_NO_MEM` (allocation failure), `PLIST_ERR_CIRCULAR_REF` (circular reference detected during serialization), `PLIST_ERR_MAX_NESTING` (max nesting depth exceeded).
**Evidence:**
> src/bplist.c:1378-1381: NULL check → `PLIST_ERR_INVALID_ARG`
> src/bplist.c:1385-1387: objects alloc failure → `PLIST_ERR_NO_MEM`
> src/bplist.c:1389-1393: ref_table alloc failure → `PLIST_ERR_NO_MEM`
> src/bplist.c:1395-1400: in_stack alloc failure → `PLIST_ERR_NO_MEM`
> src/bplist.c:1406-1412: `serialize_plist` can return `PLIST_ERR_CIRCULAR_REF` or `PLIST_ERR_MAX_NESTING`

### P2.5 Cleanup Sequence
**Claim:** On success: free the output buffer with `plist_mem_free(*plist_bin)`. On error: `*plist_bin` is not set (remains at caller's initial value), so no buffer cleanup needed. The input `plist` is not affected and must be freed separately with `plist_free()` when no longer needed.
**Evidence:**
> include/plist/plist.h:928: `@note Use plist_mem_free() to free the allocated memory.`
> include/plist/plist.h:1428: `@note Do not use this function to free plist_t nodes, use plist_free() instead.`
> src/bplist.c:1407-1411: On serialize error, internal structures are freed and function returns early without setting `*plist_bin`.

### P2.6 API Existence
**Claim:** Confirmed to exist.
**Evidence:**
> include/plist/plist.h:930: Declaration present.
> src/bplist.c:1360: Definition present.

### P2.7 Co-call Constraints
**Claim:** No paired API. The function is standalone for serialization. The input plist must have been created by some plist construction API (plist_from_bin, plist_from_xml, plist_new_dict, etc.).
**Evidence:**
> test/plist_btest.c:104: Used after `plist_from_xml` created the tree.
> test/plist_test.c:88: Used after `plist_from_xml` created the tree.

### P2.8 Prerequisite State
**Claim:** No prerequisite state beyond having a valid plist_t. No context objects, no configuration, no connections.
**Evidence:**
> src/bplist.c:1360-1381: Function only checks its direct arguments.

---

## Round-Trip Harness Protocol Summary

For a fuzzer exercising the binary plist round-trip:

1. **Parse:** Call `plist_from_bin(fuzz_data, fuzz_size, &plist)` with fuzz input. Check return value; if not `PLIST_ERR_SUCCESS`, skip (no cleanup needed since `*plist` is NULL).
2. **Serialize:** Call `plist_to_bin(plist, &bin_out, &bin_len)`. Check return value.
3. **Cleanup (success path):** Free `bin_out` with `plist_mem_free(bin_out)`, then free `plist` with `plist_free(plist)`.
4. **Cleanup (error path):** If `plist_from_bin` fails, nothing to free. If `plist_to_bin` fails, only free `plist` with `plist_free(plist)`.
5. **Important:** Initialize `plist_bin` output pointer to NULL before calling `plist_to_bin` to safely handle error paths.
6. **Cast note:** Fuzz data is typically `const uint8_t*`; cast to `const char*` for the API. Size is `uint32_t` — clamp `fuzz_size` if needed (though libFuzzer sizes are typically well within uint32_t range).