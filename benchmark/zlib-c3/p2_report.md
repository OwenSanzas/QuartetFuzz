## target: gzopen

### P2.1 Init Sequence
**Claim:** No zlib global init call is required before `gzopen`. `gzopen(path, mode)` is a thin wrapper that directly calls the internal worker `gz_open(path, -1, mode)`; it allocates and fully initializes its own `gz_state` from scratch (no prior `gzbuffer`/`gzsetparams`/other zlib call is needed). `gzbuffer()` and `gzsetparams()` are *optional* configuration calls that, if used, must come strictly **after** `gzopen`/`gzdopen` returns a non-NULL handle and before any `gzread`/`gzwrite`/`gzgetc`/`gzgets` call (buffer allocation is deferred to first I/O).
**Evidence:**
> zlib/gzlib.c:288-290: `gzFile ZEXPORT gzopen(const char *path, const char *mode) { return gz_open(path, -1, mode); }`
> zlib/gzlib.c:87-102: `gz_open` does `if (path == NULL || mode == NULL) return NULL;` then `state = (gz_statep)malloc(sizeof(gz_state)); if (state == NULL) return NULL;` — self-contained allocation, no external init.
> zlib.h:1429-1437 (gzbuffer doc): "This function must be called after gzopen() or gzdopen(), and before any other calls that read or write the file. The buffer memory allocation is always deferred to the first read or write."
> zlib.h:1445-1453 (gzsetparams doc): usable only after a successful open, only in write mode.

### P2.2 Parameter Construction
**Claim:** `path` is a NUL-terminated C string (file path, may be any length; stored internally with `strlen`+`malloc`). `mode` is a NUL-terminated C string like fopen mode, parsed character-by-character: must contain exactly one of `r`/`w`/`a` (else `gzopen` returns NULL); may contain one digit `'0'`-`'9'` for compression level (last digit wins, default `Z_DEFAULT_COMPRESSION`); may contain `'f'`/`'h'`/`'R'`/`'F'` for strategy (`Z_FILTERED`/`Z_HUFFMAN_ONLY`/`Z_RLE`/`Z_FIXED`); may contain `'T'` (force transparent, write/append only) or `'G'` (force gzip-only decode, read only — writing+`'G'` is rejected); `'+'` is rejected (returns NULL, can't read+write simultaneously); other OS-flag letters `'e'`(O_CLOEXEC)/`'x'`(O_EXCL)/`'N'`(O_NONBLOCK)/`'b'` are accepted/ignored platform-dependently. Unknown characters are silently ignored.
**Evidence:**
> zlib/gzlib.c:96-97: `if (path == NULL || mode == NULL) return NULL;`
> zlib/gzlib.c:108-171: mode-parsing while-loop (digits → level, 'r'/'w'/'a' → state->mode, '+' → free+NULL, 'f'/'h'/'R'/'F' → strategy, 'G'→direct=-1, 'T'→direct=1, 'e'/'x'/'N' → oflag bits, default → ignored).
> zlib/gzlib.c:173-177: `if (state->mode == GZ_NONE) { free(state); return NULL; }` — mode letter mandatory.
> zlib/gzlib.c:179-195: read+'T' rejected, write/append+'G' rejected, both `free(state); return NULL;`.
> zlib.h:1357-1370ff (doc comment above gzopen declaration): documents level digits, strategy letters, 'T'/'G' semantics.

### P2.3 Object Lifecycle
**Claim:** Create (`gzopen`/`gzdopen`) → optionally configure (`gzbuffer`, `gzsetparams`) → use (`gzread`/`gzgets`/`gzgetc`/`gzungetc` for GZ_READ, or `gzwrite`/`gzputc`/`gzputs`/`gzprintf`/`gzflush` for GZ_WRITE) → destroy (`gzclose`/`gzclose_r`/`gzclose_w`). The returned `gzFile` is an opaque pointer to heap-allocated `gz_state`; it must be closed exactly once.
**Evidence:**
> zlib/gzlib.c:99-284 (`gz_open`): allocates `gz_state`, parses mode, opens the fd (`open()`/uses provided fd), seeks for append, calls `gz_reset(state)`, returns `(gzFile)state`.
> zlib/test/example.c:99-163 (`test_gzio`): `file = gzopen(...)` → `gzputc`/`gzputs`/`gzprintf` → `gzclose(file)`; then reopen `file = gzopen(fname,"rb")` → `gzread`/`gzseek`/`gzgetc`/`gzungetc`/`gzgets` → `gzclose(file)`.
> zlib.h:1755-1756: "gzclose must not be called more than once on the same file, just as free must not be called more than once on the same allocation."

### P2.4 Return Value Handling
**Claim:** `gzopen` returns `NULL` on failure (bad path/mode args, malformed mode string, malloc failure, or `open()` failure), and a non-NULL `gzFile` handle on success. Callers must check for NULL before any further gz* call, since the handle is the structure itself.
**Evidence:**
> zlib/gzlib.c:96-97,101-102,129-131,174-177,183-184,193-194,207-210,264-268: every failure path does `free(...); return NULL;`.
> zlib/test/minigzip.c:439-444: `out = gzopen(outfile, mode); if (out == NULL) { fclose(in); fprintf(...); exit(1); }`
> zlib/test/example.c:99-103: `file = gzopen(fname, "wb"); if (file == NULL) { fprintf(stderr, "gzopen error\n"); exit(1); }`

### P2.5 Cleanup Sequence
**Claim:** On the read path, `gzclose`/`gzclose_r` frees, in order: (if buffers were allocated, i.e. `state->size != 0`) `inflateEnd(&state->strm)`, `free(state->out)`, `free(state->in)`; then always `free(state->path)`, `close(state->fd)`, `free(state)`. On the write path, `gzclose`/`gzclose_w` flushes pending compressed data (`gz_comp(state, Z_FINISH)`), then frees deflate state/buffers, `free(state->path)`, `close(state->fd)`, `free(state)`. `gzclose()` dispatches by `state->mode` to the right one. These must be the *last* calls on the handle — the memory is freed, so no further gz* calls on the same pointer.
**Evidence:**
> zlib/gzread.c:644-668 (`gzclose_r`): `if (state->size) { inflateEnd(&(state->strm)); free(state->out); free(state->in); } ... free(state->path); ret = close(state->fd); free(state);`
> zlib/gzwrite.c:667-701 (`gzclose_w`, partially read): `if (gz_comp(state, Z_FINISH) == -1) ret = state->err; if (state->size) { ... }` (continues freeing deflate state, out/in, path, fd, state).
> zlib/gzclose.c:11-23 (`gzclose`): `return state->mode == GZ_READ ? gzclose_r(file) : gzclose_w(file);`

### P2.6 API Existence
**Claim:** All functions discussed (`gzopen`, `gzopen64`, `gzdopen`, `gzopen_w`, `gzbuffer`, `gzsetparams`, `gzread`, `gzfread`, `gzgets`, `gzgetc`, `gzungetc`, `gzwrite`, `gzfwrite`, `gzputc`, `gzputs`, `gzprintf`, `gzflush`, `gzrewind`, `gzseek`/`gzseek64`, `gztell`, `gzdirect`, `gzerror`, `gzclose`, `gzclose_r`, `gzclose_w`) exist in the current codebase, declared in `zlib.h` and defined in `gzlib.c`/`gzread.c`/`gzwrite.c`/`gzclose.c`.
**Evidence:**
> zlib.h:1357,1404,1429,1445,1456,1492,1519,1549,1588,1607,1613,1630,1647,1750,1763-1764 — all `ZEXTERN ... ZEXPORT` declarations found in this file.
> zlib/gzlib.c:288-343 (`gzopen`, `gzopen64`, `gzdopen`, `gzopen_w`, `gzbuffer`).
> zlib/gzread.c:645-668 (`gzclose_r`), zlib/gzwrite.c:631-701 (`gzsetparams`,`gzflush` at 604,`gzclose_w`), zlib/gzclose.c:11-23 (`gzclose`).

### P2.7 Co-call Constraints
**Claim:** `gzopen`/`gzdopen`/`gzopen_w` (create) must be paired with exactly one of `gzclose`/`gzclose_r`/`gzclose_w` (destroy), called exactly once. `gzclose_r` only valid if `state->mode==GZ_READ`; `gzclose_w` works for `GZ_WRITE` (GZ_APPEND is normalized to GZ_WRITE inside `gz_open`). `gzbuffer`/`gzsetparams` have no "paired" close call themselves — they are one-shot mutators of the already-open state, not separately-lifecycled objects. `gzungetc(-1, file)` is documented to force completion of a pending seek, a special combination with `gzseek`.
**Evidence:**
> zlib/gzread.c:652-654: `if (state->mode != GZ_READ) return Z_STREAM_ERROR;` inside `gzclose_r`.
> zlib/gzwrite.c:677-679: `if (state->mode != GZ_WRITE) return Z_STREAM_ERROR;` inside `gzclose_w`.
> zlib/gzlib.c:269-272: `if (state->mode == GZ_APPEND) { LSEEK(...); state->mode = GZ_WRITE; }`.
> zlib.h:1641-1644: "gzungetc(-1, file) will force any pending seek to execute. Then gztell() will report the position..."

### P2.8 Prerequisite State
**Claim:** No external zlib context/connection must exist before calling `gzopen`; it is a leaf entry point that only needs valid `path`/`mode` C-string pointers and (for write/append) permission/space to create or open the target file via `open()`. For `gzdopen`, the caller must supply an already-valid, open file descriptor (not checked at open time beyond `fd != -1`).
**Evidence:**
> zlib/gzlib.c:246-268 (`gz_open`): uses raw POSIX `open()`/supplied `fd`; no zlib-level global state referenced.
> zlib.h:1422-1426 (gzdopen doc): "gzdopen will not detect if fd is invalid (unless fd is -1)... file descriptor is not used until the next gz* read, write, seek, or close operation".

### P2.9 Result Usage
**Claim:** The `gzFile` handle returned by `gzopen` is passed as the first argument to every other gz* function in the surface: readers (`gzread`, `gzfread`, `gzgets`, `gzgetc`, `gzungetc`, `gzrewind`, `gzseek`, `gztell`, `gzdirect`, `gzerror`, `gzclearerr`) when opened for reading; writers (`gzwrite`, `gzfwrite`, `gzputc`, `gzputs`, `gzprintf`, `gzflush`, `gzsetparams`) when opened for writing; and finally one close function. The returned bytes/strings from `gzread`/`gzgets` are then consumed by ordinary C code (e.g. `fwrite`, `strcmp`, `memcmp`) in callers, not fed back into other zlib APIs.
**Evidence:**
> zlib/test/example.c:99-163: `file=gzopen(...)` then `gzputc(file,'h')`, `gzputs(file,"ello")`, `gzprintf(file,...)`, `gzseek(file,...)`, `gzclose(file)`; reopened `file=gzopen(fname,"rb")` then `gzread(file,uncompr,...)`, `gzseek`, `gztell(file)`, `gzgetc(file)`, `gzungetc(' ',file)`, `gzgets(file,...)`, `gzclose(file)`.
> zlib/test/minigzip.c:380-414 (`gz_compress`/`gz_uncompress`): `gzwrite(out, buf, len)` result compared to `len`; error via `gzerror(out,&err)`; `gzread(in, buf, sizeof(buf))` result fed into `fwrite(buf, 1, len, out)` (plain libc, not zlib).
> zlib/gzwrite.c:626-628 (`gzflush`): operates on `state` from the handle, calls `gz_comp(state, flush)`.
> zlib/gzwrite.c:631-665 (`gzsetparams`): reads/mutates `state->level`/`state->strategy`, may call `deflateParams(strm, level, strategy)`.

### P2.10 Behaviour Controls
**Claim:** `mode` string controls: direction (`r`/`w`/`a` → `GZ_READ`/`GZ_WRITE`/`GZ_APPEND`), compression level (digit 0-9, default `Z_DEFAULT_COMPRESSION`), deflate strategy (`f`/`h`/`R`/`F`), and the read-time transparent-vs-gzip decode mode via `state->direct` (`'T'` disallowed for read; default leaves `direct=1` meaning "auto-detect by sniffing first bytes" until `gz_look()` actually inspects the stream; `'G'` forces gzip-only, i.e. `direct=-1`, disallowed for write). Once opened, `gzbuffer(file,size)` changes `state->want` (internal chunk size, min 8, must be called before first I/O) which changes how much data `gz_look`/`gz_avail`/`gz_comp` buffer at once. `gzsetparams(file,level,strategy)` changes deflate parameters mid-stream for write mode (only if `!state->direct`). `gzflush(file,flush)` parameter `flush` must be in `[0, Z_FINISH]` and selects the deflate flush mode passed to `gz_comp`.
**Evidence:**
> zlib/gzlib.c:113-171 (mode parse loop, as quoted in P2.2).
> zlib/gzlib.c:179-197: `/* direct is 0, 1 if "T", or -1 if "G" ... if reading, direct == 1 for auto-detect, -1 for gzip only; if writing or appending, direct == 0 for gzip, 1 for transparent */`
> zlib/gzlib.c:322-343 (`gzbuffer`): `if (state->size != 0) return -1;` (must precede first I/O); `if (size < 8) size = 8; state->want = size;`
> zlib/gzwrite.c:641-660 (`gzsetparams`): `if (state->mode != GZ_WRITE || ... || state->direct) return Z_STREAM_ERROR;` then conditionally `deflateParams(strm, level, strategy)`.
> zlib/gzwrite.c:617-619 (`gzflush`): `if (flush < 0 || flush > Z_FINISH) return Z_STREAM_ERROR;`

### P2.11 Input Expectations
**Claim:** `gzopen` itself places no format requirement on the *file contents* — it only validates `path`/`mode` strings and performs a POSIX `open()`. The *gzip-format* expectations apply to subsequent `gzread`/`gzgets`/`gzgetc` calls operating through `gz_look()`: they sniff the first bytes of the file for the gzip magic `1F 8B 08` (+ a 4th flag byte) to decide GZIP-decompress vs transparent COPY; if `state->direct` forces `-1` ("G" mode) then non-gzip input is NOT copied transparently and causes an error; multiple concatenated gzip members are supported; trailing non-gzip bytes after a valid gzip stream are treated as ignorable "transparent" trailing data. No explicit max-file-size check is done at `gzopen`; size limits only arise via `unsigned len` parameters of `gzread`/`gzwrite` (must fit in `int`, else `-1`/`0` returned and `Z_STREAM_ERROR` set, per zlib.h doc at lines 1485-1489 and 1526-1527).
**Evidence:**
> zlib/gzlib.c:95-97: `if (path == NULL || mode == NULL) return NULL;` — no file-content inspection in `gz_open` itself.
> zlib.h:1366-1370 (gzopen doc, truncated read above): "'T' cannot be used to force transparent reading. Transparent reading is automatically performed if there is no gzip header at the start."
> zlib.h:1462-1466 (gzread doc): "After reaching the end of a gzip stream... gzread will continue to read, looking for another gzip stream. ... trailing garbage is ignored."
> zlib.h:1485-1489 (gzread doc): "If len is too large to fit in an int, then nothing is read, -1 is returned, and the error state is set to Z_STREAM_ERROR."

## Other entries' call protocol

### gzdopen
**Claim:** Same worker (`gz_open`) as `gzopen` but keyed by an already-open fd instead of a path; mode string parsing identical. Caller must have obtained `fd` from `open`/`dup`/`creat`/`pipe`/`fileno`. Returns NULL on fd==-1, malloc failure, or malformed mode. On `gzclose`, the fd is closed too (so don't double-close). Combines with target in a harness as an *alternative* creation path (fuzz input could choose path-vs-fd), not usually both in the same run on the same resource.
**Evidence:** zlib/gzlib.c:298-312; zlib.h:1404-1427.

### gzbuffer
**Claim:** Optional, called once right after `gzopen`/`gzdopen`, before any read/write; sets `state->want`. Returns 0 success / -1 failure (file NULL, wrong mode, already-allocated buffers, or size overflow on `size<<1`). No separate cleanup — freed as part of `gzclose`'s `free(state->out)`/`free(state->in)`.
**Evidence:** zlib/gzlib.c:321-343; zlib.h:1429-1443.

### gzsetparams
**Claim:** Write-mode only, callable any time after open while writing, before close. Requires `state->mode==GZ_WRITE` and `!state->direct` (fails on transparent "T" writes). Flushes previously buffered input with `gz_comp(state, Z_BLOCK)` before calling `deflateParams`. Returns `Z_OK`/`Z_STREAM_ERROR`/`Z_ERRNO`/`Z_MEM_ERROR`. No separate resource to free.
**Evidence:** zlib/gzwrite.c:631-665; zlib.h:1445-1454.

### gzread / gzgets / gzgetc / gzungetc
**Claim:** All require `state->mode==GZ_READ` (checked inside, e.g. gzgets uses `state->mode != GZ_READ` guard — see gzread.c). They drive `gz_look()`/`gz_fetch()`/`gz_decomp()`/`gz_read()` to pull and decompress bytes. `gzread`/`gzfread` return byte/item counts or -1/0 on error — check via `gzerror()`. `gzgets` returns `buf` or NULL at EOF/error. `gzgetc`/`gzungetc` return the char or -1. No separate init beyond `gzopen`; no separate free (buffers freed by `gzclose_r`). Used in sequence after `gzopen(..., "r...")` in `example.c:116-163` (gzread → gzseek → gzgetc → gzungetc → gzgets → gzclose).
**Evidence:** zlib/gzread.c (gz_look/gz_fetch/gz_decomp/gz_read implement the state machine — file read via Grep shown callers); zlib.h:1456-1646 (doc blocks for gzread/gzfread/gzgets/gzgetc/gzungetc); zlib/test/example.c:116-163.

### gzwrite / gzprintf / gzflush
**Claim:** Require `state->mode==GZ_WRITE` (GZ_APPEND already folded into GZ_WRITE by `gz_open`). Drive `gz_init()`/`gz_comp()` to deflate-compress and buffer/flush to fd (or bypass deflate entirely when `state->direct` i.e. "T" mode is set — transparent copy-through write). `gzwrite`/`gzfwrite` return bytes/items written, 0 on error/len==0. `gzprintf` returns bytes written or 0 on error (bounded internally by buffer/`state->want`, see gzbuffer note at zlib.h:1439). `gzflush(file, flush)` requires `flush` in `[0, Z_FINISH]`, `state->mode==GZ_WRITE`, returns `state->err`. Example: `test_gzio` at example.c:99-114: `gzopen(...,"wb")` → `gzputc`/`gzputs`/`gzprintf` → `gzseek` → `gzclose`.
**Evidence:** zlib/gzwrite.c:604-628 (gzflush), 631-665 (gzsetparams, for context), zlib.h:1519-1551 (gzwrite/gzfwrite/gzprintf docs); zlib/test/example.c:99-114.

### gzclose / gzclose_r / gzclose_w
**Claim:** Terminal lifecycle call, must be called exactly once per successfully-opened handle, on both success and error paths (minigzip.c calls `gzclose` even after detecting a write/read error, to release resources — see `error(gzerror(...))` pattern which calls `exit`, but gzclose is still invoked in the normal-completion branches at lines 391/413). `gzclose()` auto-dispatches by stored `state->mode`; `gzclose_r`/`gzclose_w` are mode-specific (and avoid linking unused compress/decompress code). Return codes: `Z_STREAM_ERROR` (file NULL or wrong mode for `_r`/`_w` variant), `Z_ERRNO` (close() failed), `Z_BUF_ERROR` (last gzread ended mid-stream), `Z_MEM_ERROR`, or `Z_OK`.
**Evidence:** zlib/gzclose.c:11-23; zlib/gzread.c:645-668; zlib/gzwrite.c:667-701(partial); zlib.h:1750-1769; zlib/test/minigzip.c:391,413 (`if (gzclose(out) != Z_OK) error(...)`).

## How these combine in one harness (as evidenced by existing project fuzzers)
**Claim:** The project's own `minigzip_fuzzer.c` (in the OSS-Fuzz tree) demonstrates the canonical round-trip harness pattern built around `gzopen`: write fuzz bytes to a temp file via plain `fopen`/`fwrite`, build a mode string `"wb" + level-digit + strategy-letter` derived from fuzz bytes, call `file_compress()` → `gzopen(outfile, mode)` → loop `fread`/`gzwrite` → `gzclose`; then `file_uncompress()` → `gzopen(infile, "rb")` → loop `gzread`/`fwrite` → `gzclose`; finally compare decompressed output to the original input bytes. This is the strongest evidence for how `gzopen`'s mode string should be fuzzed (digit+strategy letter) and how read/write must be paired with their own `gzclose`.
**Evidence:**
> ossfuzz_view/zlib-c3/minigzip_fuzzer.c:445-490 (`LLVMFuzzerTestOneInput`): builds `outmode` from `data[0]` (`outmode[2] = data[0] % 10;` level digit; `switch(data[0]%4)` picks strategy letter 'f'/'h'/'R'/none at `outmode[3]`), then `file_compress(inFileName, outmode); file_uncompress(outFileName);`.
> ossfuzz_view/zlib-c3/minigzip_fuzzer.c:371-394 (`file_compress`): `out = gzopen(outfile, mode); ... gz_compress(in, out); unlink(file);` where `gz_compress` loops `fread`+`gzwrite` then `gzclose(out)`.
> ossfuzz_view/zlib-c3/minigzip_fuzzer.c:404-441 (`file_uncompress`): `in = gzopen(infile, "rb"); ... gz_uncompress(in, out);` where `gz_uncompress` loops `gzread`+`fwrite` then `gzclose(in)`.
> Note: the currently-checked-in `gzio_fuzzer.c` in this build dir is a stub (`/* Gold harness replaced by an empty stub for baseline generation. */`, lines 1-16) that does not call `gzopen` at all — it only keeps the symbol linked via `__asm__("gzopen")`; it is NOT usable evidence of calling convention, only `minigzip_fuzzer.c` and `test/example.c`/`test/minigzip.c` are.

## Build notes
**Claim:** Fuzzers are plain C (or C++ for `.cc` files) compiled against the static `libz.a` built by zlib's own `./configure && make`. The target's header `zlib.h` is found via `-I.` (fuzzer sources `#include "zlib.h"` while sitting in/linked against the zlib build root), and there is no special include path beyond the project root; linking uses `$LIB_FUZZING_ENGINE` plus `./libz.a`.
**Evidence:**
> ossfuzz_view/zlib-c3/build.sh:18-24: `./configure` then `make all` (and `make check` unless MSan) builds `libz.a` in the source root.
> ossfuzz_view/zlib-c3/build.sh:38-43: `for f in $(find $SRC -name '*_fuzzer.c'); do $CC $CFLAGS -I. $f -c -o /tmp/$b.o; $CXX $CXXFLAGS -o $OUT/$b /tmp/$b.o -stdlib=libc++ $LIB_FUZZING_ENGINE ./libz.a; done` — C fuzzers compiled with `$CC`, linked with `$CXX`+libc++ against the static `libz.a`.
> ossfuzz_view/zlib-c3/build.sh:31-34: `.cc` fuzzers compiled directly with `$CXX -std=c++11 -I. $f -o $OUT/$b $LIB_FUZZING_ENGINE ./libz.a`.
> ossfuzz_view/zlib-c3/minigzip_fuzzer.c:17: `#include "zlib.h"` (relative include, resolved via `-I.` pointing at the zlib source/build root where `zlib.h` lives, confirmed at zlib/zlib.h — the header is part of the normal zlib package, not under a special subdir).