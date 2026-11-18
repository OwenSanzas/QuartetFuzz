#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_helpers.h"
#include "fuzz_data_producer.h"
#include "lz4.h"
#include "lz4hc.h"

static uint32_t FUZZ_readLE32(const uint8_t* p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{

    uint32_t repeatSeed = 0, levelSeed = 0, dstCapacitySeed = 0, boundarySeed = 0;
    size_t consumed = 0;

    if (size >= consumed + 4) {
        repeatSeed = FUZZ_readLE32(data + size - consumed - 4);
        consumed += 4;
    }
    if (size >= consumed + 4) {
        levelSeed = FUZZ_readLE32(data + size - consumed - 4);
        consumed += 4;
    }
    if (size >= consumed + 4) {
        dstCapacitySeed = FUZZ_readLE32(data + size - consumed - 4);
        consumed += 4;
    }
    if (size >= consumed + 4) {
        boundarySeed = FUZZ_readLE32(data + size - consumed - 4);
        consumed += 4;
    }

    size_t const baseSize = size - consumed;

    const uint8_t* const baseData = data;

    int const forceLargeRepeat = (repeatSeed & 0x80000000U) != 0;
    uint32_t const repeatSeedLow = repeatSeed & 0x7FFFFFFFU;
    uint32_t const repeat = forceLargeRepeat
        ? FUZZ_getRange_from_uint32(repeatSeedLow, 16, 48)
        : FUZZ_getRange_from_uint32(repeatSeedLow, 1, 8);

    size = (baseSize == 0) ? 0 : baseSize * (size_t)repeat;

    int const forceRLE = (boundarySeed & 0x1) != 0;

    if (((boundarySeed >> 1) & 0x1) && baseSize > 0) {
        size_t target = size;
        uint32_t const boundaryPick = (boundarySeed >> 2) % 4;
        switch (boundaryPick) {
            case 0: target = (size_t)LZ4_DISTANCE_MAX - 1; break;
            case 1: target = (size_t)LZ4_DISTANCE_MAX;     break;
            case 2: target = (size_t)LZ4_DISTANCE_MAX + 1; break;
            case 3: {
                uint32_t const cb = (uint32_t)LZ4_compressBound((int)size);
                target = cb > 0 ? (size_t)cb : size;
                break;
            }
        }

        if (target > (size_t)LZ4_MAX_INPUT_SIZE) target = (size_t)LZ4_MAX_INPUT_SIZE;
        if (target == 0) target = baseSize;
        size = target;
    }

    uint32_t const compressBound = (uint32_t)LZ4_compressBound((int)size);
    size_t dstCapacity;
    if ((boundarySeed >> 4) & 0x1) {
        uint32_t const pick = (boundarySeed >> 5) % 3;
        if (pick == 0) {
            dstCapacity = (compressBound > 0) ? (size_t)(compressBound - 1) : 0;
        } else if (pick == 1) {
            dstCapacity = (size_t)compressBound;
        } else {
            dstCapacity = (size_t)compressBound + 1;
        }
    } else {
        dstCapacity = (size_t)FUZZ_getRange_from_uint32(dstCapacitySeed,
                                                         0, compressBound);
    }

    int level;
    if (levelSeed & 0x80000000U) {

        uint32_t const sub = (levelSeed >> 16) % 20;
        level = (int)sub - 5;
    } else {
        uint32_t const bucket = (levelSeed >> 2) % 3;
        uint32_t const sub = (levelSeed >> 5);
        if (bucket == 0) {
            level = (int)(sub % 3);
        } else if (bucket == 1) {
            level = 3 + (int)(sub % 7);
        } else {
            level = LZ4HC_CLEVEL_OPT_MIN + (int)(sub % 3);
        }
    }

    char* const src = (char*)malloc(size > 0 ? size : 1);
    FUZZ_ASSERT(src);
    if (size > 0) {
        size_t off = 0;
        size_t tileIdx = 0;
        uint8_t const rleByte = (uint8_t)(boundarySeed >> 24);
        while (off < size) {
            size_t const chunk = (size - off < baseSize) ? (size - off) : baseSize;
            if (forceRLE) {
                memset(src + off, rleByte, chunk);
            } else {
                memcpy(src + off, baseData, chunk);
            }
            if (tileIdx > 0 && !forceRLE) {
                size_t i;
                for (i = 0; i < chunk; i++) {
                    if ((i % 7) != 0) {
                        src[off + i] = (char)((uint8_t)src[off + i]
                                               ^ (uint8_t)(tileIdx * 37u + i));
                    }
                }
            }
            off += chunk;
            tileIdx++;
        }
    }

    char* const dst = (char*)malloc(dstCapacity > 0 ? dstCapacity : 1);
    char* const rt = (char*)malloc(size > 0 ? size : 1);

    FUZZ_ASSERT(dst);
    FUZZ_ASSERT(rt);

    {
        int const dstSize = LZ4_compress_HC(src, dst, (int)size,
                                             (int)dstCapacity, level);
        if (dstSize > 0) {
            int const rtSize = LZ4_decompress_safe(dst, rt, dstSize, (int)size);
            FUZZ_ASSERT_MSG(rtSize == (int)size, "Incorrect regenerated size");
            FUZZ_ASSERT_MSG(size == 0 || !memcmp(src, rt, size), "Corruption!");
        }
    }

    free(src);
    free(dst);
    free(rt);

    return 0;
}