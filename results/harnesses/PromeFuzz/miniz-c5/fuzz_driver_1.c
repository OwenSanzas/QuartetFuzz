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

    const unsigned char *pSource = Data;
    mz_ulong source_len = (mz_ulong)Size;

    mz_ulong dest_capacities[] = {
        0,
        1,
        16,
        64,
        256,
        1024,
        (mz_ulong)(Size * 2 + 16)
    };

    size_t num_tests = sizeof(dest_capacities) / sizeof(dest_capacities[0]);

    for (size_t i = 0; i < num_tests; i++) {
        mz_ulong dest_len = dest_capacities[i];
        unsigned char *pDest = NULL;

        if (dest_len > 0) {
            pDest = (unsigned char *)malloc(dest_len);
            if (!pDest) {
                continue;
            }
            memset(pDest, 0, dest_len);
        }

        mz_ulong actual_dest_len = dest_len;
        int ret = mz_uncompress(pDest, &actual_dest_len, pSource, source_len);

        (void)ret;

        if (pDest) {
            free(pDest);
        }
    }

    if (Size > 4) {
        unsigned char small_dest[32];
        mz_ulong small_dest_len = sizeof(small_dest);
        mz_ulong zero_source_len = 0;
        int ret2 = mz_uncompress(small_dest, &small_dest_len, pSource, zero_source_len);
        (void)ret2;
    }

    if (Size >= 1) {
        unsigned char small_dest2[32];
        mz_ulong small_dest_len2 = sizeof(small_dest2);
        mz_ulong tiny_source_len = 1;
        int ret3 = mz_uncompress(small_dest2, &small_dest_len2, pSource, tiny_source_len);
        (void)ret3;
    }

    FILE *fp = fopen("./dummy_file", "wb");
    if (fp) {
        fwrite(Data, 1, Size, fp);
        fclose(fp);
    }

    return 0;
}