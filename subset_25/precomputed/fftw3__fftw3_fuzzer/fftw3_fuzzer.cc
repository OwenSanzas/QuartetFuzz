#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "api/fftw3.h"

/*
 * Copyright (c) 2003, 2007-14 Matteo Frigo
 * Copyright (c) 2003, 2007-14 Massachusetts Institute of Technology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * LibFuzzer harness for FFTW3 DFT planner and executor.
 * This harness tests the lifecycle of DFT plans, including planning and execution.
 * It maps fuzz input to wisdom, dimensions, signs, and flags.
 */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Minimum size required for basic parameters
    if (size < 16) return 0;

    // Set a time limit for planning to avoid timeouts in the fuzzer
    // A small limit like 0.1s ensures the fuzzer stays responsive.
    fftw_set_timelimit(0.1);

    // Use a small portion of data to potentially import wisdom.
    // The first byte determines the length of the wisdom string.
    size_t wisdom_len = data[0] % (size / 4);
    if (wisdom_len > 0) {
        char *wisdom_str = (char *)malloc(wisdom_len + 1);
        if (wisdom_str) {
            memcpy(wisdom_str, data + 1, wisdom_len);
            wisdom_str[wisdom_len] = '\0';
            fftw_import_wisdom_from_string(wisdom_str);
            free(wisdom_str);
        }
    }

    // Offset input pointer past wisdom
    const uint8_t *d = data + 1 + wisdom_len;
    size_t remaining = size - 1 - wisdom_len;

    // Need enough bytes for rank, dims, sign, and flags
    if (remaining < 12) return 0;

    // Fuzz rank (1 to 3) and dimensions (1 to 128)
    int rank = (d[0] % 3) + 1;
    int dims[3];
    long total_n = 1;
    for (int i = 0; i < rank; ++i) {
        dims[i] = (d[1+i] % 128) + 1;
        total_n *= dims[i];
    }
    
    // Safety check for total_n to avoid excessive memory usage or timeouts.
    // FFTW can handle large transforms, but for fuzzing we want many small ones.
    if (total_n > 1024 * 64) return 0;

    // Map sign: -1 (FFTW_FORWARD) or +1 (FFTW_BACKWARD)
    int sign = (d[4] % 2) ? FFTW_FORWARD : FFTW_BACKWARD;

    // Map a subset of documented flags to the fuzz input.
    unsigned flags_raw = d[5];
    unsigned actual_flags = 0;
    if (flags_raw & 0x01) actual_flags |= FFTW_ESTIMATE;
    if (flags_raw & 0x02) actual_flags |= FFTW_MEASURE;
    if (flags_raw & 0x04) actual_flags |= FFTW_PATIENT;
    if (flags_raw & 0x08) actual_flags |= FFTW_EXHAUSTIVE;
    if (flags_raw & 0x10) actual_flags |= FFTW_UNALIGNED;
    if (flags_raw & 0x20) actual_flags |= FFTW_CONSERVE_MEMORY;
    if (flags_raw & 0x40) actual_flags |= FFTW_PRESERVE_INPUT;

    // Shift data pointer past parameters
    d += 6;
    remaining -= 6;

    // Allocate buffers using fftw_malloc for proper alignment
    fftw_complex *in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * total_n);
    fftw_complex *out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * total_n);

    if (in && out) {
        // Initialize input buffer with remaining fuzz data if available.
        size_t bytes_needed = sizeof(fftw_complex) * total_n;
        size_t bytes_to_copy = (remaining < bytes_needed) ? remaining : bytes_needed;
        if (bytes_to_copy > 0) {
            memcpy(in, d, bytes_to_copy);
            // Zero-fill the rest of the buffer if input was short
            if (bytes_to_copy < bytes_needed) {
                memset((char*)in + bytes_to_copy, 0, bytes_needed - bytes_to_copy);
            }
        } else {
            memset(in, 0, bytes_needed);
        }

        // Create the plan
        fftw_plan plan = fftw_plan_dft(rank, dims, in, out, sign, actual_flags);
        if (plan) {
            // Execute the transform
            fftw_execute(plan);
            // Destroy the plan to free internal structures
            fftw_destroy_plan(plan);
        }
    }

    // Free buffers
    fftw_free(in);
    fftw_free(out);
    
    // Clean up global planner state to prevent state leak across iterations
    fftw_cleanup();

    return 0;
}

#ifdef __cplusplus
}
#endif
