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

    int level = (int)(Data[0] % 11) - 1;
    const unsigned char *pSource = Data + 1;
    mz_ulong source_len = (mz_ulong)(Size - 1);

    mz_ulong dest_len = compressBound(source_len);
    if (dest_len == 0) {
        dest_len = 128;
    }

    unsigned char *pDest = (unsigned char *)malloc(dest_len);
    if (!pDest) {
        return 0;
    }

    mz_ulong actual_dest_len = dest_len;
    int ret = mz_compress2(pDest, &actual_dest_len, pSource, source_len, level);
    (void)ret;

    if (dest_len > 1) {
        mz_ulong small_dest_len = 1;
        unsigned char small_buf[1];
        int ret2 = mz_compress2(small_buf, &small_dest_len, pSource, source_len, level);
        (void)ret2;
    }

    if (source_len > 0) {
        mz_ulong zero_dest_len = dest_len;
        int ret3 = mz_compress2(pDest, &zero_dest_len, pSource, 0, level);
        (void)ret3;
    }

    {
        mz_ulong invalid_dest_len = dest_len;
        int ret4 = mz_compress2(pDest, &invalid_dest_len, pSource, source_len, 100);
        (void)ret4;
    }

    FILE *f = fopen("./dummy_file", "wb");
    if (f) {
        fwrite(pDest, 1, actual_dest_len, f);
        fclose(f);
    }

    free(pDest);
    return 0;
}