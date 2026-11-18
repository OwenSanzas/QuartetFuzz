#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "lz4frame.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    LZ4F_dctx* dctx = NULL;
    LZ4F_errorCode_t errCode = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
    if (LZ4F_isError(errCode)) {
        return 0;
    }

    uint8_t ctrl = Data[0];
    const uint8_t *srcData = Data + 1;
    size_t srcRemaining = Size - 1;

    size_t dstCapacity = 64 + (ctrl % 256) * 16;
    if (dstCapacity > 4096) {
        dstCapacity = 4096;
    }

    uint8_t* dstBuffer = (uint8_t*)malloc(dstCapacity);
    if (!dstBuffer) {
        LZ4F_freeDecompressionContext(dctx);
        return 0;
    }

    size_t chunkSize = 1 + (ctrl % 32);
    if (chunkSize > srcRemaining && srcRemaining > 0) {
        chunkSize = srcRemaining;
    }

    LZ4F_decompressOptions_t dOpts;
    memset(&dOpts, 0, sizeof(dOpts));
    dOpts.stableDst = 0;
    dOpts.skipChecksums = (ctrl & 0x1) ? 1 : 0;

    const uint8_t *srcPtr = srcData;
    size_t remaining = srcRemaining;

    int iterationLimit = 10000;
    while (remaining > 0 && iterationLimit-- > 0) {
        size_t thisChunk = chunkSize;
        if (thisChunk > remaining) {
            thisChunk = remaining;
        }
        if (thisChunk == 0) {
            break;
        }

        size_t srcSize = thisChunk;
        size_t dstSize = dstCapacity;

        size_t ret = LZ4F_decompress(dctx, dstBuffer, &dstSize, srcPtr, &srcSize, &dOpts);

        if (LZ4F_isError(ret)) {

            LZ4F_resetDecompressionContext(dctx);
            break;
        }

        if (srcSize == 0) {

            srcPtr += thisChunk;
            remaining -= thisChunk;
        } else {
            srcPtr += srcSize;
            remaining -= srcSize;
        }

        if (ret == 0) {

            continue;
        }
    }

    if (remaining > 0) {
        size_t srcSize = remaining;
        size_t dstSize = dstCapacity;
        size_t ret = LZ4F_decompress(dctx, dstBuffer, &dstSize, srcPtr, &srcSize, NULL);
        (void)ret;
    }

    free(dstBuffer);
    LZ4F_freeDecompressionContext(dctx);

    return 0;
}