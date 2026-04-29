/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright © 2009-2026 Inria.  All rights reserved.
 * See COPYING in top-level directory.
 */

/*
 * Fuzz harness for hwloc's base64 codec.
 * Tests round-trip correctness: encode fuzz bytes, then decode the result,
 * and verify the decoded output matches the original input.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Include the private header that declares the base64 functions */
#include "private/private.h"

/* Macro matching topology-xml.c usage */
#define BASE64_ENCODED_LENGTH(length) (4*(((length)+2)/3))

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Step 1: Encode the fuzz input bytes to base64 */
    size_t encoded_size = BASE64_ENCODED_LENGTH(size) + 1; /* +1 for null terminator */
    char *encoded_buf = (char *)malloc(encoded_size);
    if (!encoded_buf)
        return 0;

    int enc_ret = hwloc_encode_to_base64((const char *)data, size, encoded_buf, encoded_size);
    if (enc_ret < 0) {
        /* Should not happen with a correctly sized buffer */
        free(encoded_buf);
        return 0;
    }

    /* enc_ret is the number of base64 characters written (not counting '\0') */
    /* The encoded string is null-terminated */

    /* Step 2: Decode the base64 string back to raw bytes */
    /* targsize must be at least encoded_strlen * 3/4 + 1 */
    size_t encoded_strlen = strlen(encoded_buf);
    size_t decoded_size = encoded_strlen * 3 / 4 + 1;
    /* Ensure decoded_size is at least size (for round-trip) */
    if (decoded_size < size + 1)
        decoded_size = size + 1;

    char *decoded_buf = (char *)malloc(decoded_size);
    if (!decoded_buf) {
        free(encoded_buf);
        return 0;
    }

    int dec_ret = hwloc_decode_from_base64(encoded_buf, decoded_buf, decoded_size);
    if (dec_ret < 0) {
        /* Should not happen for valid base64 produced by encode */
        free(decoded_buf);
        free(encoded_buf);
        return 0;
    }

    /* Step 3: Verify round-trip correctness */
    /* dec_ret should equal the original size */
    assert(dec_ret == (int)size);
    /* The decoded bytes should match the original input */
    assert(memcmp(decoded_buf, data, size) == 0);

    free(decoded_buf);
    free(encoded_buf);
    return 0;
}
