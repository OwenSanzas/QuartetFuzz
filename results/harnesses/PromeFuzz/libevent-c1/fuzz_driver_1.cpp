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
#include <cstring>

extern "C" {
#include "event2/buffer.h"
#include "event2/event.h"
#include "tag.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    struct evbuffer *src = evbuffer_new();
    struct evbuffer *dst = evbuffer_new();

    if (!src || !dst) {
        if (src) evbuffer_free(src);
        if (dst) evbuffer_free(dst);
        return 0;
    }

    evbuffer_add(src, Data, Size);

    int iterations = 0;
    const int max_iterations = 64;

    while (evbuffer_get_length(src) > 0 && iterations < max_iterations) {
        ev_uint32_t tag = 0;

        evbuffer_drain(dst, evbuffer_get_length(dst));

        int result = evtag_unmarshal(src, &tag, dst);

        if (result == -1) {

            break;
        }

        iterations++;
    }

    evbuffer_free(src);
    evbuffer_free(dst);

    return 0;
}