#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>

#include "allheaders.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  const l_int32 depth =
      stream.PickValueInArray<l_int32>({1, 2, 4, 8, 16, 32});

  const l_int32 pix_width = stream.ConsumeIntegralInRange<l_int32>(1, 256);
  const l_int32 pix_height = stream.ConsumeIntegralInRange<l_int32>(1, 256);

  PIX *pix = pixCreate(pix_width, pix_height, depth);
  if (pix == nullptr) {
    return 0;
  }

  if ((depth == 2 || depth == 4 || depth == 8) && stream.ConsumeBool()) {
    const l_int32 hasblack = stream.ConsumeIntegralInRange<l_int32>(0, 1);
    const l_int32 haswhite = stream.ConsumeIntegralInRange<l_int32>(0, 1);
    PIXCMAP *cmap = pixcmapCreateRandom(depth, hasblack, haswhite);
    if (cmap != nullptr) {
      pixSetColormap(pix, cmap);
    }
  }

  l_int32 box_w = stream.ConsumeIntegralInRange<l_int32>(1, 512);
  l_int32 box_h = stream.ConsumeIntegralInRange<l_int32>(1, 512);
  if (stream.ConsumeIntegralInRange<int>(0, 19) == 0) {
    if (stream.ConsumeBool()) {
      box_w = 0;
    } else {
      box_h = 0;
    }
  }

  const l_int32 box_x =
      stream.ConsumeIntegralInRange<l_int32>(-16, pix_width + 300);
  const l_int32 box_y =
      stream.ConsumeIntegralInRange<l_int32>(-16, pix_height + 300);

  const bool want_null_box = (stream.ConsumeIntegralInRange<int>(0, 19) == 0);

  BOX *box = nullptr;
  if (!want_null_box) {
    box = boxCreate(box_x, box_y, box_w, box_h);

  }

  const l_int32 width = stream.ConsumeIntegralInRange<l_int32>(-2, 10);

  l_int32 op;
  if (stream.ConsumeIntegralInRange<int>(0, 99) < 85) {
    op = stream.PickValueInArray<l_int32>(
        {L_SET_PIXELS, L_CLEAR_PIXELS, L_FLIP_PIXELS});
  } else {
    op = stream.ConsumeIntegral<l_int32>();
  }

  pixRenderBox(pix, box, width, op);

  if (box != nullptr) {
    boxDestroy(&box);
  }
  pixDestroy(&pix);

  return 0;
}