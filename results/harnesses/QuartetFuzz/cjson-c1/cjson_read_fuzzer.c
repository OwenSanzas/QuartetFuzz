#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../cJSON.h"

static void walk_tree(const cJSON *node, int depth)
{
    if (node == NULL || depth > 1024)
    {
        return;
    }

    if (cJSON_IsObject(node) || cJSON_IsArray(node))
    {
        int size = cJSON_GetArraySize(node);
        int i;
        for (i = 0; i < size; i++)
        {
            cJSON *child = cJSON_GetArrayItem(node, i);
            walk_tree(child, depth + 1);
        }
    }
    else if (cJSON_IsString(node))
    {
        char *s = cJSON_GetStringValue((cJSON *)node);
        if (s != NULL)
        {
            volatile size_t len = strlen(s);
            (void)len;
        }
    }
    else if (cJSON_IsNumber(node))
    {
        volatile double v = cJSON_GetNumberValue((cJSON *)node);
        (void)v;
    }
    else if (cJSON_IsBool(node) || cJSON_IsNull(node) || cJSON_IsInvalid(node) || cJSON_IsRaw(node))
    {

    }
}

#ifdef __cplusplus
extern "C"
#endif
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1)
    {
        return 0;
    }

    uint8_t selector = data[0];
    const uint8_t *json_bytes = data + 1;
    size_t json_len = size - 1;

    char *buffer = (char *)malloc(json_len + 1);
    if (buffer == NULL)
    {
        return 0;
    }
    if (json_len > 0)
    {
        memcpy(buffer, json_bytes, json_len);
    }
    buffer[json_len] = '\0';

    if (selector & 0x01)
    {
        cJSON_Minify(buffer);
    }

    cJSON_bool require_null_terminated = (selector & 0x02) ? 1 : 0;

    const char *parse_end = NULL;

    cJSON *item = cJSON_ParseWithOpts(buffer, &parse_end, require_null_terminated);

    if (item != NULL)
    {

        walk_tree(item, 0);

        char *printed = NULL;
        if (selector & 0x04)
        {
            printed = cJSON_Print(item);
        }
        else
        {
            printed = cJSON_PrintUnformatted(item);
        }
        if (printed != NULL)
        {

            volatile size_t printed_len = strlen(printed);
            (void)printed_len;
            free(printed);
        }

        if (cJSON_IsObject(item))
        {
            cJSON *found = cJSON_GetObjectItemCaseSensitive(item, "key");
            (void)found;
        }

        cJSON_Delete(item);
    }

    const char *parse_end2 = NULL;
    cJSON *item2 = cJSON_ParseWithLengthOpts((const char *)json_bytes, json_len,
                                              &parse_end2,
                                              (selector & 0x08) ? 1 : 0);
    if (item2 != NULL)
    {
        cJSON_Delete(item2);
    }

    free(buffer);

    return 0;
}
