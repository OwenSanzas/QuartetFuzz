#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "lz4.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size < 4) {
        return 0;
    }

    uint16_t dictSizeRaw = (uint16_t)(Data[0] | (Data[1] << 8));
    uint16_t dstCapRaw   = (uint16_t)(Data[2] | (Data[3] << 8));

    Data += 4;
    Size -= 4;

    size_t dictSize = (Size > 0) ? (dictSizeRaw % (Size + 1)) : 0;
    if (dictSize > Size) {
        dictSize = Size;
    }

    size_t dstCapacity = (size_t)(dstCapRaw % 4096) + 1;

    const uint8_t *dict = Data;
    const uint8_t *src = Data + dictSize;
    size_t srcSize = Size - dictSize;

    if (srcSize > (size_t)INT32_MAX || dictSize > (size_t)INT32_MAX ||
        dstCapacity > (size_t)INT32_MAX) {
        return 0;
    }

    char *dst1 = (char *)malloc(dstCapacity);
    if (dst1 != NULL) {
        int ret1 = LZ4_decompress_safe_usingDict(
            (const char *)src,
            dst1,
            (int)srcSize,
            (int)dstCapacity,
            (const char *)dict,
            (int)dictSize
        );
        (void)ret1;
        free(dst1);
    }

    size_t combinedSize = dictSize + dstCapacity;
    char *combined = (char *)malloc(combinedSize > 0 ? combinedSize : 1);
    if (combined != NULL) {
        if (dictSize > 0) {
            memcpy(combined, dict, dictSize);
        }
        char *dst2 = combined + dictSize;

        int ret2 = LZ4_decompress_safe_usingDict(
            (const char *)src,
            dst2,
            (int)srcSize,
            (int)dstCapacity,
            combined,
            (int)dictSize
        );
        (void)ret2;

        free(combined);
    }

    char *dst3 = (char *)malloc(dstCapacity);
    if (dst3 != NULL) {
        int ret3 = LZ4_decompress_safe_usingDict(
            (const char *)src,
            dst3,
            (int)srcSize,
            (int)dstCapacity,
            NULL,
            0
        );
        (void)ret3;
        free(dst3);
    }

    return 0;
}