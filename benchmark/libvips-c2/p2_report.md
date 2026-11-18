## target: vips_smartcrop

### P2.1 Init Sequence
**Claim:** The VIPS library must be globally initialised with `VIPS_INIT()` (or `vips_init()`) before any image/operation API, including `vips_smartcrop`, is used. There is no per-call smartcrop-specific init: the input `VipsImage *in` must already exist (typically produced by a loader such as `vips_image_new_from_buffer`/`vips_image_new_from_file`) before calling `vips_smartcrop`.
**Evidence:**
> fuzz/thumbnail_fuzzer.cc:8: `if (VIPS_INIT(*argv[0])) return -1;` called once in `LLVMFuzzerInitialize`, before any per-input work.
> fuzz/sharpen_fuzzer.cc:8, fuzz/generic_buffer_fuzzer.cc (same pattern) — every real libvips fuzz driver calls `VIPS_INIT` first.
> libvips/resample/thumbnail.c:1081-1085: `vips_crop(t[13], &t[18], ...)` is called, then its result `t[18]` is passed as `in` to `vips_smartcrop(t[18], &t[19], crop_width, crop_height, ...)` — the input image must already be a valid, built `VipsImage` before the call.
> libvips/conversion/smartcrop.c:546-556: the wrapper itself does no initialisation; it only packs the varargs and calls `vips_call_split("smartcrop", ap, in, out, width, height)`, relying on the already-initialised VIPS operation-cache/type system.

### P2.2 Parameter Construction
**Claim:** `in` must be a non-NULL, successfully-built `VipsImage*` (e.g. from a loader). `out` must be a non-NULL `VipsImage**` that receives a new reference; the caller must not pre-populate it. `width`/`height` are required ints, declared with range `[1, VIPS_MAX_COORD]` and are further checked against `in->Xsize`/`in->Ysize` inside `build()`. Optional varargs (NULL-terminated) select `"interesting"` (enum `VipsInteresting`), `"interesting_x"`/`"interesting_y"` (ints, default 0), and `"premultiplied"` (gboolean, default FALSE).
**Evidence:**
> libvips/include/vips/conversion.h:188-190: `VIPS_API int vips_smartcrop(VipsImage *in, VipsImage **out, int width, int height, ...) G_GNUC_NULL_TERMINATED;`
> libvips/conversion/smartcrop.c:447-459: `VIPS_ARG_INT(class, "width", ..., G_STRUCT_OFFSET(VipsSmartcrop, width), 1, VIPS_MAX_COORD, 1);` and same for `"height"`.
> libvips/conversion/smartcrop.c:461-466: `VIPS_ARG_ENUM(class, "interesting", ..., VIPS_TYPE_INTERESTING, VIPS_INTERESTING_ATTENTION);` — default is ATTENTION.
> libvips/conversion/smartcrop.c:468-480: `interesting_x`/`interesting_y` are `VIPS_ARGUMENT_OPTIONAL_INPUT`, range `[0, VIPS_MAX_COORD]`, default 0.
> libvips/conversion/smartcrop.c:496-501: `VIPS_ARG_BOOL(class, "premultiplied", ..., FALSE)`.
> libvips/conversion/smartcrop.c:340-345 (build): `if (smartcrop->width > smartcrop->in->Xsize || smartcrop->height > smartcrop->in->Ysize || smartcrop->width <= 0 || smartcrop->height <= 0) { vips_error(...); return -1; }` — explicit bound check on width/height vs image size.

### P2.3 Object Lifecycle
**Claim:** `vips_smartcrop` follows the standard VIPS "operation" lifecycle hidden behind `vips_call_split`: create the `VipsSmartcrop` GObject, set its properties (`input`, `width`, `height`, optional args), call its `build()` method (which validates params, dispatches on `interesting`, and calls `vips_extract_area`/`vips_image_write` to fill `out`), then the operation object itself is unreffed by the vips operation-cache machinery — the caller never holds the operation object directly, only `in` (caller-owned, pre-existing) and the new `out` (caller-owned, must be unreffed).
**Evidence:**
> libvips/conversion/smartcrop.c:545-556: the public function body is only `vips_call_split("smartcrop", ap, in, out, width, height)` — the create/configure/build/cache steps are internal to `vips_call_split`/`vips_object_build`, not exposed to the caller.
> libvips/conversion/smartcrop.c:323-345: `vips_smartcrop_build()` first calls the parent class build (`VIPS_OBJECT_CLASS(vips_smartcrop_parent_class)->build(object)`), i.e. "configure" happens via property-setting before build, consistent with the general VipsObject lifecycle.
> libvips/conversion/smartcrop.c:417-420: at the end of build, `vips_extract_area(smartcrop->in, &t[1], left, top, smartcrop->width, smartcrop->height, NULL)` then `vips_image_write(t[1], conversion->out)` — `conversion->out` is the same memory as the caller's `out` pointer (filled via `vips_call_split`).
> libvips/resample/thumbnail.c:1085-1099: caller pattern — `vips_smartcrop(...)` result `t[19]` is then read (`vips_image_get_xoffset/yoffset`) and used as geometry input to further processing (`vips_thumbnail_crop_pages`); `t[]` array entries are auto-freed via `vips_object_local_array`, not manually unreffed in this internal caller.

### P2.4 Return Value Handling
**Claim:** `vips_smartcrop` returns `0` on success, `-1` on error (matching the general VIPS convention); on `-1` `*out` is not a valid image and must not be unreffed/used, and `vips_error_buffer()` holds a diagnostic string. All call sites check the return value and bail out (returning error / unreffing inputs) on failure.
**Evidence:**
> libvips/conversion/smartcrop.c:543: doc comment "Returns: 0 on success, -1 on error."
> libvips/resample/thumbnail.c:1085-1091: `if (vips_smartcrop(t[18], &t[19], crop_width, crop_height, "interesting", thumbnail->crop, ...)) return -1;` — error path propagates upward, no cleanup of `t[18]` (array-managed).
> libvips/conversion/smartcrop.c:343-344 (internal error example): `vips_error(class->nickname, "%s", _("bad extract area")); return -1;` shows the kind of internal error that surfaces as `vips_smartcrop()` returning -1.

### P2.5 Cleanup Sequence
**Claim:** On success, caller owns `*out` and must `g_object_unref(*out)` when done (standard VipsImage refcounting); `in` is also caller-owned and must be unreffed independently (smartcrop takes a reference internally via property set, it does not consume/free `in`). On error (-1 return), `*out` is untouched/invalid and must not be unreffed; only `in` (and any images already allocated by the caller before the call) need cleanup.
**Evidence:**
> fuzz/thumbnail_fuzzer.cc:31-39 (analogous `vips_thumbnail_image` pattern used across all the project's own fuzzers): `if (vips_thumbnail_image(image, &out, 42, nullptr)) { g_object_unref(image); return 0; } ... g_object_unref(out); g_object_unref(image);` — demonstrates the unref-both-on-success / unref-input-only-on-error pattern used throughout for every "`in` -> `&out`" VIPS conversion call, which `vips_smartcrop` follows (same `VipsImage*in, VipsImage**out` calling convention, same `vips_call_split` plumbing).
> libvips/conversion/smartcrop.c:328: `VipsImage **t = (VipsImage **) vips_object_local_array(object, 2);` — temporaries created inside `build()` (`t[0]` for premultiply, `t[1]` for extract_area) are attached to the operation object's local array and freed automatically when the operation object is disposed by the cache; they are not the caller's responsibility.

### P2.6 API Existence
**Claim:** All functions cited in this report exist in the current codebase.
**Evidence:**
> libvips/include/vips/conversion.h:188-190: `vips_smartcrop` declared.
> libvips/include/vips/conversion.h:181-183: `vips_extract_area` declared.
> libvips/include/vips/conversion.h:335-336: `vips_premultiply` declared.
> libvips/include/vips/image.h:474-475,488-490: `vips_image_new_from_file`, `vips_image_new_from_buffer` declared.
> libvips/include/vips/resample.h:84-96: `vips_thumbnail`, `vips_thumbnail_buffer`, `vips_thumbnail_image`, `vips_thumbnail_source` declared.
> libvips/conversion/smartcrop.c:546: `vips_smartcrop` defined.
> libvips/resample/thumbnail.c:1085: real call site of `vips_smartcrop` inside `vips_thumbnail_build`-family code.

### P2.7 Co-call Constraints
**Claim:** No explicit lock/unlock-style pairing is required for `vips_smartcrop` itself. The one structural pairing enforced by the library is the general "every `VipsImage` you create/receive must eventually be `g_object_unref`'d exactly once" rule (create/destroy pairing), and VIPS_INIT()/vips_shutdown() as a global open/close pair for the whole process. Internally, `vips_smartcrop_build` pairs `vips_premultiply` (conditionally) with the subsequent analysis, and `vips_extract_area`+`vips_image_write` as a fixed produce/commit pair at the end of every code path.
**Evidence:**
> libvips/conversion/smartcrop.c:353-357: `if (vips_image_hasalpha(in) && !smartcrop->premultiplied) { if (vips_premultiply(in, &t[0], NULL)) return -1; in = t[0]; }` — premultiply only called conditionally, feeding into all subsequent branches via the rebound `in`.
> libvips/conversion/smartcrop.c:417-421: `vips_extract_area(smartcrop->in, &t[1], left, top, smartcrop->width, smartcrop->height, NULL) || vips_image_write(t[1], conversion->out)` — every `interesting` branch converges here; extract_area's output is always written into the final output via `vips_image_write`.
> libvips/include/vips/vips.h:153,175: `VIPS_INIT(ARGV0)` macro and `void vips_shutdown(void);` — process-level open/close pairing used by all fuzzers (`VIPS_INIT` called once in `LLVMFuzzerInitialize`; no matching `vips_shutdown()` call is made in the project's own fuzzers, which is standard libFuzzer practice of relying on process exit).

### P2.8 Prerequisite State
**Claim:** Requires a successfully decoded `VipsImage *in` with valid `Xsize`, `Ysize`, `Bands` fields (i.e., the image must have gone through a loader's `build()` so header fields are populated) before `vips_smartcrop` is called; no other global/connection state is needed beyond the one-time VIPS runtime init.
**Evidence:**
> libvips/conversion/smartcrop.c:340-341: `smartcrop->width > smartcrop->in->Xsize || smartcrop->height > smartcrop->in->Ysize` directly dereferences `in->Xsize`/`in->Ysize`, which are only valid once the image has been loaded/built.
> libvips/conversion/smartcrop.c:116-117,226-227: `width = in->Xsize; height = in->Ysize;` (entropy) and `hscale = 32.0 / in->Xsize; vscale = 32.0 / in->Ysize;` (attention) — both divide by image dimensions, so `Xsize`/`Ysize` must be non-zero (loader-guaranteed for well-formed images, but a maliciously-crafted 0-sized or degenerate image could violate this; this is exactly the kind of boundary noted in the feature description).
> libvips/conversion/smartcrop.c:189-198 (pythagoras, called from the attention path) loops `for (i = 0; i < in->Bands; i++)` — depends on `in->Bands` being set correctly by the loader.

### P2.9 Result Usage
**Claim:** The filled `*out` `VipsImage` from `vips_smartcrop` is consumed exactly like any other VIPS image: passed to further conversion/resample ops, queried for header fields (xoffset/yoffset), or written out via a saver — never written to directly.
**Evidence:**
> libvips/resample/thumbnail.c:1093-1094: `bbox_x = -vips_image_get_xoffset(t[19]); bbox_y = -vips_image_get_yoffset(t[19]);` — reads offset metadata set on the smartcrop result to recover the crop bounding box.
> libvips/resample/thumbnail.c:1096-1099: `t[14] = vips_thumbnail_crop_pages(object, t[13], bbox_x, bbox_y, crop_width, crop_height, page_height, n_pages)` — the smartcrop-derived bbox is fed into a further cropping routine applied to all pages.
> libvips/conversion/smartcrop.c:524-525 (doc comment): "You can test xoffset / yoffset on @out to find the location of the crop within the input image." confirms the intended post-call usage pattern of reading offsets from the result.
> General pattern (analogous real callers for other `in`/`out` conversions in this codebase, e.g. fuzz/thumbnail_fuzzer.cc:36): `vips_avg(out, &d, nullptr);` then `g_object_unref(out);` — i.e. the result is typically passed to one more read/query/measurement call before being unreffed.

### P2.10 Behaviour Controls
**Claim:** `"interesting"` (enum `VipsInteresting`: NONE, CENTRE, ENTROPY, ATTENTION [default], LOW, HIGH, ALL, SPECIFIC) is the primary mode selector inside `vips_smartcrop_build`, each value choosing a structurally different code path. `"interesting_x"`/`"interesting_y"` only matter for SPECIFIC. `"premultiplied"` toggles whether `vips_premultiply` runs when the image `vips_image_hasalpha()`. `width`/`height` affect which branch of bounds-arithmetic is exercised (equal-to-source-size vs much-smaller) and, for ALL, are themselves overwritten.
**Evidence:**
> libvips/include/vips/conversion.h:98-106 (VipsInteresting enum): `VIPS_INTERESTING_NONE, _CENTRE, _ENTROPY, _ATTENTION, _LOW, _HIGH, _ALL, _SPECIFIC, _LAST`.
> libvips/conversion/smartcrop.c:359-410: the `switch (smartcrop->interesting)` block in `vips_smartcrop_build` with 7 distinct cases (NONE/LOW fall through to left=top=0; CENTRE computes `(Xsize-width)/2`; ENTROPY calls `vips_smartcrop_entropy`; ATTENTION calls `vips_smartcrop_attention`; SPECIFIC uses `VIPS_CLIP(0, interesting_x - width/2, Xsize - width)`; HIGH uses `Xsize - width`; ALL sets `left=top=0` and overwrites `smartcrop->width = in->Xsize` / `height = in->Ysize` with a `// FIXME: Invalidates operation cache` comment).
> libvips/conversion/smartcrop.c:353-357: `premultiplied` combined with `vips_image_hasalpha(in)` gates the `vips_premultiply` call.
> libvips/conversion/smartcrop.c:461-466 and 468-480: enum arg default `VIPS_INTERESTING_ATTENTION`; `interesting_x`/`interesting_y` default 0, only consumed at line 384-387 (SPECIFIC case).
> libvips/resample/thumbnail.c:1085-1090: real caller forwards `thumbnail->crop` as `"interesting"` and divides `interesting_x`/`interesting_y` by shrink factors before passing them — showing how the parameter is meant to be derived/transformed by a higher-level caller, not just a raw passthrough.

### P2.11 Input Expectations
**Claim:** `in` has no special pixel-format requirement enforced in `smartcrop.c` itself beyond what its own internal ops (`vips_extract_area`, `vips_hist_find`/`vips_hist_entropy`, `vips_resize`, `vips_colourspace`, `vips_conv`, `vips_extract_band`, `vips_multiply`, `vips_sum`, `vips_pow_const1`) accept; `width`/`height` must be in `[1, in->Xsize]`/`[1, in->Ysize]` respectively (checked explicitly), or build() fails with "bad extract area". No explicit header/magic/termination check is done by smartcrop on `in` — that responsibility belongs entirely to the loader that produced `in` (e.g. `vips_image_new_from_buffer`). Degenerate images (0 bands, 1x1, huge band count) are not separately rejected in `smartcrop.c` and flow directly into `pythagoras()`'s `in->Bands`-sized loops and the `32.0/in->Xsize` division in `vips_smartcrop_attention`.
**Evidence:**
> libvips/conversion/smartcrop.c:340-345: the only explicit input-size validation in smartcrop: `width > in->Xsize || height > in->Ysize || width <= 0 || height <= 0` -> error.
> libvips/conversion/smartcrop.c:226-227: `hscale = 32.0 / in->Xsize; vscale = 32.0 / in->Ysize;` — no guard against `in->Xsize`/`in->Ysize` being 0 inside smartcrop.c (relies on loader never producing a 0-sized image).
> libvips/conversion/smartcrop.c:183-198 (pythagoras): `vips_object_local_array(..., 2 * in->Bands + 1)` and `for (i = 0; i < in->Bands; i++)` — no cap on `in->Bands` inside smartcrop.c itself; behaviour for 0-band or very-high-band images depends entirely on the called sub-ops' own validation.
> libvips/conversion/smartcrop.c:122-124 (entropy): `max_slice_size = VIPS_MAX(ceil((width - smartcrop->width) / 8.0), ceil((height - smartcrop->height) / 8.0));` assumes `width>=smartcrop->width` is already true by construction from the earlier bounds check (build() validated at lines 340-345 before this function is ever invoked).

## Other entries: call protocol and combination with the target

### vips_image_new_from_buffer (source, loader)
**Claim:** Decodes an in-memory buffer into a new `VipsImage*`. No prior VIPS calls needed besides global init. Returns `NULL` on error (format-sniff failure, corrupt data); returns a valid, fully-built `VipsImage*` on success, ready to be used directly as the `in` argument to `vips_smartcrop`. Caller must `g_object_unref()` it eventually.
**Evidence:**
> libvips/include/vips/image.h:488-490: `VipsImage *vips_image_new_from_buffer(const void *buf, size_t len, const char *option_string, ...) G_GNUC_NULL_TERMINATED;`
> fuzz/thumbnail_fuzzer.cc:21-22: `if (!(image = vips_image_new_from_buffer(data, size, "", nullptr))) return 0;` — NULL-check pattern used by every project fuzzer; `option_string` is empty to let format auto-sniff from the buffer's magic bytes.
> fuzz/thumbnail_fuzzer.cc:24-29: dimension/band sanity-capping (`Xsize>100 || Ysize>100 || Bands>4`) is applied by the harness author as a defensive measure before passing the image into heavier ops — a pattern worth mirroring before calling `vips_smartcrop` to bound run time/memory, not something the library enforces itself.
**Combination:** Produces the `in` for `vips_smartcrop`; its return must be checked (NULL => skip/return); its result is unreffed together with smartcrop's output at the end of the harness.

### vips_image_new_from_file (source, loader variant)
**Claim:** Same contract as `vips_image_new_from_buffer` but takes a path and sniffs format from file content/extension; useful if the harness first dumps fuzz bytes to a temp file. Returns `NULL` on failure.
**Evidence:**
> libvips/include/vips/image.h:474-475: `VipsImage *vips_image_new_from_file(const char *name, ...) G_GNUC_NULL_TERMINATED;`
**Combination:** Alternative to `vips_image_new_from_buffer` for producing `in`; not used by the existing `smartcrop_fuzzer.cc` stub (which is currently an empty baseline, see Build notes) nor by `thumbnail_fuzzer.cc`/`sharpen_fuzzer.cc`, which use the buffer loader exclusively.

### vips_thumbnail / vips_thumbnail_image (consumer/real caller, higher-level pipeline)
**Claim:** `vips_thumbnail(filename, &out, width, ...)` loads-and-thumbnails from a path; `vips_thumbnail_image(in, &out, width, ...)` does the same from an already-loaded `VipsImage*`. Internally (`vips_thumbnail_build`, in `resample/thumbnail.c`) the pipeline is shrink -> crop -> **smartcrop** -> final resize, i.e. it is a real, in-tree production caller of `vips_smartcrop`, showing the correct surrounding sequence: load -> (optional) `vips_crop` -> `vips_smartcrop` -> read xoffset/yoffset -> `vips_thumbnail_crop_pages`. `"crop"` option (a `VipsInteresting` value) and `"interesting_x"`/`"interesting_y"` options on `vips_thumbnail*` are forwarded (after shrink-factor scaling) into the internal `vips_smartcrop` call as its `"interesting"`/`"interesting_x"`/`"interesting_y"` optional args.
**Evidence:**
> libvips/include/vips/resample.h:84-96: declarations of `vips_thumbnail`, `vips_thumbnail_buffer`, `vips_thumbnail_image`, `vips_thumbnail_source`.
> libvips/resample/thumbnail.c:1081-1091: `vips_crop(t[13], &t[18], 0, 0, in->Xsize, page_height, NULL)` then `vips_smartcrop(t[18], &t[19], crop_width, crop_height, "interesting", thumbnail->crop, "interesting_x", VIPS_ROUND_UINT(...), "interesting_y", VIPS_ROUND_UINT(...), NULL)`.
> fuzz/thumbnail_fuzzer.cc:21-39: real fuzz harness pattern — load via `vips_image_new_from_buffer`, call `vips_thumbnail_image(image, &out, 42, nullptr)`, check return, then `vips_avg(out, &d, nullptr)` to exercise the result, then unref both `out` and `image`. This is the project's existing template for combining a loader + a smartcrop-using high-level op in one harness; a `vips_smartcrop`-focused harness should follow the same load -> call -> consume -> unref shape but calling `vips_smartcrop` directly instead of `vips_thumbnail_image`, with `width`/`height`/`"interesting"`/`"interesting_x"`/`"interesting_y"`/`"premultiplied"` derived from fuzz input.
**Combination:** Not a prerequisite of `vips_smartcrop` — it is an alternative, higher-level entry that *uses* smartcrop internally. A harness that calls `vips_smartcrop` directly does not need to call `vips_thumbnail*` at all; they are mutually illustrative, not sequential prerequisites.

### vips_extract_area (consumer/companion, used internally and as documented complement)
**Claim:** `vips_extract_area(in, &out, left, top, width, height, NULL)` cuts a rectangular sub-image. It is both (a) used internally by every branch of `vips_smartcrop_build` to materialise the final crop, and (b) documented as the "see also" op for manual cropping of/around a smartcrop result. Requires `left>=0`, `top>=0`, `left+width<=in->Xsize`, `top+height<=in->Ysize` (validated by extract_area itself, not shown here but referenced from smartcrop.c's own internal use at line 417-421 with values already clamped by smartcrop's own arithmetic).
**Evidence:**
> libvips/include/vips/conversion.h:181-183: `int vips_extract_area(VipsImage *in, VipsImage **out, int left, int top, int width, int height, ...) G_GNUC_NULL_TERMINATED;`
> libvips/conversion/smartcrop.c:417-420: `vips_extract_area(smartcrop->in, &t[1], left, top, smartcrop->width, smartcrop->height, NULL) || vips_image_write(t[1], conversion->out)`.
> libvips/conversion/smartcrop.c:95: `vips_extract_area(in, &t[0], left, top, width, height, NULL)` inside `vips_smartcrop_score`, called repeatedly by the ENTROPY path.
> libvips/conversion/smartcrop.c:540-541 (doc comment): "::: seealso\n *     [method@Image.extract_area]." — explicit documented relationship.
**Combination:** A harness does not need to call `vips_extract_area` directly to exercise `vips_smartcrop` (it's invoked internally); it is optional as a post-processing step on the `*out` result (e.g., re-cropping) or to construct alternative test inputs.

### vips_premultiply (config path, conditionally invoked internally)
**Claim:** `vips_premultiply(in, &out, NULL)` premultiplies color channels by alpha. `vips_smartcrop_build` calls it automatically, with no harness action required, exactly when `vips_image_hasalpha(in)` is true and the `"premultiplied"` argument was not set to TRUE. A harness wanting to exercise this code path simply needs to supply/decode an image with an alpha channel and leave `"premultiplied"` at its default (FALSE) or explicitly pass FALSE; to skip it, pass `"premultiplied", TRUE` (meaning "treat the already-supplied pixel data as pre-multiplied", bypassing the internal call) or supply an alpha-less image.
**Evidence:**
> libvips/include/vips/conversion.h:335-336: `int vips_premultiply(VipsImage *in, VipsImage **out, ...) G_GNUC_NULL_TERMINATED;`
> libvips/conversion/smartcrop.c:353-357: `if (vips_image_hasalpha(in) && !smartcrop->premultiplied) { if (vips_premultiply(in, &t[0], NULL)) return -1; in = t[0]; }`.
> libvips/include/vips/image.h:549: `gboolean vips_image_hasalpha(VipsImage *image);` — the gating predicate.
**Combination:** Not called by the harness directly; its invocation is entirely data-driven (image has alpha) and flag-driven (`"premultiplied"` optional arg to `vips_smartcrop`). Including both alpha and non-alpha seed images, and toggling `"premultiplied"`, is the way a harness exercises/skips this path.

## Build notes

**Claim:** `vips_smartcrop` fuzz harnesses in this project are compiled as C++ (`.cc`) files against the libvips public headers (`<vips/vips.h>`, which transitively includes `<vips/conversion.h>` and `<vips/resample.h>` where the target and its API-surface functions are declared), linked statically against libvips and its bundled image-format dependencies, using Meson + the OSS-Fuzz libFuzzer engine. The existing `fuzz/smartcrop_fuzzer.cc` in this checkout is a non-functional "gold harness" stub (keeps the symbol linked via `__asm__` alias but never calls it) — it is NOT a real usage example and must be rewritten by the downstream harness generator.
**Evidence:**
> fuzz/smartcrop_fuzzer.cc:1-16: "/* Gold harness replaced by an empty stub for baseline generation. */" — confirms the current file is a stub, not a real caller; `LLVMFuzzerTestOneInput` ignores `data`/`size` and returns 0.
> fuzz/meson.build:17-23,30-36: `fuzz_progs = ['sharpen_fuzzer', 'thumbnail_fuzzer', 'smartcrop_fuzzer', 'mosaic_fuzzer', 'vips_fuzzer']` each built via `executable(fuzz_prog, fuzz_prog + '.cc', dependencies: [libvips_dep, fuzz_deps], link_args: fuzz_ldflags)` — i.e. compiled as C++ (`.cc`), linked against `libvips_dep` (the project's own pkg-config/meson dependency object exposing the public headers) and `fuzz_deps` (`libvips_deps` plus the fuzzing engine).
> fuzz/oss_fuzz_build.sh:258-266: `meson setup build --prefix=$WORK ... -Dfuzz=true -Dfuzzing_engine=oss-fuzz -Dfuzzer_ldflags="$LIB_FUZZING_ENGINE" ...` then `meson install -C build --tag devel` and `find build/fuzz -maxdepth 1 -executable -type f -exec cp -v '{}' $OUT \;` — shows the actual OSS-Fuzz build flow: Meson configures/builds libvips with fuzzing enabled, fuzz executables land in `build/fuzz/`, then are copied to `$OUT`.
> fuzz/oss_fuzz_build.sh:24-236: dependencies (zlib, libexif, lcms, aom, libheif, libjpeg-turbo, libpng, libwebp, libtiff, libjxl, libultrahdr, etc.) are each built as static libraries installed into `$WORK` before the libvips meson build, and `PKG_CONFIG_PATH="$WORK/lib/pkgconfig"` / `CPPFLAGS="-I$WORK/include"` make their headers/libs available to the libvips build and, transitively, to anything linking `libvips_dep`.
> fuzz/thumbnail_fuzzer.cc:3,16: `#include <vips/vips.h>` is the only project header needed; `vips_image_new_from_buffer`, `vips_thumbnail_image`, `g_object_unref` etc. are all reachable through this single umbrella header (which includes `vips/conversion.h` declaring `vips_smartcrop`, `vips_extract_area`, `vips_premultiply`, and `vips/resample.h` declaring `vips_thumbnail*`).