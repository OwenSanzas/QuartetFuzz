#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "avahi-common/strlst.h"
#include "avahi-common/malloc.h"

#ifdef HAVE_NALLOCFUZZ
#include "nallocinc.c"
#endif

static size_t g_alloc_count;
static size_t g_fail_after;

static void *fuzz_malloc(size_t size) {
    if (g_alloc_count++ == g_fail_after)
        return NULL;
    return malloc(size);
}

static void fuzz_free(void *p) {
    free(p);
}

static void *fuzz_realloc(void *p, size_t size) {
    if (g_alloc_count++ == g_fail_after)
        return NULL;
    return realloc(p, size);
}

static const AvahiAllocator fuzz_allocator = {
    .malloc  = fuzz_malloc,
    .free    = fuzz_free,
    .realloc = fuzz_realloc,
    .calloc  = NULL,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    AvahiStringList *list = NULL;
    int r;
    uint8_t ctrl;

#ifdef HAVE_NALLOCFUZZ
    nalloc_start(data, size);
#endif

    if (size < 1) {
#ifdef HAVE_NALLOCFUZZ
        nalloc_end();
#endif
        return 0;
    }

    ctrl = data[size - 1];

    if (ctrl == 0xFF) {

        avahi_set_allocator(NULL);
    } else {
        g_alloc_count = 0;
        g_fail_after  = ctrl;
        avahi_set_allocator(&fuzz_allocator);
    }

    r = avahi_string_list_parse(data, size - 1, &list);

    avahi_set_allocator(NULL);

    if (r == 0 && list != NULL) {
        avahi_string_list_free(list);
    }

#ifdef HAVE_NALLOCFUZZ
    nalloc_end();
#endif

    return 0;
}