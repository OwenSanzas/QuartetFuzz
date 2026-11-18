#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_helpers.h"
#include "lz4.h"

static uint32_t FUZZ_readLE32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t FUZZ_getRangeFromU32(uint32_t seed, uint32_t min, uint32_t max) {
    uint32_t range = max - min;
    if (range == 0xffffffffu) {
        return seed;
    }
    return min + seed % (range + 1);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{

    if (size < 8) {
        return 0;
    }

    uint32_t const dictSizeSeed = FUZZ_readLE32(data);
    uint32_t const capSeed = FUZZ_readLE32(data + 4);

    const uint8_t *payload = data + 8;
    size_t const payloadSize = size - 8;

    if (payloadSize == 0) {
        return 0;
    }

    size_t const dictSize = (size_t)FUZZ_getRangeFromU32(
        dictSizeSeed, 0, 70 * 1024);

    char *const dict = (char *)malloc(dictSize > 0 ? dictSize : 1);
    FUZZ_ASSERT(dict);
    {
        size_t i;
        for (i = 0; i < dictSize; i++) {
            dict[i] = (char)payload[i % payloadSize];
        }
    }

    size_t const dstCapacity = LZ4_compressBound((int)payloadSize);
    size_t const largePrefixSize = 70 * 1024;

    char *const dstPlusPrefix = (char *)malloc(dstCapacity + largePrefixSize);
    FUZZ_ASSERT(dstPlusPrefix);
    char *const dst = dstPlusPrefix + largePrefixSize;

    char *const rt = (char *)malloc(payloadSize > 0 ? payloadSize : 1);
    FUZZ_ASSERT(rt);

    LZ4_stream_t *const cstream = LZ4_createStream();
    FUZZ_ASSERT(cstream);
    LZ4_loadDict(cstream, dict, (int)dictSize);
    int const acceleration = 1 + (int)(dictSizeSeed % 16);
    int const dstSize = LZ4_compress_fast_continue(
        cstream, (const char *)payload, dst, (int)payloadSize,
        (int)dstCapacity, acceleration);
    FUZZ_ASSERT(dstSize > 0);
    LZ4_freeStream(cstream);

    {
        int const rtSize = LZ4_decompress_safe_usingDict(
            dst, rt, dstSize, (int)payloadSize, dict, (int)dictSize);
        FUZZ_ASSERT(rtSize == (int)payloadSize);
        FUZZ_ASSERT(!memcmp(rt, payload, payloadSize));
    }

    if (dictSize <= largePrefixSize) {
        char *const prefixDictStart = dst - dictSize;
        if (dictSize > 0) {
            memcpy(prefixDictStart, dict, dictSize);
        }
        int const rtSize2 = LZ4_decompress_safe_usingDict(
            dst, rt, dstSize, (int)payloadSize,
            prefixDictStart, (int)dictSize);
        FUZZ_ASSERT(rtSize2 == (int)payloadSize);
        FUZZ_ASSERT(!memcmp(rt, payload, payloadSize));
    }

    {
        size_t const boundaryDictSizes[2] = { 65535, 65536 };
        int b;
        for (b = 0; b < 2; b++) {
            size_t const bds = boundaryDictSizes[b];
            if (bds <= largePrefixSize) {
                char *const prefixDictStart = dst - bds;
                size_t const copyLen = bds < dictSize ? bds : dictSize;
                if (copyLen > 0) {
                    memcpy(prefixDictStart, dict, copyLen);
                }
                int const rBoundary = LZ4_decompress_safe_usingDict(
                    dst, rt, dstSize, (int)payloadSize,
                    prefixDictStart, (int)bds);
                if (bds == dictSize) {
                    FUZZ_ASSERT(rBoundary == (int)payloadSize);
                    FUZZ_ASSERT(!memcmp(rt, payload, payloadSize));
                } else {
                    FUZZ_ASSERT(rBoundary <= (int)payloadSize);
                }
            }
        }
    }

    {
        size_t const garbageSize = dictSize > payloadSize ? payloadSize : dictSize;
        char *const garbageDict = (char *)malloc(garbageSize > 0 ? garbageSize : 1);
        FUZZ_ASSERT(garbageDict);
        if (garbageSize > 0) {
            memcpy(garbageDict, payload, garbageSize);
        }
        int const rtSize3 = LZ4_decompress_safe_usingDict(
            dst, rt, dstSize, (int)payloadSize,
            garbageDict, (int)garbageSize);
        FUZZ_ASSERT(rtSize3 <= (int)payloadSize);
        free(garbageDict);
    }

    {

        int const fuzzSrcSize = (int)payloadSize;

        int const fuzzDstCapacity = (int)FUZZ_getRangeFromU32(
            capSeed, 0, (uint32_t)payloadSize + 16);

        char *const fuzzDst = (char *)malloc(fuzzDstCapacity > 0 ? fuzzDstCapacity : 1);
        FUZZ_ASSERT(fuzzDst);

        {
            int const r = LZ4_decompress_safe_usingDict(
                (const char *)payload, fuzzDst, fuzzSrcSize, fuzzDstCapacity,
                NULL, 0);
            FUZZ_ASSERT(r <= fuzzDstCapacity);
        }

        {
            int const r = LZ4_decompress_safe_usingDict(
                (const char *)payload, fuzzDst, fuzzSrcSize, fuzzDstCapacity,
                dict, (int)dictSize);
            FUZZ_ASSERT(r <= fuzzDstCapacity);
        }

        {
            char *const prefixBuf =
                (char *)malloc((size_t)fuzzDstCapacity + dictSize + 1);
            FUZZ_ASSERT(prefixBuf);
            if (dictSize > 0) {
                memcpy(prefixBuf, dict, dictSize);
            }
            char *const prefDst = prefixBuf + dictSize;
            int const r = LZ4_decompress_safe_usingDict(
                (const char *)payload, prefDst, fuzzSrcSize, fuzzDstCapacity,
                prefixBuf, (int)dictSize);
            FUZZ_ASSERT(r <= fuzzDstCapacity);
            free(prefixBuf);
        }

        free(fuzzDst);
    }

    {

        int const rNullSrc = LZ4_decompress_safe_usingDict(
            NULL, rt, (int)payloadSize, (int)payloadSize, dict, (int)dictSize);
        FUZZ_ASSERT(rNullSrc == -1);

        int const rNegCap = LZ4_decompress_safe_usingDict(
            dst, rt, dstSize, -1, dict, (int)dictSize);
        FUZZ_ASSERT(rNegCap == -1);

        int const rZeroSrc = LZ4_decompress_safe_usingDict(
            dst, rt, 0, (int)payloadSize, dict, (int)dictSize);
        FUZZ_ASSERT(rZeroSrc == -1);
    }

    if (payloadSize > 1) {
        int const shrunk = (int)payloadSize - 1;
        int const rShort = LZ4_decompress_safe_usingDict(
            dst, rt, dstSize, shrunk, dict, (int)dictSize);
        FUZZ_ASSERT(rShort < 0 || rShort <= shrunk);
    }

    if (dstSize > 0) {
        char *const mutated = (char *)malloc((size_t)dstSize);
        FUZZ_ASSERT(mutated);
        memcpy(mutated, dst, (size_t)dstSize);

        size_t const nMutations = 1 + (payload[0] % 4);
        size_t m;
        for (m = 0; m < nMutations; m++) {
            size_t const idx =
                payload[(m * 2 + 1) % payloadSize] % (size_t)dstSize;
            mutated[idx] ^= (char)payload[(m * 2 + 2) % payloadSize];
        }

        int const rMut = LZ4_decompress_safe_usingDict(
            mutated, rt, dstSize, (int)payloadSize, dict, (int)dictSize);
        FUZZ_ASSERT(rMut <= (int)payloadSize);

        if (dictSize <= largePrefixSize) {
            char *const prefixDictStart = dst - dictSize;

            int const rMutPrefix = LZ4_decompress_safe_usingDict(
                mutated, rt, dstSize, (int)payloadSize,
                prefixDictStart, (int)dictSize);
            FUZZ_ASSERT(rMutPrefix <= (int)payloadSize);
        }

        free(mutated);
    }

    free(dstPlusPrefix);
    free(rt);
    free(dict);

    return 0;
}