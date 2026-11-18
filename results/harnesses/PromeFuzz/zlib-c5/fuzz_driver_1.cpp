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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    uLong sourceLen = static_cast<uLong>(Size);
    const Bytef *source = reinterpret_cast<const Bytef *>(Data);

    uint8_t multiplierByte = Data[0];
    size_t multiplier = (multiplierByte % 8) + 1;
    size_t destCapacity = sourceLen * multiplier + 128;

    const size_t kMaxDest = 10 * 1024 * 1024;
    if (destCapacity > kMaxDest) {
        destCapacity = kMaxDest;
    }

    std::vector<Bytef> dest(destCapacity);
    uLongf destLen = static_cast<uLongf>(destCapacity);

    int ret = uncompress(dest.data(), &destLen, source, sourceLen);
    (void)ret;

    if (destCapacity > 16) {
        size_t smallerCapacity = destCapacity / 4;
        if (smallerCapacity == 0) {
            smallerCapacity = 1;
        }
        std::vector<Bytef> dest2(smallerCapacity);
        uLongf destLen2 = static_cast<uLongf>(smallerCapacity);
        int ret2 = uncompress(dest2.data(), &destLen2, source, sourceLen);
        (void)ret2;
    }

    {
        Bytef dummy[1];
        uLongf destLen3 = 0;
        int ret3 = uncompress(dummy, &destLen3, source, sourceLen);
        (void)ret3;
    }

    return 0;
}