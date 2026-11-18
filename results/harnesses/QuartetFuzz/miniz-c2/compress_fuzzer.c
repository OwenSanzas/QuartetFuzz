#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2)
        return 0;

    int level = (int)(int8_t)data[0];

    uint8_t size_ctl = data[1];

    const unsigned char *pSource = data + 2;
    mz_ulong source_len = (mz_ulong)(size - 2);

    mz_ulong bound = mz_compressBound(source_len);

    mz_ulong dest_cap;
    if (bound == 0) {
        dest_cap = 1;
    } else {
        dest_cap = (mz_ulong)(((mz_uint64)bound * (mz_uint64)(size_ctl + 1)) / 256);
        if (dest_cap == 0)
            dest_cap = 1;
    }

    unsigned char *pDest = (unsigned char *)malloc(dest_cap);
    if (!pDest)
        return 0;

    mz_ulong dest_len = dest_cap;
    int status = mz_compress2(pDest, &dest_len, pSource, source_len, level);

    if (status == MZ_OK) {

        mz_ulong decomp_cap = (source_len > 0) ? source_len : 1;
        unsigned char *pDecomp = (unsigned char *)malloc(decomp_cap);
        if (pDecomp) {
            mz_ulong decomp_len = decomp_cap;
            int unc_status = mz_uncompress(pDecomp, &decomp_len, pDest, dest_len);
            if (unc_status == MZ_OK && decomp_len == source_len && source_len > 0) {

                if (memcmp(pDecomp, pSource, (size_t)source_len) != 0) {

                }
            }
            free(pDecomp);
        }

        mz_ulong decomp_cap2 = (source_len > 0) ? source_len : 1;
        unsigned char *pDecomp2 = (unsigned char *)malloc(decomp_cap2);
        if (pDecomp2) {
            mz_ulong decomp_len2 = decomp_cap2;
            mz_ulong src_len2 = dest_len;
            mz_uncompress2(pDecomp2, &decomp_len2, pDest, &src_len2);
            free(pDecomp2);
        }
    }

    free(pDest);

    {
        mz_ulong dest_len2 = (bound > 0) ? bound : 1;
        unsigned char *pDest2 = (unsigned char *)malloc(dest_len2);
        if (pDest2) {
            mz_compress(pDest2, &dest_len2, pSource, source_len);
            free(pDest2);
        }
    }

    return 0;
}
