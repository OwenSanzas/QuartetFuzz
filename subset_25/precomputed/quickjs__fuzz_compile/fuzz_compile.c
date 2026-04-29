/* Copyright 2020 Google Inc.

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#include "quickjs.h"
#include "quickjs-libc.h"
#include "fuzz/fuzz_common.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1)
        return 0;

    JSRuntime *rt = JS_NewRuntime();
    if (!rt)
        return 0;
    
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 0;
    }

    /* Set memory and stack limits and initialize common fuzzing state */
    test_one_input_init(rt, ctx);
    reset_nbinterrupts();

    /* Use the first byte to determine flags and the rest as JSON input */
    int flags = (data[0] & 1) ? JS_PARSE_JSON_EXT : 0;
    size_t json_size = size - 1;
    const uint8_t *json_data = data + 1;

    if (json_size > 0) {
        /* Create null-terminated buffer as required by JS_ParseJSON APIs */
        char *buf = malloc(json_size + 1);
        if (buf) {
            memcpy(buf, json_data, json_size);
            buf[json_size] = '\0';

            /* Entry point: Parse JSON string into JSValue */
            JSValue val = JS_ParseJSON2(ctx, buf, json_size, "<fuzz>", flags);
            
            if (!JS_IsException(val)) {
                /* If parsing succeeded, exercise the reverse operation: stringification */
                JSValue str_val = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
                JS_FreeValue(ctx, str_val);
            }

            /* Resource cleanup for the JSValue */
            JS_FreeValue(ctx, val);
            free(buf);
        }
    }

    /* Proper teardown sequence to prevent leaks */
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    return 0;
}
