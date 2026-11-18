## target: deflate

### P2.1 Init Sequence
**Claim:** Before the first call to `deflate()`, the caller must initialize `strm->zalloc`, `strm->zfree`, `strm->opaque` (or leave them `Z_NULL` for default allocators) and call `deflateInit_()` / `deflateInit2_()` (normally invoked through the `deflateInit`/`deflateInit2` macros) to allocate `deflate_state`. Optional configuration calls (`deflateSetDictionary`, `deflateSetHeader`, `deflatePrime`) may be made after init but before the first productive `deflate()` call, subject to state restrictions below. No process-wide/global zlib init call exists (`zlibVersion()` is only a compatibility check performed internally by the init functions).
**Evidence:**
> zlib.h:231-251: comment on `deflateInit`: "The fields zalloc, zfree and opaque must be initialized before by the caller... deflateInit does not perform any compression: this will be done by deflate()."
> deflate.c:387-398 (`deflateInit2_`): checks version/stream_size, `if (strm == Z_NULL) return Z_STREAM_ERROR;`, defaults `zalloc/zfree` if 0.
> deflate.c:985-988 (`deflate`): first line calls `deflateStateCheck(strm)` which requires `strm->state != Z_NULL` and `s->strm == strm` (i.e., state object created by init) — deflate.c:538-556.
> example_flush_fuzzer.c:46-53: `deflateInit(&c_stream, Z_DEFAULT_COMPRESSION); ... deflate(&c_stream, Z_FULL_FLUSH);` — init always precedes deflate.
> example_dict_fuzzer.c:61-79: `deflateInit2(...)` then `deflateSetDictionary(...)` then `deflate(&c_stream, Z_FINISH)`.

### P2.2 Parameter Construction
**Claim:** `strm` must be a valid, non-NULL `z_streamp` whose `state` field was set by a prior successful `deflateInit*`; `next_in`/`avail_in` must describe a readable buffer of `avail_in` bytes (next_in may be `Z_NULL` only if `avail_in==0`); `next_out`/`avail_out` must describe a writable buffer with `avail_out` > 0 (deflate returns `Z_BUF_ERROR` if `avail_out==0` on entry, and `Z_STREAM_ERROR` if `next_out==Z_NULL`). `flush` must be one of `Z_NO_FLUSH`(0)..`Z_BLOCK`(5) — values above `Z_BLOCK` or negative are rejected with `Z_STREAM_ERROR` (note `Z_TREES`(6) is NOT a legal deflate flush value, only inflate's).
**Evidence:**
> deflate.c:985-987: `if (deflateStateCheck(strm) || flush > Z_BLOCK || flush < 0) { return Z_STREAM_ERROR; }`
> deflate.c:990-995: `if (strm->next_out == Z_NULL || (strm->avail_in != 0 && strm->next_in == Z_NULL) || (s->status == FINISH_STATE && flush != Z_FINISH)) { ERR_RETURN(strm, Z_STREAM_ERROR); } if (strm->avail_out == 0) ERR_RETURN(strm, Z_BUF_ERROR);`
> zlib.h:172-179: `#define Z_NO_FLUSH 0 ... #define Z_BLOCK 5 #define Z_TREES 6` with comment "Allowed flush values; see deflate() and inflate() below for details".

### P2.3 Object Lifecycle
**Claim:** Lifecycle is create (`deflateInit2_`/`deflateInit_`) → optional configure (`deflateSetDictionary`, `deflateSetHeader`, `deflatePrime`, `deflateTune`) → repeated `deflate()` calls, optionally interleaved with `deflateParams` (which itself calls `deflate(strm, Z_BLOCK)` internally) or `deflateReset`/`deflateResetKeep` to restart → terminal `deflate(strm, Z_FINISH)` loop until `Z_STREAM_END` → `deflateEnd()` to destroy. After `Z_STREAM_END`, only `deflateReset` or `deflateEnd` are valid per the header doc.
**Evidence:**
> zlib.h:330-336: "After deflate has returned Z_STREAM_END, the only possible operations on the stream are deflateReset or deflateEnd."
> deflate.c:1024-1026: `if (s->status == FINISH_STATE && strm->avail_in != 0) { ERR_RETURN(strm, Z_BUF_ERROR); }` — enforces no more input after FINISH_STATE entered.
> example_large_fuzzer.c:35-75: deflateInit → deflate(Z_NO_FLUSH) → deflateParams → deflate(Z_NO_FLUSH) → deflateParams → deflate(Z_NO_FLUSH) → deflate(Z_FINISH) → deflateEnd.
> compress.c:42-65 (`compress2_z`): deflateInit → loop of `deflate(&stream, sourceLen ? Z_NO_FLUSH : Z_FINISH)` → (not shown) deflateEnd.

### P2.4 Return Value Handling
**Claim:** `deflate()` returns `Z_OK` (progress made, call again), `Z_STREAM_END` (only when flush==Z_FINISH and all output produced — terminal, success), `Z_STREAM_ERROR` (invalid state/args — stream is unusable, do not call deflate again without reset), `Z_BUF_ERROR` (no progress possible, not fatal — supply more output space/input and retry), or (per header) `Z_STREAM_END`/errors from `ERR_RETURN` macro which also sets `strm->msg`. Callers must check for `Z_STREAM_ERROR` specifically to abort, and treat `Z_BUF_ERROR` as "try again with more space", not a hard failure.
**Evidence:**
> deflate.c:993, 995, 1020, 1025: `ERR_RETURN(strm, Z_STREAM_ERROR)` / `ERR_RETURN(strm, Z_BUF_ERROR)` call sites.
> zlib.h:330-336: semantics of `Z_STREAM_END`/`Z_BUF_ERROR` under `Z_FINISH`.
> example_flush_fuzzer.c:59-62: `err = deflate(&c_stream, Z_FINISH); if (err != Z_STREAM_END) { CHECK_ERR(err, "deflate flush 2"); }` — explicit distinction between expected `Z_STREAM_END` and other errors.
> example_dict_fuzzer.c:79-83: same pattern, `if (err != Z_STREAM_END) { ...return 0; }`.
> deflate.c:1217-1237: on `need_more`/`finish_started` with `avail_out==0`, sets `s->last_flush = -1` then `return Z_OK;` (progress path, not an error).

### P2.5 Cleanup Sequence
**Claim:** Exactly one `deflateEnd(strm)` call frees, in order, `pending_buf`, `head`, `prev`, `window`, then the `deflate_state` struct itself and sets `strm->state = Z_NULL`. This must be called exactly once per successful `deflateInit*`, on both success and error paths (including after a `Z_STREAM_ERROR` from deflate, as long as `deflateInit*` had succeeded). `deflateEnd` itself validates: if the stream was still `BUSY_STATE` (i.e., not finished), it returns `Z_DATA_ERROR` (but still frees memory).
**Evidence:**
> deflate.c:1293-1310: `int ZEXPORT deflateEnd(z_streamp strm) { ... TRY_FREE(strm, strm->state->pending_buf); TRY_FREE(strm, strm->state->head); TRY_FREE(strm, strm->state->prev); TRY_FREE(strm, strm->state->window); ZFREE(strm, strm->state); strm->state = Z_NULL; return status == BUSY_STATE ? Z_DATA_ERROR : Z_OK; }`
> example_large_fuzzer.c:73-74: `err = deflateEnd(&c_stream); CHECK_ERR(err, "deflateEnd");` called once after the finish loop.
> example_flush_fuzzer.c:63-64: `err = deflateEnd(&c_stream); CHECK_ERR(err, "deflateEnd");`

### P2.6 API Existence
**Claim:** All functions discussed (`deflate`, `deflateInit2_`, `deflateSetDictionary`, `deflateSetHeader`, `deflateParams`, `deflateReset`, `deflateResetKeep`, `deflatePrime`, `deflateEnd`, plus `deflateTune`, `deflateBound`, `deflateCopy`, `deflateGetDictionary`, `deflatePending`) exist in the current codebase as public symbols declared in `zlib.h` and defined in `deflate.c`.
**Evidence:**
> deflate.c:387 `int ZEXPORT deflateInit2_(...)`; 559 `deflateSetDictionary`; 644 `deflateResetKeep`; 704 `deflateReset`; 714 `deflateSetHeader`; 745 `deflatePrime`; 774 `deflateParams`; 981 `deflate`; 1293 `deflateEnd`; 625 `deflateGetDictionary`; 722 `deflatePending`; 819 `deflateTune`; 1317 `deflateCopy`.
> zlib.h:254 `ZEXTERN int ZEXPORT deflate(z_streamp strm, int flush);` (and surrounding macro-commented declarations for `deflateInit`/`deflateInit2`).

### P2.7 Co-call Constraints
**Claim:** `deflateInit2_` (create) must be paired with exactly one `deflateEnd` (destroy) per stream — classic open/close pairing. `deflateParams` internally performs a paired "flush-then-switch": it calls `deflate(strm, Z_BLOCK)` before changing level/strategy, and callers must check that no error (`Z_STREAM_ERROR`/`Z_BUF_ERROR`) is returned before assuming the switch succeeded. `deflateReset`/`deflateResetKeep` pairs with a prior `deflateInit2_` (requires existing state) and effectively lets one `deflateInit2_`/`deflateEnd` pair host multiple deflate "sessions". There is no explicit lock/unlock; the state machine itself enforces ordering via `s->status` transitions (INIT_STATE → GZIP_STATE(optional)/EXTRA/NAME/COMMENT/HCRC → BUSY_STATE → FINISH_STATE).
**Evidence:**
> deflate.c:778-816 (`deflateParams`): `if ((strategy != s->strategy || func != configuration_table[level].func) && s->last_flush != -2) { int err = deflate(strm, Z_BLOCK); if (err == Z_STREAM_ERROR) return err; if (strm->avail_in || (s->strstart - s->block_start) + s->lookahead) return Z_BUF_ERROR; }`
> deflate.c:704-711 (`deflateReset`): `ret = deflateResetKeep(strm); if (ret == Z_OK) lm_init(strm->state); return ret;` — requires `strm->state` already allocated (checked via `deflateStateCheck` inside `deflateResetKeep`, deflate.c:647-649).
> deflate.c:538-556 (`deflateStateCheck`): enforces `s->status` is one of the valid enumerated states at every entry point, implementing the state-machine ordering constraint shared by deflate/deflateReset/deflateParams/deflateSetHeader/etc.
> example_large_fuzzer.c:54,62 (`deflateParams(&c_stream, Z_NO_COMPRESSION, ...)`, `deflateParams(&c_stream, Z_BEST_COMPRESSION, ...)`) interleaved with `deflate()` calls, each preceded by setting new `next_in`/`avail_in`.

### P2.8 Prerequisite State
**Claim:** Before `deflate()` can run, `strm->state` must exist and have `status` in {INIT_STATE, GZIP_STATE, EXTRA_STATE, NAME_STATE, COMMENT_STATE, HCRC_STATE, BUSY_STATE, FINISH_STATE} (anything else, e.g. state torn down by `deflateEnd`, is rejected). If a gzip header is desired, `deflateSetHeader` must be called while status is still before BUSY_STATE-emitting the header (in practice, right after init, before first `deflate()` call) and only valid when `wrap==2` (i.e., windowBits was configured >15 for gzip at `deflateInit2_` time). If a preset dictionary is desired, `deflateSetDictionary` must be called while `wrap!=2` and `status==INIT_STATE` (zlib/raw wrap only, not gzip) and before any data has been compressed (`s->lookahead==0`).
**Evidence:**
> deflate.c:538-556 (`deflateStateCheck`): full list of valid `status` values.
> deflate.c:714-718 (`deflateSetHeader`): `if (deflateStateCheck(strm) || strm->state->wrap != 2) return Z_STREAM_ERROR; strm->state->gzhead = head;`
> deflate.c:567-572 (`deflateSetDictionary`): `if (deflateStateCheck(strm) || dictionary == Z_NULL) return Z_STREAM_ERROR; ... if (wrap == 2 || (wrap == 1 && s->status != INIT_STATE) || s->lookahead) return Z_STREAM_ERROR;`
> deflate.c:422-433 (`deflateInit2_`): `windowBits` sign/magnitude select `wrap`: negative → raw (`wrap=0`), >15 → gzip (`wrap=2`, `#ifdef GZIP`), else zlib (`wrap=1`).

### P2.9 Result Usage
**Claim:** The target's "results" are (a) the bytes written into `strm->next_out`/consumed via `strm->avail_out` (the compressed stream) and (b) the updated `strm` fields (`total_out`, `adler`, `avail_in`, `next_in`). Real callers (i) loop calling `deflate()` again when `avail_out` hits 0 or when `Z_BUF_ERROR`/`Z_OK` with pending data is returned, consuming/writing the filled `next_out` buffer to a file/socket (`gz_comp` in gzwrite.c) or buffer (`compress2_z`); (ii) pass the full compressed buffer later to `inflateInit`/`inflate` for round-trip verification (example_flush_fuzzer.c, example_large_fuzzer.c, example_dict_fuzzer.c — "test_sync"/"test_large_inflate"/"test_dict_inflate"); (iii) read `total_out` to know final compressed length (example_flush_fuzzer.c:66 `*comprLen = (z_size_t)c_stream.total_out;`).
**Evidence:**
> gzwrite.c:65-... (`gz_comp`): calls `deflate()` (not shown in the 100-line excerpt but function exists for this purpose, see gzwrite.c `local int gz_comp(gz_statep state, int flush)`), consumes `strm->next_out`/`avail_out` into `state->out` and writes it via `write()`.
> compress.c:50-65 (`compress2_z`): `err = deflate(&stream, sourceLen ? Z_NO_FLUSH : Z_FINISH);` in a loop that refills `avail_out`/`avail_in` from caller-provided buffers — the compressed bytes accumulate directly in caller's `dest` buffer.
> example_flush_fuzzer.c:59-68: after `deflate(..., Z_FINISH)` returns `Z_STREAM_END`, `*comprLen = (z_size_t)c_stream.total_out;` then that buffer (`compr`) is fed to `test_sync()`/`inflateInit`+`inflate` at lines 83-99 for round-trip decoding.
> example_large_fuzzer.c:38-76 then 81-110: compressed buffer `compr` produced by `test_large_deflate` is passed into `test_large_inflate(compr, comprLen, ...)` which calls `inflateInit`/`inflate` on it.
> example_dict_fuzzer.c:69-86 then subsequent `test_dict_inflate(compr, comprLen)` (gzip-dict round trip, calling inflateSetDictionary/inflate).

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting parameters/flags across the API surface:
- `flush` argument to `deflate()`: `Z_NO_FLUSH`/`Z_PARTIAL_FLUSH`/`Z_SYNC_FLUSH`/`Z_FULL_FLUSH`/`Z_BLOCK`/`Z_FINISH` select different block termination / history-reset / trailer-emission behaviour.
- `level` (0-9, or `Z_DEFAULT_COMPRESSION`=-1) set at `deflateInit2_`/changed via `deflateParams`: `level==0` forces `deflate_stored`; otherwise `configuration_table[level].func` selects `deflate_fast`/`deflate_slow` (see dispatch below).
- `strategy` (`Z_DEFAULT_STRATEGY`, `Z_FILTERED`, `Z_HUFFMAN_ONLY`, `Z_RLE`, `Z_FIXED`) set at init or via `deflateParams`: `Z_HUFFMAN_ONLY`→`deflate_huff`, `Z_RLE`→`deflate_rle`, else dispatch via level/config table.
- `windowBits` sign/magnitude at `deflateInit2_` selects wrapper format: negative = raw deflate (`wrap=0`), 9-15 = zlib wrapper (`wrap=1`), >15 (with `-DGZIP`) = gzip wrapper (`wrap=2`, triggers `GZIP_STATE`/header-field branches in `deflate()`).
- `memLevel` (1-9) sizes the hash table (`hash_bits = memLevel+7`) and `lit_bufsize`, affecting how often `deflate()`'s internal buffer-fill/flush paths trigger.
- Presence of `gzhead` (set via `deflateSetHeader`, only if `wrap==2`) switches `deflate()`'s status machine through `EXTRA_STATE`/`NAME_STATE`/`COMMENT_STATE`/`HCRC_STATE` byte-copy loops instead of writing default gzip header bytes.
- Presence/size of a preset dictionary via `deflateSetDictionary` sets `s->strstart!=0`, causing `deflate()`'s zlib-header logic to emit `PRESET_DICT` flag plus extra adler32 bytes (deflate.c:1045-1054).
**Evidence:**
> zlib.h:172-179: flush constant definitions.
> deflate.c:1217-1220: `bstate = s->level == 0 ? deflate_stored(s, flush) : s->strategy == Z_HUFFMAN_ONLY ? deflate_huff(s, flush) : s->strategy == Z_RLE ? deflate_rle(s, flush) : (*(configuration_table[s->level].func))(s, flush);`
> deflate.c:422-433: windowBits sign/magnitude → `wrap` selection.
> deflate.c:453-456: `s->hash_bits = (uInt)memLevel + 7; s->hash_size = 1 << s->hash_bits; ...`
> deflate.c:1065-1208: `#ifdef GZIP` block walking `GZIP_STATE`→...→`HCRC_STATE` driven by `s->gzhead` being `Z_NULL` or not.
> deflate.c:1045-1054: `if (s->strstart != 0) header |= PRESET_DICT; ... if (s->strstart != 0) { putShortMSB(... adler32 high/low ...); }`

### P2.11 Input Expectations
**Claim:** `deflate()` treats `next_in` as an opaque raw byte stream of length `avail_in` with no required format, header, or terminator of its own — any byte sequence (including empty, `avail_in==0`) is valid input; there is no implicit size limit beyond `uInt`/`unsigned` range of `avail_in` (caller must chunk larger inputs across multiple calls). The *output* format (zlib RFC1950 / gzip RFC1952 / raw deflate RFC1951) is determined solely by `windowBits`/`wrap` at init time, not by inspecting input content. The one structural requirement on input ordering is: once `flush==Z_FINISH` has been passed, no more input may be supplied (`avail_in` must be 0 on subsequent calls) until `deflateReset`.
**Evidence:**
> deflate.c:990-991: only requires `next_in != Z_NULL` when `avail_in != 0` — no content/format constraint.
> deflate.c:1024-1026: `if (s->status == FINISH_STATE && strm->avail_in != 0) { ERR_RETURN(strm, Z_BUF_ERROR); }` — enforces no new input after FINISH_STATE.
> zlib.h:275-284: "Before the call of deflate(), the application should ensure that at least one of the actions is possible, by providing more input and/or consuming more output... avail_out should never be zero before the call."
> compress.c:50-60 (`compress2_z`): demonstrates chunking arbitrarily large `sourceLen`/`destLen` (size_t) across the `uInt`-limited `avail_in`/`avail_out` fields in a loop.

### Build notes
**Claim:** Fuzz harnesses for this feature are built as plain C (`*_fuzzer.c`, compiled with `$CC $CFLAGS`) or C++ (`*_fuzzer.cc`, compiled with `$CXX -std=c++11`), both built from the zlib source root with `-I.` so `#include "zlib.h"` resolves to the project's own header (not a system-installed one), and linked against the locally-built static archive `./libz.a` plus `$LIB_FUZZING_ENGINE`. No extra libraries beyond libz.a are referenced.
**Evidence:**
> build.sh:31-34: `for f in $(find $SRC -name '*_fuzzer.cc'); do b=$(basename -s .cc $f); $CXX $CXXFLAGS -std=c++11 -I. $f -o $OUT/$b $LIB_FUZZING_ENGINE ./libz.a; done`
> build.sh:38-44: `for f in $(find $SRC -name '*_fuzzer.c'); do b=$(basename -s .c $f); $CC $CFLAGS -I. $f -c -o /tmp/$b.o; $CXX $CXXFLAGS -o $OUT/$b /tmp/$b.o -stdlib=libc++ $LIB_FUZZING_ENGINE ./libz.a; rm -f /tmp/$b.o; ... done`
> build.sh:23-24: `make -j$(nproc) all` builds `libz.a` from the project's own `configure`/`Makefile` (same source tree providing `zlib.h`/`deflate.c`), before the fuzzer sources are compiled with `-I.`.
> example_flush_fuzzer.c:20 `#include "zlib.h"` — the fuzz sources include the project-local header via the `-I.` include path, not a system copy.
> Dockerfile:19-21: `git clone ... zlib.git` then `WORKDIR zlib`, `COPY ... *_fuzzer.c* $SRC/` — fuzzer sources are copied alongside the cloned zlib source tree that build.sh compiles against.