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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* handle timeouts from infinite loops */
static int nbinterrupts = 0;
static int interrupt_handler(JSRuntime *rt, void *opaque) {
    nbinterrupts++;
    return (nbinterrupts > 100);
}

static void my_test_init(JSRuntime *rt, JSContext *ctx) {
    /* 64 MB memory limit */
    JS_SetMemoryLimit(rt, 0x4000000);
    /* 64 KB stack size */
    JS_SetMaxStackSize(rt, 0x10000);
    JS_SetInterruptHandler(rt, interrupt_handler, NULL);
    js_std_add_helpers(ctx, 0, NULL);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1)
        return 0;

    /* Use the first byte to decide the flags (e.g. extended JSON) */
    int flags = (data[0] & 1) ? JS_PARSE_JSON_EXT : 0;
    data++;
    size--;

    JSRuntime *rt = JS_NewRuntime();
    if (!rt)
        return 0;
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return 0;
    }

    my_test_init(rt, ctx);
    nbinterrupts = 0;

    /* JS_ParseJSON2 requires a null-terminated buffer */
    char *buf = malloc(size + 1);
    if (!buf) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';

    /* Target the JSON parser */
    JSValue val = JS_ParseJSON2(ctx, buf, size, "<none>", flags);
    
    if (!JS_IsException(val)) {
        /* Exercise the stringifier too if we successfully parsed */
        JSValue str_val = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
        JS_FreeValue(ctx, str_val);
        
        /* Process any pending jobs (though JSON shouldn't really have any) */
        js_std_loop(ctx);
        JS_FreeValue(ctx, val);
    } else {
        /* Clear the exception from the context */
        JSValue exc = JS_GetException(ctx);
        JS_FreeValue(ctx, exc);
    }

    free(buf);
    /* Standard cleanup for QuickJS fuzzers */
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    return 0;
}
