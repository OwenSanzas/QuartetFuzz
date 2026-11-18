## target: mz_zip_reader_init_mem

### P2.1 Init Sequence
**Claim:** The `mz_zip_archive` struct must be zero-initialized (either via `mz_zip_zero_struct()` or `memset(&zip, 0, sizeof(zip))`) before `mz_zip_reader_init_mem` is called. No other library-global init is required. The struct must not already be in use (its mode must be `MZ_ZIP_MODE_INVALID`, i.e. not already initialized by a prior init call without a matching `mz_zip_reader_end`).
**Evidence:**
> miniz_zip.h:237-239: "/* Clears a mz_zip_archive struct to all zeros. */ /* Important: This must be done before passing the struct to any mz_zip functions. */ MINIZ_EXPORT void mz_zip_zero_struct(mz_zip_archive *pZip);"
> miniz_zip.c:489-493 (`mz_zip_reader_init_internal`): `if ((!pZip) || (pZip->m_pState) || (pZip->m_zip_mode != MZ_ZIP_MODE_INVALID)) return mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);` — rejects re-init on a struct that already has state.
> miniz_zip.c:2629 (`mz_zip_validate_mem_archive`): `mz_zip_zero_struct(&zip); if (!mz_zip_reader_init_mem(&zip, pMem, size, flags)) ...` — canonical pattern: zero struct, then init_mem.
> tests/miniz_tester.cpp:933-935: `memset(&zip, 0, sizeof(zip)); if (!mz_zip_reader_init_file(&zip, pZip_filename, 0))` — same pattern for the file variant, confirming zero-then-init protocol.

### P2.2 Parameter Construction
**Claim:** `pZip` is a pointer to a caller-owned, zeroed `mz_zip_archive`. `pMem` must be non-NULL and point to a buffer that remains valid (and, per the header comment, growable via realloc if later reused for writing) for the lifetime of the reader; `size` is the exact length of that buffer; `flags` is a `mz_zip_flags` bitmask (0 is valid/default).
**Evidence:**
> miniz_zip.c:1046-1050: `if (!pMem) return mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER); if (size < MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE) return mz_zip_set_error(pZip, MZ_ZIP_NOT_AN_ARCHIVE);` — pMem checked non-null, size checked against minimum EOCD record size (22 bytes).
> miniz_zip.h:217: declaration `mz_bool mz_zip_reader_init_mem(mz_zip_archive *pZip, const void *pMem, size_t size, mz_uint flags);`
> miniz_zip.h:376: "/* For archives opened using mz_zip_reader_init_mem, the memory block must be growable using the realloc callback ... */" — only relevant if later promoted to writing; for pure reading the buffer only needs to stay alive.
> miniz_zip.h:96-108: flags enum values (`MZ_ZIP_FLAG_CASE_SENSITIVE=0x0100`, `MZ_ZIP_FLAG_IGNORE_PATH=0x0200`, `MZ_ZIP_FLAG_COMPRESSED_DATA=0x0400`, `MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY=0x0800`, `MZ_ZIP_FLAG_VALIDATE_LOCATE_FILE_FLAG=0x1000`, `MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY=0x2000`, write-related flags not applicable here).

### P2.3 Object Lifecycle
**Claim:** Lifecycle is create (zero struct) → init (`mz_zip_reader_init_mem`, which internally allocates `pZip->m_pState`) → use (locate/stat/extract calls) → destroy (`mz_zip_reader_end`). On init failure the function itself tears down any partial state via `mz_zip_reader_end_internal`, so the caller must NOT call `mz_zip_reader_end` again on a failed init (it would hit `!pZip->m_pState` and just return `MZ_FALSE` harmlessly, but no double free occurs since `m_pState` is NULLed first).
**Evidence:**
> miniz_zip.c:1044-1076 (`mz_zip_reader_init_mem` body): on success sets `m_pState->m_pMem`, `m_mem_size`, then calls `mz_zip_reader_read_central_dir`; on its failure: `mz_zip_reader_end_internal(pZip, MZ_FALSE); return MZ_FALSE;` (lines 1069-1073).
> miniz_zip.c:965-1010 (`mz_zip_reader_end_internal`): frees `m_pState` arrays, sets `pZip->m_pState = NULL;` before calling `pZip->m_pFree(...)`, then `pZip->m_zip_mode = MZ_ZIP_MODE_INVALID;` — safe against double-free, and resets mode so the struct can be reused for a fresh init.

### P2.4 Return Value Handling
**Claim:** Returns `mz_bool` (`MZ_TRUE`/`MZ_FALSE`). On `MZ_FALSE`, call `mz_zip_get_last_error(pZip)` / `mz_zip_peek_last_error(pZip)` to learn why; `pZip->m_last_error` is the most fine-grained error code (e.g. `MZ_ZIP_INVALID_PARAMETER`, `MZ_ZIP_NOT_AN_ARCHIVE`, `MZ_ZIP_FAILED_FINDING_CENTRAL_DIR`, `MZ_ZIP_INVALID_HEADER_OR_CORRUPTED`, `MZ_ZIP_UNSUPPORTED_MULTIDISK`, `MZ_ZIP_UNSUPPORTED_ENCRYPTION`, `MZ_ZIP_ALLOC_FAILED`). Caller must not use `pZip` for any reader call afterward unless init returned `MZ_TRUE` (the struct's state has already been cleaned up on failure).
**Evidence:**
> miniz_zip.c:1046-1073: multiple `return mz_zip_set_error(pZip, <code>);` call sites feeding `m_last_error`.
> miniz_zip.c:2631-2636 (`mz_zip_validate_mem_archive`): `if (!mz_zip_reader_init_mem(&zip, pMem, size, flags)) { if (pErr) *pErr = zip.m_last_error; return MZ_FALSE; }` — canonical consumption of the error code on failure, no cleanup call needed since init already cleaned up.
> miniz_zip.h:256-259: `mz_zip_set_last_error`, `mz_zip_peek_last_error`, `mz_zip_clear_last_error`, `mz_zip_get_last_error` accessors.

### P2.5 Cleanup Sequence
**Claim:** On success, caller must eventually call `mz_zip_reader_end(pZip)` exactly once to free `pZip->m_pState` (central-dir arrays + sorted-offset array) — this does NOT free the user's `pMem` buffer (the reader never takes ownership of it). On failure of `mz_zip_reader_init_mem` itself, no cleanup call is needed (already done internally). Any heap buffers obtained from extraction APIs (`mz_zip_reader_extract_to_heap`, `mz_zip_reader_extract_file_to_heap`) must be freed separately with `mz_free`/`pZip->m_pFree`, independent of `mz_zip_reader_end`.
**Evidence:**
> miniz_zip.h:232: "/* Ends archive reading, freeing all allocations, and closing the input archive file if mz_zip_reader_init_file() was used. */ MINIZ_EXPORT mz_bool mz_zip_reader_end(mz_zip_archive *pZip);"
> miniz_zip.c:1036-1042 (`mz_zip_mem_read_func`): reads straight from `pZip->m_pState->m_pMem` (the caller's buffer) with no copy/ownership transfer — confirms `pMem` is never freed by the library.
> examples/example2.c:146-147,155,159: `mz_free(p); mz_zip_reader_end(&zip_archive);` — heap-extracted buffer freed separately from archive end.
> miniz_zip.c:965-1010: cleanup order inside `mz_zip_reader_end_internal` — clear central_dir arrays, clear central_dir_offsets, clear sorted_central_dir_offsets, close FILE* if type is MZ_ZIP_TYPE_FILE, then free `m_pState` itself.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited locations.
**Evidence:**
> miniz_zip.c:1044 `mz_zip_reader_init_mem`; :1016 `mz_zip_reader_init`; :1097 `mz_zip_reader_init_file_v2`; :1092 `mz_zip_reader_init_file`; :1012 `mz_zip_reader_end`; :965 `mz_zip_reader_end_internal`; :489 `mz_zip_reader_init_internal`; :663 `mz_zip_reader_read_central_dir`; :1479 `mz_zip_reader_locate_file_v2`; :1470 `mz_zip_reader_locate_file`; :4871 `mz_zip_reader_file_stat`; :1704 `mz_zip_reader_extract_to_mem`; :1691 `mz_zip_reader_extract_to_mem_no_alloc`; :2616 `mz_zip_validate_mem_archive`; :2566 `mz_zip_validate_archive`.
> miniz_zip.h:215,217,223,233,279,282,298,347 — matching public declarations for the above.

### P2.7 Co-call Constraints
**Claim:** `mz_zip_reader_init_mem` / `mz_zip_reader_init` / `mz_zip_reader_init_file*` are mutually-exclusive "open" calls — exactly one must be paired with exactly one `mz_zip_reader_end` per `mz_zip_archive` instance (open/close pairing). `mz_zip_reader_locate_file_v2` has no separate close (its result, a file index, is just consumed by stat/extract calls, which themselves need no pairing beyond the archive's open/close). `mz_zip_validate_mem_archive` internally pairs its own `mz_zip_reader_init_mem` with `mz_zip_reader_end_internal` (equivalent to `mz_zip_reader_end`), so it is self-contained and does not require caller-side cleanup.
**Evidence:**
> miniz_zip.c:492: `pZip->m_zip_mode != MZ_ZIP_MODE_INVALID` guard in `mz_zip_reader_init_internal` — enforces that a second init call without an intervening `end` fails, i.e. open/close must alternate.
> miniz_zip.c:2631,2644 (`mz_zip_validate_mem_archive`): `mz_zip_reader_init_mem(&zip, ...)` ... `mz_zip_reader_end_internal(&zip, success)` — paired within the same function.
> miniz_zip.h:232-233: doc comment pairs "Ends archive reading" with the various init functions above it.

### P2.8 Prerequisite State
**Claim:** No external context, config, or connection is required beyond the zeroed `mz_zip_archive` struct and a valid memory buffer. Allocation callbacks (`m_pAlloc`/`m_pFree`/`m_pRealloc`) are optional — if left NULL (as after zeroing), `mz_zip_reader_init_internal` installs miniz's default malloc/free/realloc wrappers.
**Evidence:**
> miniz_zip.c:495-500: `if (!pZip->m_pAlloc) pZip->m_pAlloc = miniz_def_alloc_func; if (!pZip->m_pFree) pZip->m_pFree = miniz_def_free_func; if (!pZip->m_pRealloc) pZip->m_pRealloc = miniz_def_realloc_func;`
> miniz_zip.c:1057-1059 (`mz_zip_reader_init_mem`): `pZip->m_pRead = mz_zip_mem_read_func; pZip->m_pIO_opaque = pZip; pZip->m_pNeeds_keepalive = NULL;` — init_mem installs its own read callback itself, caller does not need to set `m_pRead` beforehand (unlike `mz_zip_reader_init`, which requires caller-supplied `m_pRead`, see P2.1-adjacent note below).
> miniz_zip.c:1018-1019 (`mz_zip_reader_init`): `if ((!pZip) || (!pZip->m_pRead)) return mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);` — contrast: the generic `mz_zip_reader_init` *does* require the caller to pre-set `m_pRead`/`m_pIO_opaque` before calling it, whereas `mz_zip_reader_init_mem` sets these up internally.

### P2.9 Result Usage
**Claim:** After a successful `mz_zip_reader_init_mem`, the populated `pZip` (specifically `pZip->m_pState`'s central-directory tables and `pZip->m_total_files`) is consumed by: `mz_zip_reader_get_num_files` (iteration bound), `mz_zip_reader_file_stat` (per-index metadata), `mz_zip_reader_locate_file`/`_v2` (name→index lookup, using the sorted-offsets table built by the core unless `MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY` was set), and the extraction family `mz_zip_reader_extract_to_mem[_no_alloc]`, `mz_zip_reader_extract_to_heap`, `mz_zip_reader_extract_to_callback`, which read the local header + compressed data from the same buffer and run it through `tinfl_decompress`. `mz_zip_validate_mem_archive` combines file_stat + locate_file_v2 + a full validate/decompress pass (`mz_zip_validate_archive`→`mz_zip_validate_file`) over every entry.
**Evidence:**
> examples/example2.c:91-101: `for (i = 0; i < (int)mz_zip_reader_get_num_files(&zip_archive); i++) { mz_zip_archive_file_stat file_stat; if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) ... }` — iterate indices from init'd archive, stat each.
> examples/example2.c:134: `p = mz_zip_reader_extract_file_to_heap(&zip_archive, archive_filename, &uncomp_size, 0);` — name-based extraction after init.
> tests/miniz_tester.cpp:941-958: `mz_zip_reader_locate_file(&zip, "test.bin", "test Comment", 0)` then `mz_zip_reader_extract_to_file(&zip, file_index, pDst_filename, 0)` — locate then extract by index.
> miniz_zip.c:1714-1747 (`mz_zip_reader_extract_to_heap`): calls `mz_zip_reader_file_stat` first to size the allocation, then `mz_zip_reader_extract_to_mem_no_alloc1`.
> miniz_zip.c:2591-2610 (`mz_zip_validate_archive`): loops `i < pZip->m_total_files`, calling `mz_zip_reader_file_stat`, `mz_zip_reader_locate_file_v2` (if `MZ_ZIP_FLAG_VALIDATE_LOCATE_FILE_FLAG`), and `mz_zip_validate_file` for every entry built by the target's central-dir parse.

### P2.10 Behaviour Controls
**Claim:** `flags` passed to `mz_zip_reader_init_mem` controls whether the central directory is sorted (`MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY`, stored into `pZip->m_pState->m_init_flags` and checked later by `mz_zip_reader_locate_file_v2` to decide binary-search vs linear scan). `flags` passed independently to `mz_zip_reader_locate_file_v2` control `MZ_ZIP_FLAG_CASE_SENSITIVE`/`MZ_ZIP_FLAG_IGNORE_PATH` matching and force a linear scan if set. `flags` passed to extraction functions control `MZ_ZIP_FLAG_COMPRESSED_DATA` (return raw compressed bytes instead of inflating). `flags` passed to `mz_zip_validate_mem_archive`/`mz_zip_validate_archive` add `MZ_ZIP_FLAG_VALIDATE_LOCATE_FILE_FLAG` (cross-check locate) and `MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY` (skip full decompress+CRC). The ZIP byte stream itself also branches: presence of a zip64 EOCD locator sets `pZip->m_pState->m_zip64`, and each central-directory entry's compression method (STORED vs DEFLATED) selects raw-copy vs `tinfl_decompress` during extraction.
**Evidence:**
> miniz_zip.c:672: `mz_bool sort_central_dir = ((flags & MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY) == 0);` inside `mz_zip_reader_read_central_dir`.
> miniz_zip.c:514: `pZip->m_pState->m_init_flags = flags;` (stored during init for later reference).
> miniz_zip.c:1491-1496 (`mz_zip_reader_locate_file_v2`): `if (((pZip->m_pState->m_init_flags & MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY) == 0) && ... && ((flags & (MZ_ZIP_FLAG_IGNORE_PATH | MZ_ZIP_FLAG_CASE_SENSITIVE)) == 0) ...) return mz_zip_locate_file_binary_search(...);` else linear scan.
> miniz_zip.c:700-703,707-721: zip64 locator signature check sets `pZip->m_pState->m_zip64 = MZ_TRUE;`, driving the EOCD64 parsing branch.
> miniz_zip.c:2593,2609: `if (MZ_ZIP_FLAG_VALIDATE_LOCATE_FILE_FLAG & flags) {...}` and `mz_zip_validate_file(pZip, i, flags)` (which itself checks `MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY`, per header comment at miniz_zip.h:340).
> miniz_zip.h:389: "/* uncomp_size/uncomp_crc32 are only used if the MZ_ZIP_FLAG_COMPRESSED_DATA flag is specified. */" (extract_to_mem family).

### P2.11 Input Expectations
**Claim:** `size` must be ≥ `MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE` (22 bytes). Beyond that, the buffer must contain somewhere a valid End-Of-Central-Directory record (signature `MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIG`), located by scanning backward (`mz_zip_reader_locate_header_sig`); optionally a Zip64 EOCD locator (right before the EOCD) and Zip64 EOCD record are parsed if the locator signature is found. Every central-directory header must fit within the declared `n` bytes of central-directory data (filename+extra+comment length fields are bounds-checked against remaining size), local-header offset + local-header-size + comp_size must not exceed `m_archive_size`, multi-disk archives (disk index ≠ 0/1) are rejected, and masked/encrypted local headers (`MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_LOCAL_DIR_IS_MASKED`) are rejected as unsupported encryption. There is no requirement for a specific terminator byte beyond the EOCD record itself (comment field length can be 0).
**Evidence:**
> miniz_zip.c:1049-1050: `if (size < MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE) return mz_zip_set_error(pZip, MZ_ZIP_NOT_AN_ARCHIVE);`
> miniz_zip.c:682-694: `if (pZip->m_archive_size < MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE) ...`; `mz_zip_reader_locate_header_sig(...)`; `if (MZ_READ_LE32(pBuf + MZ_ZIP_ECDH_SIG_OFS) != MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIG) return mz_zip_set_error(pZip, MZ_ZIP_NOT_AN_ARCHIVE);`
> miniz_zip.c:696-721: Zip64 locator detection and EOCD64 validation (`mz_zip_reader_eocd64_valid`).
> miniz_zip.c:931-946: disk-index check (`MZ_ZIP_UNSUPPORTED_MULTIDISK`), local-header/comp_size bound vs `pZip->m_archive_size` (`MZ_ZIP_INVALID_HEADER_OR_CORRUPTED`), masked bit-flag check (`MZ_ZIP_UNSUPPORTED_ENCRYPTION`), and `total_header_size > n` bound check (`MZ_ZIP_INVALID_HEADER_OR_CORRUPTED`).

## Other Entries: call protocol and combination with the target

### mz_zip_reader_init (variant, not the target)
**Claim:** Alternative opener driven by a user-supplied read callback instead of a raw buffer. Requires `pZip->m_pRead` (and typically `m_pIO_opaque`) to be set by the caller BEFORE calling it — unlike `mz_zip_reader_init_mem`, which installs its own callback. `size` here is the logical archive size in bytes (mz_uint64), `flags` same meaning as target. Returns `mz_bool`; on failure the internal state is likewise cleaned up by `mz_zip_reader_end_internal`. Cleanup: `mz_zip_reader_end`. Not generally useful in a memory-buffer fuzz harness unless the harness supplies its own read callback over the fuzz buffer; the target (`init_mem`) is the natural choice for buffer-based fuzzing.
**Evidence:** miniz_zip.c:1016-1034; miniz_zip.h:215.

### mz_zip_reader_init_file / mz_zip_reader_init_file_v2 (alternative source, same core)
**Claim:** Opens from a path on disk (`MZ_FOPEN`), not from memory — not suited for a pure in-memory fuzz harness unless the harness first writes the fuzz data to a temp file. Internally ends up calling the same `mz_zip_reader_read_central_dir` core. Requires `pFilename` be openable; computes `file_size` via `fseek`/`ftell` if `archive_size` is 0. Cleanup: `mz_zip_reader_end` (which additionally closes the FILE* if `m_zip_type == MZ_ZIP_TYPE_FILE`).
**Evidence:** miniz_zip.c:1092-1097 (`init_file` wraps `init_file_v2`); miniz_zip.c:1097-1124 (`init_file_v2` opens/seeks/validates size then proceeds to central-dir read, not shown further here); miniz_zip.h:220-224 doc comments; miniz_zip.c:990-1003 (FILE close in `mz_zip_reader_end_internal`).

### mz_zip_reader_locate_file_v2 (consumer)
**Claim:** Must be called AFTER a successful init (`pZip->m_pState` must be non-NULL, checked at entry). Parameters: `pName` (required, else `MZ_ZIP_INVALID_PARAMETER`), `pComment` (optional — forces full linear scan path, bypassing the binary search, if non-NULL), `flags` (`MZ_ZIP_FLAG_CASE_SENSITIVE`, `MZ_ZIP_FLAG_IGNORE_PATH` select linear-scan comparison semantics and also force linear scan instead of binary search), `pIndex` output. Returns `mz_bool`; `*pIndex` is set to 0 on failure path entry and to the found index on success. Result (`file_index`) is fed into `mz_zip_reader_file_stat`, `mz_zip_reader_extract_to_mem`, `mz_zip_reader_extract_to_mem_no_alloc`, `mz_zip_reader_extract_to_heap`, `mz_zip_reader_extract_to_callback`, etc. No separate cleanup — index is just an integer into tables owned by `pZip->m_pState`, freed by `mz_zip_reader_end`.
**Evidence:** miniz_zip.c:1484-1496 (NULL checks and binary-search fast path condition); miniz_zip.h:276 "/* Valid flags: MZ_ZIP_FLAG_CASE_SENSITIVE, MZ_ZIP_FLAG_IGNORE_PATH */"; miniz_zip.c:1699,1753,2601 (callers feeding `file_index` to extraction/validation).

### mz_zip_reader_file_stat (consumer)
**Claim:** Must be called after init, with a `file_index` in `[0, mz_zip_reader_get_num_files(pZip))` (commonly obtained by iterating or from `locate_file_v2`). Fills a caller-allocated `mz_zip_archive_file_stat` (stack struct is fine, no separate free needed — it's POD with fixed-size filename/comment buffers, per header). Returns `mz_bool`. No flags parameter. Decodes zip64 extra fields from the same archive buffer for that one entry.
**Evidence:** miniz_zip.c:4871-4874 (`mz_zip_reader_file_stat` → `mz_zip_file_stat_internal(pZip, file_index, mz_zip_get_cdh(pZip, file_index), pStat, NULL)`); miniz_zip.h:282; examples/example2.c:94-101 (typical call after init, stack-allocated `mz_zip_archive_file_stat file_stat;`, no explicit free).

### mz_zip_reader_extract_to_mem / mz_zip_reader_extract_to_mem_no_alloc (consumer / decompression core entry)
**Claim:** Must be called after init and (usually) after obtaining `file_index` via locate or iteration. `pBuf`/`buf_size` is a caller-allocated output buffer sized to at least the uncompressed size reported by `mz_zip_reader_file_stat` (or `m_comp_size` if `MZ_ZIP_FLAG_COMPRESSED_DATA` is set). Internally dispatches to `mz_zip_reader_extract_to_mem_no_alloc1` (core), which reads the local header from the same buffer and runs `tinfl_decompress` for DEFLATE entries or memcpy for STORED entries. Returns `mz_bool`. Caller owns `pBuf` (stack/heap, caller's own alloc/free); no return value needs freeing by this function itself.
**Evidence:** miniz_zip.c:1704-1707 (`mz_zip_reader_extract_to_mem` → `mz_zip_reader_extract_to_mem_no_alloc1(..., NULL, 0, NULL)`); miniz_zip.c:1691-1694 (`_no_alloc` → same core, letting caller supply an optional user read-buffer); miniz_zip.h:298.

### mz_zip_reader_end (lifecycle counterpart)
**Claim:** Must be called exactly once per successful init to free `pZip->m_pState` (central-dir arrays, sorted-offset array, and close FILE* for file-based opens). Safe/no-op-returning-FALSE if called on an un-initialized or already-ended struct (checked via `!pZip->m_pState` etc.). Required before re-initializing the same `mz_zip_archive` struct for another iteration (multi-iteration fuzzing), because `mz_zip_reader_init_internal` rejects re-init while `m_pState` or `m_zip_mode != MZ_ZIP_MODE_INVALID`.
**Evidence:** miniz_zip.c:1012-1015 (`mz_zip_reader_end` → `mz_zip_reader_end_internal(pZip, MZ_TRUE)`); miniz_zip.c:969-978 (guard: `if ((!pZip->m_pState) || (!pZip->m_pAlloc) || (!pZip->m_pFree) || (pZip->m_zip_mode != MZ_ZIP_MODE_READING)) { ...; return MZ_FALSE; }`); miniz_zip.c:492 (re-init guard in `mz_zip_reader_init_internal`).

### mz_zip_validate_mem_archive (higher-level consumer/wrapper)
**Claim:** Self-contained: takes raw `pMem`/`size`/`flags`/`pErr` directly (no pre-existing `mz_zip_archive` needed from the caller — it creates and zeroes its own local one), internally calls `mz_zip_reader_init_mem` then `mz_zip_validate_archive` then `mz_zip_reader_end_internal`. If used in the same harness as a direct call to the target, it is an ALTERNATIVE top-level entry point that itself performs init/use/cleanup — do not also separately call `mz_zip_reader_init_mem`/`mz_zip_reader_end` on the same buffer through this wrapper (it manages its own `zip` struct internally, not the caller's). It returns `mz_bool`, and reports fine-grained error via `*pErr` (optional, caller may pass NULL).
**Evidence:** miniz_zip.c:2616-2651 (full body: `mz_zip_zero_struct`, `mz_zip_reader_init_mem`, `mz_zip_validate_archive`, `mz_zip_reader_end_internal`); miniz_zip.h:347; tests/miniz_zip_validate_fuzzer.cc:19-24,28 (real harness calling `mz_zip_validate_mem_archive(data, size, flags, &zip_error)` directly on fuzz input, no manual init/end needed by the caller).

### Combination in one harness
**Claim:** A typical harness: (1) `mz_zip_zero_struct(&zip)` (or memset), (2) `mz_zip_reader_init_mem(&zip, data, size, flags)` — if it returns `MZ_FALSE`, stop (no `mz_zip_reader_end` needed, already cleaned internally); (3) on success, iterate `i` in `[0, mz_zip_reader_get_num_files(&zip))`, call `mz_zip_reader_file_stat(&zip, i, &stat)`, optionally `mz_zip_reader_locate_file_v2(&zip, stat.m_filename, ...)` to exercise the lookup path, and `mz_zip_reader_extract_to_mem`/`extract_to_heap` to exercise `tinfl_decompress`; (4) always call `mz_zip_reader_end(&zip)` exactly once at the end on the success path. Separately/optionally, `mz_zip_validate_mem_archive(data, size, flags, &err)` can be called as an independent top-level alternative that exercises the same core without any manual init/end bookkeeping by the harness.
**Evidence:** examples/example2.c:80-115 (full init→iterate-stat→end sequence, file-based variant but same protocol); miniz_zip.c:2616-2651 (`mz_zip_validate_mem_archive` as self-contained alternative); tests/miniz_zip_validate_fuzzer.cc:13-34 (existing OSS-Fuzz harness using the validate wrapper directly on fuzz data, confirming it's a valid standalone harness pattern though it does not call `mz_zip_reader_init_mem` from harness code directly).

## Build notes
**Claim:** The fuzzer is built as a C or C++ source file compiled against the amalgamated single-header/single-source build of miniz (`miniz.h`/`miniz.c` which `#include`s `miniz_zip.h`/`miniz_zip.c`), with `-Ibuild/amalgamation` as the include path, linked against the static lib `libminiz.a` plus `$LIB_FUZZING_ENGINE`. `MINIZ_EXPORT` is forced to empty by a hand-written `miniz_export.h` placed ahead of the real one.
**Evidence:**
> miniz/tests/ossfuzz.sh:21-30: generates `miniz_export.h` defining `MINIZ_EXPORT` to empty, then `cmake .. -DAMALGAMATE_SOURCES=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_FUZZERS=ON -DBUILD_TESTS=OFF; make -j$(nproc)`.
> miniz/tests/ossfuzz.sh:35-41: `for f in $(find $SRC -name '*_fuzzer.c'); do b=$(basename -s .c $f); $CC $CFLAGS -Ibuild/amalgamation $f -c -o /tmp/$b.o; $CXX $CXXFLAGS -stdlib=libc++ -Ibuild/amalgamation /tmp/$b.o -o $OUT/$b $LIB_FUZZING_ENGINE ./build/libminiz.a; done` — C fuzzer sources are compiled with `$CC` (project.yaml declares `language: c`) against the amalgamated headers in `build/amalgamation`, then linked with `$CXX` against `libminiz.a` and the fuzzing engine.
> ossfuzz_view/miniz-c1/project.yaml:2: `language: c`.
> ossfuzz_view/miniz-c1/build.sh:19: `$SRC/miniz/tests/ossfuzz.sh` — the project's own build.sh simply delegates to the above script.
> miniz/tests/miniz_zip_validate_fuzzer.cc:6: `#include "miniz.h"` — existing C++ fuzzer includes the umbrella header which pulls in `miniz_zip.h`'s declarations (including `mz_zip_reader_init_mem`, `mz_zip_validate_mem_archive`).
> miniz/CMakeLists.txt (via ossfuzz.sh invocation `-DBUILD_FUZZERS=ON`) builds `libminiz.a` as the static archive referenced in the link step above.