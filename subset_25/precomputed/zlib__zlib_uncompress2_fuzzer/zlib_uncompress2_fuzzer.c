/*
 * Copyright (C) 1995-2026 Jean-loup Gailly, Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "zlib.h"

#ifdef __cplusplus
extern "C" {
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) {
        return 0;
    }

    /* Use a fixed large buffer for decompression to maximize coverage.
     * 1 MB is enough for many compressed streams and fits within typical memory limits.
     */
    z_size_t destLen = 1024 * 1024;
    Bytef *dest = (Bytef *)malloc(destLen);
    if (!dest) {
        return 0;
    }

    z_size_t sourceLen = (z_size_t)size;
    
    /* 
     * uncompress2_z performs internal stream initialization (inflateInit),
     * the core decompression loop (inflate), and termination (inflateEnd).
     * It handles various DEFLATE block types and malformed input.
     * This is the high-level, recommended entry point for memory-to-memory 
     * decompression as it manages the z_stream lifecycle internally.
     */
    int ret = uncompress2_z(dest, &destLen, (const Bytef *)data, &sourceLen);

    /* 
     * The return value 'ret' can be Z_OK, Z_DATA_ERROR, Z_MEM_ERROR, 
     * Z_BUF_ERROR, or Z_STREAM_ERROR. The fuzzer primarily looks for 
     * crashes or hangs within the library logic.
     */
    (void)ret;

    free(dest);
    return 0;
}

#ifdef __cplusplus
}
#endif
