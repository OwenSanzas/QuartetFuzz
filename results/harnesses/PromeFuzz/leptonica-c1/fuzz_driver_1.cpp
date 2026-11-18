#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "allheaders.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size < 16) {
        return 0;
    }

    l_int32 width = static_cast<l_int32>(Data[0] % 50);
    l_int32 height = static_cast<l_int32>(Data[1] % 50);
    l_int32 depth_choice = Data[2] % 4;
    l_int32 depth = 1;
    switch (depth_choice) {
        case 0: depth = 1; break;
        case 1: depth = 8; break;
        case 2: depth = 32; break;
        default: depth = 1; break;
    }

    l_int32 box_x = static_cast<l_int32>(Data[3] % 100) - 25;
    l_int32 box_y = static_cast<l_int32>(Data[4] % 100) - 25;
    l_int32 box_w = static_cast<l_int32>(Data[5] % 100);
    l_int32 box_h = static_cast<l_int32>(Data[6] % 100);

    l_int32 render_width = static_cast<l_int32>(Data[7] % 10);
    l_int32 op_choice = Data[8] % 3;
    l_int32 op = L_SET_PIXELS;
    switch (op_choice) {
        case 0: op = L_SET_PIXELS; break;
        case 1: op = L_CLEAR_PIXELS; break;
        case 2: op = L_FLIP_PIXELS; break;
        default: op = L_SET_PIXELS; break;
    }

    if (width <= 0) width = 10;
    if (height <= 0) height = 10;

    PIX *pix = pixCreate(width, height, depth);
    if (pix == nullptr) {
        return 0;
    }

    if (Size > 16) {
        l_uint32 *data = pixGetData(pix);
        l_int32 wpl = pixGetWpl(pix);
        size_t data_size = static_cast<size_t>(wpl) * height * sizeof(l_uint32);
        size_t copy_size = Size - 16;
        if (copy_size > data_size) {
            copy_size = data_size;
        }
        if (data != nullptr && copy_size > 0) {
            memcpy(data, Data + 16, copy_size);
        }
    }

    BOX *box = boxCreate(box_x, box_y, box_w, box_h);
    if (box == nullptr) {
        pixDestroy(&pix);
        return 0;
    }

    pixRenderBox(pix, box, render_width, op);

    pixRenderBox(pix, box, 0, L_SET_PIXELS);
    pixRenderBox(pix, box, -1, L_CLEAR_PIXELS);
    pixRenderBox(pix, nullptr, render_width, op);

    pixRenderBox(nullptr, box, render_width, op);

    boxDestroy(&box);
    pixDestroy(&pix);

    return 0;
}