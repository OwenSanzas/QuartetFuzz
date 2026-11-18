#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "fuzz_helpers.h"
#include "lz4.h"
#include "lz4frame.h"
#include "lz4_helpers.h"
#include "fuzz_data_producer.h"

#define FUZZ_MAX_SYNTH_SIZE (2 * 4 * 1024 * 1024 + 4096)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FUZZ_dataProducer_t* producer = FUZZ_dataProducer_create(data, size);

    LZ4F_preferences_t prefs = FUZZ_dataProducer_preferences(producer);

    prefs.frameInfo.contentSize =
        (FUZZ_dataProducer_range32(producer, 0, 1) ? 1ULL : 0ULL);

    {
        int const useDictID = (FUZZ_dataProducer_range32(producer, 0, 1) == 1);
        prefs.frameInfo.dictID =
            useDictID ? FUZZ_dataProducer_range32(producer, 1, 0xFFFFFFFFu) : 0u;
    }

    {
        uint32_t const levelMode = FUZZ_dataProducer_range32(producer, 0, 3);
        switch (levelMode) {
            case 0:

                prefs.compressionLevel =
                    (int)FUZZ_dataProducer_range32(producer, 0, LZ4HC_CLEVEL_MAX + 3) - 3;
                break;
            case 1:

                prefs.compressionLevel =
                    -(int)FUZZ_dataProducer_range32(producer, 65538, 200000);
                break;
            case 2:

                prefs.compressionLevel =
                    (int)FUZZ_dataProducer_range32(producer, LZ4HC_CLEVEL_MAX + 1, 1000);
                break;
            default:

                prefs.compressionLevel =
                    (FUZZ_dataProducer_range32(producer, 0, 1))
                        ? INT32_MIN : INT32_MAX;
                break;
        }
    }

    int const useNullPrefs = (FUZZ_dataProducer_range32(producer, 0, 9) == 0);
    LZ4F_preferences_t const* const prefsPtr = useNullPrefs ? NULL : &prefs;

    size_t effectiveBlockSize = 64u * 1024u;
    if (prefsPtr != NULL) {
        switch (prefs.frameInfo.blockSizeID) {
            case LZ4F_max256KB: effectiveBlockSize = 256u * 1024u; break;
            case LZ4F_max1MB:   effectiveBlockSize = 1024u * 1024u; break;
            case LZ4F_max4MB:   effectiveBlockSize = 4u * 1024u * 1024u; break;
            case LZ4F_max64KB:
            case LZ4F_default:
            default:            effectiveBlockSize = 64u * 1024u; break;
        }
    }

    uint32_t const sizeMode = FUZZ_dataProducer_range32(producer, 0, 5);
    uint64_t targetSize64;
    switch (sizeMode) {
        case 0:
            targetSize64 = 0;
            break;
        case 1:

            targetSize64 = (effectiveBlockSize > 0) ? (effectiveBlockSize - 1) : 0;
            break;
        case 2:

            targetSize64 = effectiveBlockSize;
            break;
        case 3:

            targetSize64 = (uint64_t)effectiveBlockSize + 1;
            break;
        case 4:

            targetSize64 = (uint64_t)effectiveBlockSize * 2 + 17;
            break;
        default:
            targetSize64 =
                FUZZ_dataProducer_range32(producer, 0, FUZZ_MAX_SYNTH_SIZE);
            break;
    }
    if (targetSize64 > FUZZ_MAX_SYNTH_SIZE) {
        targetSize64 = FUZZ_MAX_SYNTH_SIZE;
    }
    uint32_t const targetSize = (uint32_t)targetSize64;

    size_t const remaining = FUZZ_dataProducer_remainingBytes(producer);

    int const forceEmptyNullSrc = (FUZZ_dataProducer_range32(producer, 0, 9) == 0);
    size_t const srcSize = forceEmptyNullSrc ? 0 : (size_t)targetSize;

    char* src = NULL;
    if (srcSize > 0) {
        src = (char*)malloc(srcSize);
        FUZZ_ASSERT(src);

        if (remaining > 0) {

            size_t copied = 0;
            while (copied < srcSize) {
                size_t const chunk =
                    (remaining < (srcSize - copied)) ? remaining : (srcSize - copied);
                memcpy(src + copied, data, chunk);
                copied += chunk;
            }
        } else {
            memset(src, 0, srcSize);
        }
    }

    size_t dstCapacity = LZ4F_compressFrameBound(srcSize, prefsPtr);

    if (LZ4F_isError(dstCapacity)) {

        dstCapacity = 256;
    } else if (dstCapacity > 0 && FUZZ_dataProducer_range32(producer, 0, 4) == 0) {

        dstCapacity = FUZZ_dataProducer_range32(producer, 0, (uint32_t)(dstCapacity - 1));
    }

    char* const dst = (char*)malloc(dstCapacity > 0 ? dstCapacity : 1);
    FUZZ_ASSERT(dst);

    size_t const dstSize =
            LZ4F_compressFrame(dst, dstCapacity, src, srcSize, prefsPtr);

    if (LZ4F_isError(dstSize)) {
        free(dst);
        free(src);
        FUZZ_dataProducer_free(producer);
        return 0;
    }

    FUZZ_ASSERT(dstSize <= dstCapacity);

    free(dst);
    free(src);
    FUZZ_dataProducer_free(producer);

    return 0;
}