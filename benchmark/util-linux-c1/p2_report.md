## target: fdisk_script_read_file

### P2.1 Init Sequence
**Claim:** A `struct fdisk_script *dp` must already exist (obtained via `fdisk_new_script(cxt)`, where `cxt` may be `NULL` or a valid `struct fdisk_context *`) and the `FILE *f` must already be open for reading, before calling `fdisk_script_read_file(dp, f)`. No other library-wide init call is required. Internally the target calls `fdisk_script_read_line()` in a loop, which calls `fdisk_script_read_buffer()`; the first call to `fdisk_script_read_buffer()` lazily creates the backing `fdisk_table` via `fdisk_script_get_table()` if it does not already exist, so a table does not need to be pre-created.
**Evidence:**
> libfdisk/src/script.c:1509-1523: `int fdisk_script_read_file(struct fdisk_script *dp, FILE *f) { ... assert(dp); assert(f); ... while (!feof(f)) { rc = fdisk_script_read_line(dp, f, buf, sizeof(buf)); ... } }`
> libfdisk/src/script.c:1403-1404 (`fdisk_script_read_buffer`): `if (!dp->table && fdisk_script_get_table(dp) == NULL) return -ENOMEM;`
> libfdisk/src/script.c:1740-1757 (`test_read`, TEST_PROGRAM): `cxt = fdisk_new_context(); dp = fdisk_new_script(cxt); fdisk_script_read_file(dp, f); fclose(f); fdisk_script_write_file(dp, stdout); fdisk_unref_script(dp); fdisk_unref_context(cxt);` — canonical real-caller order: new_context → new_script → read_file → write_file → unref_script → unref_context.
> libfdisk/src/script.c:133-148 (`fdisk_new_script`): accepts `cxt` that can be any pointer; `fdisk_ref_context(cxt)` is a no-op safe-guard if `cxt==NULL` (libfdisk/src/context.c:204-208: `if (cxt) cxt->refcount++;`), so a context-free script is legal for the plain-parse path, but some parsing paths dereference `dp->cxt` directly without a NULL check (see P2.8).

### P2.2 Parameter Construction
**Claim:** `dp` must be a non-NULL, previously-allocated `struct fdisk_script*` (from `fdisk_new_script()` or `fdisk_new_script_from_file()`); `f` must be a non-NULL `FILE*` opened in a readable mode (text content, may or may not end in `\n`). Both are `assert()`ed non-NULL inside the target; passing NULL is undefined behavior (abort in debug builds, crash otherwise).
**Evidence:**
> libfdisk/src/script.c:1514-1515: `assert(dp); assert(f);`
> libfdisk/src/script.c:1456-1462 (`fdisk_script_read_line`): `assert(dp); assert(f); assert(bufsz);` — reused by the target in a loop.
> libfdisk/libfdisk.h:844: declaration `int fdisk_script_read_file(struct fdisk_script *dp, FILE *f);` (no extra flags/length parameters — line length is fixed internally to `BUFSIZ`).

### P2.3 Object Lifecycle
**Claim:** Create (`fdisk_new_script`) → optionally configure (`fdisk_script_set_fgets`, `fdisk_script_set_userdata`, `fdisk_script_set_header`/`fdisk_script_set_table` to pre-seed) → use (`fdisk_script_read_file`, possibly multiple times to append more lines/table entries) → use results (`fdisk_script_get_table`, `fdisk_script_write_file`, `fdisk_apply_script`) → destroy (`fdisk_unref_script`). The script object is refcounted, not a plain malloc/free pair.
**Evidence:**
> libfdisk/src/script.c:133-148: `fdisk_new_script()` → `calloc`, `dp->refcount = 1`.
> libfdisk/src/script.c:201-205: `fdisk_ref_script()` increments refcount.
> libfdisk/src/script.c:231-244: `fdisk_unref_script()` decrements; at `refcount<=0` calls `fdisk_reset_script(dp)`, `fdisk_unref_context(dp->cxt)`, `fdisk_unref_table(dp->table)`, then `free(dp)`.
> libfdisk/src/script.c:1870-1877 (sfdisk.c `command_fdisk`): `dp = fdisk_new_script(sf->cxt); fdisk_set_script(sf->cxt, dp); fdisk_script_set_fgets(dp, sfdisk_fgets); fdisk_script_set_userdata(dp, (void*) sf);` — configuration happens right after creation, before any read.

### P2.4 Return Value Handling
**Claim:** Target returns `0` on success (including an empty/EOF-only file), and a negative `errno`-style value (`-EINVAL`, `-ENOMEM`, `-ENOTSUP` is specially filtered out and NOT propagated as fatal) on error. Callers must check `rc != 0` and typically convert via `errno = -rc`.
**Evidence:**
> libfdisk/src/script.c:1519-1529:
> ```
> while (!feof(f)) {
>     rc = fdisk_script_read_line(dp, f, buf, sizeof(buf));
>     if (rc && rc != -ENOTSUP)
>         break;
> }
> if (rc == 1)
>     rc = 0;   /* end of file */
> ```
> libfdisk/src/script.c:1453-1455 (doc comment on `fdisk_script_read_line`): "Returns: 0 on success, <0 on error, 1 when nothing to read. For unknown headers returns -ENOTSUP, it's usually safe to ignore this error."
> libfdisk/src/script.c:178-182 (`fdisk_new_script_from_file`): `rc = fdisk_script_read_file(dp, f); if (rc) { errno = -rc; goto done; }` — real caller pattern for error propagation.

### P2.5 Cleanup Sequence
**Claim:** On both success and error paths the single `struct fdisk_script*` must eventually be released with `fdisk_unref_script(dp)` (not `free()`), which internally frees the header list and unrefs the internal table and context. The `FILE *f` passed to the target is **not** closed by the target; the caller owns it and must `fclose()` it itself.
**Evidence:**
> libfdisk/src/script.c:231-244 (`fdisk_unref_script`): frees headers via `fdisk_reset_script`, calls `fdisk_unref_context(dp->cxt)`, `fdisk_unref_table(dp->table)`, `free(dp)`.
> libfdisk/src/script.c:185-188 (`fdisk_new_script_from_file`): `fclose(f); if (!res) fdisk_unref_script(dp); else errno = 0;` — note `fclose` is done by this wrapper itself, not by the target; a harness calling `fdisk_script_read_file` directly must `fclose()` its own FILE*.
> libfdisk/src/script.c:1757-1762 (TEST_PROGRAM `test_read`): `fdisk_script_read_file(dp, f); fclose(f); ... fdisk_unref_script(dp); fdisk_unref_context(cxt);` — fclose happens right after read, unref_script/unref_context at the end.

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited locations.
**Evidence:**
> fdisk_script_read_file: libfdisk/src/script.c:1509; declared libfdisk/libfdisk.h:844
> fdisk_new_script: libfdisk/src/script.c:133
> fdisk_new_script_from_file: libfdisk/src/script.c:159
> fdisk_script_read_line: libfdisk/src/script.c:1456
> fdisk_script_write_file: libfdisk/src/script.c:799
> fdisk_apply_script: libfdisk/src/script.c:1674
> fdisk_apply_script_headers: libfdisk/src/script.c:1594
> fdisk_script_set_header: libfdisk/src/script.c:325
> fdisk_script_get_table: libfdisk/src/script.c:393
> fdisk_script_set_table: libfdisk/src/script.c:430
> fdisk_unref_script / fdisk_ref_script: libfdisk/src/script.c:231 / 201
> fdisk_script_set_fgets: libfdisk/src/script.c:1435
> fdisk_script_set_userdata / fdisk_script_get_userdata: libfdisk/src/script.c:255 / 268
> fdisk_apply_table (table.c): libfdisk/src/table.c:707
> fdisk_new_context / fdisk_unref_context: libfdisk/src/context.c:49 / :1130
> (core, internal, not public API) fdisk_script_read_buffer: libfdisk/src/script.c:1390; parse_line_header: :820; parse_line_nameval: :1181; parse_line_valcommas: :1300; parse_start_value: :1057; parse_size_value: :1114; next_token: :916

### P2.7 Co-call Constraints
**Claim:** `fdisk_new_script`/`fdisk_new_script_from_file` pairs with `fdisk_unref_script` (ref/unref, not create/destroy — refcounted). `fdisk_apply_script` internally pairs `fdisk_ref_script(old)`/`fdisk_unref_script(old)` around swapping the context's active script via `fdisk_set_script`. `fdisk_script_read_file` has no required "close" call of its own, but the `FILE*` it is given must be opened by the caller and later `fclose()`d by the caller (the target never closes it). `fdisk_new_context`/`fdisk_unref_context` similarly pair for the optional `struct fdisk_context*` passed into `fdisk_new_script`.
**Evidence:**
> libfdisk/src/script.c:1684-1695 (`fdisk_apply_script`): `old = fdisk_get_script(cxt); fdisk_ref_script(old); ... fdisk_set_script(cxt, old); fdisk_unref_script(old);` — ref/unref pair around the headers+table apply sequence.
> libfdisk/src/script.c:1509-1530: target never calls `fclose`; see `fdisk_new_script_from_file` (lines 170-188) which explicitly owns and closes its own `FILE*` after calling the target, confirming the target itself does not.
> libfdisk/src/context.c:204-208 and :1130 (fdisk_ref_context/fdisk_unref_context definitions) — paired inside fdisk_new_script (ref at :144) / fdisk_unref_script (unref at :239).

### P2.8 Prerequisite State
**Claim:** `dp->cxt` (the `struct fdisk_context*` passed to `fdisk_new_script`) may be `NULL` for plain parsing of start=/size=/bootable/attrs=/uuid=/name= fields and the legacy comma format's start/size/bootable fields, since those call `recount_script2device_sectors()` and `dp->cxt->sector_size` is guarded (`!dp->cxt || ...`, :1038-1041) in the "no suffix" numeric path — BUT if a size/start token carries a unit suffix (e.g. "10MiB") the code unconditionally dereferences `dp->cxt->sector_size` with **no NULL check on `dp->cxt` itself**, and any `type=`/`Id=` field or legacy-format type field calls `script_get_label(dp)` which does `assert(dp->cxt)` — both are crash/assert-failure risks if `dp->cxt` is NULL. Also header line "label" validation (`fdisk_get_label(dp->cxt,...)`) is itself guarded by `if (dp->cxt && ...)` so is safe with NULL context, but type parsing is not.
**Evidence:**
> libfdisk/src/script.c:1082-1087 (`parse_start_value`): `if (pow) { if (!dp->cxt->sector_size) { rc=-EINVAL; goto done; } num /= dp->cxt->sector_size; }` — dereferences `dp->cxt` with no prior null check in this branch (pow!=0, i.e., a parsed suffix like "K"/"M"/"G").
> libfdisk/src/script.c:1138-1143 (`parse_size_value`): same pattern, `dp->cxt->sector_size`.
> libfdisk/src/script.c:443-446 (`script_get_label`): `assert(dp); assert(dp->cxt);` — called from parse_line_nameval (:1275) and parse_line_valcommas (:1350) whenever a `type=`/`Id=` field or 3rd comma-field type is present.
> libfdisk/src/script.c:858-860 (`parse_line_header`, "label" case): `if (dp->cxt && !fdisk_get_label(dp->cxt, value)) return -EINVAL;` — this one IS guarded.
> Conclusion for the harness: to exercise the full input grammar safely (including type=/Id= fields and unit-suffixed sizes) a non-NULL `struct fdisk_context*` from `fdisk_new_context()` should be passed into `fdisk_new_script()`; a NULL context restricts safe fuzzing to header lines without "label" producing a label object, and to start=/size=/bootable without suffixed units, and no type fields.

### P2.9 Result Usage
**Claim:** After `fdisk_script_read_file` returns, real callers use the filled `dp` in three ways: (1) serialize it back out via `fdisk_script_write_file(dp, FILE*)`; (2) hand it to `fdisk_apply_script(cxt, dp)` to actually build the in-memory partition table/label in a `fdisk_context`; (3) read/modify its internal table directly via `fdisk_script_get_table(dp)` and the `fdisk_table_...` API.
**Evidence:**
> libfdisk/src/script.c:1757-1762 (test_read): `fdisk_script_read_file(dp, f); fclose(f); fdisk_script_write_file(dp, stdout); fdisk_unref_script(dp); fdisk_unref_context(cxt);`
> libfdisk/src/script.c:1674-1699 (`fdisk_apply_script`): `rc = fdisk_apply_script_headers(cxt, dp); if (!rc && dp->table) rc = fdisk_apply_table(cxt, dp->table);` — consumes `dp`'s headers list and `dp->table` (filled by the target) to create a disklabel (`fdisk_create_disklabel`, inside `fdisk_apply_script_headers` :1648) and add partitions (`fdisk_add_partition`, inside `fdisk_apply_table`, libfdisk/src/table.c:722).
> libfdisk/src/script.c:1974 (disk-utils/sfdisk.c): `tb = fdisk_script_get_table(dp);` — sfdisk reads the parsed table out of the script object to iterate/modify partitions directly.
> libfdisk/src/table.c:707-728 (`fdisk_apply_table`): `while (tb && fdisk_table_next_partition(tb, &itr, &pa) == 0) { if (!fdisk_partition_has_start(pa) && !pa->start_follow_default) continue; rc = fdisk_add_partition(cxt, pa, NULL); ... }` — directly consumes `fdisk_partition` objects created by `parse_line_nameval`/`parse_line_valcommas` during the target's execution.

### P2.10 Behaviour Controls
**Claim:** Per-line grammar selection inside the core happens in `fdisk_script_read_buffer()` based on simple syntactic sniffing (no explicit flag parameter on the target itself): if the table is still empty and the line matches `is_header_line()` (has a `:' not followed by '=' and non-empty value) it is parsed as a header line; else if the line contains `=` it is a name=value partition line; else it falls back to the legacy comma format. Header names are restricted to a fixed table (`"label","unit","label-id","device","grain","first-lba","last-lba","table-length","sector-size"`), each with its own validation (e.g. "sector-size" must be a multiple of 512 and ≤ULONG_MAX, "unit" must equal "sectors", "label" must name a label the `fdisk_context` knows about if `dp->cxt` is set). `parse_line_nameval` recognizes a fixed field set (`start=`,`size=`,`bootable`,`attrs=`,`uuid=`,`name=`,`type=`/`Id=`) plus an optional leading `"<devname|partno> :"` / `" : "` / `": "` prefix consumed by `partno_from_devname()`. `parse_line_valcommas` is strictly positional: `start,size,type,bootable,...`. `fdisk_script_set_fgets()` lets a harness override the physical line-reading function (default `fgets`), changing how line boundaries/EOF are detected without altering the per-line grammar. `fdisk_apply_script_headers`/`fdisk_apply_table` further gate behavior on the parsed headers (grain, sector-size mismatch handling, table-length → `fdisk_gpt_set_npartitions`).
**Evidence:**
> libfdisk/src/script.c:1403-1416 (`fdisk_script_read_buffer`): the three-way dispatch `is_header_line(s)` / `strchr(s,'=')` / else.
> libfdisk/src/script.c:809-817 (`is_header_line`): `p = strchr(s, ':'); if (!p || p == s || !*(p+1) || strchr(s,'=')) return 0; return 1;`
> libfdisk/src/script.c:824-855 (`parse_line_header`): fixed `supported[]` array and per-name validation including `sector-size % 512` check (:868) and `unit` must be `"sectors"` (:877).
> libfdisk/src/script.c:1203-1224 (`parse_line_nameval`): prefix detection via `strstr(s," : ")`/`" :"`/`": "`/`strchr(s,':')` then `partno_from_devname(s)`.
> libfdisk/src/script.c:1231-1285: the field dispatch `start=`,`size=`,`bootable`,`attrs=`,`uuid=`,`name=`,`type=`/`Id=`.
> libfdisk/src/script.c:1329-1374 (`parse_line_valcommas`): positional `ITEM_START, ITEM_SIZE, ITEM_TYPE, ITEM_BOOTABLE`.
> libfdisk/src/script.c:1435-1441 (`fdisk_script_set_fgets`): overrides line-reading callback used inside `fdisk_script_read_line`.
> libfdisk/src/script.c:1606-1659 (`fdisk_apply_script_headers`): sector-size mismatch handling, `"grain"`→`fdisk_save_user_grain`, `"label"`→`fdisk_create_disklabel`, `"table-length"`→`fdisk_gpt_set_npartitions`.

### P2.11 Input Expectations
**Claim:** The target expects a text stream, read line-by-line through an internal fixed-size stack buffer `char buf[BUFSIZ]` (typically 8192 bytes). Each physical line is expected to be newline-terminated (`\n`) EXCEPT the final line of the file, which is allowed to lack `\n` only if `feof(f)` is true at that point (otherwise treated as a corrupted/too-long line and rejected with `-EINVAL`). CR before LF is stripped (`\r\n` tolerated). Leading/trailing blanks are skipped (`skip_blank`/`ltrim_whitespace`/`rtrim_whitespace`). Lines that are blank or start with `#` after blank-skipping are silently skipped (comments). No other size limit, magic header, or termination sequence is documented or enforced beyond the implicit `BUFSIZ`-per-line cap. Numeric fields are parsed with `ul_parse_size()`/`ul_strtou64()`/`strtol()` (via `next_token`), so malformed numbers yield `-EINVAL` from those helpers, not a crash (except for the `dp->cxt` NULL-deref risk noted in P2.8).
**Evidence:**
> libfdisk/src/script.c:1511: `char buf[BUFSIZ] = { '\0' };` — fixed stack buffer size for the target's own `fgets` calls (delegated to `fdisk_script_read_line`).
> libfdisk/src/script.c:1476-1494 (`fdisk_script_read_line`): newline detection `s = strchr(buf, '\n'); if (!s) { if (feof(f)) { ...s = strchr(buf,'\0'); } else { ...; return -EINVAL; } } *s = '\0'; if (--s >= buf && *s == '\r') *s = '\0'; s = skip_blank(buf); } while (*s=='\0' || *s=='#');`
> libfdisk/src/script.c:1399-1401 (`fdisk_script_read_buffer`): `s = skip_blank(s); if (!s || !*s) return 0; /* nothing baby, ignore */` — blank lines after the initial filter are also tolerated as no-ops.
> libfdisk/src/script.c:866-869 (`sector-size` header parse): `if (ul_strtou64(value, &x, 10) != 0) return -EINVAL; if (x > ULONG_MAX || x % 512) return -ERANGE;` — concrete example of documented input constraint (multiple of 512).
> libfdisk/src/script.c:1080, :1136 (`ul_parse_size(tk, &num, &pow)`): numeric parsing helper for start=/size=, returns error code on malformed numeric text rather than crashing.

## Build notes
**Claim:** The project builds this functionality as C (not C++) via autotools; the fuzz target compiled for `script.c` is `test_fdisk_script_fuzz` (built only `FUZZING_ENGINE` is enabled), compiling `libfdisk/src/script.c` itself with `-DFUZZ_TARGET` added to the normal `libfdisk_la_CFLAGS`, and linking against the already-built `libfdisk.la` (`libfdisk_tests_ldadd = libfdisk.la $(LDADD)`) plus `$(LIB_FUZZING_ENGINE)`. The public header `libfdisk/libfdisk.h` (generated into the build dir, declaring `fdisk_script_read_file` at line 844 in this checkout) is reachable via `-I$(ul_libfdisk_incdir)` which is part of `libfdisk_la_CFLAGS`. A second, separate fuzzer `test_fdisk_fuzz` (libfdisk/src/fuzz.c) exercises a different path (`fdisk_assign_device`/`fdisk_get_partitions`, reading a *binary disk image*, not a script file) and is not related to `fdisk_script_read_file`. Note: in this checkout, the `#ifdef FUZZ_TARGET` block at the bottom of script.c (libfdisk/src/script.c:1701-1714) is a **stub** `LLVMFuzzerTestOneInput` that ignores its input (`(void)data; (void)size; return 0;`) — the actual harness body for `fdisk_script_read_file` must be (re)written by the downstream harness generator; it is not present/functional in the current source.
**Evidence:**
> ossfuzz_view/util-linux-c1/build.sh:18: `./tools/oss-fuzz.sh`
> tools/oss-fuzz.sh:27: `CONFIGURE_ARGS="--disable-all-programs --enable-libuuid --enable-libfdisk ... --enable-fuzzing-engine ..."`
> tools/oss-fuzz.sh:36-38: `./autogen.sh; ./configure $CONFIGURE_ARGS; make -j... check-programs` — builds all `check_PROGRAMS`, which under `FUZZING_ENGINE` includes `test_fdisk_script_fuzz` and `test_fdisk_fuzz`.
> libfdisk/src/Makemodule.am:93-107:
> ```
> test_fdisk_script_SOURCES = libfdisk/src/script.c
> ...
> if FUZZING_ENGINE
> check_PROGRAMS += test_fdisk_script_fuzz
> nodist_EXTRA_test_fdisk_script_fuzz_SOURCES = dummy.cxx
> test_fdisk_script_fuzz_SOURCES = libfdisk/src/script.c
> test_fdisk_script_fuzz_CFLAGS = -DFUZZ_TARGET $(libfdisk_la_CFLAGS)
> test_fdisk_script_fuzz_LDFLAGS = $(libfdisk_tests_ldflags) $(PTHREAD_LIBS)
> test_fdisk_script_fuzz_LDADD = $(libfdisk_tests_ldadd) $(LIB_FUZZING_ENGINE)
> ```
> libfdisk/src/Makemodule.am:36-41: `libfdisk_la_CFLAGS = $(AM_CFLAGS) $(SOLIB_CFLAGS) -I$(ul_libuuid_incdir) -I$(ul_libfdisk_incdir) -I$(top_srcdir)/libfdisk/src` — include path for the generated `libfdisk.h`.
> libfdisk/src/Makemodule.am:2-4: `# libfdisk.h is generated, so it's stored in builddir! ... nodist_fdiskinc_HEADERS = libfdisk/src/libfdisk.h`
> libfdisk/src/script.c:10-12: `#ifdef FUZZ_TARGET\n#include "fuzz.h"\n#endif`
> libfdisk/src/script.c:1701-1714 (current stub): `#ifdef FUZZ_TARGET ... QF_EXTERN int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { (void)data; (void)size; return 0; } #endif` — comment above it reads "/* Gold harness replaced by an empty stub for baseline generation. */" confirming this is a placeholder, not the real harness.
> libfdisk/src/fuzz.c:28-45 (separate, unrelated `test_fdisk_fuzz` harness): operates on `fdisk_assign_device`/`fdisk_get_partitions`, a binary-disk-image attack surface, not the script-text parser.

## Other entries — call protocol summary

### fdisk_new_script (entry, lifecycle source for `dp`)
**Claim:** `struct fdisk_script *fdisk_new_script(struct fdisk_context *cxt)` — takes an optional `cxt` (may be NULL, see P2.8 for consequences), returns a newly `calloc`'d script with `refcount=1`, or NULL on OOM. No prerequisite besides optionally having a `struct fdisk_context*` from `fdisk_new_context()`. Must eventually be released with `fdisk_unref_script()`.
**Evidence:** libfdisk/src/script.c:133-148.

### fdisk_new_script_from_file (entry, alternative source combining open+new+read+close)
**Claim:** `struct fdisk_script *fdisk_new_script_from_file(struct fdisk_context *cxt, const char *filename)` — `assert(cxt); assert(filename);` (unlike `fdisk_new_script`, here `cxt` MUST be non-NULL or it asserts/aborts). Opens `filename` with `fopen(filename,"r")`, calls `fdisk_new_script(cxt)`, then `fdisk_script_read_file(dp,f)` internally, always `fclose(f)`s itself, and on error unrefs the partially built `dp` and returns NULL with `errno` set to `-rc`; on success returns the filled `dp` and clears `errno`. A harness using this entry must still eventually call `fdisk_unref_script()` on the returned pointer (non-NULL case) and does not need to separately call `fdisk_script_read_file` or manage the `FILE*` at all — this entry is mutually exclusive with calling the raw target directly on the same input source.
**Evidence:** libfdisk/src/script.c:159-193 (full body quoted in P2.1 excerpt above); line 166-167 `assert(cxt); assert(filename);`.

### fdisk_script_read_line (entry, lower-level per-line variant)
**Claim:** `int fdisk_script_read_line(struct fdisk_script *dp, FILE *f, char *buf, size_t bufsz)` — `assert(dp); assert(f); assert(bufsz);`. Caller supplies its own line buffer (`buf`,`bufsz`) instead of the target's internal `BUFSIZ` one, letting a harness control maximum line length directly. Returns `1` when nothing left to read (EOF), `0` on successful parse of one line, `<0` on error (`-ENOTSUP` for unknown header names is "safe to ignore" per the doc comment). Combines with the target's own grammar via the same `fdisk_script_read_buffer()` call at the end (script.c:1496). A harness can call this directly in its own loop (as the target itself does) to get finer-grained per-line control/instrumentation, bypassing `fdisk_script_read_file`'s fixed `BUFSIZ` buffer and its own EOF loop, but still needs the same prerequisite non-NULL `dp` and open `FILE*`/buffer.
**Evidence:** libfdisk/src/script.c:1444-1497 (doc comment + body, quoted above in P2.1/P2.4).

### fdisk_script_write_file (entry, consumer/serializer — not the target, inverse operation)
**Claim:** `int fdisk_script_write_file(struct fdisk_script *dp, FILE *f)` — `assert(dp)` only (no assert on `f`, but passing NULL as `f` will crash inside `write_file_json`/`write_file_sfdisk` since they call `fprintf(f,...)` unconditionally). Dispatches to JSON (`write_file_json`) or plain sfdisk-dump format (`write_file_sfdisk`) based on `dp->json` flag (set via a separate, not-listed-here `fdisk_script_enable_json()`). Typically called AFTER the target (or after `fdisk_apply_script`) to dump the parsed/modified script back to text; combines with the target in a harness as a round-trip check (`read_file` → `write_file`) to exercise the serializer on attacker-influenced parsed data. Returns 0 success, <0 error. Nothing to free afterward beyond the normal `fdisk_unref_script(dp)`.
**Evidence:** libfdisk/src/script.c:799-807 (`fdisk_script_write_file` body); libfdisk/src/script.c:1760 (test_read: `fdisk_script_write_file(dp, stdout);` right after the target).

### fdisk_apply_script (entry, consumer — real downstream use of the parsed script)
**Claim:** `int fdisk_apply_script(struct fdisk_context *cxt, struct fdisk_script *dp)` — `assert(dp); assert(cxt);`, so BOTH must be non-NULL; this means a harness that wants to exercise this entry must have created `dp` via `fdisk_new_script(cxt)` with a real non-NULL `cxt` (reinforcing P2.8's note that NULL-context scripts can't safely reach this consumer either, since `fdisk_apply_script_headers` itself also needs `dp->cxt` to be set — see :1606 `dp->cxt->sector_size`). It saves the context's previous script (`fdisk_ref_script(old)`), calls `fdisk_apply_script_headers(cxt, dp)` (creates a disklabel from the `"label"` header, applies `"grain"`/`"table-length"`/sector-size headers), then if headers succeeded and `dp->table` is non-NULL, calls `fdisk_apply_table(cxt, dp->table)` which iterates the table's `fdisk_partition` entries (built by the target's `parse_line_nameval`/`parse_line_valcommas`) and calls `fdisk_add_partition(cxt, pa, NULL)` for each one that has a start. Finally restores the old script (`fdisk_set_script(cxt,old); fdisk_unref_script(old)`). Returns 0 on success (doc comment says "0 on error" but code returns 0 from `fdisk_apply_table`'s success path, so this is a doc typo — treat 0 as success, <0 as error per actual code and symmetry with other functions), <0 on error. Combines with the target as: `dp = fdisk_new_script(cxt); fdisk_script_read_file(dp, f); fdisk_apply_script(cxt, dp); ... fdisk_unref_script(dp);` — this is the realistic "parse untrusted script then apply it" pipeline that most stresses downstream numeric/type/uuid handling.
**Evidence:** libfdisk/src/script.c:1674-1699 (full body, quoted above); libfdisk/src/script.c:1594-1662 (`fdisk_apply_script_headers`, called first); libfdisk/src/table.c:707-728 (`fdisk_apply_table`, called second).

### fdisk_script_set_header / fdisk_script_get_table (configuration/accessor — used alongside the target, not independent entries into the parser)
**Claim:** `fdisk_script_set_header(dp, name, data)` lets a harness pre-seed or override a header (e.g. force `"label"` to a known value) independent of/before or after parsing; `!dp || !name` → `-EINVAL`; passing `data=NULL` removes the header. `fdisk_script_get_table(dp)` returns `dp->table`, lazily creating an empty `fdisk_new_table()` if none exists yet — this is how a harness/caller obtains the partitions parsed by the target to iterate/inspect them (`fdisk_table_get_nents`, `fdisk_table_get_partition`, etc., not detailed here as they are table.c APIs outside the listed entries). Both are safe to call before or after the target.
**Evidence:** libfdisk/src/script.c:325-344 (`fdisk_script_set_header` signature and NULL/empty-data handling); libfdisk/src/script.c:393-405 (`fdisk_script_get_table` lazy-create); libfdisk/src/script.c:1974 (disk-utils/sfdisk.c) real caller: `tb = fdisk_script_get_table(dp);` used after parsing/building the script.