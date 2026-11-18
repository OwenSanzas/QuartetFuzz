#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_helpers.h"
#include "lz4.h"
#include "lz4frame.h"
#include "lz4_helpers.h"
#include "fuzz_data_producer.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FUZZ_dataProducer_t* producer = FUZZ_dataProducer_create(data, size);
    LZ4F_preferences_t prefs = FUZZ_dataProducer_preferences(producer);
    uint32_t const useDict = FUZZ_dataProducer_range32(producer, 0, 1);
    uint32_t const useContentSize = FUZZ_dataProducer_range32(producer, 0, 1);
    prefs.frameInfo.dictID = FUZZ_dataProducer_range32(producer, 0, 0xFFFFFFFFu);
    size = FUZZ_dataProducer_remainingBytes(producer);

    prefs.frameInfo.contentSize = useContentSize ? (unsigned long long)size + 1ULL : 0ULL;

    size_t const dstCapacity = LZ4F_compressFrameBound(size, &prefs);
    char* const dst = (char*)malloc(dstCapacity);
    FUZZ_ASSERT(dst);

    size_t const dstSize = LZ4F_compressFrame(dst, dstCapacity, data, size, &prefs);
    FUZZ_ASSERT(!LZ4F_isError(dstSize));

    {
        char* const rt = (char*)malloc(size ? size : 1);
        FUZZ_ASSERT(rt);
        size_t const rtSize = FUZZ_decompressFrame(rt, size, dst, dstSize);
        FUZZ_ASSERT_MSG(rtSize == size, "Incorrect regenerated size");
        if (size) FUZZ_ASSERT_MSG(!memcmp(data, rt, size), "Corruption!");
        free(rt);
    }

    if (useDict && size > 0) {
        size_t const dictSize = size < 4096 ? size : 4096;
        LZ4F_CDict* const cdict = LZ4F_createCDict(data, dictSize);
        if (cdict != NULL) {
            LZ4F_cctx* cctx = NULL;
            if (!LZ4F_isError(LZ4F_createCompressionContext(&cctx, LZ4F_VERSION)) && cctx) {
                size_t const dstCapacity2 = LZ4F_compressFrameBound(size, &prefs);
                char* const dst2 = (char*)malloc(dstCapacity2);
                if (dst2) {
                    size_t const cSize = LZ4F_compressFrame_usingCDict(
                        cctx, dst2, dstCapacity2, data, size, cdict, &prefs);
                    if (!LZ4F_isError(cSize)) {
                        char* const rt2 = (char*)malloc(size);
                        if (rt2) {
                            LZ4F_dctx* dctx = NULL;
                            if (!LZ4F_isError(LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION)) && dctx) {
                                size_t dstSize2 = size;
                                size_t srcConsumed2 = cSize;
                                size_t const rc = LZ4F_decompress_usingDict(
                                    dctx, rt2, &dstSize2, dst2, &srcConsumed2,
                                    data, dictSize, NULL);
                                FUZZ_ASSERT(!LZ4F_isError(rc));
                                LZ4F_freeDecompressionContext(dctx);
                            }
                            free(rt2);
                        }
                    }
                    free(dst2);
                }
                LZ4F_freeCompressionContext(cctx);
            }
            LZ4F_freeCDict(cdict);
        }
    }

    free(dst);
    FUZZ_dataProducer_free(producer);

    return 0;
}
