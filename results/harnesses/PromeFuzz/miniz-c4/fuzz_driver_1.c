#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "build/amalgamation/miniz.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    mz_ulong source_len = (mz_ulong)Size;

    unsigned char *pSource = (unsigned char *)malloc(Size);
    if (!pSource) {
        return 0;
    }
    memcpy(pSource, Data, Size);

    size_t dest_sizes[] = {0, 1, 16, 64, 256, 1024, 4096, Size * 2 + 16};

    for (size_t i = 0; i < sizeof(dest_sizes) / sizeof(dest_sizes[0]); i++) {
        size_t dest_cap = dest_sizes[i];
        unsigned char *pDest = NULL;

        if (dest_cap > 0) {
            pDest = (unsigned char *)malloc(dest_cap);
            if (!pDest) {
                continue;
            }
        }

        mz_ulong dest_len = (mz_ulong)dest_cap;
        mz_ulong src_len_copy = source_len;

        int status = mz_uncompress2(pDest, &dest_len, pSource, &src_len_copy);

        (void)status;

        if (pDest) {
            free(pDest);
        }
    }

    if (Size > 1) {
        mz_ulong partial_src_len = (mz_ulong)(Size / 2);
        unsigned char *pDest = (unsigned char *)malloc(1024);
        if (pDest) {
            mz_ulong dest_len = 1024;
            int status = mz_uncompress2(pDest, &dest_len, pSource, &partial_src_len);
            (void)status;
            free(pDest);
        }
    }

    {
        unsigned char *pDest = (unsigned char *)malloc(64);
        if (pDest) {
            mz_ulong dest_len = 64;
            mz_ulong zero_src_len = 0;
            int status = mz_uncompress2(pDest, &dest_len, pSource, &zero_src_len);
            (void)status;
            free(pDest);
        }
    }

    free(pSource);

    return 0;
}