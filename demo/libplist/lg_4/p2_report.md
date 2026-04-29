## target: `plist_from_memory`

### P2.6 API Existence
**Claim:** The function exists and is a public API.
**Evidence:**
> include/plist/plist.h:1064: `PLIST_API plist_err_t plist_from_memory(const char *plist_data, uint32_t length, plist_t *plist, plist_format_t *format);`
> src/plist.c:225-304: Full implementation

### P2.1 Init Sequence
**Claim:** No global initialization or library init call is required. The function is self-contained — it takes raw data and produces a plist tree. No prior setup is needed.
**Evidence:**
> tools/plistutil.c:376: `input_res = plist_from_memory(plist_entire, read_size, &root_node, NULL);` — called directly with no prior library init
> src/plist.c:342: `plist_err_t res = plist_from_memory(buf, total, plist, format);` — called directly in `plist_read_from_file` with no prior init

### P2.2 Parameter Construction
**Claim:** Four parameters:
1. `const char *plist_data` — pointer to raw plist data buffer (not NULL-terminated; binary data is valid). Must not be NULL.
2. `uint32_t length` — byte length of the buffer. Must be > 0.
3. `plist_t *plist` — output pointer. Must not be NULL. Will be set to NULL initially by the function (`*plist = NULL` at line 231).
4. `plist_format_t *format` — optional output pointer (may be NULL). If non-NULL, set to the detected format on success.

**Evidence:**
> src/plist.c:228-233: NULL checks — returns `PLIST_ERR_INVALID_ARG` if `plist` is NULL or if `plist_data` is NULL or `length == 0`
> src/plist.c:236: `if (format) *format = PLIST_FORMAT_NONE;` — format is optional, checked before dereference
> tools/plistutil.c:376: `plist_from_memory(plist_entire, read_size, &root_node, NULL);` — format passed as NULL
> include/plist/plist.h:1061: `@param format If non-NULL, the #plist_format_t value pointed to will be set to the parsed format.`

**For fuzzing:** `plist_data` should come directly from the fuzz input buffer, `length` from the fuzz input size. Both `plist` and `format` should be stack-allocated output variables.

### P2.3 Object Lifecycle
**Claim:** Create → use → destroy pattern:
1. **Create:** Call `plist_from_memory(data, len, &plist, &fmt)` — on success, `plist` holds a newly allocated plist tree.
2. **Use:** (optional) Inspect/traverse the plist tree.
3. **Destroy:** Call `plist_free(plist)` to free the entire tree.

**Evidence:**
> src/plist.c:231: `*plist = NULL;` — output initialized to NULL
> src/plist.c:712-718: `void plist_free(plist_t plist) { if (plist) { plist_free_node((node_t)plist); } }`
> tools/plistutil.c:376-383: After `plist_from_memory`, on error path calls `plist_free(root_node)` then `free(plist_entire)`

### P2.4 Return Value Handling
**Claim:** Returns `plist_err_t`. Check for `PLIST_ERR_SUCCESS` (0) before using the output plist. On error, `*plist` remains NULL (set at line 231).
**Evidence:**
> src/plist.c:227: `plist_err_t res = PLIST_ERR_UNKNOWN;`
> src/plist.c:228-233: Returns `PLIST_ERR_INVALID_ARG` for bad args
> src/plist.c:247: Returns `PLIST_ERR_PARSE` for whitespace-only input
> include/plist/plist.h:139-150: Error enum values: `PLIST_ERR_SUCCESS=0`, `PLIST_ERR_INVALID_ARG=-1`, `PLIST_ERR_PARSE=-3`, etc.
> tools/plistutil.c:377: `if (input_res == PLIST_ERR_SUCCESS) {` — checks return before using result

### P2.5 Cleanup Sequence
**Claim:** Only one resource to free: the output `plist_t`. Call `plist_free(plist)` after use. Safe to call with NULL (has NULL guard). No other cleanup needed — the function does not allocate any other user-visible resources.
**Evidence:**
> src/plist.c:712-714: `void plist_free(plist_t plist) { if (plist) { plist_free_node((node_t)plist); } }` — NULL-safe
> tools/plistutil.c:383: `plist_free(root_node);` — cleanup in plistutil

### P2.7 Co-call Constraints
**Claim:** No paired API constraints. The function is a standalone parser. The only pairing is the implicit `plist_from_memory` → `plist_free` pair for the output plist.
**Evidence:**
> All callers (tools/plistutil.c:376, src/plist.c:342) follow the pattern: call `plist_from_memory`, use result, call `plist_free`.

### P2.8 Prerequisite State
**Claim:** No prerequisite state. No context objects, connections, or configuration needed. The function operates purely on the input buffer.
**Evidence:**
> src/plist.c:225-304: Function body references no global state, no context objects. Only reads from `plist_data` buffer.

---

### Recommended Harness Pattern

```c
#include <plist/plist.h>
#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > UINT32_MAX) {
        return 0;
    }

    plist_t plist = NULL;
    plist_format_t format = PLIST_FORMAT_NONE;

    plist_err_t err = plist_from_memory((const char *)data, (uint32_t)size, &plist, &format);

    if (plist) {
        plist_free(plist);
    }

    return 0;
}
```

**Key points for harness generation:**
- Cast `uint8_t*` fuzz data to `const char*` (safe, same size)
- Cast `size_t` to `uint32_t` after bounds check (function takes `uint32_t`)
- `format` parameter is optional but useful to exercise that code path — pass address of stack variable
- Always call `plist_free(plist)` — safe even if NULL
- No init/teardown needed — function is fully self-contained