## target: plist_from_bin

### P2.1 Init Sequence
**Claim:** No library-wide init call is required. `plist_from_bin` can be called directly on a raw buffer; the only "setup" is allocating/obtaining the input buffer and an output `plist_t*` variable initialized by the function itself.
**Evidence:**
> include/plist/plist.h:1073-1081: "/** Import the #plist_t structure from binary format. ... */ PLIST_API plist_err_t plist_from_bin(const char *plist_bin, uint32_t length, plist_t * plist);" — no precondition on global state documented.
> src/bplist.c:917-923: `if (!plist) return PLIST_ERR_INVALID_ARG; *plist = NULL; if (!plist_bin || length == 0) return PLIST_ERR_INVALID_ARG;` — function itself nulls out `*plist` before parsing, so caller need not pre-initialize `*plist` to NULL (but doing so is harmless/good practice).
> tools/plistutil.c:355: `input_res = plist_from_bin(plist_entire, read_size, &root_node);` called right after reading raw file bytes into `plist_entire`, no other libplist call precedes it.

### P2.2 Parameter Construction
**Claim:** `plist_bin` is a `const char*` pointer to raw bytes (need not be NUL-terminated, length is explicit); `length` is a `uint32_t` byte count of the buffer (must be ≥ 6+2+32 = sizeof(magic)+sizeof(version)+sizeof(trailer) to pass the first size check); `plist` is an out-parameter, a pointer to a `plist_t` (i.e. `plist_t*`), non-NULL, that receives the parsed tree root on success.
**Evidence:**
> src/bplist.c:905: `plist_err_t plist_from_bin(const char *plist_bin, uint32_t length, plist_t * plist)`
> src/bplist.c:926-929: `if (!(length >= BPLIST_MAGIC_SIZE + BPLIST_VERSION_SIZE + sizeof(bplist_trailer_t))) { ... return PLIST_ERR_PARSE; }` — minimum length is 6+2+sizeof(bplist_trailer_t) (trailer is a packed struct with offset_size(1)+ref_size(1)+num_objects(8)+root_object_index(8)+offset_table_offset(8) plus 6 unused bytes = 32 bytes total, see struct at bplist.c:50-58), so minimum total length is 40 bytes.
> src/bplist.c:931-934/936-938: magic must equal "bplist" (6 bytes) and version must equal "00" (2 bytes), else `PLIST_ERR_PARSE`.
> src/bplist.c:917-922: `plist` must be non-NULL; `plist_bin` must be non-NULL and `length != 0`, else `PLIST_ERR_INVALID_ARG`.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: allocate/obtain input buffer → call `plist_from_bin` (creates the `plist_t` tree internally) → use the resulting tree (traverse, convert, serialize) → `plist_free()` the tree exactly once when done. There is no separate "create" step before the call and no "configure" step; the tree object is entirely owned/created by the call.
**Evidence:**
> src/bplist.c:1011: `*plist = parse_bin_node_at_index(&bplist, root_object);` — the plist_t tree is allocated during the call.
> test/plist_btest.c:80-121: `plist_from_bin(plist_bin, size_in, &root_node1); ... plist_to_xml(root_node1, &plist_xml, &size_out); ... plist_free(root_node1); plist_free(root_node2);` shows create→use→free.
> include/plist/plist.h:297-302: `/** Destruct a plist_t node and all its children recursively @param plist the plist to free */ PLIST_API void plist_free(plist_t plist);` — confirms `plist_free` is the sole destructor for the tree, freeing all children recursively.

### P2.4 Return Value Handling
**Claim:** Return value is a `plist_err_t`. Caller must check it is `PLIST_ERR_SUCCESS` (0) before using `*plist`; on any negative value (`PLIST_ERR_INVALID_ARG`, `PLIST_ERR_PARSE`, `PLIST_ERR_NO_MEM`, `PLIST_ERR_CIRCULAR_REF`, `PLIST_ERR_MAX_NESTING`, etc.) `*plist` is guaranteed NULL and must not be dereferenced or freed as a valid tree (though `plist_free(NULL)` is safe, a no-op).
**Evidence:**
> include/plist/plist.h:139-150: enum `plist_err_t` values `PLIST_ERR_SUCCESS=0, PLIST_ERR_INVALID_ARG=-1, PLIST_ERR_FORMAT=-2, PLIST_ERR_PARSE=-3, PLIST_ERR_NO_MEM=-4, PLIST_ERR_IO=-5, PLIST_ERR_CIRCULAR_REF=-6, PLIST_ERR_MAX_NESTING=-7, PLIST_ERR_UNKNOWN=-255`.
> src/bplist.c:1015-1019: `if (!*plist) { return (bplist.err != PLIST_ERR_SUCCESS) ? bplist.err : PLIST_ERR_PARSE; } return PLIST_ERR_SUCCESS;` — explicit contract: non-NULL `*plist` iff success.
> src/plist.c:712-718: `void plist_free(plist_t plist) { if (plist) { plist_free_node((node_t)plist); } }` — NULL-safe, so calling it unconditionally after checking `root_node` is still fine, but best practice is to only use the tree when return==PLIST_ERR_SUCCESS.
> tools/plistutil.c:355-356: `input_res = plist_from_bin(plist_entire, read_size, &root_node); if (input_res == PLIST_ERR_SUCCESS) { ... }` — real caller gates all subsequent use on success.

### P2.5 Cleanup Sequence
**Claim:** On success, call `plist_free(*plist)` exactly once after all uses are done (frees the whole tree recursively, no partial/ordered sub-frees needed). On failure, `*plist` is NULL so nothing from this call needs freeing (but the caller's own input buffer, e.g. a `malloc`'d file-read buffer, must still be freed independently). Internal resources (`bplist.used_indexes` ptr_array) are freed by the function itself before returning, in both success and failure paths.
**Evidence:**
> src/bplist.c:1003-1013: `bplist.used_indexes = ptr_array_new(16); ... *plist = parse_bin_node_at_index(&bplist, root_object); ptr_array_free(bplist.used_indexes);` — internal array always freed before return.
> src/plist.c:712-718: `plist_free` recursively frees node and all children (single call suffices).
> test/plist_btest.c:120-123: `plist_free(root_node1); plist_free(root_node2); free(plist_xml); free(plist_bin);` — shows cleanup ordering: free plist trees, free derived buffers (xml/bin) separately.

### P2.6 API Existence
**Claim:** All functions named in this report exist in the current codebase at the cited locations.
**Evidence:**
> include/plist/plist.h:1081: `plist_from_bin` declared.
> include/plist/plist.h:1121: `plist_from_memory` declared.
> include/plist/plist.h:1201: `plist_is_binary` declared.
> include/plist/plist.h:987: `plist_to_bin` declared.
> include/plist/plist.h:975: `plist_to_xml` declared.
> include/plist/plist.h:1000/1023: `plist_to_json` / `plist_to_json_with_options` declared.
> include/plist/plist.h:302: `plist_free` declared.
> include/plist/plist.h:1488: `plist_mem_free` declared.
> src/bplist.c:905: `plist_from_bin` defined.
> src/plist.c:225: `plist_from_memory` defined.
> src/plist.c:211: `plist_is_binary` defined.
> src/bplist.c:1360: `plist_to_bin` defined.
> src/xplist.c:515: `plist_to_xml` defined.
> src/jplist.c:436/442: `plist_to_json` / `plist_to_json_with_options` defined.
> src/plist.c:712: `plist_free` defined.
> src/plist.c:720: `plist_mem_free` defined.

### P2.7 Co-call Constraints
**Claim:** The only strict pairing is create/destroy: every successful `plist_from_bin` (or `plist_from_memory`/`plist_from_xml`/`plist_from_json`/`plist_from_openstep`) must be matched by exactly one `plist_free()` on the returned root. The `plist_to_*` family (xml/json/bin/openstep) each independently allocate an output buffer that must be freed with `plist_mem_free()` (or plain `free()`, since `plist_mem_free` just wraps `free`), separate from the tree's `plist_free`. There is no lock/unlock or begin/commit pairing — the API is stateless creation+destruction, no global state.
**Evidence:**
> src/plist.c:720-726: `void plist_mem_free(void* ptr) { if (ptr) { free(ptr); } }` confirms it's a thin libc free wrapper, safe/equivalent to caller-side free (as seen in test/plist_btest.c:122-123 using plain `free()`).
> include/plist/plist.h:973,985,998,1021,1034,1058: doc notes "Use plist_mem_free() to free the allocated memory" for each `plist_to_*` output buffer, separate from `plist_free` for the tree.
> fuzz/xplist_fuzzer.cc:27-29 and fuzz/jplist_fuzzer.cc:27-29 and fuzz/oplist_fuzzer.cc:27-29: each shows the pattern `plist_t root_node = NULL; plist_from_X(...,&root_node); plist_free(root_node);` — one parse call paired with one free call, free is safe even if parse failed (root_node stays NULL).

### P2.8 Prerequisite State
**Claim:** No prerequisite global/library context, connection, or configuration object is required before calling `plist_from_bin`. The only prerequisite is a valid memory buffer containing (or claimed to contain) binary-plist bytes and its length known to the caller.
**Evidence:**
> src/bplist.c:905-923: function takes only `plist_bin`, `length`, `plist` — no handle/context parameter, no call to any init/config function in its body.
> get_callees confirms it only calls internal parsing helpers and `ptr_array_new`/`ptr_array_free`, ptr_array_index.

### P2.9 Result Usage
**Claim:** The `plist_t` tree produced by `plist_from_bin` is consumed by: (a) the serializer family `plist_to_xml`, `plist_to_json[_with_options]`, `plist_to_bin`, `plist_to_openstep[_with_options]` (encode the tree back to a byte buffer), (b) tree-navigation/query functions such as `plist_get_node_type`, `plist_dict_get_item`, `plist_array_get_item`, `plist_array_get_size` (seen used post-parse in tools/plistutil.c), (c) `plist_sort` (in-place reordering before re-serializing), and (d) ultimately `plist_free` to destroy it. Real callers check the `plist_err_t` from `plist_from_bin` before touching the tree.
**Evidence:**
> tools/plistutil.c:355-360: `input_res = plist_from_bin(plist_entire, read_size, &root_node); if (input_res == PLIST_ERR_SUCCESS) { if (options->flags & OPT_SORT) { plist_sort(root_node); } output_res = plist_to_xml(root_node, &plist_out, &size); }` — tree is sorted then serialized to XML.
> test/plist_btest.c:80-104: result of `plist_from_bin` (`root_node1`) is fed into `plist_to_xml(root_node1, &plist_xml, &size_out)`, whose output is fed into `plist_from_xml`, whose output (`root_node2`) is fed into `plist_to_bin(root_node2, &plist_bin2, &size_out2)` — classic round-trip consumption chain.
> tools/plistutil.c:392-408 (reached via `plist_from_memory` which internally calls `plist_from_bin` for binary input, src/plist.c:237-239): tree is traversed with `plist_get_node_type(current)`, `plist_dict_get_item(current, tok)`, `plist_array_get_size(current)`, `plist_array_get_item(current, idx)` to walk a node path.
> src/Structure.cpp:58-68 (C++ wrapper, `Node` class): `plist_to_xml(_node, &xml, &length); ... plist_to_bin(_node, &bin, &length);` — same tree consumed by both exporters.

### P2.10 Behaviour Controls
**Claim:** `plist_from_bin` itself has no caller-supplied behaviour flags — every branch (node type dispatch, extended "fill" size encoding, nesting-depth guard, recursion/circular-reference guard) is driven purely by bytes inside `plist_bin`. The surrounding API surface does expose behaviour-selecting parameters: `plist_from_memory`'s optional `format` out-param reports which branch was taken (binary/xml/json/openstep) but does not select it — selection is automatic via `plist_is_binary()` then content sniffing (`<`, `[`, `{`, `(`). `plist_to_json`/`plist_to_json_with_options` and `plist_to_openstep[_with_options]` accept `prettify`/`options` (`PLIST_OPT_COMPACT`, `PLIST_OPT_COERCE`, etc.) that pick different serialization paths. `plistutil`'s `OPT_SORT` flag controls whether `plist_sort` is applied to the parsed tree before re-export.
**Evidence:**
> src/bplist.c:211-218 doesn't apply here (that's plist_is_binary) — see below.
> src/plist.c:211-217: `int plist_is_binary(const char *plist_data, uint32_t length) { if (plist_data == NULL || length < 8) return 0; return (memcmp(plist_data, "bplist00", 8) == 0); }` — binary-format gate, used by `plist_from_memory`.
> src/plist.c:237-239: `if (plist_is_binary(plist_data, length)) { res = plist_from_bin(plist_data, length, plist); fmt = PLIST_FORMAT_BINARY; }` — confirms automatic dispatch into the target.
> src/jplist.c:442,456-457: `int prettify = !(options & PLIST_OPT_COMPACT); int coerce = options & PLIST_OPT_COERCE;` — options parameter changes json encoding path (pretty vs compact; coercion of PLIST_DATE/PLIST_DATA/PLIST_UID).
> include/plist/plist.h:1041-1050: `plist_to_openstep_with_options` doc: `PLIST_OPT_COMPACT` / `PLIST_OPT_COERCE` select different output representations for PLIST_BOOLEAN/PLIST_DATE/PLIST_NULL/PLIST_UID.
> tools/plistutil.c:357-358,367-368: `if (options->flags & OPT_SORT) { plist_sort(root_node); }` — caller-level flag controlling whether the tree is normalized before being re-serialized.
> src/bplist.c:869-874 and :885-896 (internal, not caller-controlled): nesting-depth guard (`PLIST_MAX_NESTING_DEPTH`) and circular-reference guard are purely data-driven from the parsed offsets/indices, not exposed as parameters.

### P2.11 Input Expectations
**Claim:** Input must begin with 6-byte magic `"bplist"` immediately followed by 2-byte version `"00"`, then node data, then an offset table, then a fixed 32-byte (packed) trailer at the very end of the buffer (`unused[6] + offset_size(1) + ref_size(1) + num_objects(8,BE) + root_object_index(8,BE) + offset_table_offset(8,BE)`). Total length must be at least `6+2+sizeof(bplist_trailer_t)` bytes. `offset_size` and `ref_size` must both be > 0; `num_objects` must be > 0; `root_object_index` must be < `num_objects`; `offset_table_offset` must point within `[start_data, end_data)` and the offset table (size = `num_objects * offset_size`, checked for integer overflow) must fit before the trailer. All further per-node offsets/sizes are bounds-checked against `bplist->data`/`bplist->size`/`bplist->offset_table` while walking. No NUL-termination is required; `length` is the authoritative size, not strlen. Nesting depth is capped at `PLIST_MAX_NESTING_DEPTH` and repeated visits to the same node index along one recursion path are rejected as circular references.
**Evidence:**
> src/bplist.c:42-47: `#define BPLIST_MAGIC ((uint8_t*)"bplist")` / `#define BPLIST_MAGIC_SIZE 6` / `#define BPLIST_VERSION ((uint8_t*)"00")` / `#define BPLIST_VERSION_SIZE 2`.
> src/bplist.c:50-58: `typedef struct { uint8_t unused[6]; uint8_t offset_size; uint8_t ref_size; uint64_t num_objects; uint64_t root_object_index; uint64_t offset_table_offset; } bplist_trailer_t;` (packed).
> src/bplist.c:926-938: minimum-length check, magic check, version check.
> src/bplist.c:944-993: trailer field extraction (`be64toh`), `offset_table_offset` range check, `num_objects==0`/`offset_size==0`/`ref_size==0` rejection, `root_object >= num_objects` rejection, offset-table bounds and `uint64_mul_overflow` check for `offset_table_size`.
> src/bplist.c:850-867: per-node-index checks (`node_offset > bplist->size`, pointer-range check against `start_data`/`offset_table`).
> src/bplist.c:869-896: nesting-depth guard (`bplist->level > PLIST_MAX_NESTING_DEPTH` → `PLIST_ERR_MAX_NESTING`) and circular-reference guard (`PLIST_ERR_CIRCULAR_REF`).

## Other entries of this Logic Group

### Entry: plist_from_memory
**Role:** Source/variant entry; format-autodetecting wrapper that dispatches to `plist_from_bin` for binary input (and to `plist_from_xml`/`plist_from_json`/`plist_from_openstep` otherwise).
**Needs before it:** Nothing beyond a buffer+length; no other libplist call required first.
**Parameters / flags selecting paths:**
- `plist_data` (`const char*`), `length` (`uint32_t`): the raw bytes, like `plist_from_bin`.
- `plist` (`plist_t*`): out-param, must be non-NULL; `*plist` is set to NULL first (src/plist.c:228-231), filled with the parsed tree on success.
- `format` (`plist_format_t*`, optional, may be NULL): if non-NULL, set to `PLIST_FORMAT_NONE` initially (src/plist.c:236) then to `PLIST_FORMAT_BINARY`/`PLIST_FORMAT_XML`/`PLIST_FORMAT_JSON`/`PLIST_FORMAT_OSTEP` depending on which branch was taken (src/plist.c:238-294 and following, not fully shown but pattern confirmed at 238-239 for BINARY).
- Path selection is content-driven: `plist_is_binary()` first (magic `"bplist00"`), else sniffs first non-whitespace byte for `<` (XML), `[` (JSON-only), `{` (JSON or OpenStep, disambiguated by looking for a `"..."[:]` key pattern), `(` (OpenStep); default falls through to OpenStep parsing.
**Return value:** `plist_err_t`, same enum as `plist_from_bin`; `PLIST_ERR_INVALID_ARG` if `plist`==NULL or (`plist_data`==NULL or `length`==0); `PLIST_ERR_PARSE` if no recognizable format found or underlying parser fails.
**Cleanup:** Same as plist_from_bin — call `plist_free(*plist)` once when `*plist` is non-NULL (i.e. result == PLIST_ERR_SUCCESS).
**Combination with target:** `plist_from_memory` IS an indirect caller of `plist_from_bin` (src/plist.c:237-239) — using it with a buffer starting `"bplist00"` exercises the exact same bplist core plus the format-sniffing gate. In a single harness, calling `plist_from_memory` covers both the sniffing logic and (for binary-looking input) the target; calling `plist_from_bin` directly guarantees the bplist core is exercised regardless of magic-detection edge cases. Both can be called on the same input in one harness (they do not conflict; each produces its own independent tree that must be freed separately).
**Evidence:**
> src/plist.c:225-239: full function signature and binary-dispatch branch.
> include/plist/plist.h:1121: `PLIST_API plist_err_t plist_from_memory(const char *plist_data, uint32_t length, plist_t *plist, plist_format_t *format);`

### Entry: plist_is_binary
**Role:** Config/detector helper, not a parser; pure predicate used by `plist_from_memory` (and directly by `tools/plistutil.c`) to decide whether to route into the bplist core.
**Needs before it:** Nothing.
**Parameters:** `plist_data` (`const char*`), `length` (`uint32_t`). Returns `int`: 1 if the first 8 bytes equal `"bplist00"` and `length>=8`, else 0. No error codes, no out-params, no allocation, nothing to free.
**Combination with target:** Typically called immediately before `plist_from_bin` to gate the call, as in `tools/plistutil.c:353-355`: `if (plist_is_binary(plist_entire, read_size)) { input_res = plist_from_bin(plist_entire, read_size, &root_node); ... }`. In a harness this can be used to decide which of `plist_from_bin` vs `plist_from_xml`/etc. to call on the fuzz input, or simply called for coverage alongside `plist_from_bin` on the same bytes (no state interaction between the two).
**Evidence:**
> src/plist.c:211-218: full definition.
> include/plist/plist.h:1192-1201: doc + declaration, notes it is heuristic only ("not validating the whole memory buffer").

### Entry: plist_to_bin
**Role:** Inverse/encoder entry; serializes a `plist_t` tree (e.g., the one produced by `plist_from_bin`) back into a binary-plist byte buffer. Useful for round-trip testing in the same harness.
**Needs before it:** A valid, non-NULL `plist_t` tree already built (by `plist_from_bin`, `plist_new_dict`, etc.).
**Parameters:** `plist` (`plist_t`, the root node to export — must be non-NULL), `plist_bin` (`char**` out-param, function allocates, caller frees), `length` (`uint32_t*` out-param, receives size of allocated buffer). All three must be non-NULL or `PLIST_ERR_INVALID_ARG` is returned (src/bplist.c:1379-1381).
**Internal lifecycle:** allocates `objects` ptrarray, `ref_table`/`in_stack` hashtables for de-duplication and circular-reference detection during serialization (src/bplist.c:1384-1400), calls `serialize_plist`, frees `in_stack` immediately (src/bplist.c:1414), computes output sizes, writes trailer, and returns.
**Return value:** `plist_err_t`; on error paths frees intermediate `objects`/`ref_table`/`in_stack` itself (src/bplist.c:1385-1400) before returning, so caller only needs to free the returned `*plist_bin` buffer on success.
**Cleanup:** Caller must `plist_mem_free(*plist_bin)` (or `free()`) on success; the `plist_t` input tree is NOT freed/consumed by `plist_to_bin` — caller still owns it and must `plist_free()` it separately when done.
**Combination with target:** Classic round-trip: `plist_from_bin(input, len, &tree)` → `plist_to_bin(tree, &out_buf, &out_len)` → (optionally re-parse `out_buf` with `plist_from_bin` again to compare) → `plist_free(tree)` + `plist_mem_free(out_buf)`. Demonstrated in test/plist_btest.c:80-123 (there using `plist_to_xml`/`plist_from_xml` as the intermediate step, with `plist_to_bin` closing the loop at line 104).
**Evidence:**
> src/bplist.c:1360-1420 (shown above): definition, parameter validation, intermediate structures.
> include/plist/plist.h:977-987: doc + declaration; "@note Use plist_mem_free() to free the allocated memory."

### Entry: plist_to_xml
**Role:** Consumer/serializer entry; traverses the `plist_t` tree (e.g. from `plist_from_bin`) and renders XML.
**Needs before it:** Valid non-NULL `plist_t` tree.
**Parameters:** `plist` (`plist_t`), `plist_xml` (`char**` out, allocated by function), `length` (`uint32_t*` out). All three required non-NULL else `PLIST_ERR_INVALID_ARG` (src/xplist.c:520-522).
**Internal flow:** `node_estimate_size()` (allocates/frees a visited-hashtable internally for cycle-safe size estimation, src/xplist.c:506-513) → allocate `strbuf_t` output buffer → `node_to_xml()` traversal → on error frees the buffer and sets `*plist_xml=NULL`, `*length=0` (src/xplist.c:538-544).
**Return value:** `plist_err_t`; negative on failure (`*plist_xml` left NULL in that case).
**Cleanup:** Caller frees `*plist_xml` via `plist_mem_free()`/`free()` on success; does not free/consume the input tree.
**Combination with target:** `plist_from_bin(...,&tree)` → `plist_to_xml(tree,&xml,&xlen)` → use/compare `xml` → `plist_free(tree)` + free(xml). Seen directly in test/plist_btest.c:80-95 and tools/plistutil.c:355-360, and in the standalone `fuzz/xplist_fuzzer.cc:27-29` pattern (which instead starts from `plist_from_xml`, showing the general from_X→(optional to_Y)→plist_free shape).
**Evidence:**
> src/xplist.c:515-544: definition and buffer-lifecycle.
> include/plist/plist.h:965-975: doc + declaration.

### Entry: plist_to_json
**Role:** Consumer/serializer entry; traverses the `plist_t` tree and renders JSON. Thin wrapper over `plist_to_json_with_options`.
**Needs before it:** Valid non-NULL `plist_t` tree, AND the tree's root must be of type `PLIST_DICT` or `PLIST_ARRAY` — other root types are rejected.
**Parameters:** `plist` (`plist_t`), `plist_json` (`char**` out), `length` (`uint32_t*` out), `prettify` (`int`, non-zero = pretty-print; mapped internally to `PLIST_OPT_NONE` vs `PLIST_OPT_COMPACT`, src/jplist.c:436-439).
**Behaviour-format check:** `if (!PLIST_IS_DICT(plist) && !PLIST_IS_ARRAY(plist)) { ... return PLIST_ERR_FORMAT; }` (src/jplist.c:451-454) — this is a real constraint a harness feeding arbitrary bplist-decoded trees must account for: calling `plist_to_json` on a tree whose root is e.g. PLIST_INT or PLIST_STRING (which `plist_from_bin` can legitimately produce if the root_object index points at a scalar) returns `PLIST_ERR_FORMAT`, not a crash — must check return value, not assume success.
**Return value:** `plist_err_t`; `PLIST_ERR_INVALID_ARG` if any of plist/plist_json/length is NULL; `PLIST_ERR_FORMAT` for incompatible root type; other negative codes from the estimate/write phase.
**Cleanup:** Caller frees `*plist_json` via `plist_mem_free()`/`free()` on success; input tree not consumed, free separately.
**Combination with target:** `plist_from_bin(...,&tree)` → `if (PLIST_IS_DICT(tree) || PLIST_IS_ARRAY(tree)) plist_to_json(tree, &json, &jlen, 1);` → use/compare → `plist_free(tree)` + free(json). Mirrors the standalone `fuzz/jplist_fuzzer.cc:27-29` pattern (from_json→plist_free) but here used as a post-processing/round-trip step after `plist_from_bin`.
**Evidence:**
> src/jplist.c:436-457: definition, option mapping, root-type guard.
> include/plist/plist.h:989-1000,1002-1023: doc + declarations for `plist_to_json` and `plist_to_json_with_options`.

### Combining all entries in one harness
**Claim:** A harness built from this Logic Group would: (1) feed the raw fuzz bytes to `plist_from_bin` (the target) directly to guarantee the bplist core is exercised regardless of magic bytes; (2) optionally also feed the same bytes to `plist_from_memory` to additionally exercise the format-sniffing gate (`plist_is_binary` is called internally, no need to call it separately unless testing the predicate itself); (3) on success (`PLIST_ERR_SUCCESS` and non-NULL tree), pass the resulting tree to `plist_to_bin` and/or `plist_to_xml` and/or `plist_to_json` (guarding the latter with a `PLIST_IS_DICT`/`PLIST_IS_ARRAY` check or simply ignoring its `PLIST_ERR_FORMAT` return) to exercise the round-trip/export side; (4) free every independently-produced tree with `plist_free()` and every independently-allocated output buffer with `plist_mem_free()`/`free()`, matching each `from_*`/`to_*` call 1:1 with its own cleanup call, never double-freeing and never freeing input buffers owned by the harness (e.g. the libFuzzer-provided `data`) through these APIs.
**Evidence:** Combination of evidence above: src/bplist.c:905-1020 (target), src/plist.c:225-239 (plist_from_memory dispatch into target), test/plist_btest.c:78-123 (full from_bin→to_xml→from_xml→to_bin→free chain), tools/plistutil.c:351-373 (is_binary→from_bin→sort→to_xml chain), src/jplist.c:451-454 (root-type gate for plist_to_json).

## Build notes

**Claim:** Fuzzers for this project are compiled as C++ (`.cc`), linked against the static library `libplist-2.0.a`, with the public header included via `-Iinclude/` as `<plist/plist.h>`. No cython/python bindings are built for fuzzing (`--without-cython`).
**Evidence:**
> ossfuzz_view/libplist-c1/build.sh:21-23: `./autogen.sh --without-cython --enable-debug` (or `--without-tests` under coverage/introspector sanitizers).
> ossfuzz_view/libplist-c1/build.sh:25-26: `make -j$(nproc) clean` / `make -j$(nproc) all` — builds the full library first (producing `src/.libs/libplist-2.0.a`).
> ossfuzz_view/libplist-c1/build.sh:28-32:
> ```
> for fuzzer in bplist_fuzzer xplist_fuzzer jplist_fuzzer oplist_fuzzer; do
>   $CXX $CXXFLAGS -std=c++11 -Iinclude/ \
>       fuzz/$fuzzer.cc -o $OUT/$fuzzer \
>       $LIB_FUZZING_ENGINE src/.libs/libplist-2.0.a
> done
> ```
> This shows: C++ compilation (`-std=c++11`), include path `-Iinclude/` (so `#include <plist/plist.h>` resolves to `include/plist/plist.h`), and static linking against `src/.libs/libplist-2.0.a` plus the fuzzing engine.
> fuzz/xplist_fuzzer.cc:22: `#include <plist/plist.h>` confirms the include-path convention used by sibling fuzzers in this same build.
> ossfuzz_view/libplist-c1/build.sh:34-37: seed corpora are zipped per-format from `test/data/*.bplist`, `*.plist`, `*.json`, `*.ostep` — `bplist_fuzzer_seed_corpus.zip` specifically uses `.bplist` sample files, confirming `bplist_fuzzer` (whose target is `plist_from_bin`, per the stub at fuzz/bplist_fuzzer.cc:10 pinning the symbol `plist_from_bin`) is meant to consume binary-plist-formatted input.
> ossfuzz_view/libplist-c1/Dockerfile:17-23: base image `gcr.io/oss-fuzz-base/base-builder`, project cloned from `https://github.com/libimobiledevice/libplist`, confirming standard OSS-Fuzz libFuzzer harness conventions (`LLVMFuzzerTestOneInput`, `$LIB_FUZZING_ENGINE`) apply.
> Note: the current `fuzz/bplist_fuzzer.cc` in this checkout is a stub ("Gold harness replaced by an empty stub for baseline generation") that only keeps `plist_from_bin` linked via an `__asm__` alias and returns 0 without calling it — it is NOT a functional harness and must be replaced by the downstream Harness Generator with real calls as described in this report.