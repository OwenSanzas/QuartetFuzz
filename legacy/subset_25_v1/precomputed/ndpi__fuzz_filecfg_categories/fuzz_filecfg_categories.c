#define _GNU_SOURCE
#define NDPI_LIB_COMPILATION
#include "ndpi_api.h"
#include "ndpi_private.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Mock/Copy parts of fuzz_common_code.c to keep it self-contained */
static int mem_alloc_state = 0;

__attribute__((no_sanitize("integer")))
static int fastrand() {
    if (!mem_alloc_state) return 1; /* No failures */
    mem_alloc_state = (214013 * mem_alloc_state + 2531011);
    return (mem_alloc_state >> 16) & 0x7FFF;
}

static void *malloc_wrapper(size_t size) {
    return (fastrand() % 16) ? malloc(size) : NULL;
}
static void free_wrapper(void *freeable) {
    free(freeable);
}
static void *calloc_wrapper(size_t nmemb, size_t size) {
    return (fastrand() % 16) ? calloc(nmemb, size) : NULL;
}
static void *realloc_wrapper(void *ptr, size_t size) {
    return (fastrand() % 16) ? realloc(ptr, size) : NULL;
}

static void fuzz_set_alloc_callbacks_and_seed(int seed) {
    ndpi_set_memory_alloction_functions(malloc_wrapper,
                                        free_wrapper,
                                        calloc_wrapper,
                                        realloc_wrapper,
                                        NULL, NULL,
                                        malloc_wrapper,
                                        free_wrapper);
    mem_alloc_state = seed;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    struct ndpi_detection_module_struct *ndpi_struct;
    FILE *fd;

    /* To allow memory allocation failures and deterministic reproduction */
    fuzz_set_alloc_callbacks_and_seed((int)size);

    ndpi_struct = ndpi_init_detection_module(NULL);
    if (ndpi_struct == NULL) {
        return 0;
    }

    /* Suppress logging for performance and cleaner output */
    ndpi_set_config_u64(ndpi_struct, NULL, "log.level", 0);

    /* Use fmemopen to treat the fuzz data as a file */
    fd = fmemopen((void *)data, size, "r");
    if (fd) {
        load_categories_file_fd(ndpi_struct, fd, NULL);
        fclose(fd);
    }

    /* Clean up */
    ndpi_exit_detection_module(ndpi_struct);

    return 0;
}
