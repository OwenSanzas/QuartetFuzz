#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_helpers.h"
#include "fuzz_data_producer.h"
#include "lz4.h"
#include "lz4hc.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FUZZ_dataProducer_t *producer = FUZZ_dataProducer_create(data, size);

    uint32_t const dstCapacitySeed = FUZZ_dataProducer_retrieve32(producer);
    uint32_t const levelSeed       = FUZZ_dataProducer_retrieve32(producer);
    uint32_t const dictSeed        = FUZZ_dataProducer_retrieve32(producer);
    uint32_t const favorSeed       = FUZZ_dataProducer_retrieve32(producer);
    uint32_t const splitSeed       = FUZZ_dataProducer_retrieve32(producer);
    uint32_t const modeSeed        = FUZZ_dataProducer_retrieve32(producer);

    size = FUZZ_dataProducer_remainingBytes(producer);

    if (size > (size_t)LZ4_MAX_INPUT_SIZE) {
        FUZZ_dataProducer_free(producer);
        return 0;
    }

    int const level = (int)FUZZ_getRange_from_uint32(levelSeed, 0, LZ4HC_CLEVEL_MAX + 2);
    int const favor = (int)(favorSeed & 1u);

    uint32_t const boundCap = (uint32_t)LZ4_compressBound((int)size);
    size_t const dstCapacity = FUZZ_getRange_from_uint32(dstCapacitySeed, 0, boundCap);

    char* const dst = (char*)malloc(dstCapacity > 0 ? dstCapacity : 1);
    char* const rt  = (char*)malloc(size > 0 ? size : 1);
    FUZZ_ASSERT(dst);
    FUZZ_ASSERT(rt);

    {
        int const dstSize = LZ4_compress_HC((const char*)data, dst, (int)size,
                                             (int)dstCapacity, level);
        if (dstSize > 0) {
            int const rtSize = LZ4_decompress_safe(dst, rt, dstSize, (int)size);
            FUZZ_ASSERT_MSG(rtSize == (int)size, "Incorrect regenerated size");
            FUZZ_ASSERT_MSG(!memcmp(data, rt, size), "Corruption!");
        }
    }

    {
        void* const extState = malloc((size_t)LZ4_sizeofStateHC());
        FUZZ_ASSERT(extState);
        int const dstSize = LZ4_compress_HC_extStateHC(extState, (const char*)data, dst,
                                                         (int)size, (int)dstCapacity, level);
        if (dstSize > 0) {
            int const rtSize = LZ4_decompress_safe(dst, rt, dstSize, (int)size);
            FUZZ_ASSERT_MSG(rtSize == (int)size, "Incorrect regenerated size (extState)");
            FUZZ_ASSERT_MSG(!memcmp(data, rt, size), "Corruption! (extState)");
        }
        free(extState);
    }

    if (dstCapacity > 0) {
        void* state = malloc((size_t)LZ4_sizeofStateHC());
        FUZZ_ASSERT(state);
        int compressedSize = (int)size;
        int const dstSize = LZ4_compress_HC_destSize(state, (const char*)data, dst,
                                                       &compressedSize, (int)dstCapacity,
                                                       level);
        if (dstSize > 0) {
            FUZZ_ASSERT_MSG(compressedSize >= 0 && (size_t)compressedSize <= size,
                             "destSize consumed more than provided");
            int const rtSize = LZ4_decompress_safe(dst, rt, dstSize, compressedSize);
            FUZZ_ASSERT_MSG(rtSize == compressedSize, "Incorrect regenerated size (destSize)");
            FUZZ_ASSERT_MSG(!memcmp(data, rt, (size_t)compressedSize), "Corruption! (destSize)");
        }
        free(state);
    }

    {

        size_t const dictSize = (size > 0)
            ? FUZZ_getRange_from_uint32(dictSeed, 0, (uint32_t)size)
            : 0;
        const char* const dict = (const char*)data;
        const char* const payload = (const char*)data + dictSize;
        size_t const payloadSize = size - dictSize;

        LZ4_streamHC_t* const stream = LZ4_createStreamHC();
        FUZZ_ASSERT(stream);
        LZ4_streamDecode_t* const dstream = LZ4_createStreamDecode();
        FUZZ_ASSERT(dstream);

        LZ4_resetStreamHC_fast(stream, level);
        LZ4_favorDecompressionSpeed(stream, favor);

        LZ4_streamHC_t* dictStream = NULL;
        if ((modeSeed & 1u) && dictSize > 0) {

            dictStream = LZ4_createStreamHC();
            FUZZ_ASSERT(dictStream);
            LZ4_resetStreamHC_fast(dictStream, level);
            LZ4_loadDictHC(dictStream, dict, (int)dictSize);
            LZ4_attach_HC_dictionary(stream, dictStream);
        } else if (dictSize > 0) {

            LZ4_loadDictHC(stream, dict, (int)dictSize);
        }

        LZ4_setStreamDecode(dstream, dict, (int)dictSize);

        size_t const blockCapacity = (size_t)LZ4_compressBound((int)payloadSize) + 1024;
        char* const blockDst = (char*)malloc(blockCapacity > 0 ? blockCapacity : 1);
        char* const blockRt  = (char*)malloc(payloadSize > 0 ? payloadSize : 1);
        FUZZ_ASSERT(blockDst);
        FUZZ_ASSERT(blockRt);

        size_t split = (payloadSize > 0)
            ? (size_t)FUZZ_getRange_from_uint32(splitSeed, 0, (uint32_t)payloadSize)
            : 0;
        if (split > payloadSize) split = payloadSize;

        const char* const src1 = payload;
        int const srcSize1 = (int)split;
        const char* const src2 = payload + split;
        int const srcSize2 = (int)(payloadSize - split);

        size_t rtPos = 0;

        int const dstSize1 = LZ4_compress_HC_continue(stream, src1, blockDst,
                                                        srcSize1, (int)blockCapacity);
        if (dstSize1 > 0) {
            int const rtSize = LZ4_decompress_safe_continue(dstream, blockDst,
                                                              blockRt + rtPos,
                                                              dstSize1, srcSize1);
            FUZZ_ASSERT_MSG(rtSize == srcSize1, "Incorrect regenerated size (continue-1)");
            FUZZ_ASSERT_MSG(!memcmp(src1, blockRt + rtPos, (size_t)srcSize1), "Corruption! (continue-1)");
            rtPos += (size_t)rtSize;

            if (srcSize2 > 0) {
                int const dstSize2 = LZ4_compress_HC_continue(stream, src2, blockDst,
                                                                srcSize2, (int)blockCapacity);
                if (dstSize2 > 0) {
                    int const rtSize2 = LZ4_decompress_safe_continue(dstream, blockDst,
                                                                       blockRt + rtPos,
                                                                       dstSize2, srcSize2);
                    FUZZ_ASSERT_MSG(rtSize2 == srcSize2, "Incorrect regenerated size (continue-2)");
                    FUZZ_ASSERT_MSG(!memcmp(src2, blockRt + rtPos, (size_t)srcSize2), "Corruption! (continue-2)");
                }
            }
        }

        free(blockDst);
        free(blockRt);
        LZ4_freeStreamHC(stream);
        LZ4_freeStreamDecode(dstream);
        if (dictStream) LZ4_freeStreamHC(dictStream);
    }

    free(dst);
    free(rt);
    FUZZ_dataProducer_free(producer);

    return 0;
}
