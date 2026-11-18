#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <zlib.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    std::vector<Bytef> source(Data, Data + Size);
    uLong sourceLen = static_cast<uLong>(Size);

    std::vector<uLongf> destLenCandidates = {
        0,
        1,
        16,
        64,
        256,
        static_cast<uLongf>(Size),
        static_cast<uLongf>(Size * 2),
        static_cast<uLongf>(Size * 4 + 64)
    };

    for (uLongf candidateLen : destLenCandidates) {
        if (candidateLen == 0) {
            continue;
        }

        std::vector<Bytef> dest(candidateLen);
        uLongf destLen = candidateLen;
        uLong localSourceLen = sourceLen;

        int ret = uncompress2(dest.data(), &destLen, source.data(), &localSourceLen);

        (void)ret;
    }

    {
        Bytef smallDest[4];
        uLongf smallDestLen = sizeof(smallDest);
        uLong localSourceLen = sourceLen;
        int ret = uncompress2(smallDest, &smallDestLen, source.data(), &localSourceLen);
        (void)ret;
    }

    {
        std::vector<Bytef> dest(64);
        uLongf destLen = static_cast<uLongf>(dest.size());
        uLong zeroSourceLen = 0;
        int ret = uncompress2(dest.data(), &destLen, source.data(), &zeroSourceLen);
        (void)ret;
    }

    if (sourceLen > 1) {
        std::vector<Bytef> dest(128);
        uLongf destLen = static_cast<uLongf>(dest.size());
        uLong smallerSourceLen = sourceLen - 1;
        int ret = uncompress2(dest.data(), &destLen, source.data(), &smallerSourceLen);
        (void)ret;
    }

    return 0;
}