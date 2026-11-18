## target: vips_jpegsave_buffer

### P2.1 Init Sequence
**Claim:** The vips library must be initialised with `VIPS_INIT()` (or `vips_init()`) before any VipsOperation (including `vips_jpegsave_buffer`) is called. A `VipsImage *in` must already exist (loaded/built) before the call; no other explicit "open" call is required for the save itself, but `in` must be a fully-defined `VipsImage` (dimensions, Bands, BandFmt, Type set, pixel data accessible).
**Evidence:**
> fuzz/generic_buffer_fuzzer.cc:16-17: `if (VIPS_INIT(*argv[0])) return -1;` called once in `LLVMFuzzerInitialize`, before any image load/save.
> libvips/iofuncs/image.c:2196: `vips_check_init();` inside `vips_image_new_from_buffer` (loader path) confirms the library auto-checks init, but OSS-Fuzz harnesses explicitly call `VIPS_INIT` up front.
> libvips/foreign/jpegsave.c:699-709: `vips_jpegsave_buffer(VipsImage *in, void **buf, size_t *len, ...)` takes `in` as the first, required argument — it must already be a valid `VipsImage*` (e.g., returned by `vips_jpegload_buffer` or `vips_image_new_from_buffer`).

### P2.2 Parameter Construction
**Claim:** `in`: non-NULL, built `VipsImage*` (any Bands/BandFmt/Type; internally promoted/converted to uchar + mono/RGB/CMYK before encode). `buf`: pointer to a `void*` that receives a GLib-allocated buffer (may be NULL if caller doesn't want the data, per `vips_jpegsave_buffer` body check `if (buf)`). `len`: pointer to `size_t` that receives the buffer length (may be NULL similarly). Varargs: `NULL`-terminated name/value pairs for optional GObject properties (`"Q"`, `"optimize_coding"`, `"interlace"`, `"subsample_mode"`, `"trellis_quant"`, `"overshoot_deringing"`, `"optimize_scans"`, `"quant_table"`, `"restart_interval"`, `"no_subsample"` (deprecated), plus inherited save options `"profile"`, `"keep"`, `"strip"` (deprecated), `"background"`, `"page_height"`) terminated by a literal `NULL`.
**Evidence:**
> libvips/foreign/jpegsave.c:699-709: `va_start(ap, len); result = vips_call_split("jpegsave_buffer", ap, in, &area); va_end(ap);` then `if (buf) { *buf = area->data; area->free_fn = NULL; } if (len) *len = area->length;`
> libvips/foreign/jpegsave.c:220-289: `VIPS_ARG_INT(class, "Q", ..., 1, 100, 75)`, `VIPS_ARG_BOOL(class, "optimize_coding"...)`, `VIPS_ARG_BOOL(class, "interlace"...)`, `VIPS_ARG_BOOL(class,"no_subsample", ...VIPS_ARGUMENT_DEPRECATED)`, `VIPS_ARG_BOOL(class,"trellis_quant"...)`, `VIPS_ARG_BOOL(class,"overshoot_deringing"...)`, `VIPS_ARG_BOOL(class,"optimize_scans"...)`, `VIPS_ARG_INT(class,"quant_table",...,0,8,0)`, `VIPS_ARG_ENUM(class,"subsample_mode",...,VIPS_TYPE_FOREIGN_SUBSAMPLE, VIPS_FOREIGN_SUBSAMPLE_AUTO)`, `VIPS_ARG_INT(class,"restart_interval",...,0,INT_MAX,0)`.
> libvips/foreign/foreign.c:1904-1937: inherited `VipsForeignSave` properties: `VIPS_ARG_IMAGE(class,"in",0,...REQUIRED_INPUT)`, `VIPS_ARG_FLAGS(class,"keep",100,...)`, `VIPS_ARG_STRING(class,"profile",103,...)`, `VIPS_ARG_BOOL(class,"strip",104,...DEPRECATED)`.
> libvips/foreign/jpegsave.c:453-459 (VipsForeignSaveJpegBuffer class_init): `VIPS_ARG_BOXED(class,"buffer",1,...VIPS_ARGUMENT_REQUIRED_OUTPUT...VIPS_TYPE_BLOB)` — the output buffer property, filled internally and surfaced through `buf`/`len` out-parameters.

### P2.3 Object Lifecycle
**Claim:** `vips_jpegsave_buffer` is a thin wrapper that builds a `VipsForeignSaveJpegBuffer` GObject via `vips_call_split`, which triggers GObject construction → `build()` chain (`vips_foreign_save_jpeg_buffer_build` → `vips_foreign_save_jpeg_build` → `vips_foreign_save_build`) → encode → object is unreffed internally by `vips_call_split`'s operation machinery once the output args are extracted. Caller never manually creates/destroys the operation object; only the returned `buf` memory and the source `in` image must be released by the caller.
**Evidence:**
> libvips/foreign/jpegsave.c:699-724: wrapper calls `vips_call_split("jpegsave_buffer", ap, in, &area)`; after success it transfers raw bytes out of the internal `VipsArea *area` (`*buf = area->data; area->free_fn = NULL;`) then `vips_area_unref(area)` — i.e., ownership of the `VipsBlob`/`VipsArea` wrapper is dropped internally but the underlying GLib-allocated byte buffer is handed to the caller (because `free_fn` is cleared so unref doesn't free the data).
> libvips/foreign/jpegsave.c:420-438 (`vips_foreign_save_jpeg_buffer_build`): creates `jpeg->target = vips_target_new_to_memory()`, calls parent `build()` (which ends up calling `vips__jpeg_write_target`), then `g_object_get(jpeg->target, "blob", &blob, NULL); g_object_set(buffer, "buffer", blob, NULL); vips_area_unref(VIPS_AREA(blob));` — the `VipsTarget` memory sink is the true owner of the encoded bytes until this transfer.
> libvips/foreign/jpegsave.c:116-124 (`vips_foreign_save_jpeg_dispose`): `VIPS_UNREF(jpeg->target);` releases the target when the save operation object is disposed (handled by vips internals, not by the harness).

### P2.4 Return Value Handling
**Claim:** Returns `0` on success, `-1` on error (standard vips convention). On error, no buffer/length are written (the out-params are left untouched because `area` stays NULL or the `if (!result && area)` guard fails); `vips_error()` state is set and should be checked/cleared by the caller if desired (`vips_error_buffer()` or by calling `vips_error_clear()`), but this is optional for a fuzzer. Caller must not read `*buf`/`*len` if the return is `-1`.
**Evidence:**
> libvips/foreign/jpegsave.c:696: doc comment `Returns: 0 on success, -1 on error.`
> libvips/foreign/jpegsave.c:710-724: `if (!result && area) { ...; vips_area_unref(area); } return result;` — buf/len only populated when `result==0` and `area` non-NULL.
> fuzz/generic_buffer_fuzzer.cc:58-61: real caller pattern — `if (vips_image_write_to_buffer(image, SAVE_SUFFIX, &buf, &len, nullptr)) { g_object_unref(image); return 0; } g_free(buf); g_object_unref(image);` (same pattern applies directly to `vips_jpegsave_buffer`, which `vips_image_write_to_buffer` dispatches to for `.jpg`/`.jpeg` suffixes).
> libvips/foreign/uhdrsave.c:298-304: `if (vips_jpegsave_buffer(gainmap, &to_free, &length, ...)) { VIPS_UNREF(gainmap); return -1; }` — error path unrefs the input image and propagates failure; no attempt to free `to_free` since it was never populated.

### P2.5 Cleanup Sequence
**Claim:** On success: free the returned buffer with `g_free()` (per docs) when done with it; unref/free the source `VipsImage *in` with `g_object_unref()`/`VIPS_UNREF()` when no longer needed (independent of the save call — the save does not consume/free `in`). On error: nothing to free from the save itself (buf/len untouched); still must release `in`.
**Evidence:**
> libvips/foreign/jpegsave.c:679-680: doc comment: "You are responsible for freeing the buffer with [func@GLib.free] when you are done with it."
> fuzz/generic_buffer_fuzzer.cc:63-64: `g_free(buf); g_object_unref(image);` — real cleanup order: free the output buffer, then unref the image.
> libvips/deprecated/im_vips2jpeg.c:95-99: `if (vips_jpegsave_buffer(in, (void **) obuf, &len, "Q", qfac, NULL)) return -1; if (out) im_add_callback(out, "close", (im_callback_fn) vips_free, obuf, NULL);` — buffer freed via `vips_free`/`g_free`-equivalent callback.

### P2.6 API Existence
**Claim:** All functions cited below exist in the current codebase at the given locations.
**Evidence:**
> vips_jpegsave_buffer: libvips/foreign/jpegsave.c:699; declared libvips/include/vips/foreign.h:521
> vips_jpegsave: libvips/foreign/jpegsave.c (file build fn at line ~380); declared libvips/include/vips/foreign.h:518
> vips_jpegsave_target: libvips/foreign/jpegsave.c:657; declared libvips/include/vips/foreign.h:515
> vips_jpegsave_mime: libvips/foreign/jpegsave.c:727 (doc)/further in file; declared libvips/include/vips/foreign.h:524
> vips_jpegload_buffer: libvips/foreign/jpegload.c:535; declared libvips/include/vips/foreign.h:508
> vips_image_new_from_buffer: libvips/iofuncs/image.c:2187; declared in vips/image.h (standard public header)
> vips_foreign_save_jpeg_build: libvips/foreign/jpegsave.c:142
> vips__jpeg_write_target: libvips/foreign/vips2jpeg.c:869
> write_vips: libvips/foreign/vips2jpeg.c:729 (static)
> set_cinfo: libvips/foreign/vips2jpeg.c:548 (static)
> write_metadata: libvips/foreign/vips2jpeg.c:702 (static)
> write_jpeg_block: libvips/foreign/vips2jpeg.c:515 (static)
> vips__foreign_convert_saveable: libvips/foreign/foreign.c:1546
> empty_output_buffer: libvips/foreign/vips2jpeg.c:810 (static)

### P2.7 Co-call Constraints
**Claim:** No explicit lock/unlock, but there is a paired "produce then consume then free" relationship: `vips_jpegload_buffer`/`vips_image_new_from_buffer` (produce `VipsImage*`) must be paired with a `g_object_unref`/`VIPS_UNREF` eventually; `vips_jpegsave_buffer` (produce `buf`) must be paired with `g_free(buf)`. Internally `vips_target_new_to_memory()` is paired with the target being unreffed on dispose (`VIPS_UNREF(jpeg->target)` in dispose handler) — not caller-visible. `jpeg_create_compress`/`jpeg_destroy_compress` are paired inside `vips__jpeg_write_target`/`write_destroy` (libjpeg-level, internal to the core, not exposed to caller).
**Evidence:**
> libvips/foreign/vips2jpeg.c:879-906 (`vips__jpeg_write_target`): `jpeg_create_compress(&write->cinfo);` ... on every exit path `write_destroy(write);` which calls `jpeg_destroy_compress(&write->cinfo); VIPS_FREE(write->row_pointer); g_free(write);` (vips2jpeg.c:220-226).
> libvips/foreign/jpegsave.c:117-124: `VIPS_UNREF(jpeg->target);` in `vips_foreign_save_jpeg_dispose`.
> libvips/foreign/jpegsave.c:425: `jpeg->target = vips_target_new_to_memory()` created in buffer-save build.

### P2.8 Prerequisite State
**Claim:** `in` must have `in->Coding`/`BandFmt`/`Type`/`Bands` already meaningful (the save machinery promotes/validates via `vips__foreign_convert_saveable` and asserts `in->BandFmt == VIPS_FORMAT_UCHAR && in->Coding == VIPS_CODING_NONE` by the time `write_vips` runs — this conversion happens automatically inside `build()`, so the raw input image handed to `vips_jpegsave_buffer` can be any saveable format). No open VipsSource/VipsTarget needs to be pre-created by the caller (the buffer variant creates its own `VipsTarget` memory sink internally). If the image carries `"gainmap-data"`, scRGB `Type`, or CICP-HDR metadata and `libultrahdr` support is compiled in, the save is silently redirected to `vips_uhdrsave_target` instead of the plain JPEG path.
**Evidence:**
> libvips/foreign/vips2jpeg.c:737-738 (`write_vips`): `g_assert(in->BandFmt == VIPS_FORMAT_UCHAR); g_assert(in->Coding == VIPS_CODING_NONE);`
> libvips/foreign/jpegsave.c:173-178: `vips__foreign_convert_saveable(save->ready, &x, VIPS_FOREIGN_SAVEABLE_MONO|VIPS_FOREIGN_SAVEABLE_RGB|VIPS_FOREIGN_SAVEABLE_CMYK, bandfmt_jpeg, VIPS_FOREIGN_CODING_NONE, save->background)`
> libvips/foreign/jpegsave.c:157-167: `if (have_uhdr && (vips_image_get_typeof(save->ready,"gainmap-data") || save->ready->Type==VIPS_INTERPRETATION_scRGB || vips__image_is_cicp_hdr(save->ready))) { if (vips_uhdrsave_target(save->ready, jpeg->target, "Q", jpeg->Q, NULL)) return -1; }`

### P2.9 Result Usage
**Claim:** The returned `(buf, len)` pair is raw encoded JPEG bytes. Real callers either (a) free it directly with `g_free`, (b) attach a close-callback to free it later (deprecated API), or (c) feed it into further vips loaders (`vips_jpegload_buffer`) or external consumers as an opaque byte blob (e.g., embedding it as a sub-image / gainmap payload attached via `vips_image_set_blob`/similar metadata calls in `uhdrsave.c`). No project API re-parses `buf`/`len` back through `vips_jpegsave_buffer` itself; they are terminal output bytes.
**Evidence:**
> libvips/deprecated/im_vips2jpeg.c:95-99: result buffer handed off via `im_add_callback(out,"close",(im_callback_fn) vips_free, obuf, NULL)` — deferred free when the `IMAGE* out` closes, not reloaded.
> libvips/foreign/uhdrsave.c:298-309: `vips_jpegsave_buffer(gainmap, &to_free, &length, "subsample-mode", VIPS_FOREIGN_SUBSAMPLE_OFF, "Q", uhdr->Q, NULL)` then `data = to_free;` — the encoded JPEG bytes become `data`/`length` which are subsequently embedded as a gainmap payload (passed on to the libultrahdr writer, not re-decoded in this file).
> fuzz/generic_buffer_fuzzer.cc:58-63: canonical fuzz pattern — buffer produced then immediately freed with `g_free(buf)`, no further use except length/pointer validity checks already done by the save call itself.

### P2.10 Behaviour Controls
**Claim:** Key behaviour-selecting parameters/flags:
- `Q` (1-100, default 75): JPEG quality; also influences default subsampling decision when `subsample_mode==AUTO` (auto-disables subsampling for `Q>=90`, per source comment).
- `subsample_mode` (`VIPS_FOREIGN_SUBSAMPLE_AUTO/ON/OFF`) and deprecated `no_subsample` (bool) — `no_subsample` overrides `subsample_mode` if explicitly set (`vips_object_argument_isset(object,"no_subsample")`).
- `interlace` (progressive) gates whether `optimize_scans` has any effect (`optimize_scans` is ignored with a `g_warning` if `interlace` is FALSE).
- `trellis_quant`, `overshoot_deringing`, `optimize_scans`, `quant_table` are only applied if `HAVE_JPEG_EXT_PARAMS` is defined (libjpeg-turbo extension API); otherwise ignored with `g_warning`.
- `restart_interval` > 0 sets `cinfo->restart_interval`.
- `profile` (string): a filename, `"none"`, or unset — "A profile supplied as an argument overrides an embedded profile"; if unset but image carries an ICC profile blob (`VIPS_META_ICC_NAME`), that embedded profile is written instead.
- `keep`/`strip` (inherited from `VipsForeignSave`): selects which metadata (EXIF/XMP/IPTC/ICC) survives into `save->ready`/`ready` before the jpeg-specific writer runs, feeding `write_metadata`.
- Presence of `"gainmap-data"`, `Type==VIPS_INTERPRETATION_scRGB`, or CICP-HDR metadata diverts the whole build to `vips_uhdrsave_target` instead of the libjpeg path (only if `have_uhdr`).
**Evidence:**
> libvips/foreign/vips2jpeg.c:589-664 (`set_cinfo`): shows all the `HAVE_JPEG_EXT_PARAMS`-gated branches for `trellis_quant`, `overshoot_deringing`, `optimize_scans` (requires `progressive`), `quant_table`.
> libvips/foreign/jpegsave.c:150-155: `no_subsample` deprecated override logic.
> libvips/foreign/vips2jpeg.c:711-721 (`write_metadata`): profile-argument-overrides-embedded-profile logic.
> libvips/foreign/jpegsave.c:157-167: uhdr/gainmap diversion logic.
> libvips/foreign/foreign.c:1804-1816 (`vips_foreign_save_build`): `keep`/`strip` precedence logic feeding `vips__foreign_update_metadata`.

### P2.11 Input Expectations
**Claim:** `in` is expected to be a fully realized `VipsImage` with `Coding==VIPS_CODING_NONE` after internal conversion, Bands/Type compatible with mono (1 band), RGB (3 bands), or CMYK (4 bands, Type==CMYK) — any other configuration is converted by `vips__foreign_convert_saveable` (band-format promoted to uchar via `bandfmt_jpeg[10]` table, all entries map to `VIPS_FORMAT_UCHAR`). No explicit max width/height/size check in the save path itself (libjpeg itself enforces `JPEG_MAX_DIMENSION` = 65500 inside `jpeg_set_defaults`/`jpeg_start_compress`, surfaced as a libjpeg `ERREXIT`/longjmp caught by vips' error manager). Metadata blobs larger than `MAX_DATA_BYTES_IN_MARKER` (65533 - 14 overhead) for ICC/XMP are split across multiple APP markers or truncated with a warning (not a hard failure) per the file's revision history ("ignore large XMP", "raise single-chunk limit on APP to 65533").
**Evidence:**
> libvips/foreign/jpegsave.c:126-133: `bandfmt_jpeg[10]` promotion table: `UC,UC,UC,UC,UC,UC,UC,UC,UC,UC` — always promotes to `VIPS_FORMAT_UCHAR`.
> libvips/foreign/jpegsave.c:169-178: comment: "This is a SAVEABLE_ANY image, we need mono, rgb, cmyk for JPEG save." and the `vips__foreign_convert_saveable` call with `VIPS_FOREIGN_SAVEABLE_MONO|RGB|CMYK`.
> libvips/foreign/vips2jpeg.c:737-738: runtime assertions `g_assert(in->BandFmt==VIPS_FORMAT_UCHAR); g_assert(in->Coding==VIPS_CODING_NONE);` confirming the expected input state at `write_vips` entry.
> libvips/foreign/vips2jpeg.c:157-160: `#define ICC_MARKER (JPEG_APP0+2)`, `#define MAX_BYTES_IN_MARKER 65533`, `#define MAX_DATA_BYTES_IN_MARKER (MAX_BYTES_IN_MARKER - ICC_OVERHEAD_LEN)` — size limits for embedded profile/metadata markers.
> libvips/foreign/vips2jpeg.c:99-102 (file header changelog): "21/10/21 usualuse - raise single-chunk limit on APP to 65533" and "19/7/19 - ignore large XMP" document historical size-limit handling for metadata.

## Other entries of the Logic Group (how they combine with the target in one harness)

### vips_jpegsave
**Call protocol:** `int vips_jpegsave(VipsImage *in, const char *filename, ...)` — thin `vips_call_split("jpegsave", ap, in, filename)` wrapper, same `VipsForeignSaveJpeg` base class as the target, but subclass `VipsForeignSaveJpegFile` requires a writable `filename` (required input, `VIPS_ARG_STRING(class,"filename",1,...VIPS_ARGUMENT_REQUIRED_INPUT...)`) and internally does `vips_target_new_to_file(file->filename)` before the shared `vips_foreign_save_jpeg_build`. Needs: `in` already built; `filename` a path string the process can create/overwrite (filesystem side effect — generally avoided in in-memory fuzzers in favor of `vips_jpegsave_buffer`/`vips_jpegsave_target`). Same optional args as the target (Q, optimize_coding, interlace, subsample_mode, etc.). Returns 0/-1. Nothing extra to free beyond `in` (file is written to disk, no buffer returned). Not typically combined with the buffer target in the same harness because it is pure file I/O, except when chained as a round-trip (save-to-file then reload) — not seen in current source.
**Evidence:**
> libvips/foreign/jpegsave.c:620-630: `vips_jpegsave(VipsImage *in, const char *filename, ...) { ...; result = vips_call_split("jpegsave", ap, in, filename); ...}`
> libvips/foreign/jpegsave.c:392-397: `VIPS_ARG_STRING(class, "filename", 1, ..., VIPS_ARGUMENT_REQUIRED_INPUT, G_STRUCT_OFFSET(VipsForeignSaveJpegFile, filename), NULL);`
> libvips/foreign/jpegsave.c:370-374 (`vips_foreign_save_jpeg_file_build`): `if (file->filename && !(jpeg->target = vips_target_new_to_file(file->filename))) return -1;`

### vips_jpegsave_target
**Call protocol:** `int vips_jpegsave_target(VipsImage *in, VipsTarget *target, ...)` — wraps `vips_call_split("jpegsave_target", ap, in, target)`. Requires a pre-built `VipsTarget *target` (e.g., `vips_target_new_to_memory()`, `vips_target_new_to_file()`, `vips_target_new_to_descriptor()`) passed as `VIPS_ARG_OBJECT(class,"target",1,...VIPS_ARGUMENT_REQUIRED_INPUT...VIPS_TYPE_TARGET)`; the subclass `vips_foreign_save_jpeg_target_build` refs the caller's target (`g_object_ref(jpeg->target)`) rather than creating its own, so the caller owns and must `g_object_unref`/`VIPS_UNREF` the target after use (mirroring the `VipsForeignSaveJpegBuffer` internal pattern but caller-visible here). Same optional args as target function. Returns 0/-1. Useful in a harness to drive the exact same core encode path (`vips__jpeg_write_target`/`write_vips`) with a fuzzer-controlled sink object, while `vips_jpegsave_buffer` is the convenience wrapper that creates/owns a memory target internally and hands back raw bytes — functionally interchangeable cores.
**Evidence:**
> libvips/foreign/jpegsave.c:657-667: `vips_jpegsave_target(VipsImage *in, VipsTarget *target, ...) { ...; result = vips_call_split("jpegsave_target", ap, in, target); ...}`
> libvips/foreign/jpegsave.c:311-324 (`vips_foreign_save_jpeg_target_build`): `if (target->target) { jpeg->target = target->target; g_object_ref(jpeg->target); }` then calls parent build.
> libvips/foreign/jpegsave.c:339-344: `VIPS_ARG_OBJECT(class,"target",1,...VIPS_ARGUMENT_REQUIRED_INPUT...VIPS_TYPE_TARGET);`

### vips_jpegsave_mime
**Call protocol:** `int vips_jpegsave_mime(VipsImage *in, ...)` — wraps `vips_call_split("jpegsave_mime", ap, in)`; subclass creates `jpeg->target = vips_target_new_to_memory()` internally (like the buffer variant) then **writes the result to stdout** as a mime-typed stream (per doc: "save as a mime jpeg on stdout"). Needs: `in` built; no output buffer is returned to caller (writes directly to process stdout) — **not safe/appropriate for a fuzz harness to call repeatedly** since it pollutes stdout; OSS-Fuzz's own `vips_fuzzer.cc` explicitly blocklists this operation (`"VipsForeignSaveJpegMime"`) to avoid cluttering output. Returns 0/-1. Nothing to free (no out-buffer).
**Evidence:**
> libvips/foreign/jpegsave.c:750-759: `vips_jpegsave_mime(VipsImage *in, ...) { ...; result = vips_call_split("jpegsave_mime", ap, in); ...}`
> libvips/foreign/jpegsave.c:731: doc: "As [method@Image.jpegsave], but save as a mime jpeg on stdout."
> fuzz/vips_fuzzer.cc:41: `"VipsForeignSaveJpegMime",` listed in the blocklist with comment (lines 37-39) "Block matrixprint and {jpeg,webp}save_mime to prevent image data from being written to stdout and cluttering the output."

### vips_jpegload_buffer (source entry feeding the target)
**Call protocol:** `int vips_jpegload_buffer(void *buf, size_t len, VipsImage **out, ...)` — wraps the raw bytes in a `VipsBlob` (`vips_blob_new(NULL, buf, len)`, no copy, no ownership transfer) then `vips_call_split("jpegload_buffer", ap, blob, out)`, then `vips_area_unref(VIPS_AREA(blob))` (releases the wrapper, not the caller's `buf`, which the caller must keep alive "You must not free the buffer while @out is active"). Optional args: `shrink` (int), `fail_on` (enum `VipsFailOn`), `autorotate` (bool) select different decode paths (shrink-on-load, strictness of error tolerance, EXIF-orientation auto-rotation). Returns 0/-1; on success `*out` is a newly-built `VipsImage*` that the caller owns and must eventually `g_object_unref`/`VIPS_UNREF`. This is the natural upstream producer for the harness: decode fuzzer-controlled JPEG bytes into `*out`, then feed `*out` as `in` to `vips_jpegsave_buffer` (round-trip fuzzing the full decode+encode pipeline), then free both the loaded image and the saved buffer.
**Evidence:**
> libvips/foreign/jpegload.c:535-552: full body as quoted above (`vips_blob_new`, `vips_call_split("jpegload_buffer", ...)`, `vips_area_unref`).
> libvips/foreign/jpegload.c:520-521: doc: "You must not free the buffer while @out is active."
> libvips/foreign/jpegload.c:523-526: optional args doc: `shrink`, `fail_on`, `autorotate`.
> libvips/deprecated/im_jpeg2vips.c:158: real caller `if (vips_jpegload_buffer(buf, len, &t, NULL)) ...` (simple no-option call).
> libvips/foreign/uhdrload.c:426,573: two more real call sites, both immediately checking the int return before using `out`.

### vips_image_new_from_buffer (generic source entry, alternative to vips_jpegload_buffer)
**Call protocol:** `VipsImage * vips_image_new_from_buffer(const void *buf, size_t len, const char *option_string, ...)` — auto-detects the loader via `vips_foreign_find_load_buffer(buf, len)` (format sniffing on magic bytes/header), wraps `buf` in a `VipsBlob` (no copy — "it's up to you to make sure it's freed when the image is closed"), then `vips_call_split_option_string(operation_name, option_string, ap, blob, &out)`. Returns `NULL` on error (both on unrecognized format and on load failure) or a new, caller-owned `VipsImage*` on success (`(transfer full)`). `option_string` can hold loader-specific options as `"[name=value,...]"`, overridable by trailing named-arg pairs. This is the broader, format-agnostic alternative to `vips_jpegload_buffer` used by the project's own generic fuzzers (`generic_buffer_fuzzer.cc`, `vips_fuzzer.cc`) to turn arbitrary fuzz bytes into a `VipsImage*` of any supported format (PNG, TIFF, JPEG, etc.) before routing it into a saver such as `vips_jpegsave_buffer`/`vips_image_write_to_buffer`.
**Evidence:**
> libvips/iofuncs/image.c:2186-2216: full body as quoted above (`vips_check_init()`, `vips_foreign_find_load_buffer`, `vips_blob_new`, `vips_call_split_option_string`, `vips_area_unref`, `return out;`).
> libvips/iofuncs/image.c:2184: doc: `Returns: (transfer full): the new [class@Image], or NULL on error.`
> fuzz/generic_buffer_fuzzer.cc:48-56: real caller: `if (!(image = vips_image_new_from_buffer(data, size, "", nullptr))) return 0;` followed by dimension/bands sanity checks (`image->Xsize > 100 || image->Ysize > 100 || image->Bands > 4`) before calling the saver.
> fuzz/generic_buffer_with_args_fuzzer.cc:55-59: same pattern but with a fuzzer-controlled `option_string` extracted from the input stream.

### How these combine in one harness
**Claim:** The idiomatic libvips fuzz pattern (already used by `generic_buffer_fuzzer.cc` for other savers, and matching `fuzz_save_buffer_progs` wiring up `'jpegsave': '.jpg'` for a would-be `jpegsave_buffer_fuzzer`) is: `VIPS_INIT()` once in `LLVMFuzzerInitialize`; per input, call `vips_image_new_from_buffer(data, size, "", NULL)` (or `vips_jpegload_buffer` for a JPEG-specific harness) to get `VipsImage *image`; on failure return early; optionally bound `image->Xsize/Ysize/Bands` to avoid timeouts; call `vips_jpegsave_buffer(image, &buf, &len, NULL /* or Q=..., subsample_mode=..., etc. */)`; on success `g_free(buf)`; always `g_object_unref(image)`.
**Evidence:**
> fuzz/meson.build:56-58: `if libjpeg_dep.found() fuzz_save_buffer_progs += {'jpegsave': '.jpg'} endif` — confirms `jpegsave` is wired into the generic `generic_buffer_fuzzer.cc`-based buffer-save fuzzer family (compiled as `jpegsave_buffer_fuzzer` with `-DSAVE_SUFFIX=".jpg"`, dispatching through `vips_image_write_to_buffer` → `vips_jpegsave_buffer`).
> fuzz/generic_buffer_fuzzer.cc:41-66: the concrete load→bound-check→save→free pattern used for every registered suffix including `.jpg`.
> libvips/iofuncs/image.c:2795-2805 (`vips_image_write_to_buffer`): the suffix-based dispatcher that picks `jpegsave_buffer`-equivalent operation by `suffix`, confirming `.jpg`/`.jpeg` routes to the jpegsave_buffer operation class.

## Build notes
**Claim:** Fuzzers are C++ (`.cc`), compiled via Meson as `executable()` targets depending on `libvips_dep` (the installed/just-built libvips, which exposes `<vips/vips.h>` and `<vips/foreign.h>` through its public include dirs) and `fuzz_deps = libvips_deps` (transitively glib/gobject, libjpeg-turbo, etc.). No manual `-I` flags are needed in the fuzzer source beyond `#include <vips/vips.h>` because Meson's `libvips_dep` carries the include path. Linking uses `$LIB_FUZZING_ENGINE` passed as `fuzzer_ldflags` / `cpp_link_args`. `HAVE_JPEG`/libjpeg_dep gates inclusion of jpeg-specific fuzz targets.
**Evidence:**
> fuzz/meson.build:25-27: `if libjpeg_dep.found() fuzz_progs += ['jpegsave_file_fuzzer'] endif`
> fuzz/meson.build:30-36: `fuzz_execs += executable(fuzz_prog, fuzz_prog + '.cc', dependencies: [libvips_dep, fuzz_deps], link_args: fuzz_ldflags)`
> fuzz/meson.build:95-102: buffer-save fuzzers built as `executable(fuzz_basename + '_buffer_fuzzer', 'generic_buffer_fuzzer.cc', dependencies: [libvips_dep, fuzz_deps], link_args: fuzz_ldflags, cpp_args: '-DSAVE_SUFFIX="@0@"'.format(fuzz_save_suffix))`
> fuzz/generic_buffer_fuzzer.cc:1-3: `#include "config.h"` then `#include <vips/vips.h>` — the only include needed, resolved via the Meson dependency's include directories (no relative/manual path).
> fuzz/oss_fuzz_build.sh:258-263: `meson setup build --prefix=$WORK ... -Dfuzz=true -Dfuzzing_engine=oss-fuzz -Dfuzzer_ldflags="$LIB_FUZZING_ENGINE" -Dcpp_link_args="$LDFLAGS -Wl,-rpath=\$ORIGIN/lib"` then `meson install -C build --tag devel` and `find build/fuzz -maxdepth 1 -executable -type f -exec cp -v '{}' $OUT \;` — shows the actual OSS-Fuzz build invocation and how compiled fuzzers are collected into `$OUT`.
> fuzz/oss_fuzz_build.sh:88-101: libjpeg-turbo is built as a static lib and installed to `$WORK` (`-DENABLE_STATIC=TRUE -DENABLE_SHARED=FALSE`), discovered by libvips' Meson build via `PKG_CONFIG_PATH="$WORK/lib/pkgconfig"` (oss_fuzz_build.sh:4), so `HAVE_JPEG`/`libjpeg_dep` resolves against this static build.