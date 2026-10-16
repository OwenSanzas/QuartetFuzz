/*
 * Copyright (C) 1995-2026 Jean-loup Gailly and Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"

/**
 * LibFuzzer harness for zlib's compress2_z API.
 * This fuzzer tests the high-level compression interface which handles
 * its own z_stream lifecycle.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Need at least one byte for the compression level */
    if (size < 1) {
        return 0;
    }

    /* The first byte of input is used to determine the compression level.
     * Valid levels are Z_DEFAULT_COMPRESSION (-1) or 0-9.
     * Mapping data[0] [0, 255] to [-1, 9].
     */
    int level = (int)(data[0] % 11) - 1;

    /* The rest of the input is the data to be compressed */
    const Bytef *source = (const Bytef *)(data + 1);
    z_size_t sourceLen = (z_size_t)(size - 1);

    /* Use compressBound_z to calculate the maximum required destination buffer size.
     * This ensures P2.2 compliance (destLen must be at least compressBound).
     */
    z_size_t destLen = compressBound_z(sourceLen);
    
    /* compressBound_z may return (z_size_t)-1 on overflow, although unlikely
     * with typical LibFuzzer input sizes.
     */
    if (destLen == (z_size_t)-1) {
        return 0;
    }

    /* Allocate the destination buffer. */
    Bytef *dest = (Bytef *)malloc(destLen);
    if (!dest) {
        return 0;
    }

    /* compress2_z handles deflateInit, deflate, and deflateEnd internally (P2.3, P2.5).
     * It maps the fuzz input directly to the library's compression engine (P1.4).
     */
    int result = compress2_z(dest, &destLen, source, sourceLen, level);

    /* Clean up resources (P1.1, P2.5). */
    free(dest);

    return 0;
}
