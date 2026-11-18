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
#include <vips/foreign.h>
#include <cstdint>
#include <cstddef>
#include <cstring>

static bool vips_initialized = false;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    if (!vips_initialized) {
        if (VIPS_INIT(NULL)) {

            return 0;
        }
        vips_initialized = true;
    }

    VipsImage *image = vips_image_new_from_buffer(Data, Size, "", NULL);
    if (image == nullptr) {

        return 0;
    }

    int quality = 75;
    gboolean optimize_coding = FALSE;
    gboolean interlace = FALSE;
    gboolean strip = FALSE;
    int subsample_mode = VIPS_FOREIGN_SUBSAMPLE_AUTO;

    if (Size >= 1) {
        quality = 1 + (Data[0] % 100);
    }
    if (Size >= 2) {
        optimize_coding = (Data[1] & 0x01) ? TRUE : FALSE;
        interlace = (Data[1] & 0x02) ? TRUE : FALSE;
        strip = (Data[1] & 0x04) ? TRUE : FALSE;
    }
    if (Size >= 3) {
        subsample_mode = Data[2] % 3;
    }

    void *buf = nullptr;
    size_t len = 0;

    int ret1 = vips_jpegsave_buffer(image, &buf, &len,
                                     "Q", quality,
                                     NULL);
    if (ret1 == 0 && buf != nullptr) {
        g_free(buf);
        buf = nullptr;
        len = 0;
    }

    void *buf2 = nullptr;
    size_t len2 = 0;
    int ret2 = vips_jpegsave_buffer(image, &buf2, &len2,
                                     "Q", quality,
                                     "optimize_coding", optimize_coding,
                                     "interlace", interlace,
                                     "strip", strip,
                                     "subsample_mode", subsample_mode,
                                     NULL);
    if (ret2 == 0 && buf2 != nullptr) {
        g_free(buf2);
        buf2 = nullptr;
        len2 = 0;
    }

    void *buf3 = nullptr;
    size_t len3 = 0;
    int ret3 = vips_jpegsave_buffer(image, &buf3, &len3, NULL);
    if (ret3 == 0 && buf3 != nullptr) {
        g_free(buf3);
        buf3 = nullptr;
        len3 = 0;
    }

    g_object_unref(image);

    return 0;
}