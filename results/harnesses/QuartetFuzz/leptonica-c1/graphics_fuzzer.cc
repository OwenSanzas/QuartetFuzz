#include "leptfuzz.h"

#include <string.h>

static l_int32 NextI32(const uint8_t *data, size_t size, size_t *pos) {
  l_uint32 v = 0;
  for (int i = 0; i < 4; i++) {
    uint8_t b = (*pos < size) ? data[(*pos)++] : 0;
    v |= ((l_uint32)b) << (8 * i);
  }
  return (l_int32)v;
}

static uint8_t NextByte(const uint8_t *data, size_t size, size_t *pos) {
  if (*pos < size) return data[(*pos)++];
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 16) return 0;

  leptSetStdNullHandler();

  size_t pos = 0;

  static const l_int32 kDepths[6] = {1, 2, 4, 8, 16, 32};
  l_int32 depth = kDepths[NextByte(data, size, &pos) % 6];
  l_int32 w = 1 + (NextByte(data, size, &pos) % 200);
  l_int32 h = 1 + (NextByte(data, size, &pos) % 200);

  PIX *pix = pixCreate(w, h, depth);
  if (!pix) return 0;

  bool want_cmap = (NextByte(data, size, &pos) & 1) != 0;
  if (want_cmap && depth <= 8) {
    PIXCMAP *cmap = pixcmapCreateRandom(depth, 0, 1);
    if (cmap) pixSetColormap(pix, cmap);
  }

  l_int32 bx = NextI32(data, size, &pos);
  l_int32 by = NextI32(data, size, &pos);
  l_int32 bw = 1 + (NextByte(data, size, &pos) % 300);
  l_int32 bh = 1 + (NextByte(data, size, &pos) % 300);
  BOX *box = boxCreate(bx, by, bw, bh);

  l_int32 bx2 = NextI32(data, size, &pos);
  l_int32 by2 = NextI32(data, size, &pos);
  l_int32 bw2 = 1 + (NextByte(data, size, &pos) % 300);
  l_int32 bh2 = 1 + (NextByte(data, size, &pos) % 300);

  char boxa_text[512];
  snprintf(boxa_text, sizeof(boxa_text),
           "\nBoxa Version 2\n"
           "Number of boxes = 2\n"
           "  Box[0]: x = %d, y = %d, w = %d, h = %d\n"
           "  Box[1]: x = %d, y = %d, w = %d, h = %d\n",
           bx, by, bw, bh, bx2, by2, bw2, bh2);
  BOXA *boxa = boxaReadMem((const l_uint8 *)boxa_text, strlen(boxa_text));

  static const l_int32 kOps[3] = {L_SET_PIXELS, L_CLEAR_PIXELS, L_FLIP_PIXELS};
  l_int32 op = kOps[NextByte(data, size, &pos) % 3];
  l_int32 width = (l_int32)(int8_t)NextByte(data, size, &pos);
  l_uint8 rval = NextByte(data, size, &pos);
  l_uint8 gval = NextByte(data, size, &pos);
  l_uint8 bval = NextByte(data, size, &pos);
  l_float32 fract = (float)(NextByte(data, size, &pos)) / 255.0f;
  static const l_int32 kOrients[4] = {L_HORIZONTAL_LINE, L_POS_SLOPE_LINE,
                                       L_VERTICAL_LINE, L_NEG_SLOPE_LINE};
  l_int32 orient = kOrients[NextByte(data, size, &pos) % 4];
  l_int32 spacing = 2 + (NextByte(data, size, &pos) % 20);
  l_int32 outline = NextByte(data, size, &pos) & 1;
  l_int32 removedups = NextByte(data, size, &pos) & 1;
  l_int32 connectivity = (NextByte(data, size, &pos) & 1) ? 8 : 4;

  if (box) {
    (void)pixRenderBox(pix, box, width, op);
  }

  if (box) {
    (void)pixRenderBoxArb(pix, box, width, rval, gval, bval);
  }

  PIX *pix32 = pixCreate(w, h, 32);
  if (pix32 && box) {
    (void)pixRenderBoxBlend(pix32, box, width, rval, gval, bval, fract);
  }

  if (boxa) {
    (void)pixRenderBoxa(pix, boxa, width, op);
    (void)pixRenderBoxaArb(pix, boxa, width, rval, gval, bval);
    if (pix32)
      (void)pixRenderBoxaBlend(pix32, boxa, width, rval, gval, bval, fract,
                                removedups);
  }

  if (box) {
    (void)pixRenderHashBox(pix, box, spacing, width, orient, outline, op);
    (void)pixRenderHashBoxArb(pix, box, spacing, width, orient, outline, rval,
                               gval, bval);
  }

  if (pixGetDepth(pix) == 1) {
    PIX *pixFilled = pixFillClosedBorders(pix, connectivity);
    if (pixFilled) {
      l_uint8 *fdata = NULL;
      size_t fsize = 0;
      if (pixWriteMemBmp(&fdata, &fsize, pixFilled) == 0 && fdata) {
        lept_free(fdata);
      }
      pixDestroy(&pixFilled);
    }
  }

  l_uint8 *encdata = NULL;
  size_t encsize = 0;
  if (pixWriteMemBmp(&encdata, &encsize, pix) == 0 && encdata) {
    lept_free(encdata);
  }

  encdata = NULL;
  encsize = 0;
  if (pix32 && pixWriteMemBmp(&encdata, &encsize, pix32) == 0 && encdata) {
    lept_free(encdata);
  }

  boxDestroy(&box);
  boxaDestroy(&boxa);
  pixDestroy(&pix);
  pixDestroy(&pix32);

  return 0;
}
