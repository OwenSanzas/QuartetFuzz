# P2 Report: libplist Serialization/Output Entry Functions

## target: plist_from_memory

### P2.1 Init Sequence
**Claim:** No global initialization required. The function is self-contained — pass raw data and it auto-detects the format and parses it.
**Evidence:**
> include/plist/plist.h:1064: `PLIST_API plist_err_t plist_from_memory(const char *plist_data, uint32_t length, plist_t *plist, plist_format_t *format);`
> src/plist.c:225 (approx): Function checks `if (!plist) return PLIST_ERR_INVALID_ARG`, then skips whitespace and auto-detects format.
> cython/cnary.c:28: Called directly with buffer data, no prior init.

### P2.2 Parameter Construction
**Claim:** 
- `plist_data`: `const char*` — raw plist data (binary, XML, JSON, or OpenStep). Can be fuzz input directly.
- `length`: `uint32_t` — size of the data buffer.
- `plist`: `plist_t*` — output pointer, must be a valid pointer to a `plist_t` variable (initialized to NULL is fine).
- `format`: `plist_format_t*` — optional output, can be NULL. If non-NULL, receives the detected format.
**Evidence:**
> include/plist/plist.h:1058-1064: Full declaration with doc comments.
> src/Structure.cpp:135: `plist_from_memory(buf, size, &root, format)` — `format` passed as pointer or NULL.

### P2.3 Object Lifecycle
**Claim:** On success (`PLIST_ERR_SUCCESS`), `*plist` is set to a newly allocated plist tree. Caller must free with `plist_free()`. On error, `*plist` may be NULL.
**Evidence:**
> src/plist.c:306: `plist_read_from_file` calls `plist_from_memory(buf, size, &root, format)` then uses `root`.
> src/Structure.cpp:135-140: After `plist_from_memory`, result is used; cleanup via `plist_free` in destructor.

### P2.4 Return Value Handling
**Claim:** Returns `plist_err_t`. Check for `PLIST_ERR_SUCCESS` (0). Other values: `PLIST_ERR_INVALID_ARG` (-1), `PLIST_ERR_NO_MEM` (-3), `PLIST_ERR_UNKNOWN` (-255).
**Evidence:**
> include/plist/plist.h:144-150: Error enum definition.
> src/plist.c:225: Returns `PLIST_ERR_UNKNOWN` by default if format not detected.

### P2.5 Cleanup Sequence
**Claim:** On success, the returned `plist_t` must be freed with `plist_free(plist)`. On error, no cleanup needed (no allocation occurred).
**Evidence:**
> include/plist/plist.h:395: `PLIST_API void plist_free(plist_t plist);`

### P2.6 API Existence
**Claim:** Confirmed exists as public API.
**Evidence:**
> include/plist/plist.h:1064: Declared with `PLIST_API`.

### P2.7 Co-call Constraints
**Claim:** Paired with `plist_free()` for cleanup. No lock/unlock or begin/commit patterns.
**Evidence:**
> include/plist/plist.h:395: `plist_free` is the corresponding destructor.

### P2.8 Prerequisite State
**Claim:** No prerequisite state. The function operates on raw input data with no context objects.
**Evidence:**
> include/plist/plist.h:1064: Only takes data buffer, length, and output pointers.

---

## target: plist_to_bin

### P2.1 Init Sequence
**Claim:** No global initialization required. Requires a valid `plist_t` node (typically obtained from `plist_from_memory` or `plist_new_*` functions).
**Evidence:**
> src/bplist.c:1360 (approx): Function signature `plist_err_t plist_to_bin(plist_t plist, char **plist_bin, uint32_t * length)`
> src/Structure.cpp:64: `plist_to_bin(_node, &bin, &length)` — called directly on a plist node with no prior init.

### P2.2 Parameter Construction
**Claim:**
- `plist`: `plist_t` — a valid plist node tree (root node). Must not be NULL.
- `plist_bin`: `char**` — output pointer for the serialized binary data. Must be a valid pointer to a `char*`.
- `length`: `uint32_t*` — output pointer for the length of the serialized data.
**Evidence:**
> include/plist/plist.h (declaration): `PLIST_API plist_err_t plist_to_bin(plist_t plist, char **plist_bin, uint32_t * length);`
> src/Structure.cpp:64: `plist_to_bin(_node, &bin, &length)` — typical usage pattern.

### P2.3 Object Lifecycle
**Claim:** On success, `*plist_bin` is set to a newly `malloc`'d buffer and `*length` is set to its size. Caller must free the output buffer. The input `plist` is NOT consumed/freed — it remains valid.
**Evidence:**
> src/bplist.c:1611-1615: `*plist_bin = (char*)bplist_buff->data; *length = bplist_buff->len; bplist_buff->data = NULL; byte_array_free(bplist_buff);` — ownership transferred to caller.
> src/Structure.cpp:65-66: After `plist_to_bin`, result buffer used then freed.

### P2.4 Return Value Handling
**Claim:** Returns `plist_err_t`. `PLIST_ERR_SUCCESS` on success. Can return `PLIST_ERR_NO_MEM` on allocation failure, or error from `serialize_plist` (e.g., circular reference detection).
**Evidence:**
> src/bplist.c:1407-1411: Returns error from `serialize_plist` if it fails.
> src/bplist.c:1510-1513: Returns `PLIST_ERR_NO_MEM` if `bplist_buff` allocation fails.
> src/bplist.c:1617: Returns `PLIST_ERR_SUCCESS` at end.

### P2.5 Cleanup Sequence
**Claim:** On success: free `*plist_bin` with `plist_mem_free()` (or `free()`). The input `plist` is NOT freed by this function. On error: no output buffer allocated, nothing to free for the output.
**Evidence:**
> include/plist/plist.h: `PLIST_API void plist_mem_free(void* ptr);` — recommended way to free API-allocated buffers.
> src/plist.c: `plist_mem_free` wraps `free()`.

### P2.6 API Existence
**Claim:** Confirmed exists as public API.
**Evidence:**
> include/plist/plist.h: Declared with `PLIST_API`.
> src/bplist.c:1360+: Implementation present.

### P2.7 Co-call Constraints
**Claim:** Output buffer must be freed with `plist_mem_free()`. No other pairing constraints.
**Evidence:**
> src/Structure.cpp:64-66: Buffer freed after use.

### P2.8 Prerequisite State
**Claim:** Requires a valid plist tree as input. The tree can be created via `plist_from_memory()` or any `plist_new_*()` constructor.
**Evidence:**
> src/Structure.cpp:64: Uses `_node` member which is a parsed plist tree.

---

## target: plist_write_to_string

### P2.1 Init Sequence
**Claim:** No global initialization required. Requires a valid `plist_t` node. The `format` parameter selects the output format.
**Evidence:**
> include/plist/plist.h:1094: `PLIST_API plist_err_t plist_write_to_string(plist_t plist, char **output, uint32_t* length, plist_format_t format, plist_write_options_t options);`

### P2.2 Parameter Construction
**Claim:**
- `plist`: `plist_t` — valid plist node tree.
- `output`: `char**` — output pointer for serialized string. Must be valid pointer to `char*`.
- `length`: `uint32_t*` — output pointer for length.
- `format`: `plist_format_t` — one of: `PLIST_FORMAT_XML` (1), `PLIST_FORMAT_BINARY` (2), `PLIST_FORMAT_JSON` (3), `PLIST_FORMAT_OSTEP` (4), `PLIST_FORMAT_PRINT` (10), `PLIST_FORMAT_LIMD` (11), `PLIST_FORMAT_PLUTIL` (12).
- `options`: `plist_write_options_t` — bitwise OR of: `PLIST_OPT_NONE` (0), `PLIST_OPT_COMPACT` (1), `PLIST_OPT_PARTIAL_DATA` (2), `PLIST_OPT_NO_NEWLINE` (4), `PLIST_OPT_INDENT` (8), `PLIST_OPT_COERCE` (16). Use `PLIST_OPT_INDENT_BY(x)` macro for indentation level.
**Evidence:**
> include/plist/plist.h:155-166: `plist_format_t` enum definition.
> include/plist/plist.h:171-183: `plist_write_options_t` enum definition.
> include/plist/plist.h:1085-1094: Full declaration with doc comments.

### P2.3 Object Lifecycle
**Claim:** On success, `*output` is set to a newly allocated buffer and `*length` to its size. Caller must free with `plist_mem_free()`. The input `plist` is NOT consumed. Internally dispatches to format-specific writers (`plist_to_xml`, `plist_to_json`, `plist_to_bin`, etc.).
**Evidence:**
> src/plist.c:2399 (approx): Switch on `plist_format_t` dispatching to `plist_to_xml`, `plist_to_json`, `plist_to_openstep`, `plist_to_bin`, `plist_write_to_string_default`, `plist_write_to_string_limd`, `plist_write_to_string_plutil`.

### P2.4 Return Value Handling
**Claim:** Returns `plist_err_t`. `PLIST_ERR_SUCCESS` on success. May return `PLIST_ERR_INVALID_ARG` for NULL inputs or invalid format, `PLIST_ERR_NO_MEM` on allocation failure, `PLIST_ERR_FORMAT` for incompatible type/format combinations (e.g., DATE in JSON without COERCE option).
**Evidence:**
> include/plist/plist.h:144-150: Error enum.
> include/plist/plist.h:178-182: `PLIST_OPT_COERCE` doc mentions `PLIST_ERR_FORMAT` for incompatible types.

### P2.5 Cleanup Sequence
**Claim:** On success: free `*output` with `plist_mem_free()`. On error: `*output` should not be freed (not allocated). Input `plist` must still be freed separately by caller.
**Evidence:**
> include/plist/plist.h: `plist_mem_free` is the API-provided free function.
> src/plist.c:2429: `plist_write_to_stream` calls `plist_write_to_string` then `plist_mem_free(output)`.

### P2.6 API Existence
**Claim:** Confirmed exists as public API.
**Evidence:**
> include/plist/plist.h:1094: Declared with `PLIST_API`.

### P2.7 Co-call Constraints
**Claim:** Output buffer must be freed with `plist_mem_free()`. No other pairing constraints.
**Evidence:**
> src/plist.c:2429: Caller `plist_write_to_stream` frees output with `plist_mem_free`.

### P2.8 Prerequisite State
**Claim:** Requires a valid plist tree. For JSON format with DATE/DATA/UID types, `PLIST_OPT_COERCE` option should be set to avoid `PLIST_ERR_FORMAT`.
**Evidence:**
> include/plist/plist.h:178-182: Documentation for `PLIST_OPT_COERCE`.

---

## Recommended Harness Pattern

```c
// 1. Parse fuzz input into a plist tree
plist_t plist = NULL;
plist_format_t fmt;
plist_err_t err = plist_from_memory((const char*)data, (uint32_t)size, &plist, &fmt);
if (err != PLIST_ERR_SUCCESS || !plist) {
    return 0;
}

// 2. Serialize to binary format
char *bin_out = NULL;
uint32_t bin_len = 0;
err = plist_to_bin(plist, &bin_out, &bin_len);
if (err == PLIST_ERR_SUCCESS) {
    plist_mem_free(bin_out);
}

// 3. Serialize to various string formats
char *str_out = NULL;
uint32_t str_len = 0;
plist_format_t formats[] = {PLIST_FORMAT_XML, PLIST_FORMAT_JSON, PLIST_FORMAT_OSTEP, PLIST_FORMAT_PRINT};
for (int i = 0; i < 4; i++) {
    str_out = NULL;
    str_len = 0;
    err = plist_write_to_string(plist, &str_out, &str_len, formats[i],
                                 PLIST_OPT_NONE | PLIST_OPT_COERCE);
    if (err == PLIST_ERR_SUCCESS && str_out) {
        plist_mem_free(str_out);
    }
}

// 4. Cleanup
plist_free(plist);
```

**Key notes for harness generator:**
- `plist_from_memory` length parameter is `uint32_t` — cast/clamp `size` appropriately.
- Always check return value before using output pointers.
- `plist_mem_free()` (not `free()`) should be used for API-allocated buffers.
- `plist_free()` frees the entire plist tree.
- `PLIST_OPT_COERCE` is needed for JSON format to handle DATE/DATA/UID types without error.
- No global init/deinit functions needed.