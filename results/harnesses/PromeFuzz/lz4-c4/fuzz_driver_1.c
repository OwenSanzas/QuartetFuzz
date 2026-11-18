#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "lz4hc.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    int compressionLevel = (int)(Data[0] % (LZ4HC_CLEVEL_MAX + 2));

    const uint8_t *srcData = Data + 1;
    size_t srcSizeT = Size - 1;

    if (srcSizeT > (size_t)LZ4_MAX_INPUT_SIZE) {
        srcSizeT = (size_t)LZ4_MAX_INPUT_SIZE;
    }

    int srcSize = (int)srcSizeT;

    char *src = (char *)malloc(srcSize > 0 ? srcSize : 1);
    if (!src) {
        return 0;
    }
    if (srcSize > 0) {
        memcpy(src, srcData, srcSize);
    }

    int dstCapacity = LZ4_compressBound(srcSize);
    if (dstCapacity <= 0) {
        dstCapacity = 16;
    }

    char *dst = (char *)malloc((size_t)dstCapacity);
    if (!dst) {
        free(src);
        return 0;
    }

    int result = LZ4_compress_HC(src, dst, srcSize, dstCapacity, compressionLevel);
    (void)result;

    int smallDstCapacity = (dstCapacity > 1) ? (dstCapacity / 2) : 0;
    char *smallDst = (char *)malloc((size_t)(smallDstCapacity > 0 ? smallDstCapacity : 1));
    if (smallDst) {
        int result2 = LZ4_compress_HC(src, smallDst, srcSize, smallDstCapacity, compressionLevel);
        (void)result2;
        free(smallDst);
    }

    int result3 = LZ4_compress_HC(src, dst, srcSize, dstCapacity, LZ4HC_CLEVEL_MAX);
    (void)result3;

    free(src);
    free(dst);

    return 0;
}