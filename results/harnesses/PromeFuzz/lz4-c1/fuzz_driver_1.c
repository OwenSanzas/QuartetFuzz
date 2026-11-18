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

static void run_compress(const uint8_t *src, size_t srcSize, const LZ4F_preferences_t *prefsPtr)
{
    size_t dstCapacity = LZ4F_compressFrameBound(srcSize, prefsPtr);
    if (dstCapacity == 0) {
        dstCapacity = 64;
    }

    void *dstBuffer = malloc(dstCapacity);
    if (!dstBuffer) {
        return;
    }

    size_t result = LZ4F_compressFrame(dstBuffer, dstCapacity, src, srcSize, prefsPtr);

    if (LZ4F_isError(result)) {

    } else {

        (void)result;
    }

    free(dstBuffer);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size < 1) {

        LZ4F_preferences_t prefs;
        memset(&prefs, 0, sizeof(prefs));
        run_compress(NULL, 0, NULL);
        run_compress(NULL, 0, &prefs);
        return 0;
    }

    uint8_t configByte = Data[0];
    const uint8_t *srcData = Data + 1;
    size_t srcSize = Size - 1;

    run_compress(srcData, srcSize, NULL);

    LZ4F_preferences_t prefs;
    memset(&prefs, 0, sizeof(prefs));

    prefs.frameInfo.blockSizeID = (LZ4F_blockSizeID_t)((configByte & 0x03) + 4);
    prefs.frameInfo.blockMode = (LZ4F_blockMode_t)((configByte >> 2) & 0x01);
    prefs.frameInfo.contentChecksumFlag = (LZ4F_contentChecksum_t)((configByte >> 3) & 0x01);
    prefs.frameInfo.frameType = (LZ4F_frameType_t)((configByte >> 4) & 0x01);
    prefs.frameInfo.contentSize = (unsigned long long)srcSize;
    prefs.frameInfo.dictID = 0;
    prefs.frameInfo.blockChecksumFlag = (LZ4F_blockChecksum_t)((configByte >> 5) & 0x01);

    prefs.compressionLevel = (int)((int8_t)configByte);

    prefs.autoFlush = (unsigned)((configByte >> 6) & 0x01);
    prefs.favorDecSpeed = (unsigned)((configByte >> 7) & 0x01);

    run_compress(srcData, srcSize, &prefs);

    LZ4F_preferences_t defaultPrefs;
    memset(&defaultPrefs, 0, sizeof(defaultPrefs));
    run_compress(srcData, srcSize, &defaultPrefs);

    LZ4F_preferences_t highPrefs;
    memset(&highPrefs, 0, sizeof(highPrefs));
    highPrefs.compressionLevel = 12;
    highPrefs.frameInfo.blockMode = LZ4F_blockLinked;
    highPrefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
    highPrefs.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
    run_compress(srcData, srcSize, &highPrefs);

    return 0;
}