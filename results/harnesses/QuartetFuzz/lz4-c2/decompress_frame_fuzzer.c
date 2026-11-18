#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lz4frame.h"
#include "fuzz_data_producer.h"

#define FUZZ_DEC_MAX_DST (4 * 1024 * 1024)

static void FUZZ_consumeOutput(const uint8_t* buf, size_t n) {

    volatile uint8_t acc = 0;
    size_t i;
    for (i = 0; i < n; i++) acc ^= buf[i];
    (void)acc;
}

static void FUZZ_runDecompress(LZ4F_dctx* dctx,
                                const uint8_t* src, size_t srcSize,
                                const void* dict, size_t dictSize,
                                const LZ4F_decompressOptions_t* opts,
                                size_t chunkSize, unsigned useDict,
                                uint8_t* outBuf, size_t outCap, size_t* outUsedPtr)
{
    uint8_t* const dst = (uint8_t*)malloc(FUZZ_DEC_MAX_DST);
    const uint8_t* srcPtr = src;
    const uint8_t* const srcEnd = src + srcSize;
    size_t guard = 0;
    size_t const maxIters = srcSize + 4096;

    if (!dst) return;

    while (srcPtr < srcEnd && guard++ < maxIters) {
        size_t toFeed = (size_t)(srcEnd - srcPtr);
        size_t srcConsumed;
        size_t dstCapacity = FUZZ_DEC_MAX_DST;
        size_t rc;

        if (chunkSize > 0 && toFeed > chunkSize) toFeed = chunkSize;
        srcConsumed = toFeed;

        if (useDict) {
            rc = LZ4F_decompress_usingDict(dctx, dst, &dstCapacity,
                                            srcPtr, &srcConsumed,
                                            dict, dictSize, opts);
        } else {
            rc = LZ4F_decompress(dctx, dst, &dstCapacity,
                                  srcPtr, &srcConsumed, opts);
        }

        if (dstCapacity > 0) {
            FUZZ_consumeOutput(dst, dstCapacity);
            if (outBuf != NULL && outUsedPtr != NULL) {
                size_t const room = outCap - *outUsedPtr;
                size_t const toCopy = dstCapacity < room ? dstCapacity : room;
                if (toCopy > 0) {
                    memcpy(outBuf + *outUsedPtr, dst, toCopy);
                    *outUsedPtr += toCopy;
                }
            }
        }

        if (LZ4F_isError(rc)) {
            LZ4F_resetDecompressionContext(dctx);
            break;
        }

        if (srcConsumed == 0) {

            break;
        }
        srcPtr += srcConsumed;

    }

    free(dst);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FUZZ_dataProducer_t* producer = FUZZ_dataProducer_create(data, size);
    uint32_t const ctrl = FUZZ_dataProducer_retrieve32(producer);

    unsigned const useGetFrameInfo = ctrl & 1;
    unsigned useDict               = (ctrl >> 1) & 1;
    unsigned const skipChecksums   = (ctrl >> 2) & 1;
    unsigned const stableDst       = (ctrl >> 3) & 1;
    unsigned const chunkMode       = (ctrl >> 4) & 0x7;
    unsigned const roundTrip       = (ctrl >> 7) & 1;

    static const size_t chunkSizes[8] = {0, 1, 2, 4, 7, 16, 64, 256};
    size_t const chunkSize = chunkSizes[chunkMode];

    LZ4F_decompressOptions_t opts;
    size_t dataSize;
    const uint8_t* bytes;
    size_t dictSize = 0;
    const uint8_t* dictPtr = NULL;
    const uint8_t* framePtr;
    size_t frameSize;
    uint8_t* compBuf = NULL;
    const uint8_t* plainData = NULL;
    size_t plainSize = 0;
    unsigned doRoundTripCheck = 0;

    memset(&opts, 0, sizeof(opts));
    opts.skipChecksums = skipChecksums;
    opts.stableDst = stableDst;

    dataSize = FUZZ_dataProducer_remainingBytes(producer);
    bytes = data;

    if (useDict && dataSize > 0) {
        dictSize = dataSize > 1024 ? 1024 : dataSize / 2;
        dictPtr = bytes;
    }

    framePtr = bytes + dictSize;
    frameSize = dataSize - dictSize;

    {
        size_t const hdrLen = LZ4F_headerSize(framePtr, frameSize);
        (void)hdrLen;
    }

    if (roundTrip) {

        LZ4F_preferences_t prefs = FUZZ_dataProducer_preferences(producer);
        size_t dstCapacity;

        dataSize = FUZZ_dataProducer_remainingBytes(producer);
        if (dictSize > dataSize) dictSize = 0;
        framePtr = bytes + dictSize;
        frameSize = dataSize - dictSize;

        plainData = framePtr;
        plainSize = frameSize;

        dstCapacity = LZ4F_compressFrameBound(frameSize, &prefs) + 64;
        compBuf = (uint8_t*)malloc(dstCapacity);
        if (compBuf != NULL) {
            size_t const compSize = LZ4F_compressFrame(compBuf, dstCapacity,
                                                         framePtr, frameSize, &prefs);
            if (!LZ4F_isError(compSize)) {
                framePtr = compBuf;
                frameSize = compSize;
                doRoundTripCheck = 1;

                useDict = 0;
            } else {
                frameSize = 0;
            }
        } else {
            frameSize = 0;
        }
    }

    {
        LZ4F_dctx* dctx = NULL;
        LZ4F_errorCode_t const err = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
        if (!LZ4F_isError(err) && dctx != NULL) {
            const uint8_t* remainPtr = framePtr;
            size_t remainSize = frameSize;
            uint8_t* outBuf = NULL;
            size_t outUsed = 0;

            if (doRoundTripCheck && plainSize > 0) {
                outBuf = (uint8_t*)malloc(plainSize);
            }

            if (useGetFrameInfo) {
                LZ4F_frameInfo_t info;
                size_t infoSrcSize = remainSize;
                size_t const infoRc = LZ4F_getFrameInfo(dctx, &info, remainPtr, &infoSrcSize);
                if (!LZ4F_isError(infoRc) && infoSrcSize <= remainSize) {
                    remainPtr += infoSrcSize;
                    remainSize -= infoSrcSize;
                    FUZZ_runDecompress(dctx, remainPtr, remainSize, dictPtr, dictSize,
                                        &opts, chunkSize, useDict,
                                        outBuf, plainSize, &outUsed);
                }

            } else {
                FUZZ_runDecompress(dctx, remainPtr, remainSize, dictPtr, dictSize,
                                    &opts, chunkSize, useDict,
                                    outBuf, plainSize, &outUsed);
            }

            if (outBuf != NULL) {
                if (outUsed == plainSize && memcmp(outBuf, plainData, plainSize) != 0) {
                    fprintf(stderr, "Corruption: LZ4F round-trip mismatch!\n");
                    abort();
                }
                free(outBuf);
            }

            LZ4F_freeDecompressionContext(dctx);
        }
    }

    free(compBuf);
    FUZZ_dataProducer_free(producer);
    return 0;
}
