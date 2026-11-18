## target: uriDissectQueryMallocA

### P2.1 Init Sequence
**Claim:** No library-global init call is required. The only hard prerequisite is that the caller already has a `first`/`afterLast` character range describing the raw query text (bytes strictly after the `'?'`, before any `'#'`), typically obtained from `uriParseUriA()`'s `UriUriA.query` text range, or simply from a raw buffer + length picked directly by the fuzzer. `uriDissectQueryMallocA` itself is a thin wrapper that forwards to `uriDissectQueryMallocExA` → `uriDissectQueryMallocExMmA` with fixed defaults `plusToSpace = URI_TRUE`, `breakConversion = URI_BR_DONT_TOUCH`, `memory = NULL` (libc default).
**Evidence:**
> src/UriQuery.c:383-390:
> ```
> int URI_FUNC(DissectQueryMalloc)(URI_TYPE(QueryList) * *dest, int * itemCount,
>         const URI_CHAR * first, const URI_CHAR * afterLast) {
>     const UriBool plusToSpace = URI_TRUE;
>     const UriBreakConversion breakConversion = URI_BR_DONT_TOUCH;
>     return URI_FUNC(DissectQueryMallocEx)(
>             dest, itemCount, first, afterLast, plusToSpace, breakConversion);
> }
> ```
> include/uriparser/Uri.h:1013-1016 (doxygen): "`first` IN: Pointer to first character after '?'", "`afterLast` IN: Pointer to character after the last one still in [query]".
> test/test.cpp:1830-1841 (helperTestQueryString): calls `uriParseUriA(&state, uriString)` first, then passes `uri.query.first, uri.query.afterLast` into `uriDissectQueryMallocA`.
> test/test.cpp:1814-1815 and 1929, test/MemoryManagerSuite.cpp:138-140: other callers build `first`/`afterLast` directly from a raw C string (`queryString`, `queryString + strlen(queryString)`), with no prior parse call — a raw buffer is sufficient, parsing a whole URI is not mandatory.

### P2.2 Parameter Construction
**Claim:** 4 parameters: `dest` (`UriQueryListA **`, out, must not be NULL — caller provides an uninitialized pointer, function sets `*dest`); `itemCount` (`int *`, out, **may be NULL**, function internally substitutes a local counter if so); `first`/`afterLast` (`const char *`, in, both must be non-NULL and satisfy `first <= afterLast`; they need not be NUL-terminated, the exact range `[first, afterLast)` is walked).
**Evidence:**
> src/UriQuery.c:399-417 (DissectQueryMallocExMm body):
> ```
> int * itemsAppended = (itemCount == NULL) ? &nullCounter : itemCount;
> if ((dest == NULL) || (first == NULL) || (afterLast == NULL)) {
>     return URI_ERROR_NULL;
> }
> if (first > afterLast) {
>     return URI_ERROR_RANGE_INVALID;
> }
> ```
> include/uriparser/Uri.h:1013-1016: "itemCount OUT: Number of items found, can be NULL".
> test/MemoryManagerSuite.cpp:140: real caller passes `NULL` for `itemCount`: `uriDissectQueryMallocA(&queryList, NULL, first, afterLast)`.

### P2.3 Object Lifecycle
**Claim:** Create (dissect) → use (read list fields / compose) → destroy (`uriFreeQueryListA`/`uriFreeQueryListMmA`). The returned list is a singly linked `UriQueryListA` (`key`, `value`, `next`), newly heap-allocated node-by-node; `*dest` is set to `NULL` first and either stays `NULL` (zero items, e.g. empty query) or points to the first node.
**Evidence:**
> src/UriQuery.c:421-422: `*dest = NULL; *itemsAppended = 0;` at function start.
> include/uriparser/Uri.h:188-194 (QueryListStruct): `key`, `value` (`const URI_CHAR*`, value can be NULL), `next`.
> test/test.cpp:1812-1827 (TestQueryStringEndingInEqualSignNonBug32): dissect → read `queryList->key/value`, `queryList->next->...` → `uriFreeQueryListA(queryList)`.
> test/test.cpp:1919 (`testQueryListHelper(L"", 0)`): empty input legally yields `itemCount == 0` (list can be empty/NULL in that case; see P2.9/P2.10).

### P2.4 Return Value Handling
**Claim:** Returns `URI_SUCCESS` (0) on success, or an error code to check and branch on. On error, `*dest` is left pointing at `NULL` (the engine frees any partially-built list itself on malloc failure), so the caller does not need to free on error, only check the return code before touching `*dest`/`itemCount`.
**Evidence:**
> include/uriparser/UriBase.h:110-120: `URI_SUCCESS=0`, `URI_ERROR_NULL`, `URI_ERROR_MALLOC=3`, `URI_ERROR_RANGE_INVALID=9`, `URI_ERROR_MEMORY_MANAGER_INCOMPLETE`.
> src/UriQuery.c:434-441 and 483-490 (DissectQueryMallocExMm): on `AppendQueryItem(...) == URI_FALSE`:
> ```
> *itemsAppended = 0;
> URI_FUNC(FreeQueryListMm)(*dest, memory);
> return URI_ERROR_MALLOC;
> ```
> (Library frees the partial list and does not leave a dangling `*dest`, but `*dest` itself is not reset to NULL afterward: callers should treat "non-`URI_SUCCESS`" as "no usable list" and not call `uriFreeQueryListA` a second time — see P2.5.)
> test/test.cpp:1814-1818 and 1929-1933: callers always check `ASSERT_EQ(res, URI_SUCCESS)` before touching `queryList`.
> test/MemoryManagerSuite.cpp:292-294: `ASSERT_EQ(uriDissectQueryMallocExMmA(...), URI_ERROR_MALLOC)` under a `FailingMemoryManager` — confirms `URI_ERROR_MALLOC` is the documented failure mode for allocation failure inside `AppendQueryItem`.

### P2.5 Cleanup Sequence
**Claim:** On success, caller must call `uriFreeQueryListA(queryList)` (or `uriFreeQueryListMmA(queryList, memory)` if a custom memory manager was used in the ExMm variant) exactly once to free every node plus its `key`/`value` buffers plus the node itself. On failure (`res != URI_SUCCESS`), the library has already freed everything it allocated internally (see P2.4); the caller must NOT call `uriFreeQueryListA` on a never-populated/already-freed `*dest`.
**Evidence:**
> src/UriQuery.c:370-381 (FreeQueryListMm):
> ```
> while (queryList != NULL) {
>     URI_TYPE(QueryList) * nextBackup = queryList->next;
>     memory->free(memory, (URI_CHAR *)queryList->key);
>     memory->free(memory, (URI_CHAR *)queryList->value);
>     memory->free(memory, queryList);
>     queryList = nextBackup;
> }
> ```
> src/UriQuery.c:366-368: `FreeQueryList` forwards to `FreeQueryListMm(queryList, NULL)` (libc free).
> test/test.cpp:1826, 1845, 1942: `uriFreeQueryListA(queryList)` called once after use, matching each successful dissect call 1:1.
> test/MemoryManagerSuite.cpp:297-304 (FreeQueryListMm test): demonstrates the Mm variant is the one to pair with a custom memory manager used at dissect/compose time.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase (verified by grep/read, ANSI "A" suffix forms; project macro-expands them from `URI_FUNC(...)` for both ANSI and Unicode passes).
**Evidence:**
> src/UriQuery.c:383 `DissectQueryMalloc`, :392 `DissectQueryMallocEx`, :399 `DissectQueryMallocExMm`, :277 `AppendQueryItem` (static, internal), :366 `FreeQueryList`, :370 `FreeQueryListMm`, :100 `ComposeQuery`, :109 `ComposeQueryEx`, :123 `ComposeQueryMalloc`, :131 `ComposeQueryMallocEx`, :138 `ComposeQueryMallocExMm`, :180 `ComposeQueryEngine`, :81 `ComposeQueryCharsRequired`, :90 `ComposeQueryCharsRequiredEx`.
> src/UriEscape.c:181 `UnescapeInPlaceEx` (called from AppendQueryItem, src/UriQuery.c:326,354).
> include/uriparser/Uri.h:1026,1047,1070,1084,1097 declare the `A`-suffixed public wrappers that macro-expand to these (`URI_PUBLIC int uriDissectQueryMallocA(...)` etc. via `UriDefsAnsi.h`).

### P2.7 Co-call Constraints
**Claim:** `uriDissectQueryMalloc*` / `uriFreeQueryList*` are a paired create/destroy API (every successful dissect needs exactly one matching free call). `uriDissectQueryMallocExMmA` and `uriComposeQueryMallocExMmA`/`uriFreeQueryListMmA` must be used with the *same* `UriMemoryManager*` instance if a non-default manager is used, since nodes/buffers allocated by one manager's `malloc` must be released by the same manager's `free`.
**Evidence:**
> src/UriQuery.c:370-372: `FreeQueryListMm` also runs `URI_CHECK_MEMORY_MANAGER(memory)` — passing NULL safely substitutes the default, but a custom manager must be passed consistently to free what it allocated (its `malloc`-returned pointers can only be freed by its own `free`).
> test/MemoryManagerSuite.cpp:283-295 (DissectQueryMallocExMm test) and :297-304 (FreeQueryListMm test): show the pairing of a dissect-with-manager call with the matching free-with-manager call.

### P2.8 Prerequisite State
**Claim:** No global/context state is required. The only "state" needed is the `[first, afterLast)` character range; it is commonly produced by a prior `uriParseUriA()` call's `UriUriA.query` field, but the dissect function itself works on any raw buffer independent of a `UriUriA`. No open "session"/"connection" objects exist in this API.
**Evidence:**
> test/test.cpp:1830-1841: `uriParseUriA(&state, uriString)` → `uri.query.first/afterLast` → `uriDissectQueryMallocA`.
> test/test.cpp:1814-1815, test/MemoryManagerSuite.cpp:138-140: dissect called directly on a raw string with no preceding `uriParseUriA`, confirming parsing a full URI is optional, not required.

### P2.9 Result Usage
**Claim:** The produced `UriQueryListA*` (and `itemCount`) are consumed by: (a) direct field reads (`queryList->key`, `->value`, `->next`) in tests; (b) the compose family (`uriComposeQueryCharsRequiredExA` to size a buffer, then `uriComposeQueryExA`/`uriComposeQueryA` to serialize into caller buffer, or `uriComposeQueryMallocA`/`uriComposeQueryMallocExA`/`uriComposeQueryMallocExMmA` to serialize into a freshly malloc'd string) as the natural round-trip; (c) `uriFreeQueryListA`/`uriFreeQueryListMmA` to release it.
**Evidence:**
> test/test.cpp:1874-1909 (testQueryListHelper): `uriDissectQueryMallocExW` → `uriComposeQueryCharsRequiredExW(queryList, &charsRequired, ...)` → allocate `recomposed` buffer of `charsRequired+1` → `uriComposeQueryExW(recomposed, queryList, charsRequired+1, &charsWritten, ...)` → compare with input → `uriComposeQueryMallocW(&recomposed, queryList)` → `free(recomposed)` → `uriFreeQueryListW(queryList)`.
> test/test.cpp:1929-1943 (testQueryListPairHelper): `uriDissectQueryMallocA` → read `queryList->key/value` → `uriComposeQueryMallocA(&recomposed, queryList)` → `free(recomposed)` → `uriFreeQueryListA(queryList)`.
> test/test.cpp:1812-1827: dissect → direct field reads only → `uriFreeQueryListA`.
> src/UriQuery.c:180-275 (ComposeQueryEngine): walks `queryList->key/value/next` exactly as produced by dissect, confirming the structural contract between the two.

### P2.10 Behaviour Controls
**Claim:** `uriDissectQueryMallocA` itself is fixed-behavior (`plusToSpace=URI_TRUE`, `breakConversion=URI_BR_DONT_TOUCH`, default memory manager). The "Ex"/"ExMm" siblings expose the real behavior switches the harness should vary: `plusToSpace` (`URI_TRUE`/`URI_FALSE`, whether '+' becomes ' ' during unescape) and `breakConversion` (`URI_BR_TO_LF`, `URI_BR_TO_CRLF`, `URI_BR_TO_CR`, `URI_BR_DONT_TOUCH` — controls how `%0D`/%0A sequences are normalized in `UnescapeInPlaceEx`), and `memory` (NULL = libc default vs a custom/failing `UriMemoryManager*` to exercise malloc-failure branches in `AppendQueryItem`/`DissectQueryMallocExMm`). On the compose side, `spaceToPlus`/`normalizeBreaks` booleans (fixed to `URI_TRUE` in the non-Ex compose wrappers, exposed in `*Ex*` variants) mirror the same choices for the inverse operation. Query-string content itself (presence/absence of `=`, repeated `&`/`=`, percent-escapes) selects distinct parse branches inside `DissectQueryMallocExMm`'s per-character `switch`.
**Evidence:**
> src/UriQuery.c:385-389 (`DissectQueryMalloc` hardcodes `plusToSpace=URI_TRUE`, `breakConversion=URI_BR_DONT_TOUCH`).
> src/UriQuery.c:392-397 (`DissectQueryMallocEx` exposes both as parameters, forwards to ExMm with `memory=NULL`).
> src/UriQuery.c:426-472: the `for` loop `switch (*walk)` on `'&'` and `'='` is the branch selector driven purely by input bytes — e.g. "NOTE: WE treat the first '=' as a separator, all following go into the value part" (src/UriQuery.c:459-460).
> include/uriparser/UriBase.h:237-246: `UriBreakConversionEnum` values `URI_BR_TO_LF/URI_BR_TO_CRLF/URI_BR_TO_CR/URI_BR_DONT_TOUCH` (aliases `URI_BR_TO_UNIX/WINDOWS/MAC`).
> test/MemoryManagerSuite.cpp:283-295: custom `FailingMemoryManager` passed to `uriDissectQueryMallocExMmA` to force `URI_ERROR_MALLOC`.
> test/test.cpp:1805-1807 (`TestPervertedQueryString`): `"http://example.org/?&&=&&&=&&&&==&===&===="` exercises repeated empty-key/value boundary branches.

### P2.11 Input Expectations
**Claim:** Input is an arbitrary byte/char range `[first, afterLast)` — NOT required to be NUL-terminated, no header/magic, no declared size limit other than pointer range validity (`first <= afterLast`, both non-NULL). It is meant to be the query component of a URI (text after '?', before '#'), but the function does not verify RFC validity of its content; percent-escapes are decoded lazily afterward inside `AppendQueryItem`/`UnescapeInPlaceEx`, not validated during the split. Empty range (`first == afterLast`) is legal and yields one single empty key/value item unless further guarded (see `testQueryListHelper(L"", 0)` → 0 items, because the terminal `AppendQueryItem` call rejects an all-empty key+NULL value — see AppendQueryItem's `keyFirst==keyAfter && valueFirst==NULL && valueAfter==NULL` guard).
**Evidence:**
> src/UriQuery.c:411-417: NULL checks and `first > afterLast` → `URI_ERROR_RANGE_INVALID`; no NUL-termination check anywhere.
> src/UriQuery.c:286-290 (AppendQueryItem guard): `((keyFirst == keyAfter) && (valueFirst == NULL) && (valueAfter == NULL))` → returns `URI_TRUE` without appending (i.e., silently skips purely-empty segments), explaining why an empty query string legitimately produces `itemCount == 0`.
> test/test.cpp:1919: `testQueryListHelper(L"", 0)` — empty input, 0 items expected.
> include/uriparser/Uri.h:1015-1016: doc says `first`/`afterLast` must bound "the first character after '?'" and "character after the last one still in" the query — a half-open range convention, not a NUL-terminated C string.

## Other Logic-Group Entries — call protocol and combination with the target

### uriDissectQueryMallocExA
**Claim:** Same core as the target (`DissectQueryMallocExMm` with `memory=NULL`), but exposes `plusToSpace` (`UriBool`) and `breakConversion` (`UriBreakConversion`) as explicit parameters instead of hardcoding them. Same init/parameter/lifecycle/return/cleanup contract as the target (P2.1-P2.8 above), only the two extra params select the unescape behavior. Combination in harness: call instead of/in addition to the target to vary `plusToSpace`/`breakConversion` directly from fuzz bytes; still cleaned up with `uriFreeQueryListA`/`uriFreeQueryListMmA`.
**Evidence:**
> src/UriQuery.c:392-397:
> ```
> int URI_FUNC(DissectQueryMallocEx)(URI_TYPE(QueryList) * *dest, int * itemCount,
>         const URI_CHAR * first, const URI_CHAR * afterLast, UriBool plusToSpace,
>         UriBreakConversion breakConversion) {
>     return URI_FUNC(DissectQueryMallocExMm)(
>             dest, itemCount, first, afterLast, plusToSpace, breakConversion, NULL);
> }
> ```
> test/test.cpp:1876-1877: `uriDissectQueryMallocExW(&queryList, &itemCount, input, input+wcslen(input), spacePlusConversion, breakConversion)`.

### uriDissectQueryMallocExMmA
**Claim:** The real core entry: same as above plus an explicit `UriMemoryManager *memory` (NULL ⇒ default libc manager, validated/filled in by `URI_CHECK_MEMORY_MANAGER`; non-NULL ⇒ must be "complete", i.e. have all 5 function pointers set, else returns `URI_ERROR_MEMORY_MANAGER_INCOMPLETE`). Used by the harness to additionally exercise malloc-failure cleanup paths (`URI_ERROR_MALLOC`) inside `AppendQueryItem`/`DissectQueryMallocExMm`. Must pair with `uriFreeQueryListMmA(list, sameMemory)` on success (not plain `uriFreeQueryListA`, if a custom manager was used, since its allocations must be freed with its own `free`).
**Evidence:**
> src/UriQuery.c:399-401,419: `URI_CHECK_MEMORY_MANAGER(memory)` then builds the list with `memory->malloc`/`memory->free` throughout (`AppendQueryItem`, src/UriQuery.c:299,307,313,334,340 etc).
> test/MemoryManagerSuite.cpp:283-295 (`FailingMemoryManagerSuite.DissectQueryMallocExMm`): passes a `FailingMemoryManager` and asserts `URI_ERROR_MALLOC`.
> src/UriMemory.h:47-54 (`URI_CHECK_MEMORY_MANAGER` macro): NULL → defaultMemoryManager; else must pass `uriMemoryManagerIsComplete`.

### uriComposeQueryA
**Claim:** Inverse/consumer operation: writes the query list back out into a **caller-provided, pre-sized** buffer `dest` (no allocation). Requires: `dest != NULL`, `queryList != NULL` (can be a list produced by any of the Dissect* entries, `maxChars >= 1`; `maxChars` must be the full size of `dest` including the terminating NUL. Fixed to `spaceToPlus=URI_TRUE, normalizeBreaks=URI_TRUE` (forwards to `ComposeQueryExA`). Returns `URI_SUCCESS`/`URI_ERROR_NULL`/`URI_ERROR_OUTPUT_TOO_LARGE`. Caller must size `dest` first via `uriComposeQueryCharsRequiredA`/`...ExA` (NOT part of this entry list but is the sizing helper used in all real call sites). Does not free/own `queryList` — the list is still owned by whoever dissected it and must separately be freed via `uriFreeQueryListA`.
**Evidence:**
> src/UriQuery.c:100-107:
> ```
> int URI_FUNC(ComposeQuery)(URI_CHAR * dest, const URI_TYPE(QueryList) * queryList,
>         int maxChars, int * charsWritten) {
>     const UriBool spaceToPlus = URI_TRUE;
>     const UriBool normalizeBreaks = URI_TRUE;
>     return URI_FUNC(ComposeQueryEx)(dest, queryList, maxChars, charsWritten, spaceToPlus, normalizeBreaks);
> }
> ```
> src/UriQuery.c:111-117 (ComposeQueryEx null/size checks): `if ((dest==NULL)||(queryList==NULL)) return URI_ERROR_NULL;` `if (maxChars < 1) return URI_ERROR_OUTPUT_TOO_LARGE;`
> test/test.cpp:1884-1897 (testQueryListHelper): sizes via `uriComposeQueryCharsRequiredExW`, allocates `recomposed`, then calls `uriComposeQueryExW(recomposed, queryList, charsRequired+1, &charsWritten, ...)`.

### uriComposeQueryExA
**Claim:** Same as `uriComposeQueryA` but `spaceToPlus`/`normalizeBreaks` are explicit parameters, mirroring the Dissect side's `plusToSpace`/`breakConversion` for a correct round-trip test. Same NULL/`maxChars` checks and same buffer-ownership contract (caller allocates/frees `dest`, `queryList` is untouched/not freed).
**Evidence:**
> src/UriQuery.c:109-121 (`ComposeQueryEx`), calls `ComposeQueryEngine(dest, queryList, maxChars, charsWritten, NULL, spaceToPlus, normalizeBreaks)`.
> test/test.cpp:2007-2020 (`TestComposeQueryCharsRequired`-style tests): explicit `spaceToPlus`,`normalizeBreaks` passed, and `URI_ERROR_OUTPUT_TOO_LARGE` checked when the dest buffer is 1 byte too small — demonstrates required P2.4 return-check behavior.

### uriComposeQueryMallocA
**Claim:** Allocating variant: computes required size via `ComposeQueryCharsRequiredEx` internally, `calloc`s a buffer with the default memory manager, composes into it, and returns the new string via `*dest`. Caller must `free()` the returned string (plain libc `free`, since default manager is libc-based) — note: NOT `uriFreeQueryListA`, that only frees `UriQueryList` nodes, not compose-malloc'd strings. `queryList` itself is unaffected/not freed; still needs its own `uriFreeQueryListA` call.
**Evidence:**
> src/UriQuery.c:123-129 (`ComposeQueryMalloc`) forwards to `ComposeQueryMallocEx` → `ComposeQueryMallocExMm(dest, queryList, spaceToPlus, normalizeBreaks, NULL)`.
> src/UriQuery.c:138-178 (`ComposeQueryMallocExMm`): sizes via `ComposeQueryCharsRequiredEx`, checks `charsRequired == INT_MAX` → `URI_ERROR_MALLOC`, `memory->calloc(...)`, composes, `*dest = queryString;` on success, `memory->free(memory, queryString)` and returns error on failed compose.
> test/test.cpp:1901-1906,1936-1941: `uriComposeQueryMallocW(&recomposed, queryList)` ... `free(recomposed)`; `uriComposeQueryMallocA(&recomposed, queryList)` ... `free(recomposed)`, both followed later by `uriFreeQueryListA(queryList)` to free the *list* separately (test.cpp:1909, 1942).

### uriComposeQueryMallocExA
**Claim:** Same as `uriComposeQueryMallocA` but `spaceToPlus`/`normalizeBreaks` explicit; forwards to `ComposeQueryMallocExMm` with `memory=NULL`. Same ownership: caller `free()`s the returned string; `queryList` freed separately via `uriFreeQueryListA`/`uriFreeQueryListMmA`.
**Evidence:**
> src/UriQuery.c:131-136:
> ```
> int URI_FUNC(ComposeQueryMallocEx)(URI_CHAR ** dest, const URI_TYPE(QueryList) * queryList,
>         UriBool spaceToPlus, UriBool normalizeBreaks) {
>     return URI_FUNC(ComposeQueryMallocExMm)(dest, queryList, spaceToPlus, normalizeBreaks, NULL);
> }
> ```
> test/MemoryManagerSuite.cpp:276-278: the ExMm sibling (`uriComposeQueryMallocExMmA`) called with a `FailingMemoryManager`, confirming `URI_ERROR_MALLOC` is the expected failure here too, same contract as ExA with default manager swapped for a custom one.

### uriFreeQueryListA
**Claim:** The destructor for the list built by any Dissect* entry. Forwards to `uriFreeQueryListMmA(queryList, NULL)` (libc default). Idempotency: NOT safe to call twice on the same list (frees node memory without nulling caller's pointer); safe to call on a `NULL` list (loop `while (queryList != NULL)` simply does nothing). Must be called exactly once per successful Dissect* call, after all reads/composes using the list are done.
**Evidence:**
> src/UriQuery.c:366-368:
> ```
> void URI_FUNC(FreeQueryList)(URI_TYPE(QueryList) * queryList) {
>     URI_FUNC(FreeQueryListMm)(queryList, NULL);
> }
> ```
> src/UriQuery.c:370-381 (`FreeQueryListMm`): `while (queryList != NULL) { ...; queryList = nextBackup; }` — tolerant of `NULL` input, not of a dangling/already-freed pointer re-passed in.
> test/test.cpp:1826,1845,1909,1942: exactly one `uriFreeQueryListA`/`uriFreeQueryListW` call per successful dissect, placed after all composes/reads.

### Combination in one harness
**Claim:** A natural single-input harness flow is: (1) derive a `first`/`afterLast` query byte range from fuzz data (either directly, or via `uriParseUriA` + `uri.query`); (2) call target `uriDissectQueryMallocA` (or vary to `*ExA`/`*ExMmA` with fuzzed `plusToSpace`/`breakConversion`/memory manager); (3) on `URI_SUCCESS`, optionally traverse `queryList` fields and/or round-trip via `uriComposeQueryCharsRequiredExA` → allocate/stack buffer → `uriComposeQueryExA`/`uriComposeQueryA`, or `uriComposeQueryMallocA`/`...ExA` (free the returned string with `free()`); (4) always call `uriFreeQueryListA` (or `uriFreeQueryListMmA` with the same manager used to dissect) exactly once on the list if dissect returned `URI_SUCCESS`; if the parsed URI came from `uriParseUriA`, also call `uriFreeUriMembersA(&uri)` for that separate object.
**Evidence:**
> test/test.cpp:1829-1848 (`helperTestQueryString`): exact pattern `uriParseUriA` → `uriDissectQueryMallocA` → asserts → `uriFreeQueryListA` → `uriFreeUriMembersA`.
> test/test.cpp:1922-1943 (`testQueryListPairHelper`): `uriDissectQueryMallocA` → read fields → `uriComposeQueryMallocA` → `free` → `uriFreeQueryListA`.

## Build notes

**Claim:** Fuzzers for this project are C++ (compiled with the CXX linker/LINK_FLAGS even though the library core is C), link against a static `fuzzparser` library built from `${API_HEADER_FILES}` + `${LIBRARY_CODE_FILES}` with include path `include/` (so the header is reached via `#include "uriparser/Uri.h"` / `<uriparser/Uri.h>`), and use `FuzzedDataProvider` from `<fuzzer/FuzzedDataProvider.h>`. The project already ships a dedicated fuzzer specifically for this feature (`fuzz/DissectQueryMallocFuzzer.cpp`, built as `uri_dissect_query_malloc_fuzzer`/`uri_dissect_query_mallocw_fuzzer`), though in this OSS-Fuzz checkout it has been replaced by an empty stub. The OSS-Fuzz `build.sh` builds via CMake with `-DURIPARSER_BUILD_FUZZERS=ON -DURIPARSER_OSSFUZZ_BUILD=ON` and copies `fuzz/*_fuzzer` binaries to `$OUT`.
**Evidence:**
> ${QF_WORK}/ossfuzz_view/uriparser-c1/build.sh:18-30:
> ```
> mkdir -p build && cd build
> cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
>       -DURIPARSER_OSSFUZZ_BUILD=ON \
>       -DURIPARSER_BUILD_FUZZERS=ON \
>       -DURIPARSER_BUILD_DOCS=OFF -DURIPARSER_BUILD_TESTS=OFF \
>       -DURIPARSER_BUILD_TOOLS=OFF -DURIPARSER_ENABLE_INSTALL=OFF ..
> make
> cp fuzz/*_fuzzer "$OUT/"
> ```
> CMakeLists.txt:442-449 (`fuzzparser` static lib): `target_include_directories(fuzzparser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_BINARY_DIR})`, `target_compile_definitions(fuzzparser PUBLIC URI_STATIC_BUILD)`.
> CMakeLists.txt:455-467 (`add_fuzzer`): `target_link_libraries(${target_name} PRIVATE fuzzparser)`; `set_target_properties(${target_name} PROPERTIES LINK_FLAGS $ENV{LIB_FUZZING_ENGINE})`; `LINKER_LANGUAGE "CXX"` under OSS-Fuzz build.
> CMakeLists.txt:479: `add_ansi_and_unicode_fuzzer(uri_dissect_query_malloc fuzz/DissectQueryMallocFuzzer.cpp)` — registers both ANSI (`URI_NO_UNICODE`) and wide-char (`URI_NO_ANSI`) build variants, i.e. `uri_dissect_query_malloc_fuzzer` and `uri_dissect_query_mallocw_fuzzer`.
> fuzz/FuzzingUtils.h:18: `#include "uriparser/Uri.h"` plus `#include <fuzzer/FuzzedDataProvider.h>` — confirms the public installed-style include path (`uriparser/Uri.h`, matching `include/uriparser/Uri.h`) and libFuzzer's data-provider helper usage convention for building `URI_CHAR`/`wchar_t` strings from fuzz bytes (`tryConsumeBytesAsString`, `consumeRandomLengthString`, `consumeRemainingBytesAsString`).
> fuzz/DissectQueryMallocFuzzer.cpp:1 "Gold harness replaced by an empty stub for baseline generation." — the committed project-specific fuzzer for exactly this target has been stubbed out in this checkout; `test/test.cpp` and `test/MemoryManagerSuite.cpp` remain the primary real-usage evidence used throughout this report.