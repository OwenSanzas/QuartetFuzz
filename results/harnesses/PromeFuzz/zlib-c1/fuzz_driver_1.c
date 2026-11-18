#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    int level = (int)(Data[0] % 11) - 1;

    const Bytef *source = (const Bytef *)Data;
    uLong sourceLen = (uLong)Size;

    uLongf destLen = compressBound(sourceLen);
    if (destLen == 0) {
        destLen = 1;
    }

    Bytef *dest = (Bytef *)malloc(destLen);
    if (dest == NULL) {
        return 0;
    }

    int ret = compress2(dest, &destLen, source, sourceLen, level);

    if (ret == Z_OK) {

        uLongf uncompLen = sourceLen > 0 ? sourceLen : 1;
        Bytef *uncompBuf = (Bytef *)malloc(uncompLen);
        if (uncompBuf != NULL) {
            int uret = uncompress(uncompBuf, &uncompLen, dest, destLen);
            (void)uret;
            free(uncompBuf);
        }
    }

    free(dest);

    {
        uLongf smallDestLen = 1;
        Bytef *smallDest = (Bytef *)malloc(smallDestLen);
        if (smallDest != NULL) {
            int ret2 = compress2(smallDest, &smallDestLen, source, sourceLen, level);
            (void)ret2;
            free(smallDest);
        }
    }

    {
        uLongf zeroDestLen = 0;
        Bytef dummy;
        int ret3 = compress2(&dummy, &zeroDestLen, source, sourceLen, level);
        (void)ret3;
    }

    {
        uLongf destLen2 = compressBound(0);
        if (destLen2 == 0) {
            destLen2 = 1;
        }
        Bytef *dest2 = (Bytef *)malloc(destLen2);
        if (dest2 != NULL) {
            int ret4 = compress2(dest2, &destLen2, source, 0, level);
            (void)ret4;
            free(dest2);
        }
    }

    FILE *f = fopen("./dummy_file", "wb");
    if (f != NULL) {
        fwrite(Data, 1, Size, f);
        fclose(f);
    }

    return 0;
}