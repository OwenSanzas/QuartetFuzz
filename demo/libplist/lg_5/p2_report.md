## P2 Report: `plist_from_memory` and `plist_write_to_string`

---

## target: plist_from_memory

### P2.6 API Existence
**Claim:** Function exists and is public API.
**Evidence:**
> include/plist/plist.h:1064: `PLIST_API plist_err_t plist_from_memory(const char *plist_data, uint32_t length, plist_t *plist, plist_format_t *format);`
> src/plist.c:226-304: Implementation defined.

### P2.1 Init Sequence
**Claim:** No global initialization required. The function auto-detects format from the input data. Just pass raw data buffer directly.
**Evidence:**
> src/plist.c:226-304: Function directly inspects data to detect format (XML, binary, JSON, OpenStep) and dispatches to the appropriate parser. No prior init calls needed.
> include/plist/plist.h:1055-1064: No documented preconditions beyond non-NULL inputs.

### P2.2 Parameter Construction
**Claim:** 
- `plist_data`: `const char*` — raw plist data buffer (fuzz input). Must not be NULL.
- `length`: `uint32_t` — size of the data buffer.
- `plist`: `plist_t*` — output pointer, receives parsed plist tree. Must not be NULL.
- `format`: `plist_format_t*` — optional output, receives detected format. Can be NULL.
**Evidence:**
> include/plist/plist.h:1057-1064: Full parameter documentation.
> src/plist.c:228-231: NULL checks on `plist_data` and `plist` return `PLIST_ERR_INVALID_ARG`.

### P2.3 Object Lifecycle
**Claim:** On success, `*plist` is allocated internally and must be freed with `plist_free()`.
**Evidence:**
> include/plist/plist.h:1059: "@param plist a pointer to a plist_t that will be set to ... the root node"
> include/plist/plist.h:1139: `PLIST_API void plist_free(plist_t plist);` — documented free function.

### P2.4 Return Value Handling
**Claim:** Returns `plist_err_t`. `PLIST_ERR_SUCCESS` (0) on success. Various error codes on failure (PLIST_ERR_INVALID_ARG, PLIST_ERR_FORMAT, PLIST_ERR_PARSE, etc.). On error, `*plist` may be NULL.
**Evidence:**
> include/plist/plist.h:1063: "@return PLIST_ERR_SUCCESS on success or a #plist_err_t on failure"
> src/plist.c:228-231: Returns PLIST_ERR_INVALID_ARG for NULL inputs.

### P2.5 Cleanup Sequence
**Claim:** Call `plist_free(*plist)` after use. Safe to call even if parse failed (check for NULL first).
**Evidence:**
> include/plist/plist.h:1139: `PLIST_API void plist_free(plist_t plist);`

---

## target: plist_write_to_string

### P2.6 API Existence
**Claim:** Function exists and is public API.
**Evidence:**
> include/plist/plist.h:1094: `PLIST_API plist_err_t plist_write_to_string(plist_t plist, char **output, uint32_t* length, plist_format_t format, plist_write_options_t options);`
> src/plist.c:2399-2427: Implementation defined.

### P2.1 Init Sequence
**Claim:** Requires a valid `plist_t` tree (e.g., from `plist_from_memory`). No other initialization needed.
**Evidence:**
> src/plist.c:2399-2427: Function directly dispatches to format-specific serializers based on `format` parameter.

### P2.2 Parameter Construction
**Claim:**
- `plist`: `plist_t` — input plist tree, must be valid (non-NULL).
- `output`: `char**` — pointer to receive allocated string buffer. Must not be NULL.
- `length`: `uint32_t*` — pointer to receive output length. Must not be NULL.
- `format`: `plist_format_t` — output format enum. Supported values: `PLIST_FORMAT_XML` (1), `PLIST_FORMAT_JSON` (3), `PLIST_FORMAT_OSTEP` (4), `PLIST_FORMAT_PRINT` (10), `PLIST_FORMAT_LIMD` (11), `PLIST_FORMAT_PLUTIL` (12). `PLIST_FORMAT_BINARY` (2) is NOT supported (returns PLIST_ERR_FORMAT).
- `options`: `plist_write_options_t` — bitwise OR of option flags. `PLIST_OPT_COMPACT` (1), `PLIST_OPT_PARTIAL_DATA` (2), `PLIST_OPT_NO_NEWLINE` (4), `PLIST_OPT_INDENT` (8). Can be 0 for defaults.
**Evidence:**
> include/plist/plist.h:1081-1094: Full parameter documentation.
> include/plist/plist.h:1092: "@note #PLIST_FORMAT_BINARY is not supported by this function."
> include/plist/plist.h:157-166: Format enum values.
> include/plist/plist.h:174-182: Write options enum values.
> src/plist.c:2421-2424: Default case returns PLIST_ERR_FORMAT for unsupported formats.

### P2.3 Object Lifecycle
**Claim:** On success, `*output` is allocated internally and must be freed with `plist_mem_free()`.
**Evidence:**
> include/plist/plist.h:1091: "@note Use plist_mem_free() to free the allocated memory."
> include/plist/plist.h:1431: `PLIST_API void plist_mem_free(void* ptr);`

### P2.4 Return Value Handling
**Claim:** Returns `plist_err_t`. `PLIST_ERR_SUCCESS` on success. `PLIST_ERR_FORMAT` for unsupported format. Other errors possible from sub-serializers.
**Evidence:**
> src/plist.c:2401: Initialized to `PLIST_ERR_UNKNOWN`.
> src/plist.c:2421-2424: Returns `PLIST_ERR_FORMAT` for unsupported formats.

### P2.5 Cleanup Sequence
**Claim:** Free `*output` with `plist_mem_free()` (NOT `free()`). Free the plist tree with `plist_free()`.
**Evidence:**
> include/plist/plist.h:1091: "@note Use plist_mem_free() to free the allocated memory."
> include/plist/plist.h:1431: `PLIST_API void plist_mem_free(void* ptr);`

### P2.7 Co-call Constraints
**Claim:** This function is the serialization counterpart to `plist_from_memory` (deserialization). The intended pattern is: parse with `plist_from_memory` → serialize with `plist_write_to_string` → free output with `plist_mem_free` → free tree with `plist_free`.
**Evidence:**
> src/plist.c:226-304: `plist_from_memory` parses data into plist_t.
> src/plist.c:2399-2427: `plist_write_to_string` serializes plist_t to string.

### P2.8 Prerequisite State
**Claim:** For the pretty-print formatters (PLIST_FORMAT_PRINT=10, PLIST_FORMAT_LIMD=11, PLIST_FORMAT_PLUTIL=12), the plist tree must be a valid parsed tree. These are output-only formats — they cannot be parsed back by `plist_from_memory`. For fuzzing round-trips, parse any supported input format, then serialize to one of the print formats.
**Evidence:**
> include/plist/plist.h:163-165: PRINT/LIMD/PLUTIL described as "output-only format".

---

## Recommended Harness Pattern

```c
// Pseudocode for fuzz harness
int LLVMFuzzerTestOneTarget(const uint8_t *data, size_t size) {
    if (size == 0 || size > UINT32_MAX) return 0;
    
    plist_t plist = NULL;
    plist_format_t format;
    
    // Parse input
    plist_err_t err = plist_from_memory((const char*)data, (uint32_t)size, &plist, &format);
    if (err != PLIST_ERR_SUCCESS || plist == NULL) {
        plist_free(plist);  // safe even if NULL
        return 0;
    }
    
    // Serialize to each pretty-print format
    plist_format_t formats[] = {PLIST_FORMAT_PRINT, PLIST_FORMAT_LIMD, PLIST_FORMAT_PLUTIL};
    for (int i = 0; i < 3; i++) {
        char *output = NULL;
        uint32_t length = 0;
        err = plist_write_to_string(plist, &output, &length, formats[i], 0);
        if (err == PLIST_ERR_SUCCESS && output) {
            plist_mem_free(output);
        }
    }
    
    plist_free(plist);
    return 0;
}
```