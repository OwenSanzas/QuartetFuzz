#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "iec61850_server.h"
#include "hal_thread.h"
#include "lib_memory.h"

#define PRINT_BUF_SIZE 4096

static void
useAndFreeMmsValue(MmsValue* mmsValue, int printBufferSize)
{
    if (mmsValue == NULL) {
        return;
    }

    int dataSize = MmsValue_encodeMmsData(mmsValue, NULL, 0, false);
    if (dataSize > 0) {
        uint8_t* mmsBuffer = (uint8_t*) GLOBAL_MALLOC(dataSize);
        if (mmsBuffer != NULL) {
            MmsValue_encodeMmsData(mmsValue, mmsBuffer, 0, true);
            GLOBAL_FREEMEM(mmsBuffer);
        }
    }

    char printBuffer[PRINT_BUF_SIZE];
    if (printBufferSize < 0) {
        printBufferSize = 0;
    }
    if (printBufferSize > PRINT_BUF_SIZE) {
        printBufferSize = PRINT_BUF_SIZE;
    }
    MmsValue_printToBuffer(mmsValue, printBuffer, printBufferSize);

    MmsValue_delete(mmsValue);
}

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{

    if (size < 3) {
        return 0;
    }

    int maxDepth = (int) (data[0] % 40);

    int printBufferSize = ((int) data[1]) * 16;

    const uint8_t* payload = data + 2;
    size_t payloadSize = size - 2;

    if (payloadSize > (size_t) INT32_MAX) {
        payloadSize = (size_t) INT32_MAX;
    }

    uint8_t* buffer1 = (uint8_t*) malloc(payloadSize);
    if (buffer1 == NULL) {
        return 0;
    }
    memcpy(buffer1, payload, payloadSize);

    int endBufPos1 = 0;
    MmsValue* mmsValue1 =
        MmsValue_decodeMmsData(buffer1, 0, (int) payloadSize, &endBufPos1);
    useAndFreeMmsValue(mmsValue1, printBufferSize);
    free(buffer1);

    uint8_t* buffer2 = (uint8_t*) malloc(payloadSize);
    if (buffer2 == NULL) {
        return 0;
    }
    memcpy(buffer2, payload, payloadSize);

    int endBufPos2 = 0;
    MmsValue* mmsValue2 = MmsValue_decodeMmsDataMaxRecursion(
        buffer2, 0, (int) payloadSize, &endBufPos2, maxDepth);
    useAndFreeMmsValue(mmsValue2, printBufferSize);
    free(buffer2);

    return 0;
}
