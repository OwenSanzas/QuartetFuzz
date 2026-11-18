#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <vips/vips.h>
#include <vips/conversion.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

static int g_vips_initialized = 0;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (!g_vips_initialized) {
        if (VIPS_INIT("fuzz_smartcrop")) {
            return 0;
        }
        g_vips_initialized = 1;
    }

    if (Size == 0) {
        return 0;
    }

    const char *filename = "./dummy_file";
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        return 0;
    }
    fwrite(Data, 1, Size, fp);
    fclose(fp);

    VipsImage *in = nullptr;
    void *buf_copy = malloc(Size);
    if (buf_copy) {
        memcpy(buf_copy, Data, Size);
        in = vips_image_new_from_buffer(buf_copy, Size, "", nullptr);

        if (!in) {
            free(buf_copy);
        }
    }

    if (!in) {
        in = vips_image_new_from_file(filename, nullptr);
    }

    if (!in) {
        return 0;
    }

    int base_w = in->Xsize > 0 ? in->Xsize : 1;
    int base_h = in->Ysize > 0 ? in->Ysize : 1;

    int crop_w = 1;
    int crop_h = 1;

    if (Size >= 2) {
        uint8_t wb = Data[0];
        uint8_t hb = Data[1];
        crop_w = (wb % base_w) + 1;
        crop_h = (hb % base_h) + 1;
    } else {
        crop_w = (base_w > 1) ? base_w / 2 : 1;
        crop_h = (base_h > 1) ? base_h / 2 : 1;
    }

    if (crop_w < 1) crop_w = 1;
    if (crop_h < 1) crop_h = 1;
    if (crop_w > base_w) crop_w = base_w;
    if (crop_h > base_h) crop_h = base_h;

    {
        VipsImage *out = nullptr;
        int ret = vips_smartcrop(in, &out, crop_w, crop_h, nullptr);
        if (ret == 0 && out) {
            g_object_unref(out);
        }
    }

    {
        VipsImage *out = nullptr;
        int interesting_val = VIPS_INTERESTING_ATTENTION;
        if (Size >= 3) {
            interesting_val = Data[2] % (VIPS_INTERESTING_LAST);
        }
        int ret = vips_smartcrop(in, &out, crop_w, crop_h,
                                  "interesting", interesting_val,
                                  nullptr);
        if (ret == 0 && out) {
            g_object_unref(out);
        }
    }

    {
        VipsImage *out = nullptr;
        int ret = vips_smartcrop(in, &out, 0, 0, nullptr);
        if (ret == 0 && out) {
            g_object_unref(out);
        }
    }

    {
        VipsImage *out = nullptr;
        int ret = vips_smartcrop(in, &out, base_w + 10, base_h + 10, nullptr);
        if (ret == 0 && out) {
            g_object_unref(out);
        }
    }

    g_object_unref(in);

    return 0;
}