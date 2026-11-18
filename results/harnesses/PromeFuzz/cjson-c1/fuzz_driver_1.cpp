#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include "cJSON.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Data == nullptr || Size == 0) {
        return 0;
    }

    char *buffer = static_cast<char *>(malloc(Size + 1));
    if (buffer == nullptr) {
        return 0;
    }
    memcpy(buffer, Data, Size);
    buffer[Size] = '\0';

    const char *return_parse_end = nullptr;

    cJSON *json1 = cJSON_ParseWithOpts(buffer, &return_parse_end, 1);
    if (json1 != nullptr) {
        char *printed = cJSON_Print(json1);
        if (printed != nullptr) {
            free(printed);
        }
        cJSON_Delete(json1);
    }

    return_parse_end = nullptr;
    cJSON *json2 = cJSON_ParseWithOpts(buffer, &return_parse_end, 0);
    if (json2 != nullptr) {
        char *printed2 = cJSON_PrintUnformatted(json2);
        if (printed2 != nullptr) {
            free(printed2);
        }
        cJSON_Delete(json2);
    }

    cJSON *json3 = cJSON_ParseWithOpts(buffer, nullptr, 1);
    if (json3 != nullptr) {
        cJSON_Delete(json3);
    }

    if (Size > 1) {
        char *subBuffer = static_cast<char *>(malloc(Size));
        if (subBuffer != nullptr) {
            memcpy(subBuffer, Data, Size - 1);
            subBuffer[Size - 1] = '\0';
            const char *sub_return_parse_end = nullptr;
            cJSON *json4 = cJSON_ParseWithOpts(subBuffer, &sub_return_parse_end, 0);
            if (json4 != nullptr) {
                cJSON_Delete(json4);
            }
            free(subBuffer);
        }
    }

    free(buffer);

    return 0;
}