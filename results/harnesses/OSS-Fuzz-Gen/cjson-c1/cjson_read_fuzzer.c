#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../cJSON.h"

#ifdef __cplusplus
extern "C"
#endif
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#ifdef __cplusplus
extern "C"
#endif
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    cJSON_bool require_null_terminated;
    const uint8_t *json_data;
    size_t json_size;
    char *value = NULL;
    const char *return_parse_end_storage = NULL;
    const char **return_parse_end_ptr;
    cJSON *item;
    uint8_t control;
    uint8_t assist = 0;
    cJSON_bool prepend_bom;
    size_t consumed = 1;
    size_t wrap_count = 0;
    size_t obj_wrap_count = 0;
    size_t pad_digits = 0;
    static const unsigned char kBOM[3] = {0xEF, 0xBB, 0xBF};

    static const char *kSurrogateSamples[] = {
        "\"\\ud834\\udd1e\"",
        "\"\\ud800\"",
        "\"\\ud800\\u0041\"",
        "\"\\udc00\"",
        "\"\\ud80"
    };
    const size_t kSurrogateSampleCount = sizeof(kSurrogateSamples) / sizeof(kSurrogateSamples[0]);
    cJSON_bool inject_surrogate = 0;
    const char *surrogate_sample = NULL;
    size_t surrogate_sample_len = 0;

    if (size < 1) {
        return 0;
    }

    control = data[0];

    require_null_terminated = (cJSON_bool)(control & 1);

    if (control == 0xFE) {
        return_parse_end_ptr = &return_parse_end_storage;
        item = cJSON_ParseWithOpts(NULL, return_parse_end_ptr, require_null_terminated);
        if (item != NULL) {
            cJSON_Delete(item);
        }
        return 0;
    }
    if (control == 0xFD) {
        item = cJSON_ParseWithOpts(NULL, NULL, require_null_terminated);
        if (item != NULL) {
            cJSON_Delete(item);
        }
        return 0;
    }

    return_parse_end_ptr = ((control & 4) != 0) ? NULL : &return_parse_end_storage;

    prepend_bom = (cJSON_bool)((control & 8) != 0);

    inject_surrogate = (cJSON_bool)((control & 0x10) != 0);

    if (size >= 2) {
        assist = data[1];
        consumed = 2;

        switch (assist & 0x03) {
            case 1:

                wrap_count = (size_t)((assist >> 2) & 0x3F) * 20;
                break;
            case 2:

                pad_digits = (size_t)((assist >> 2) & 0x3F) * 2;
                break;
            case 3:

                obj_wrap_count = (size_t)((assist >> 2) & 0x3F) * 20;
                break;
            default:
                break;
        }
    }

    json_data = data + consumed;
    json_size = (size > consumed) ? (size - consumed) : 0;

    if (inject_surrogate) {
        size_t sample_index = (size_t)(assist % kSurrogateSampleCount);
        surrogate_sample = kSurrogateSamples[sample_index];
        surrogate_sample_len = strlen(surrogate_sample);
    }

    {
        size_t extra = (prepend_bom ? sizeof(kBOM) : 0)
                      + (2 * wrap_count)
                      + pad_digits
                      + (5 * obj_wrap_count)
                      + obj_wrap_count
                      + surrogate_sample_len;
        size_t pos = 0;
        size_t i;

        value = (char *)malloc(json_size + extra + 1);
        if (value == NULL) {
            return 0;
        }

        if (prepend_bom) {
            memcpy(value, kBOM, sizeof(kBOM));
            pos += sizeof(kBOM);
        }
        for (i = 0; i < wrap_count; i++) {
            value[pos++] = '[';
        }
        for (i = 0; i < obj_wrap_count; i++) {
            memcpy(value + pos, "{\"a\":", 5);
            pos += 5;
        }
        if (json_size > 0) {
            memcpy(value + pos, json_data, json_size);
            pos += json_size;
        }
        for (i = 0; i < pad_digits; i++) {
            value[pos++] = '0';
        }
        if (surrogate_sample_len > 0) {
            memcpy(value + pos, surrogate_sample, surrogate_sample_len);
            pos += surrogate_sample_len;
        }
        for (i = 0; i < obj_wrap_count; i++) {
            value[pos++] = '}';
        }
        for (i = 0; i < wrap_count; i++) {
            value[pos++] = ']';
        }
        value[pos] = '\0';
    }

    item = cJSON_ParseWithOpts(value, return_parse_end_ptr, require_null_terminated);
    if (item != NULL) {
        cJSON_Delete(item);
    }

    free(value);

    return 0;
}