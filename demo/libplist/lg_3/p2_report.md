# P2 Protocol Report: libplist — plist_from_memory, plist_dict_merge, plist_copy

## target: plist_from_memory

### P2.1 Init Sequence
**Claim:** No global initialization required. The function is self-contained — no library-level init call is needed before use.
**Evidence:**
> tools/plistutil.c:376: `plist_from_memory(plist_entire, read_size, &root_node, NULL);` — called directly without any prior init
> src/plist.c:225-304: Implementation has no global state dependencies; it dispatches to format-specific parsers directly.

### P2.2 Parameter Construction
**Claim:** Signature is `plist_err_t plist_from_memory(const char *plist_data, uint32_t length, plist_t *plist, plist_format_t *format)`.
- `plist_data`: pointer to raw plist data buffer (can be binary, XML, JSON, or OpenStep format). Must not be NULL.
- `length`: size of the buffer as `uint32_t`. Must be > 0.
- `plist`: output pointer, must point to a valid `plist_t` variable. Will be set to NULL on entry, then populated on success.
- `format`: optional output pointer (can be NULL). If non-NULL, set to the detected format on success.

**Evidence:**
> include/plist/plist.h:1064: `PLIST_API plist_err_t plist_from_memory(const char *plist_data, uint32_t length, plist_t *plist, plist_format_t *format);`
> src/plist.c:228-234: Validates `plist != NULL`, `plist_data != NULL`, `length != 0`; returns `PLIST_ERR_INVALID_ARG` otherwise.
> tools/plistutil.c:376: `plist_from_memory(plist_entire, read_size, &root_node, NULL);` — format passed as NULL.

**For fuzzing:** Cast the fuzz input `data` to `const char*` and `size` to `uint32_t`. Pass `&plist` for output and `NULL` for format.

### P2.3 Object Lifecycle
**Claim:** On success (`PLIST_ERR_SUCCESS`), `*plist` is a newly allocated plist tree that must be freed with `plist_free()`. On failure, `*plist` is set to NULL (no cleanup needed).
**Evidence:**
> src/plist.c:231: `*plist = NULL;` — always zeroed at entry
> tools/plistutil.c:376-383: After successful parse, `root_node` is used, then freed with `plist_free(root_node)` on error paths.
> include/plist/plist.h:302: `PLIST_API void plist_free(plist_t plist);`

### P2.4 Return Value Handling
**Claim:** Returns `plist_err_t`. Must check for `PLIST_ERR_SUCCESS` (0) before using the output plist. Other values indicate errors.
**Evidence:**
> include/plist/plist.h:141-150: Error codes: `PLIST_ERR_SUCCESS=0`, `PLIST_ERR_INVALID_ARG=-1`, `PLIST_ERR_FORMAT=-2`, `PLIST_ERR_PARSE=-3`, `PLIST_ERR_NO_MEM=-4`, `PLIST_ERR_IO=-5`, `PLIST_ERR_CIRCULAR_REF=-6`, `PLIST_ERR_MAX_NESTING=-7`, `PLIST_ERR_UNKNOWN=-255`.
> tools/plistutil.c:377: `if (input_res == PLIST_ERR_SUCCESS) {` — checks return before using result.

### P2.5 Cleanup Sequence
**Claim:** Call `plist_free(plist)` on the output plist after use. Safe to call with NULL (no-op).
**Evidence:**
> src/plist.c:712-718: `plist_free` checks for NULL before calling `plist_free_node`.
> tools/plistutil.c:383: `plist_free(root_node);` — cleanup after use.

### P2.6 API Existence
**Claim:** Confirmed to exist.
**Evidence:**
> src/plist.c:225: Definition at line 225.
> include/plist/plist.h:1064: Public declaration with `PLIST_API`.

### P2.7 Co-call Constraints
**Claim:** No paired API calls required. The function is standalone — parse and get a plist tree.
**Evidence:**
> src/plist.c:225-304: No locks, transactions, or paired operations.

### P2.8 Prerequisite State
**Claim:** No prerequisite state. No context objects, connections, or configuration needed.
**Evidence:**
> src/plist.c:225-304: Function only depends on its parameters.

---

## target: plist_copy

### P2.1 Init Sequence
**Claim:** No initialization required beyond having a valid `plist_t` node (e.g., from `plist_from_memory`).
**Evidence:**
> src/plist.c:948-951: `plist_t plist_copy(plist_t node) { return node ? plist_copy_node((node_t)node) : NULL; }`

### P2.2 Parameter Construction
**Claim:** Single parameter `plist_t node` — an opaque pointer (`void*`) to a plist node. Can be any plist type (dict, array, string, etc.). NULL is safe (returns NULL).
**Evidence:**
> include/plist/plist.h:310: `PLIST_API plist_t plist_copy(plist_t node);`
> include/plist/plist.h:102: `typedef void *plist_t;`
> src/plist.c:948-950: NULL check returns NULL immediately.

### P2.3 Object Lifecycle
**Claim:** Returns a newly allocated deep copy of the entire subtree. The copy is independent of the original — both must be freed separately with `plist_free()`. Returns NULL on failure or NULL input.
**Evidence:**
> src/plist.c:811-946: `plist_copy_node` performs iterative deep copy using an explicit stack. Allocates new nodes via `plist_copy_node_shallow`, attaches children, updates hash tables for dicts/arrays.
> src/plist.c:857-862: On `NODE_MAX_DEPTH` exceeded, frees the partial copy and returns NULL.
> src/plist.c:944-945: On success, frees the stack and returns the new root.

### P2.4 Return Value Handling
**Claim:** Returns `plist_t` — the deep-copied tree, or NULL on failure. No error code; just check for NULL.
**Evidence:**
> include/plist/plist.h:308-310: `@return copied plist`
> src/plist.c:948-950: Returns NULL if input is NULL or copy fails.

### P2.5 Cleanup Sequence
**Claim:** The returned copy must be freed with `plist_free()`. The original node is NOT modified or freed.
**Evidence:**
> src/plist.c:1446: In `plist_dict_merge`, `plist_copy(subnode)` result is passed to `plist_dict_set_item` which takes ownership.
> General pattern: any `plist_t` returned by creation/copy functions must be freed with `plist_free()` unless ownership is transferred (e.g., to `plist_dict_set_item`).

### P2.6 API Existence
**Claim:** Confirmed to exist.
**Evidence:**
> src/plist.c:948: Definition.
> include/plist/plist.h:310: Public declaration.

### P2.7 Co-call Constraints
**Claim:** No paired calls. Standalone deep-copy operation.

### P2.8 Prerequisite State
**Claim:** Input must be a valid plist node (from parsing or construction APIs). No other state required.
**Evidence:**
> src/plist.c:948-950: Only requires a non-NULL plist_t.

---

## target: plist_dict_merge

### P2.1 Init Sequence
**Claim:** Both `*target` and `source` must be valid plist nodes of type `PLIST_DICT` before calling. No other initialization needed.
**Evidence:**
> src/plist.c:1431: `if (!target || !*target || (plist_get_node_type(*target) != PLIST_DICT) || !source || (plist_get_node_type(source) != PLIST_DICT)) return;`

### P2.2 Parameter Construction
**Claim:** Signature is `void plist_dict_merge(plist_t *target, plist_t source)`.
- `target`: pointer to an existing `plist_t` of type `PLIST_DICT`. Must not be NULL, and `*target` must not be NULL.
- `source`: a `plist_t` of type `PLIST_DICT` to merge from. Must not be NULL.

**Evidence:**
> include/plist/plist.h:512: `PLIST_API void plist_dict_merge(plist_t *target, plist_t source);`
> include/plist/plist.h:509-510: `@param target pointer to an existing node of type #PLIST_DICT` / `@param source node of type #PLIST_DICT that should be merged into target`
> src/plist.c:1431: Validates both are PLIST_DICT, silently returns otherwise.

**For fuzzing:** Parse fuzz input with `plist_from_memory`. If the result is a dict, use it as source. Create a separate empty dict with `plist_new_dict()` as target. Alternatively, if the parsed plist is a dict, copy it and use original as source and copy as target.

### P2.3 Object Lifecycle
**Claim:** The function iterates over `source` dict entries, deep-copies each value with `plist_copy()`, and inserts the copy into `*target` via `plist_dict_set_item()`. The source is not modified. The target dict is modified in-place (entries added/overwritten). The iterator is allocated internally and freed at the end.
**Evidence:**
> src/plist.c:1435-1437: `plist_dict_new_iter(source, &it);` — allocates iterator
> src/plist.c:1441-1448: Loop: `plist_dict_next_item` gets key+value, `plist_dict_set_item(*target, key, plist_copy(subnode))` inserts copy, `free(key)` frees the key string.
> src/plist.c:1450: `free(it);` — frees iterator.

### P2.4 Return Value Handling
**Claim:** Returns `void`. Silently returns on invalid arguments (NULL pointers or non-dict types). No error indication.
**Evidence:**
> include/plist/plist.h:512: `PLIST_API void plist_dict_merge(plist_t *target, plist_t source);`
> src/plist.c:1431-1432: Silent return on invalid args.

### P2.5 Cleanup Sequence
**Claim:** After calling `plist_dict_merge`, both `*target` and `source` must still be freed separately with `plist_free()`. The function does not take ownership of either — it copies values from source into target.
**Evidence:**
> src/plist.c:1446: `plist_dict_set_item(*target, key, plist_copy(subnode))` — copies are made, originals untouched.
> src/plist.c:1447: `free(key)` — key strings from iterator are freed within the function.
> src/plist.c:1450: `free(it)` — iterator freed internally.

### P2.6 API Existence
**Claim:** Confirmed to exist.
**Evidence:**
> src/plist.c:1429: Definition.
> include/plist/plist.h:512: Public declaration.

### P2.7 Co-call Constraints
**Claim:** No external paired calls. The iterator (`plist_dict_new_iter` / `plist_dict_next_item` / `free(it)`) is managed entirely within the function.
**Evidence:**
> src/plist.c:1435-1450: All iterator lifecycle is internal.

### P2.8 Prerequisite State
**Claim:** Both target and source must be valid `PLIST_DICT` nodes. Target can be an empty dict (from `plist_new_dict()`). Source should be a parsed or constructed dict.
**Evidence:**
> src/plist.c:1431: Type check for both parameters.
> src/plist.c:527-536: `plist_new_dict()` creates an empty dict suitable as target.

---

## Recommended Harness Pattern

```c
#include <plist/plist.h>
#include <stdlib.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > UINT32_MAX) return 0;

    plist_t plist = NULL;
    plist_err_t err = plist_from_memory((const char*)data, (uint32_t)size, &plist, NULL);
    if (err != PLIST_ERR_SUCCESS || !plist) {
        return 0;
    }

    // Exercise plist_copy (deep clone)
    plist_t copy = plist_copy(plist);

    // Exercise plist_dict_merge if parsed plist is a dict
    if (plist_get_node_type(plist) == PLIST_DICT) {
        plist_t target = plist_new_dict();
        if (target) {
            plist_dict_merge(&target, plist);
            plist_free(target);
        }
    }

    // Cleanup
    if (copy) plist_free(copy);
    plist_free(plist);

    return 0;
}
```

### Key Protocol Notes for Harness Generator:
1. **No global init** — library is ready to use immediately.
2. **plist_from_memory** returns `plist_err_t`; must check `== PLIST_ERR_SUCCESS` before using output.
3. **plist_copy** returns NULL on failure — check before freeing.
4. **plist_dict_merge** requires both args to be `PLIST_DICT` type — check with `plist_get_node_type()` first.
5. **plist_dict_merge** takes `plist_t*` as first arg (pointer-to-pointer), not `plist_t`.
6. **plist_new_dict()** creates an empty dict suitable as merge target; must be freed with `plist_free()`.
7. **All plist_t objects** must be freed with `plist_free()` — safe to call with NULL.
8. **Cast fuzz data** to `const char*` and size to `uint32_t` for `plist_from_memory`.