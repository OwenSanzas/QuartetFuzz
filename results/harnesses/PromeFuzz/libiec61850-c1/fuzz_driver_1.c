#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mms_value.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    uint8_t* buffer = (uint8_t*) malloc(Size);
    if (buffer == NULL) {
        return 0;
    }
    memcpy(buffer, Data, Size);

    int bufferLength = (int) Size;
    int endBufPos = 0;

    MmsValue* value1 = MmsValue_decodeMmsData(buffer, 0, bufferLength, &endBufPos);
    if (value1 != NULL) {
        MmsValue_delete(value1);
    }

    if (bufferLength > 1) {
        int startPos = bufferLength / 2;
        MmsValue* value2 = MmsValue_decodeMmsData(buffer, startPos, bufferLength, NULL);
        if (value2 != NULL) {
            MmsValue_delete(value2);
        }
    }

    if (endBufPos > 0 && endBufPos < bufferLength) {
        int endBufPos2 = 0;
        MmsValue* value3 = MmsValue_decodeMmsData(buffer, endBufPos, bufferLength, &endBufPos2);
        if (value3 != NULL) {
            MmsValue_delete(value3);
        }
    }

    if (bufferLength > 4) {
        int smallerLength = bufferLength - (bufferLength / 4);
        MmsValue* value4 = MmsValue_decodeMmsData(buffer, 0, smallerLength, NULL);
        if (value4 != NULL) {
            MmsValue_delete(value4);
        }
    }

    free(buffer);

    return 0;
}