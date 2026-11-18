## target: plist_from_json

### P2.1 Init Sequence
**Claim:** No global/library init call is required before `plist_from_json`. The function is self-contained: it allocates its own jsmn tokenizer state internally. The only "init" artifact in the module, `plist_json_init()`, only configures debug logging via an env var and is not required for correctness.
**Evidence:**
> include/plist/plist.h:1091: `PLIST_API plist_err_t plist_from_json(const char *json, uint32_t length, plist_t * plist);` (no precondition documented besides valid buffer/length)
> src/jplist.c:55-66: `void plist_json_init(void) { ... #ifdef DEBUG ... }` — only sets a debug flag, no state needed by `plist_from_json`.
> src/jplist.c:896-911: `plist_from_json` itself calls `jsmn_init(&parser)` internally before use — the caller does not need to initialize the parser.

### P2.2 Parameter Construction
**Claim:** `json` must be a non-NULL pointer to a byte buffer of at least `length` bytes (need not be NUL-terminated, length is explicit); `length` is a `uint32_t` byte count of the JSON text (0 is rejected); `plist` must be a non-NULL `plist_t*` output slot that will receive the parsed tree (set to `NULL` first thing inside the function, so caller does not need to pre-initialize it to NULL, but should pass a valid pointer to a `plist_t` variable).
**Evidence:**
> src/jplist.c:898-904:
> ```
> if (!plist) { return PLIST_ERR_INVALID_ARG; }
> *plist = NULL;
> if (!json || (length == 0)) { return PLIST_ERR_INVALID_ARG; }
> ```
> test/plist_jtest.c:68-80: caller allocates `plist_json = malloc(size_in+1)`, reads `size_in` bytes from file, NUL-terminates for safety (not required by the API) then calls `plist_from_json(plist_json, size_in, &root_node1)`.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is create (parse) → use (query/traverse/serialize) → destroy (`plist_free`). `plist_from_json` is the sole constructor of the returned `plist_t` tree on this path; there is no separate "configure" step for the parser (no parser handle exposed to the caller — the jsmn parser is internal and freed before return).
**Evidence:**
> src/jplist.c:906-935: internal `jsmn_parser parser` and `jsmntok_t *tokens` are allocated, grown, and ultimately `free(tokens)` is called (lines 927, 940, 944, 948, 952, 976) before the function returns in every exit path — the token buffer never escapes to the caller.
> src/jplist.c:977-980: `if (!*plist) { return (ti.err != PLIST_ERR_SUCCESS) ? ti.err : PLIST_ERR_PARSE; } return PLIST_ERR_SUCCESS;` — the only thing handed back to the caller is the `plist_t` tree (or NULL on failure).
> include/plist/plist.h:302: `PLIST_API void plist_free(plist_t plist);` "Destruct a plist_t node and all its children recursively" — the matching destroy call for the object this function returns.

### P2.4 Return Value Handling
**Claim:** Return value is a `plist_err_t`. Caller must check it is `PLIST_ERR_SUCCESS` (0) before trusting `*plist`; on any negative value `*plist` is guaranteed `NULL` (set early and never reassigned to a partial tree — any partial tree is freed internally before error return). Distinguishable error codes: `PLIST_ERR_INVALID_ARG` (bad args), `PLIST_ERR_NO_MEM` (token buffer OOM or size cap `INT_MAX-16` hit, or node allocation failure), `PLIST_ERR_PARSE` (malformed/truncated/oversized JSON, or recursive-descent failure), `PLIST_ERR_MAX_NESTING` (nesting depth exceeded in parse_object/parse_array).
**Evidence:**
> src/jplist.c:898-904: `PLIST_ERR_INVALID_ARG` for NULL plist / NULL json / zero length.
> src/jplist.c:925-935,937-956: jsmn tokenizing errors mapped to `PLIST_ERR_NO_MEM` (JSMN_ERROR_NOMEM after hitting the cap), `PLIST_ERR_PARSE` (JSMN_ERROR_INVAL, JSMN_ERROR_PART, JSMN_ERROR_LIMIT).
> src/jplist.c:752-755 / 816-818: `ti.err = PLIST_ERR_MAX_NESTING;` returned via `ti.err` then surfaced at line 978.
> include/plist/plist.h:141-149: full `plist_err_t` enum documented (`PLIST_ERR_SUCCESS=0`, negative values for errors).
> test/plist_jtest.c:80-85: real caller pattern — `plist_from_json(...)` then checks `if (!root_node1) { printf("...failed\n"); return 3; }` i.e. checks the output pointer rather than the return code directly (either is valid since `*plist` is NULL exactly when return != success).

### P2.5 Cleanup Sequence
**Claim:** On success, caller must eventually call `plist_free(*plist)` to release the whole tree (single call, recursive). On failure, nothing needs to be freed by the caller because `*plist` is NULL. No intermediate buffers (tokens) are exposed to the caller in any path.
**Evidence:**
> include/plist/plist.h:302: `plist_free` frees node and children recursively — used as the single cleanup call.
> fuzz/bplist_fuzzer.cc:27-29 (sibling fuzzer for plist_from_bin, same library convention): `plist_t root_node = NULL; plist_from_bin(...,&root_node); plist_free(root_node);` — `plist_free(NULL)` is safe to call unconditionally (no crash) matching the same convention expected for `plist_from_json`.
> src/jplist.c:976 `free(tokens);` — internal token array freed by the target itself; never the caller's responsibility.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase.
**Evidence:**
> src/jplist.c:896: `plist_err_t plist_from_json(...)` definition.
> src/plist.c:225: `plist_err_t plist_from_memory(...)` definition.
> src/jplist.c:436: `plist_err_t plist_to_json(...)` definition.
> src/jplist.c:442: `plist_err_t plist_to_json_with_options(...)` definition.
> src/plist.c:702-718 (plist_free, confirmed via grep) and include/plist/plist.h:302 declaration.
> src/plist.c:720: `void plist_mem_free(void* ptr)` definition — wraps `free()`.
> include/plist/plist.h:1081: `plist_from_bin` declaration (used in round-trip tests).
> include/plist/plist.h:987: `plist_to_bin` declaration (used in round-trip tests).

### P2.7 Co-call Constraints
**Claim:** `plist_from_json` has no lock/unlock or begin/commit pairing by itself; the only mandatory pairing is constructor/destructor: every successful `plist_from_json` (or `plist_from_memory`) call that returns a non-NULL tree must be matched by exactly one `plist_free` on that tree. `plist_to_json`/`plist_to_json_with_options` allocate an output buffer that must be paired with a `free()`/`plist_mem_free()` call (not `plist_free`, since it's a `char*`, not a `plist_t`).
**Evidence:**
> include/plist/plist.h:998: "@note Use plist_mem_free() to free the allocated memory." (doc for `plist_to_json`).
> tools/plistutil.c:478: `free(plist_out);` — the JSON string produced by `plist_to_json_with_options` is freed with plain `free` (equivalent to `plist_mem_free`).
> tools/plistutil.c:451,458: `plist_free(root_node);` called on the tree built from `plist_from_memory`, separately from freeing `plist_out`.

### P2.8 Prerequisite State
**Claim:** No external context/connection/config object must exist before calling `plist_from_json`; it is a pure function of its three parameters. The only "state" is the caller-owned input buffer and output pointer.
**Evidence:**
> src/jplist.c:896-911: function body only touches its parameters and local state (`jsmn_parser`, `tokens`) — no global/static mutable state other than the compile-time `DEBUG`-only `plist_json_debug` flag (src/jplist.c:47).

### P2.9 Result Usage
**Claim:** The `plist_t` tree produced by `plist_from_json` (or `plist_from_memory` when it auto-detects JSON) is consumed by: (a) generic plist accessors (`plist_get_node_type`, `plist_dict_get_item`, `plist_array_get_item`, `plist_array_get_size`, `plist_copy`) for traversal/inspection, and (b) by format-conversion writers `plist_to_json`/`plist_to_json_with_options`/`plist_to_bin`/`plist_to_xml`/`plist_to_openstep_with_options` for round-trip serialization, which in turn is consumed by `plist_from_bin`/`plist_from_xml` for a cross-format round trip.
**Evidence:**
> test/plist_jtest.c:80,88,96,104: `plist_from_json(...,&root_node1)` → `plist_to_bin(root_node1, &plist_bin, &size_out)` → `plist_from_bin(plist_bin, size_out, &root_node2)` → `plist_to_json(root_node2, &plist_json2, &size_out2, 0)` — a full JSON→bin→JSON round trip chain.
> tools/plistutil.c:376-408: `plist_from_memory(...,&root_node,NULL)` result is traversed with `plist_get_node_type(current)`, `plist_dict_get_item`, `plist_array_get_size`, `plist_array_get_item`, and optionally `plist_copy(current)` to extract a sub-node.
> tools/plistutil.c:443: `plist_to_json_with_options(root_node, &plist_out, &size, wropts)` — the tree from `plist_from_memory` fed straight into the JSON writer.
> src/jplist.c:960-972 (inside target): the top-level token type dispatches to `parse_primitive`/`parse_string`/`parse_array`/`parse_object`, each of which builds the plist node that becomes `*plist` and is handed to all downstream consumers above.

### P2.10 Behaviour Controls
**Claim:** `plist_from_json` itself takes no mode flags — all path selection is data-driven from the JSON content (top-level token type, nesting, escapes, numeric literal shape). The consumer entries do expose explicit flags: `plist_to_json`'s `prettify` int (0 = compact, nonzero = pretty) and `plist_to_json_with_options`'s bitfield `plist_write_options_t` (`PLIST_OPT_COMPACT`, `PLIST_OPT_COERCE`); `plist_from_memory`'s format sniffing (binary vs JSON vs XML vs OpenStep) based on first non-whitespace bytes.
**Evidence:**
> src/jplist.c:960-974: `switch (tokens[startindex].type) { case JSMN_PRIMITIVE: ... case JSMN_STRING: ... case JSMN_ARRAY: ... case JSMN_OBJECT: ... }` — top-level dispatch purely from input content.
> src/jplist.c:752-755, 816-818: recursion depth `depth` checked against `PLIST_MAX_NESTING_DEPTH` in `parse_array`/`parse_object`, controlled by how deeply the JSON input nests `[`/`{`.
> src/jplist.c:436-439: `plist_err_t plist_to_json(plist_t plist, char **plist_json, uint32_t* length, int prettify) { plist_write_options_t opts = prettify ? PLIST_OPT_NONE : PLIST_OPT_COMPACT; return plist_to_json_with_options(...); }`
> src/jplist.c:456-457: `int prettify = !(options & PLIST_OPT_COMPACT); int coerce = options & PLIST_OPT_COERCE;` inside `plist_to_json_with_options`.
> include/plist/plist.h:1008-1013: doc — `PLIST_OPT_COERCE` converts `PLIST_DATE`/`PLIST_DATA`/`PLIST_UID` (non-native JSON types) into JSON-compatible representations instead of returning `PLIST_ERR_FORMAT`.
> src/plist.c:237-298 (`plist_from_memory`): branches on `plist_is_binary()`, then `'<'`/`'['`/`'('`/`'{'` first-byte sniffing to choose `plist_from_bin`/`plist_from_xml`/`plist_from_json`/`plist_from_openstep`.

### P2.11 Input Expectations
**Claim:** Input is raw JSON text (not necessarily NUL-terminated; length is explicit) representing a single top-level JSON value — the target rejects non-object/array/string/primitive top-level and malformed/truncated input; it enforces an internal max nesting depth, grows its token buffer dynamically up to a hard cap, and accepts standard JSON string escapes including `\uXXXX` (with surrogate handling) and numeric literals (integer/float/exponent, with overflow saturation in `parse_decimal`). Note: `plist_to_json_with_options`/`plist_to_json` require the *plist* input to be a dict or array (not usable on bare scalars) — this is a constraint on the inverse/consumer entry, not on `plist_from_json` itself.
**Evidence:**
> src/jplist.c:902-904: `if (!json || (length == 0)) return PLIST_ERR_INVALID_ARG;` — length must be > 0.
> src/jplist.c:924: `r = jsmn_parse(&parser, json, length, tokens, maxtoks);` — length-bounded, no reliance on NUL termination.
> src/jplist.c:913-935: token buffer growth loop on `JSMN_ERROR_NOMEM`, incrementing `maxtoks` by 16 each round, capped: `if (maxtoks > (unsigned int)INT_MAX - 16) { free(tokens); return PLIST_ERR_NO_MEM; }` (line 926).
> src/jplist.c:942,946,950: explicit handling of `JSMN_ERROR_INVAL` ("Invalid character inside JSON string"), `JSMN_ERROR_PART` ("Incomplete JSON, more bytes expected"), `JSMN_ERROR_LIMIT` ("Input data too large").
> src/jplist.c:752-755/816-818: `PLIST_MAX_NESTING_DEPTH` enforced recursively in `parse_array`/`parse_object`.
> src/jplist.c:960-974: only `JSMN_PRIMITIVE`, `JSMN_STRING`, `JSMN_ARRAY`, `JSMN_OBJECT` top-level token types are handled; any other token type at index 0 falls through `default:` leaving `*plist` NULL, yielding `PLIST_ERR_PARSE` at line 978.
> src/jplist.c:451-454 (in `plist_to_json_with_options`): `if (!PLIST_IS_DICT(plist) && !PLIST_IS_ARRAY(plist)) { ...; return PLIST_ERR_FORMAT; }` — the writer entry requires dict/array root.

## Other entries of the Logic Group

### Entry: plist_from_memory
**Role:** source/variant of the target — auto-detects buffer format and, for JSON-looking content, forwards directly to `plist_from_json`.
**Call protocol:**
- Prerequisite: none beyond a valid `(plist_data, length)` buffer; `plist` out-param non-NULL; optional `format` out-param may be NULL.
- Parameters: `const char *plist_data`, `uint32_t length` (>0), `plist_t *plist` (required), `plist_format_t *format` (optional, receives detected format on success).
- Behaviour-selecting values: first bytes of buffer — `"bplist00"` (8 bytes) ⇒ binary (`plist_from_bin`); `<` followed by non-hex-digit triples ⇒ XML (`plist_from_xml`); `[` ⇒ JSON; `{` ⇒ JSON or OpenStep disambiguated by lookahead for a quoted key followed by `:`; `(` ⇒ OpenStep; anything else falls to `plist_from_openstep`.
- Return value: `plist_err_t`, same semantics as target (`PLIST_ERR_SUCCESS`/`PLIST_ERR_INVALID_ARG`/`PLIST_ERR_PARSE`/etc., whatever the dispatched-to parser returns); `*format` only set if `res == PLIST_ERR_SUCCESS`.
- Cleanup: same as target — caller must `plist_free(*plist)` on success; nothing to free on failure (`*plist` is NULL).
- Combination with target in one harness: calling `plist_from_memory(data, size, &root, &fmt)` on raw fuzz bytes is an **alternative** top-level entry point exercising the same JSON core when the sniffing logic picks JSON; a harness should not call both `plist_from_json` and `plist_from_memory` on the *same* buffer expecting independent trees without being aware that `plist_from_memory` may route into XML/binary/OpenStep instead of JSON. It is best used to validate the format-detection branch itself, with `plist_free(root)` as cleanup either way.
**Evidence:**
> src/plist.c:225-234: parameter validation, identical pattern to target.
> src/plist.c:237-298: full format-detection switch, `plist_from_json(plist_data, length, plist)` called at line 293 only on the `is_json` branch.
> src/plist.c:300-303: `if (format && res == PLIST_ERR_SUCCESS) { *format = fmt; }`
> include/plist/plist.h:1104-1121: doc confirms "very basic" detection heuristic and lists all four target formats.
> tools/plistutil.c:376: real caller `plist_from_memory(plist_entire, read_size, &root_node, NULL)`.

### Entry: plist_to_json
**Role:** consumer/inverse — thin wrapper over `plist_to_json_with_options`, serializes a plist tree (presumably built by `plist_from_json`) back to JSON text.
**Call protocol:**
- Prerequisite: `plist` must be a non-NULL `plist_t` whose root node type is `PLIST_DICT` or `PLIST_ARRAY` (checked inside `plist_to_json_with_options`); typically the tree returned by a prior successful `plist_from_json`/`plist_from_memory` call.
- Parameters: `plist_t plist` (root to export), `char **plist_json` (out, library-allocated), `uint32_t *length` (out, strlen of produced buffer, excludes the extra NUL byte reserved internally), `int prettify` (0 = compact single-line, non-zero = pretty/indented with trailing `\n`).
- Return value: `plist_err_t`; `PLIST_ERR_INVALID_ARG` if any pointer NULL; `PLIST_ERR_FORMAT` if root isn't dict/array; other negative codes propagate from the underlying size-estimation/serialization walk (e.g. `PLIST_ERR_NO_MEM`, `PLIST_ERR_CIRCULAR_REF`, `PLIST_ERR_MAX_NESTING` — see tools/plistutil.c:485-491 handling of these from the sibling writer entry).
- Cleanup: caller must free `*plist_json` with `free()`/`plist_mem_free()` on success; on failure `*plist_json`/`*length` are left `NULL`/`0` by `plist_to_json_with_options` (see below).
- Combination with target: natural round-trip consumer — `plist_from_json(buf,len,&root)` then, if success, `plist_to_json(root, &json2, &len2, prettify)`, then `plist_mem_free(json2)` then `plist_free(root)`.
**Evidence:**
> src/jplist.c:436-439: `plist_to_json` definition, delegates to `plist_to_json_with_options`.
> include/plist/plist.h:990-1000: doc, "@note Use plist_mem_free() to free the allocated memory."
> test/plist_jtest.c:104-109: real caller `plist_to_json(root_node2, &plist_json2, &size_out2, 0)` then checked `if (!plist_json2) { ... }`.

### Entry: plist_to_json_with_options
**Role:** consumer/inverse with explicit flags, the real implementation behind `plist_to_json`.
**Call protocol:**
- Prerequisite: same as `plist_to_json` — `plist` non-NULL and root type dict/array.
- Parameters: `plist_t plist`, `char **plist_json` (out), `uint32_t *length` (out), `plist_write_options_t options` (bitmask: `PLIST_OPT_NONE`=0 default; `PLIST_OPT_COMPACT` disables prettify; `PLIST_OPT_COERCE` converts PLIST_DATE/PLIST_DATA/PLIST_UID into JSON-native representations instead of erroring).
- Internal sequence: `node_estimate_size()` (allocates/destroys an internal hashtable for circular-ref detection) → allocate `strbuf_t` of that estimated size via `str_buf_new` → `node_to_json()` fills it → optional trailing `\n` if prettified → NUL-terminate → hand the raw buffer to caller and `str_buf_free` the wrapper struct (not the data).
- Return value: `plist_err_t`; `PLIST_ERR_INVALID_ARG` (null params), `PLIST_ERR_FORMAT` (root not dict/array), `PLIST_ERR_NO_MEM` (buffer alloc failure), `PLIST_ERR_CIRCULAR_REF`/`PLIST_ERR_MAX_NESTING`/others bubbled up from `node_estimate_size`/`node_to_json` through the generic node walker (see tools/plistutil.c:485-491 switch on `output_res`).
- Cleanup: on error, `*plist_json` and `*length` are explicitly reset to `NULL`/`0` (src/jplist.c:473-474) so nothing needs freeing by caller on failure; on success caller frees `*plist_json` with `free()`/`plist_mem_free()`.
- Combination with target: use after `plist_from_json` to round-trip with either compact or coerce semantics, e.g. to validate that plist nodes not natively JSON-expressible (if the parser ever created such — it does not, since JSON input only yields PLIST_STRING/BOOLEAN/UINT/REAL/ARRAY/DICT/NULL nodes, see parse_primitive) are handled; more realistically used to fuzz the writer with various option combinations on whatever tree `plist_from_json` produced.
**Evidence:**
> src/jplist.c:442-490: full definition as analyzed above (param checks at 447-454, option decode 456-457, size+alloc 459-468, write 470-476, NUL-terminate+handoff 481-487).
> include/plist/plist.h:1002-1023: doc for options and coercion semantics.
> tools/plistutil.c:440-443: real caller building `wropts` from CLI flags `OPT_COMPACT`/`OPT_COERCE` then calling `plist_to_json_with_options(root_node, &plist_out, &size, wropts)`.
> tools/plistutil.c:478: `free(plist_out);` cleanup of the produced buffer.

## Build notes
**Claim:** Fuzzers for this project are C++ (compiled with `$CXX`/`-std=c++11`), include the public header via `-Iinclude/` (so `#include <plist/plist.h>` resolves to `include/plist/plist.h`, the same file where `plist_from_json` is declared), and link statically against the built library archive `src/.libs/libplist-2.0.a`. No other libraries are linked explicitly in the fuzzer build step (`$LIB_FUZZING_ENGINE` provides the fuzzing engine/main).
**Evidence:**
> ${QF_WORK}/ossfuzz_view/libplist-c2/build.sh:21-26: `./autogen.sh --without-cython --enable-debug` then `make all` builds the library first.
> ${QF_WORK}/ossfuzz_view/libplist-c2/build.sh:28-32:
> ```
> for fuzzer in bplist_fuzzer xplist_fuzzer jplist_fuzzer oplist_fuzzer; do
>   $CXX $CXXFLAGS -std=c++11 -Iinclude/ \
>       fuzz/$fuzzer.cc -o $OUT/$fuzzer \
>       $LIB_FUZZING_ENGINE src/.libs/libplist-2.0.a
> done
> ```
> fuzz/bplist_fuzzer.cc:22: `#include <plist/plist.h>` — confirms the `-Iinclude/` path resolves to `include/plist/plist.h`.
> fuzz/jplist_fuzzer.cc:1-16: current on-disk `jplist_fuzzer.cc` is a stub ("Gold harness replaced by an empty stub for baseline generation") that merely keeps `plist_from_json` linked via `__asm__` aliasing and does not call it — the real target-calling logic must be (re)written by the harness generator following the pattern shown by the sibling `bplist_fuzzer.cc`/`xplist_fuzzer.cc` (parse → free).
> build.sh:34-37: seed corpora are zipped per-fuzzer from `test/data/*.json` etc., confirming `test/data/*.json` is the natural seed corpus directory for a JSON-targeting harness.