## target: pixRenderBox

### P2.1 Init Sequence
**Claim:** Before calling `pixRenderBox(pix, box, width, op)` a `PIX` must exist (via `pixCreate()`/`pixRead()` or similar) and a `BOX` must exist (via `boxCreate()` or obtained from a `BOXA`, e.g. via `boxaReadMem()`/`boxaRead()`/`boxaGetBox()`). No other library-wide init call is required; `pixRenderBox` itself does all needed validation internally and short-circuits on bad input (no crash).
**Evidence:**
> src/graphics.c:1571-1594 `pixRenderBox(PIX *pix, BOX *box, l_int32 width, l_int32 op)` begins with `if (!pix) return ERROR_INT(...)`, `if (!box) return ERROR_INT(...)` — so pix/box must already be valid, non-NULL objects when the function is entered.
> prog/compfilter_reg.c:78-91: `pixs = pixCreate(200,200,1); box1 = boxCreate(10,10,20,30); ... pixRenderBox(pixs, box1, 1, L_SET_PIXELS);` — real caller builds PIX and BOX before calling the target.
> prog/graphicstest.c:55-78: `pixs = pixRead(filein); ... box1 = boxCreate(70,80,300,245); ... pixRenderBoxArb(pix1, box1, 3, 200,200,25);` (sibling entry) shows pix obtained from file decode before rendering.

### P2.2 Parameter Construction
**Claim:**
- `pix`: non-NULL `PIX*` of any depth (1/2/4/8/16/32 bpp), **must not be colormapped** (checked downstream in `pixRenderPta`, not in `pixRenderBox` itself).
- `box`: non-NULL `BOX*`; its `w` and `h` fields must be nonzero (checked inside `generatePtaBox`), x/y may be negative/out-of-range — out-of-range points are silently clipped later.
- `width`: `l_int32`, intended >=1; if `<1` it is **not rejected**, just clamped to 1 with an `L_WARNING`.
- `op`: must be exactly one of `L_SET_PIXELS`, `L_CLEAR_PIXELS`, `L_FLIP_PIXELS`; anything else is a hard error (`ERROR_INT("invalid op", ...)`, return 1).
**Evidence:**
> src/graphics.c:1578-1587: `if (!pix) ...; if (!box) ...; if (width < 1) { L_WARNING("width < 1; setting to 1\n", __func__); width = 1; } if (op != L_SET_PIXELS && op != L_CLEAR_PIXELS && op != L_FLIP_PIXELS) return ERROR_INT("invalid op", __func__, 1);`
> src/graphics.c:286-288 (`generatePtaBox`): `boxGetGeometry(box, &x, &y, &w, &h); if (w == 0 || h == 0) return (PTA *)ERROR_PTR("box has w = 0 or h = 0", __func__, NULL);` — degenerate boxes make `pixRenderBox` return 1 ("pta not made").
> src/graphics.c:1241-1242 (`pixRenderPta`, called by `pixRenderBox`): `if (pixGetColormap(pix)) return ERROR_INT("pix is colormapped", __func__, 1);` — colormapped pix rejected only at this deeper layer, not in `pixRenderBox` directly.
> src/boxbasic.c:178-181 (`boxCreate`): `if (w < 0 || h < 0) return (BOX *)ERROR_PTR("w and h not both >= 0", __func__, NULL);` — negative w/h rejected at construction time; x<0 is accepted and adjusted ("take part in +quad").

### P2.3 Object Lifecycle
**Claim:** Pattern is create PIX → create/obtain BOX (optionally via BOXA) → call `pixRenderBox` (which internally does PTA create/use/destroy, invisible to caller) → use/write the mutated `pix` → destroy `pix` and `box`/`boxa`. `pixRenderBox` does not take ownership of `box` or `pix`; both must be destroyed by the caller afterward.
**Evidence:**
> src/graphics.c:1589-1592: `pta = generatePtaBox(box, width); ... pixRenderPta(pix, pta, op); ptaDestroy(&pta);` — internal PTA lifecycle is fully self-contained inside the target; caller never sees the PTA.
> prog/compfilter_reg.c:78-93: `pixs = pixCreate(...); box1..box4 = boxCreate(...); boxa1 = boxaCreate(4); boxaAddBox(boxa1, box1, L_INSERT); ... pixRenderBox(pixs, box1, 1, L_SET_PIXELS); ... pix1 = pixFillClosedBorders(pixs, 4);` then later presumably `boxaDestroy`/`pixDestroy` at teardown — box1 is still usable after `boxaAddBox(..., L_INSERT)` because render happens on the same pointer before the boxa (and hence box) is destroyed.
> src/boxbasic.c:272-283 (`boxDestroy`) and similarly `boxaDestroy` (src/boxbasic.c:522-...): decrement refcount/free — called by the harness after rendering, not by the target.

### P2.4 Return Value Handling
**Claim:** `pixRenderBox` returns `0` on success and `1` on error (invalid pix/box/op, or if `generatePtaBox` fails e.g. degenerate box). Callers should check for nonzero return and treat it as "no pixels rendered" (not a crash); the mutated `pix` buffer is unchanged in that case except possibly a warning printed. All sibling render functions follow the same `l_ok` (0 good / 1 bad) convention.
**Evidence:**
> src/graphics.c:1568: `* \return  0 if OK, 1 on error` (pixRenderBox doc comment).
> src/graphics.c:1589-1591: on `generatePtaBox` failure returns 1 before ever calling `pixRenderPta` — so pix buffer untouched.
> No caller in the tree checks pixRenderBox's return value explicitly (prog/compfilter_reg.c:88-91, prog/sorttest.c:68, prog/cctest1.c:104 all call it unconditionally); harness should still check it for P2 compliance even though reference code is lax.

### P2.5 Cleanup Sequence
**Claim:** On both success and error paths from `pixRenderBox`, the only cleanup the *caller* must do is eventually: destroy `box` (`boxDestroy(&box)` or indirectly via `boxaDestroy(&boxa)` if the box was added with `L_INSERT`) and destroy `pix` (`pixDestroy(&pix)`). The target's own internal PTA is destroyed unconditionally inside the function (`ptaDestroy(&pta)` at src/graphics.c:1592) before return, on the success path; on error paths before PTA creation nothing needs freeing.
**Evidence:**
> src/graphics.c:1589-1593: PTA created, used, destroyed, all inside the target — no PTA leak possible from caller's perspective.
> src/boxbasic.c:272-283 `boxDestroy` / src/boxbasic.c:522 `boxaDestroy`: standard refcounted destroy, null the pointer.
> src/pix1.c (pixDestroy not shown above but standard leptonica pattern) — used throughout e.g. prog/graphicstest.c teardown of pix1/pixs.

### P2.6 API Existence
**Claim:** All functions named below exist in the current source tree (src/graphics.c, src/boxbasic.c, src/pix1.c, src/pix2.c) and are declared in `src/allheaders.h`.
**Evidence:**
> src/allheaders.h:979 `pixRenderBox` declaration (given).
> src/graphics.c:1571,1607,1645,1682,1718,1757,1798,1846 — definitions of `pixRenderBox`, `pixRenderBoxArb`, `pixRenderBoxBlend`, `pixRenderBoxa`, `pixRenderBoxaArb`, `pixRenderBoxaBlend`, `pixRenderHashBox`, `pixRenderHashBoxArb` all found and confirmed to exist (not fabricated).
> src/graphics.c:1233,1320,1393 — `pixRenderPta`, `pixRenderPtaArb`, `pixRenderPtaBlend` exist.
> src/graphics.c:272,347,204,402 — `generatePtaBox`, `generatePtaBoxa`, `generatePtaWideLine`, `generatePtaHashBox` exist.
> src/pix2.c:263,530,590 — `pixSetPixel`, `pixClearPixel`, `pixFlipPixel` exist.
> src/boxbasic.c:2006,2038,2086 — `boxaRead`, `boxaReadStream`, `boxaReadMem` exist.
> Note: `pixRenderHashBoxBlend` is referenced in the feature description; it also exists (src/graphics.c:~1881 header comment "pixRenderHashBoxBlend()" seen in prior grep output; not re-verified line-by-line here but grep match confirms its presence in the file's rendering section).

### P2.7 Co-call Constraints
**Claim:** No lock/unlock or begin/commit pairing exists for `pixRenderBox` itself. The only "paired" constraints are the generic Leptonica create/destroy pairs: `pixCreate`/`pixDestroy`, `boxCreate`/`boxDestroy`, `boxaCreate`/`boxaDestroy`, and internally (invisible to the harness) `ptaCreate`/`ptaDestroy` used by the target and its helpers.
**Evidence:**
> src/graphics.c:289,1592 (`ptad = ptaCreate(0); ... ptaDestroy(&pta);`) — internal pairing, not caller-visible.
> src/boxbasic.c:170-190 (`boxCreate`) paired with :272-283 (`boxDestroy`).
> No mutex/transaction APIs found in src/graphics.c (grep of file shows no lock/unlock calls).

### P2.8 Prerequisite State
**Claim:** The only prerequisite state is that `pix` have valid, allocated pixel data of a supported depth (1/2/4/8/16/32 — checked via `pixGetDimensions`/switch in `pixRenderPta`) and not be colormapped, and that `box` have nonzero width/height. No global/context object, no prior "open connection" or file handle is required.
**Evidence:**
> src/graphics.c:1248-1269 (`pixRenderPta`): `pixGetDimensions(pix, &w, &h, &d); ... switch(d) { case 2: maxval=0x3; ... }` — only depths 2/4/8/16/32 get special maxval; d==1 falls through default maxval=1, and other depths (e.g. invalid ones) are structurally impossible since PIX only supports 1/2/4/8/16/32 by construction (`pixCreate`).
> src/graphics.c:287-288 (`generatePtaBox`) — box w/h==0 rejected before any pixel write.

### P2.9 Result Usage
**Claim:** `pixRenderBox` (and its siblings) mutate `pix` in place and return only a status code — there is no separate "result object" to pass onward. Real callers pass the *same* `pix` pointer onward to further Leptonica APIs (file writers, more drawing calls, format converters) after rendering, and eventually destroy it.
**Evidence:**
> prog/compfilter_reg.c:88-93: after four `pixRenderBox(pixs, boxN, 1, L_SET_PIXELS)` calls, `pix1 = pixFillClosedBorders(pixs, 4); regTestWritePixAndCheck(rp, pix1, IFF_PNG);` — the rendered `pixs` is fed into `pixFillClosedBorders()` then written out.
> prog/sorttest.c:68 `pixRenderBox(pixs, box, 2, L_FLIP_PIXELS);` followed (pattern in that file) by `pixWrite`/display calls on `pixs` (standard reg-test idiom).
> prog/cctest1.c:104 `pixRenderBox(pixs, box, 3, L_FLIP_PIXELS);` similarly feeds the same `pixs` to later processing/output in that test driver.
> prog/graphicstest.c:78-87: `pixRenderBoxArb(pix1, box1, 3, 200,200,25); pixRenderBoxaBlend(pix1, boxa, 17, 200,200,25,0.4,1); pta = ptaCreate(5); ... pixRenderPolylineBlend(pix1, pta, ...); pixWrite(fileout, pix1, ...)` — the same `pix1` is repeatedly rendered onto by multiple render-family entries, then written to a file at the end — showing that "result usage" of a render call is simply "pass pix further / render more / write".

### P2.10 Behaviour Controls
**Claim:**
- `op` (`L_SET_PIXELS`/`L_CLEAR_PIXELS`/`L_FLIP_PIXELS`) selects `pixSetPixel`/`pixClearPixel`/`pixFlipPixel` in `pixRenderPta`'s per-point switch.
- `width` controls the odd/even-width branch in `generatePtaBox` (src/graphics.c:290 vs 307) and how many offset lines `generatePtaWideLine` emits.
- Depth of `pix` (`d` from `pixGetDimensions`) selects `maxval` for `L_SET_PIXELS` (0x3/0xf/0xff/0xffff/0xffffffff) in `pixRenderPta` (src/graphics.c:1249-1268), and in `pixRenderPtaArb` selects averaging-vs-direct-RGB logic (src/graphics.c:1335-1371).
- Colormap presence on `pix` selects, in `pixRenderPtaArb`, the `pixcmapAddNearestColor` branch vs. plain averaging (src/graphics.c:1344-1357); plain `pixRenderPta`/`pixRenderBox` instead *rejects* colormapped pix outright.
- For hash-box siblings, `orient` (`L_HORIZONTAL_LINE`/`L_VERTICAL_LINE`/`L_POS_SLOPE_LINE`/`L_NEG_SLOPE_LINE`) selects the line-generation branch in `generatePtaHashBox` (src/graphics.c:430-460ish), `spacing` (must be >1) controls line density, `outline` (0/1) toggles whether the box border itself is also drawn.
- For Blend variants, `fract` in [0,1] controls blend weight in `pixRenderPtaBlend` (clamped/defaulted to 0.5 if out of range, src/graphics.c:1411-1414); for `pixRenderBoxaBlend`, `removedups` (0/1) controls whether `generatePtaBoxa` dedups points.
**Evidence:**
> src/graphics.c:1250-1292 (`pixRenderPta` op switch and depth-based maxval).
> src/graphics.c:290-324 (`generatePtaBox` odd/even width branch).
> src/graphics.c:1336-1371 (`pixRenderPtaArb` depth/cmap branches).
> src/graphics.c:1818-1820 (`pixRenderHashBox` orient validation) and src/graphics.c:434-448 (`generatePtaHashBox` horizontal/vertical loops using `spacing`).
> src/graphics.c:1411-1414 (`pixRenderPtaBlend` fract clamp/default).
> src/graphics.c:1777 (`generatePtaBoxa(boxa, width, removedups)` call inside `pixRenderBoxaBlend`).

### P2.11 Input Expectations
**Claim:** `pixRenderBox` itself expects purely in-memory numeric objects — no file format/header/magic for `pix`/`box` directly. Coordinates can be negative (clipped later in `pixRenderPta`/`pixRenderPtaArb`/`pixRenderPtaBlend` against pix w/h) but `box` width/height must be nonzero (checked). If box geometry instead comes from the text deserializer `boxaReadMem()`/`boxaReadStream()`, that function expects **Leptonica's own ASCII BOXA format**: `"\nBoxa Version %d\n"` matching `BOXA_VERSION_NUMBER`, then `"Number of boxes = %d\n"` (`n`, bounded `0 <= n <= MaxBoxaPtrArraySize`), then exactly `n` lines of the form `"  Box[%d]: x = %d, y = %d, w = %d, h = %d\n"`. Any parse failure anywhere returns `NULL` (boxa destroyed on mid-stream failure) — not a crash.
**Evidence:**
> src/boxbasic.c:2048-2073 (`boxaReadStream`): exact `fscanf` format strings as quoted above; `if (n < 0) ... ; if (n > MaxBoxaPtrArraySize) ...` bounds; per-box `fscanf(... "  Box[%d]: x = %d, y = %d, w = %d, h = %d\n" ...)`.
> src/boxbasic.c:2086-2101 (`boxaReadMem`): wraps `boxaReadStream` via `fopenReadFromMemory(data, size)` — confirms it is the from-memory deserializer of the BOXA object (matches "boxaReadMem" named in the feature description; this is the only from-memory BOX/BOXA deserializer found).
> src/graphics.c:287-288 (`generatePtaBox`): `if (w == 0 || h == 0) return ... NULL` — zero-size box rejected; no other size limit is enforced in `pixRenderBox`'s own code (large/negative coordinates are allowed and later clipped, not rejected).
> src/graphics.c:1274-1277 (`pixRenderPta`): `if (x < 0 || x >= w) continue; if (y < 0 || y >= h) continue;` — clipping, confirming out-of-bounds points are silently skipped rather than causing errors.

## Other entries of the Logic Group

### pixRenderBoxArb
**Claim:** Same preconditions as pixRenderBox except pix **may be colormapped** (handled explicitly). Params: `pix, box, width, rval, gval, bval` (rval/gval/bval are `l_uint8`, no range check needed—full byte range always valid). Returns 0/1. No separate "op" since it always performs an RGB/cmap-color set. Combines with target by being called on the *same* pix/box pair as an alternate "arbitrary color" variant; does not need `pixRenderBox` to run first/after—independent call, same lifecycle (box/pix created before, destroyed after).
**Evidence:**
> src/graphics.c:1607-1630: `if (!pix) ...; if (!box) ...; if (width<1) {warn; width=1;} pta=generatePtaBox(box,width); pixRenderPtaArb(pix,pta,rval,gval,bval); ptaDestroy(&pta);` — no op validation, no colormap rejection (unlike pixRenderBox).
> src/graphics.c:1344-1357 (`pixRenderPtaArb`) — cmap branch (`pixcmapAddNearestColor`) exercised only when `pixGetColormap(pix)` is non-NULL.
> prog/graphicstest.c:78 `pixRenderBoxArb(pix1, box1, 3, 200, 200, 25);` real usage.

### pixRenderBoxBlend
**Claim:** Requires `pix` to be **32bpp RGB** (checked downstream in `pixRenderPtaBlend`, not in pixRenderBoxBlend itself). Params add `fract` (l_float32, expected in [0,1]; clamped/defaulted to 0.5 with warning if out of range). Returns 0/1. Must not be colormapped practically (32bpp pix in Leptonica is not colormapped by convention, but the code doesn't explicitly check).
**Evidence:**
> src/graphics.c:1645-1669: body mirrors pixRenderBox but ends `pixRenderPtaBlend(pix, pta, rval, gval, bval, fract);`
> src/graphics.c:1409-1414 (`pixRenderPtaBlend`): `if (pixGetDepth(pix) != 32) return ERROR_INT("depth not 32 bpp", ...); if (fract<0.0 || fract>1.0) { L_WARNING(...); fract = 0.5; }`
> prog/graphicstest.c:66-69 uses `pixRenderLineBlend` (sibling family) with fract 0.3-0.5, same pattern as BoxBlend variants.

### pixRenderBoxa / pixRenderBoxaArb / pixRenderBoxaBlend
**Claim:** Same as single-box variants but take a `BOXA*` and internally loop via `generatePtaBoxa(boxa, width, removedups)` (removedups fixed at 0 for Boxa/BoxaArb, caller-settable for BoxaBlend). `boxa` must be non-NULL; empty boxa (n==0) is legal per `boxaReadStream` doc note but downstream behavior of `generatePtaBoxa` on an empty boxa was not traced further than "loops n times" (n=0 ⇒ empty pta, likely harmless no-op render). Returns 0/1 same convention.
**Evidence:**
> src/graphics.c:1682-1705 (`pixRenderBoxa`): `if (!boxa) ...; pta=generatePtaBoxa(boxa,width,0); pixRenderPta(pix,pta,op);`
> src/graphics.c:1718-1741 (`pixRenderBoxaArb`), src/graphics.c:1757-1782 (`pixRenderBoxaBlend`, with caller-supplied `removedups`).
> src/boxbasic.c:2034 doc note: "It is OK for the boxa to be empty (n == 0)."
> prog/graphicstest.c:74-79: `boxa=boxaCreate(3); boxaAddBox(boxa,box2,L_INSERT); ...; pixRenderBoxaBlend(pix1, boxa, 17, 200,200,25,0.4,1);` real usage combining boxCreate/boxaCreate/boxaAddBox before the render call.

### pixRenderHashBox / pixRenderHashBoxArb
**Claim:** Single-box siblings adding `spacing` (must be `>1`, else hard error "spacing not > 1"), `orient` (one of the 4 line-orientation constants, else hard error "invalid line orientation"), `outline` (0/1, whether box border itself is drawn in addition to the hash lines). `pixRenderHashBox` additionally validates `op`; `pixRenderHashBoxArb` does not (same asymmetry as Box vs BoxArb). Both call `generatePtaHashBox(box, spacing, width, orient, outline)` then the respective `pixRenderPta`/`pixRenderPtaArb`.
**Evidence:**
> src/graphics.c:1808-1830 (`pixRenderHashBox`): `if (spacing<=1) return ERROR_INT("spacing not > 1",...); ...; if (orient!=L_HORIZONTAL_LINE && orient!=L_POS_SLOPE_LINE && orient!=L_VERTICAL_LINE && orient!=L_NEG_SLOPE_LINE) return ERROR_INT("invalid line orientation",...); if (op!=L_SET_PIXELS && ...) return ERROR_INT("invalid op",...);`
> src/graphics.c:1858-1878 (`pixRenderHashBoxArb`): same spacing/orient checks, no op check (consistent with Arb pattern).
> prog/compfilter_reg.c:95-98: `pixRenderHashBox(pix2, box1, 6, 4, L_POS_SLOPE_LINE, 1, L_SET_PIXELS); pixRenderHashBox(pix2, box2, 7, 2, L_POS_SLOPE_LINE, 1, L_SET_PIXELS); pixRenderHashBox(pix2, box3, 4, 2, L_VERTICAL_LINE, 1, L_SET_PIXELS); pixRenderHashBox(pix2, box4, 3, 1, L_HORIZONTAL_LINE, 1, L_SET_PIXELS);` — real usage varying spacing/width/orient on a fresh `pix2 = pixCreateTemplate(pixs)`.

### boxaReadMem
**Claim:** Entry that deserializes a `BOXA` from an in-memory ASCII buffer. Takes `(const l_uint8 *data, size_t size)`; returns `BOXA*` or `NULL`. Caller must `boxaDestroy(&boxa)` when done. Produced boxes can then be passed to `pixRenderBox`-family functions one at a time via `boxaGetBox()`, or directly to the `*Boxa*` variants.
**Evidence:**
> src/boxbasic.c:2085-2101: full body quoted above; uses `fopenReadFromMemory` + `boxaReadStream`.
> prog/fuzzing/boxfunc3_fuzzer.cc:10-11: `boxa_payload = boxaReadMem(data, size); if (boxa_payload == NULL) return 0;` — real fuzz-driver usage, confirming the standard null-check pattern; ends with `boxaDestroy(&boxa_payload);` at line 105.
> Note: this existing fuzzer (`boxfunc3_fuzzer.cc`) does **not** call any `pixRenderBox*` function — it exercises `boxaaDisplay`, `pixBlendBoxaRandom`, `pixDrawBoxa`, `pixMaskConnComp`, `pixPaintBoxa`, `pixPaintBoxaRandom` instead. There is **no existing fuzzer in the tree that calls `pixRenderBox` or any of its siblings** — `prog/fuzzing/graphics_fuzzer.cc` is only an empty stub that keeps the symbol linked (see Build notes).

## Correction to the Logic Group description

- The description states boxaReadStream's per-box line format as `"x = .., y = .., w = .., h = .."`; the **actual** fscanf format string is `"  Box[%d]: x = %d, y = %d, w = %d, h = %d\n"` (src/boxbasic.c:2063-2064), i.e. each line is prefixed with `Box[<index>]:` and two leading spaces, and preceded by a mandatory header `"\nBoxa Version %d\n"` (must equal `BOXA_VERSION_NUMBER`) and `"Number of boxes = %d\n"` (src/boxbasic.c:2048-2052). A harness constructing a seed for `boxaReadMem` must reproduce this exact grammar or the call returns NULL immediately.
- The description says "width<1 ... silently clamped to 1, not rejected" for all variants — confirmed true for pixRenderBox/BoxArb/BoxBlend/Boxa*/HashBox*, consistently (src/graphics.c:1582-1585, 1620-1623, 1659-1662, 1693-1696, 1731-1734, 1772-1775, 1814-1817, 1864-1867).
- The description says "spacing<=1 for hash variants" is rejected — confirmed exactly (`spacing <= 1` check, src/graphics.c:1812, 1862). Note `pixRenderHashBoxBlend` (not re-read line-by-line but present in file per earlier grep at src/graphics.c:1881 docblock) should follow the same pattern; not independently re-verified here beyond its listing in the function-index comment (src/graphics.c:38 `PTA *generatePtaHashBox()`, and function list at top of file includes pixRenderHashBoxBlend per the earlier broad grep of "pixRenderBox|pixRenderPta|generatePtaBox" which matched docblock headers through line ~70).
- **No existing project fuzzer actually calls pixRenderBox or siblings.** `prog/fuzzing/graphics_fuzzer.cc` is confirmed (by reading it) to be a stub: `QF_EXTERN void qf_target_0(void) __asm__("pixRenderBox"); ... LLVMFuzzerTestOneInput(...) { return 0; }` — it keeps the symbol linked but performs no calls at all. This means the "realistic combinations" cited by the Logic Group description (prog/graphicstest.c, prog/compfilter_reg.c, prog/sorttest.c, prog/cctest1.c) are the *only* real evidence of correct call sequences; there is no fuzzer-specific seed corpus or dictionary file targeting pixRenderBox specifically.
- No allocation-failure-injection hook or SIMD-disable hook found anywhere in `prog/fuzzing/` (confirmed by reading `leptfuzz.h`, which only provides `leptSetStdNullHandler()` to silence stderr via `leptSetStderrHandler(send_to_devnull)`).

## Build notes

**Claim:** Fuzzers for this project (including a `pixRenderBox`-targeting one, should the harness generator add real calls) are compiled as **C++11**, including `allheaders.h` via `-I"$WORK/include"` (the installed Leptonica headers after `make install`), and statically linked against `libleptonica.a` plus all its third-party codec dependencies (`libtiff.a`, `libwebp.a`, `libpng.a`, `libjpeg.a`, `libjbig.a`, `libzstd.a`, `libz.a`, `libsharpyuv.a`) and `$LIB_FUZZING_ENGINE`.
**Evidence:**
> ossfuzz_view/leptonica-c1/build.sh:18: `$SRC/leptonica/prog/fuzzing/oss-fuzz-build.sh` — delegates entirely to the in-tree build script.
> prog/fuzzing/oss-fuzz-build.sh:80-93: leptonica itself is configured/built with `./configure --enable-static --disable-shared --with-libpng --with-zlib --with-jpeg --with-libwebp --with-libtiff --prefix="$WORK" ... CPPFLAGS="-I$WORK/include -DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION"` then `make install` (installs headers, incl. `allheaders.h`, to `$WORK/include`).
> prog/fuzzing/oss-fuzz-build.sh:95-110:
> ```
> for f in $SRC/leptonica/prog/fuzzing/*_fuzzer.cc; do
>   fuzzer=$(basename "$f" _fuzzer.cc)
>   $CXX $CXXFLAGS -std=c++11 -I"$WORK/include" \
>     $SRC/leptonica/prog/fuzzing/${fuzzer}_fuzzer.cc -o $OUT/${fuzzer}_fuzzer \
>     -Isrc/ \
>     "$WORK/lib/libleptonica.a" "$WORK/lib/libtiff.a" "$WORK/lib/libwebp.a" \
>     "$WORK/lib/libpng.a" "$WORK/lib/libjpeg.a" "$WORK/lib/libjbig.a" \
>     "$WORK/lib/libzstd.a" "$WORK/lib/libz.a" "$WORK/lib/libsharpyuv.a" \
>     $LIB_FUZZING_ENGINE
> done
> ```
> — every `*_fuzzer.cc` under `prog/fuzzing/` (including `graphics_fuzzer.cc`, the current placeholder for pixRenderBox) is compiled this way: C++, `#include "allheaders.h"` resolved via `-I"$WORK/include"` (see `prog/fuzzing/leptfuzz.h:6` `#include "allheaders.h"`), linked statically against the full dependency chain plus the fuzzing engine.
> prog/fuzzing/graphics_fuzzer.cc:1-16: current stub confirms `#include <stddef.h>`/`<stdint.h>` only, no `allheaders.h` — i.e. **no real `pixRenderBox` harness exists yet**; a new harness should `#include "leptfuzz.h"` (as other real fuzzers like `boxfunc3_fuzzer.cc:1` do) to get `allheaders.h` and `leptSetStdNullHandler()`.
> prog/fuzzing/oss-fuzz-build.sh:113+ : seed-corpus copying block uses `general_corpus.zip` for several fuzzers (`adaptmap_fuzzer`, `affine_fuzzer`, `baseline_fuzzer`, etc.) — no entry for `graphics_fuzzer` was found in the truncated view; if a seed corpus is desired it would need to be added analogously, pointing at JPEG/PNG sample images (same `general_corpus.zip` convention) plus/with a serialized BOXA text sample for `boxaReadMem` driven geometry.