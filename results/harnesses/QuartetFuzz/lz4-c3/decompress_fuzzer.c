#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_helpers.h"
#include "lz4.h"
#include "fuzz_data_producer.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FUZZ_dataProducer_t *producer = FUZZ_dataProducer_create(data, size);

    uint32_t const dictSplitSeed = FUZZ_dataProducer_retrieve32(producer);
    uint32_t const dstCapSeed    = FUZZ_dataProducer_retrieve32(producer);
    uint32_t const modeSeed      = FUZZ_dataProducer_retrieve32(producer);
    size = FUZZ_dataProducer_remainingBytes(producer);

    if (size > (128 * 1024)) size = 128 * 1024;

    if (size == 0) {
        FUZZ_dataProducer_free(producer);
        return 0;
    }

    size_t const maxDictSize = 70 * 1024;
    size_t const dictSize = (size_t)FUZZ_getRange_from_uint32(
        dictSplitSeed, 0, (uint32_t)MIN(size, maxDictSize));
    const uint8_t* const dictRaw = data;
    size_t const srcSize = size - dictSize;
    const uint8_t* const srcRaw = data + dictSize;

    size_t const dstCapMax = MIN(srcSize * 4 + 64, (size_t)(1 << 20));
    size_t const dstCapacity = (size_t)FUZZ_getRange_from_uint32(
        dstCapSeed, 0, (uint32_t)dstCapMax);

    size_t const prefixBufSize = dictSize + dstCapacity;
    char* const prefixBuf = (char*)malloc(prefixBufSize ? prefixBufSize : 1);
    FUZZ_ASSERT(prefixBuf);
    if (dictSize) memcpy(prefixBuf, dictRaw, dictSize);
    char* const dstPrefixed = prefixBuf + dictSize;

    char* const extDict = (char*)malloc(dictSize ? dictSize : 1);
    FUZZ_ASSERT(extDict);
    if (dictSize) memcpy(extDict, dictRaw, dictSize);

    char* const dstExt = (char*)malloc(dstCapacity ? dstCapacity : 1);
    FUZZ_ASSERT(dstExt);

    int const usePrefixDict = (modeSeed & 1);
    const char* const dictStart = usePrefixDict ? prefixBuf : extDict;
    char* const dst = usePrefixDict ? dstPrefixed : dstExt;

    int const ret = LZ4_decompress_safe_usingDict(
        (const char*)srcRaw, dst, (int)srcSize, (int)dstCapacity,
        dictStart, (int)dictSize);

    if (ret >= 0) {

        unsigned const crc = XXH32(dst, (size_t)ret, 0);
        (void)crc;
    }

    free(prefixBuf);
    free(extDict);
    free(dstExt);

    if (srcSize > 0) {
        size_t const plainSize = MIN(srcSize, (size_t)(64 * 1024));
        const char* const plain = (const char*)srcRaw;

        size_t const primeCap = (size_t)LZ4_compressBound((int)dictSize) + 16;
        char* const primeBuf = (char*)malloc(primeCap ? primeCap : 1);
        FUZZ_ASSERT(primeBuf);

        size_t const compressBound = (size_t)LZ4_compressBound((int)plainSize);
        char* const compressed = (char*)malloc(compressBound);
        FUZZ_ASSERT(compressed);

        LZ4_stream_t lz4Stream;
        LZ4_initStream(&lz4Stream, sizeof(lz4Stream));
        if (dictSize > 0) {
            int const primeRet = LZ4_compress_fast_continue(
                &lz4Stream, (const char*)dictRaw, primeBuf, (int)dictSize, (int)primeCap, 1);
            FUZZ_ASSERT(primeRet > 0);
        }
        int const compressedSize = LZ4_compress_fast_continue(
            &lz4Stream, plain, compressed, (int)plainSize, (int)compressBound, 1);
        FUZZ_ASSERT(compressedSize > 0);

        size_t const dstBufSize2 = dictSize + plainSize + 64;
        char* const dstBuf2 = (char*)malloc(dstBufSize2);
        FUZZ_ASSERT(dstBuf2);
        memset(dstBuf2, 0, dstBufSize2);
        if (dictSize) memcpy(dstBuf2, dictRaw, dictSize);

        char* const extDict2 = (char*)malloc(dictSize ? dictSize : 1);
        FUZZ_ASSERT(extDict2);
        if (dictSize) memcpy(extDict2, dictRaw, dictSize);

        const char* const dictStart2 = usePrefixDict ? dstBuf2 : extDict2;
        char* const dst2 = dstBuf2 + dictSize;

        int const ret2 = LZ4_decompress_safe_usingDict(
            compressed, dst2, compressedSize, (int)(plainSize + 16),
            dictStart2, (int)dictSize);

        if (ret2 >= 0) {
            unsigned const crc2 = XXH32(dst2, (size_t)ret2, 0);
            (void)crc2;
            if ((size_t)ret2 == plainSize) {
                FUZZ_ASSERT_MSG(!memcmp(dst2, plain, plainSize),
                                "LZ4_decompress_safe_usingDict corrupted data!");
            }
        }

        {
            size_t const maxOut = plainSize + 1;
            char* const partialDst = (char*)malloc(maxOut);
            FUZZ_ASSERT(partialDst);
            size_t const targetOut = (size_t)FUZZ_getRange_from_uint32(
                modeSeed, 0, (uint32_t)plainSize);

            int const partialRet = LZ4_decompress_safe_partial_usingDict(
                compressed, partialDst, compressedSize,
                (int)targetOut, (int)maxOut, dictStart2, (int)dictSize);
            if (partialRet >= 0) {
                FUZZ_ASSERT((size_t)partialRet <= maxOut);
            }
            free(partialDst);
        }

        {
            LZ4_streamDecode_t* const dstream = LZ4_createStreamDecode();
            FUZZ_ASSERT(dstream);
            LZ4_setStreamDecode(dstream, dictStart2, (int)dictSize);

            size_t const contCapacity = plainSize + 16;
            char* const contDst = (char*)malloc(contCapacity);
            FUZZ_ASSERT(contDst);

            int const contRet = LZ4_decompress_safe_continue(
                dstream, compressed, contDst, compressedSize, (int)contCapacity);
            if (contRet >= 0) {
                FUZZ_ASSERT((size_t)contRet <= contCapacity);
            }

            free(contDst);
            LZ4_freeStreamDecode(dstream);
        }

        free(dstBuf2);
        free(extDict2);
        free(compressed);
        free(primeBuf);
    }

    FUZZ_dataProducer_free(producer);
    return 0;
}
