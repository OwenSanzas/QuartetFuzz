#define LZ4F_STATIC_LINKING_ONLY

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_helpers.h"
#include "fuzz_data_producer.h"
#include "lz4frame.h"

#define FUZZ_MAX_GEN_CHUNK 997
#define FUZZ_AMPLIFY_TARGET (96 * 1024)

static size_t FUZZ_buildFrame(const uint8_t *payload, size_t payloadSize,
                               LZ4F_preferences_t const *prefs,
                               uint8_t **outBuf)
{
    LZ4F_cctx *cctx = NULL;
    if (LZ4F_isError(LZ4F_createCompressionContext(&cctx, LZ4F_VERSION)) || cctx == NULL) {
        return 0;
    }

    {
        size_t const numChunks = (payloadSize / FUZZ_MAX_GEN_CHUNK) + 2;
        size_t const bound = LZ4F_compressFrameBound(payloadSize, prefs)
                              + numChunks * (LZ4F_BLOCK_HEADER_SIZE +
                                             LZ4F_BLOCK_CHECKSUM_SIZE + 256)
                              + LZ4F_HEADER_SIZE_MAX + 256;
        uint8_t *buf = (uint8_t *)malloc(bound);
        size_t pos;
        size_t off = 0;
        size_t chunkIdx = 0;
        int failed = 0;

        if (!buf) {
            LZ4F_freeCompressionContext(cctx);
            return 0;
        }

        pos = LZ4F_compressBegin(cctx, buf, bound, prefs);
        if (LZ4F_isError(pos)) {
            free(buf);
            LZ4F_freeCompressionContext(cctx);
            return 0;
        }

        while (off < payloadSize) {
            size_t const chunk = MIN(payloadSize - off, (size_t)FUZZ_MAX_GEN_CHUNK);
            size_t const cSize = LZ4F_compressUpdate(cctx, buf + pos, bound - pos,
                                                      payload + off, chunk, NULL);
            if (LZ4F_isError(cSize)) { failed = 1; break; }
            pos += cSize;
            off += chunk;
            chunkIdx++;

            if ((chunkIdx % 4) == 0) {
                size_t const fSize = LZ4F_flush(cctx, buf + pos, bound - pos, NULL);
                if (!LZ4F_isError(fSize)) {
                    pos += fSize;
                }
            }
        }

        if (!failed) {
            size_t const endSize = LZ4F_compressEnd(cctx, buf + pos, bound - pos, NULL);
            if (!LZ4F_isError(endSize)) {
                pos += endSize;
            } else {
                failed = 1;
            }
        }

        LZ4F_freeCompressionContext(cctx);

        if (failed) {
            free(buf);
            return 0;
        }

        *outBuf = buf;
        return pos;
    }
}

static uint8_t *FUZZ_amplify(const uint8_t *src, size_t srcLen,
                              size_t targetLen, size_t *outLen)
{
    uint8_t *buf;
    size_t off = 0;

    *outLen = 0;
    if (srcLen == 0) {
        return NULL;
    }
    buf = (uint8_t *)malloc(targetLen);
    if (!buf) {
        return NULL;
    }
    while (off < targetLen) {
        size_t const chunk = MIN(srcLen, targetLen - off);
        memcpy(buf + off, src, chunk);
        off += chunk;
    }
    *outLen = targetLen;
    return buf;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    FUZZ_dataProducer_t *producer = FUZZ_dataProducer_create(data, size);

    uint32_t rngState = FUZZ_dataProducer_retrieve32(producer);
    uint32_t const modeSeed = FUZZ_dataProducer_retrieve32(producer);
    uint32_t const capacitySeed = FUZZ_dataProducer_retrieve32(producer);

    LZ4F_preferences_t prefs = FUZZ_dataProducer_preferences(producer);

    size_t const remaining = FUZZ_dataProducer_remainingBytes(producer);
    const uint8_t *payload = data;

    unsigned const mode = modeSeed % 6;

    uint32_t const amplifySeed = FUZZ_dataProducer_retrieve32(producer);
    unsigned const doAmplify = (mode == 1 || mode == 2) &&
                                remaining > 0 &&
                                ((amplifySeed % 4) == 0);

    uint8_t *amplified = NULL;
    const uint8_t *buildSrc = payload;
    size_t buildLen = remaining;
    if (doAmplify) {
        size_t ampLen = 0;
        amplified = FUZZ_amplify(payload, remaining, FUZZ_AMPLIFY_TARGET, &ampLen);
        if (amplified != NULL && ampLen > 0) {
            buildSrc = amplified;
            buildLen = ampLen;
        }
    }

    uint32_t const wantContentSize = FUZZ_dataProducer_retrieve32(producer) & 1u;
    {
        uint32_t const wantDictID = FUZZ_dataProducer_retrieve32(producer);

        prefs.frameInfo.contentSize = wantContentSize ? (unsigned long long)buildLen : 0;
        prefs.frameInfo.dictID = wantDictID;
    }

    uint8_t *frameBuf = NULL;
    size_t frameSize = 0;
    uint8_t *ownedBuf = NULL;

    if (mode == 1 || mode == 2) {
        uint8_t *built = NULL;
        size_t const builtSize = FUZZ_buildFrame(buildSrc, buildLen, &prefs, &built);
        if (built != NULL && builtSize > 0) {
            if (mode == 2) {

                uint32_t corruptState = rngState ^ 0x9E3779B9u;
                uint32_t const corruptStrategy = FUZZ_rand32(&corruptState, 0, 1);

                if (corruptStrategy == 0 && wantContentSize && buildLen > 0 &&
                        builtSize > 14) {

                    size_t const idx = 6 + (size_t)FUZZ_rand32(&corruptState, 0, 7);
                    built[idx] ^= (uint8_t)FUZZ_rand32(&corruptState, 1, 255);
                } else if (builtSize > 16) {

                    size_t const headerGuess =
                            (size_t)LZ4F_HEADER_SIZE_MAX + LZ4F_BLOCK_HEADER_SIZE;
                    size_t const startOff = MIN(headerGuess, builtSize - 1);
                    unsigned const nCorrupt = FUZZ_rand32(&corruptState, 1, 8);
                    unsigned i;
                    for (i = 0; i < nCorrupt; i++) {
                        size_t const idx = startOff + FUZZ_rand32(&corruptState, 0,
                                (uint32_t)(builtSize - 1 - startOff));
                        built[idx] ^= (uint8_t)FUZZ_rand32(&corruptState, 1, 255);
                    }
                }
            }
            frameBuf = built;
            frameSize = builtSize;
            ownedBuf = built;
        } else if (built != NULL) {
            free(built);
        }
    } else if (mode == 3) {

        size_t const skipHeaderSize = 8;
        size_t const skipPayload = MIN(remaining, (size_t)4096);
        uint8_t *built = (uint8_t *)malloc(skipHeaderSize + skipPayload);
        if (built != NULL) {
            uint32_t nibbleState = rngState;
            uint32_t const nibble = FUZZ_rand32(&nibbleState, 0, 0xF);
            uint32_t const magic = 0x184D2A50u | nibble;
            built[0] = (uint8_t)(magic & 0xFF);
            built[1] = (uint8_t)((magic >> 8) & 0xFF);
            built[2] = (uint8_t)((magic >> 16) & 0xFF);
            built[3] = (uint8_t)((magic >> 24) & 0xFF);
            built[4] = (uint8_t)(skipPayload & 0xFF);
            built[5] = (uint8_t)((skipPayload >> 8) & 0xFF);
            built[6] = (uint8_t)((skipPayload >> 16) & 0xFF);
            built[7] = (uint8_t)((skipPayload >> 24) & 0xFF);
            if (skipPayload > 0) {
                memcpy(built + skipHeaderSize, payload, skipPayload);
            }
            frameBuf = built;
            frameSize = skipHeaderSize + skipPayload;
            ownedBuf = built;
        }
    } else if (mode == 4) {

        uint8_t hdr[32];
        size_t hpos = 0;
        uint32_t const magic = LZ4F_MAGICNUMBER;
        uint32_t const flgSeed = FUZZ_dataProducer_retrieve32(producer);
        uint32_t const bdSeed = FUZZ_dataProducer_retrieve32(producer);
        uint8_t const FLG = (uint8_t)flgSeed;
        uint8_t const BD = (uint8_t)bdSeed;
        unsigned const contentSizeFlagHdr = (FLG >> 3) & 1u;
        unsigned const dictIDFlagHdr = FLG & 1u;

        hdr[hpos++] = (uint8_t)(magic & 0xFF);
        hdr[hpos++] = (uint8_t)((magic >> 8) & 0xFF);
        hdr[hpos++] = (uint8_t)((magic >> 16) & 0xFF);
        hdr[hpos++] = (uint8_t)((magic >> 24) & 0xFF);
        hdr[hpos++] = FLG;
        hdr[hpos++] = BD;

        if (contentSizeFlagHdr) {
            uint32_t const csLo = FUZZ_dataProducer_retrieve32(producer);
            uint32_t const csHi = FUZZ_dataProducer_retrieve32(producer);
            uint64_t const contentSizeVal =
                    ((uint64_t)csHi << 32) | (uint64_t)csLo;
            int i;
            for (i = 0; i < 8; i++) {
                hdr[hpos++] = (uint8_t)((contentSizeVal >> (8 * i)) & 0xFF);
            }
        }
        if (dictIDFlagHdr) {
            uint32_t const dictIDVal = FUZZ_dataProducer_retrieve32(producer);
            hdr[hpos++] = (uint8_t)(dictIDVal & 0xFF);
            hdr[hpos++] = (uint8_t)((dictIDVal >> 8) & 0xFF);
            hdr[hpos++] = (uint8_t)((dictIDVal >> 16) & 0xFF);
            hdr[hpos++] = (uint8_t)((dictIDVal >> 24) & 0xFF);
        }

        hdr[hpos++] = 0;

        {
            size_t const tailLen = MIN(remaining, (size_t)4096);
            uint8_t *built = (uint8_t *)malloc(hpos + tailLen);
            if (built != NULL) {
                memcpy(built, hdr, hpos);
                if (tailLen > 0) {
                    memcpy(built + hpos, payload, tailLen);
                }
                frameBuf = built;
                frameSize = hpos + tailLen;
                ownedBuf = built;
            }
        }
    } else if (mode == 5) {

        LZ4F_preferences_t hdrPrefs = prefs;
        size_t const rawBlockSize = LZ4F_getBlockSize(hdrPrefs.frameInfo.blockSizeID);
        size_t const maxBlock = LZ4F_isError(rawBlockSize) ? (size_t)(64 * 1024) : rawBlockSize;
        size_t const blockTarget = MIN(maxBlock, (size_t)(8 * 1024));
        size_t blockPayloadLen = MIN(remaining, blockTarget);
        uint8_t *blockPayload = NULL;

        if (blockPayloadLen == 0) {

            blockPayload = (uint8_t *)malloc(1);
            if (blockPayload != NULL) {
                blockPayload[0] = 0;
                blockPayloadLen = 1;
            }
        } else {
            blockPayload = (uint8_t *)malloc(blockPayloadLen);
            if (blockPayload != NULL) {
                memcpy(blockPayload, payload, blockPayloadLen);
            }
        }

        if (blockPayload != NULL) {
            unsigned const wantContentSizeMode5 = (modeSeed >> 3) & 1u;
            hdrPrefs.frameInfo.contentSize =
                    wantContentSizeMode5 ? (unsigned long long)blockPayloadLen : 0;

            LZ4F_cctx *cctx = NULL;
            if (!LZ4F_isError(LZ4F_createCompressionContext(&cctx, LZ4F_VERSION)) &&
                    cctx != NULL) {
                size_t const bound = (size_t)LZ4F_HEADER_SIZE_MAX +
                        LZ4F_BLOCK_HEADER_SIZE + blockPayloadLen +
                        LZ4F_BLOCK_CHECKSUM_SIZE + LZ4F_BLOCK_HEADER_SIZE +
                        LZ4F_BLOCK_CHECKSUM_SIZE + 64;
                uint8_t *built = (uint8_t *)malloc(bound);
                if (built != NULL) {
                    size_t pos = LZ4F_compressBegin(cctx, built, bound, &hdrPrefs);
                    if (!LZ4F_isError(pos)) {

                        uint32_t const blockHeader =
                                (uint32_t)blockPayloadLen | 0x80000000U;
                        built[pos + 0] = (uint8_t)(blockHeader & 0xFF);
                        built[pos + 1] = (uint8_t)((blockHeader >> 8) & 0xFF);
                        built[pos + 2] = (uint8_t)((blockHeader >> 16) & 0xFF);
                        built[pos + 3] = (uint8_t)((blockHeader >> 24) & 0xFF);
                        pos += 4;

                        memcpy(built + pos, blockPayload, blockPayloadLen);
                        pos += blockPayloadLen;

                        if (hdrPrefs.frameInfo.blockChecksumFlag) {
                            uint32_t const bcrc = XXH32(blockPayload, blockPayloadLen, 0);
                            built[pos + 0] = (uint8_t)(bcrc & 0xFF);
                            built[pos + 1] = (uint8_t)((bcrc >> 8) & 0xFF);
                            built[pos + 2] = (uint8_t)((bcrc >> 16) & 0xFF);
                            built[pos + 3] = (uint8_t)((bcrc >> 24) & 0xFF);
                            pos += 4;
                        }

                        built[pos + 0] = 0;
                        built[pos + 1] = 0;
                        built[pos + 2] = 0;
                        built[pos + 3] = 0;
                        pos += 4;

                        if (hdrPrefs.frameInfo.contentChecksumFlag) {
                            uint32_t const ccrc = XXH32(blockPayload, blockPayloadLen, 0);
                            built[pos + 0] = (uint8_t)(ccrc & 0xFF);
                            built[pos + 1] = (uint8_t)((ccrc >> 8) & 0xFF);
                            built[pos + 2] = (uint8_t)((ccrc >> 16) & 0xFF);
                            built[pos + 3] = (uint8_t)((ccrc >> 24) & 0xFF);
                            pos += 4;
                        }

                        frameBuf = built;
                        frameSize = pos;
                        ownedBuf = built;
                    } else {
                        free(built);
                    }
                }
                LZ4F_freeCompressionContext(cctx);
            }
            free(blockPayload);
        }
    }

    if (frameBuf == NULL) {

        frameBuf = (uint8_t *)payload;
        frameSize = remaining;
    }

    if ((modeSeed & 0x10000u) != 0) {
        size_t const secondHeaderSize = 8;
        size_t const secondPayload = MIN(remaining, (size_t)256);
        uint8_t *combined = (uint8_t *)malloc(frameSize + secondHeaderSize + secondPayload);
        if (combined != NULL) {
            uint32_t const magic = 0x184D2A50u;
            uint32_t const sz = (uint32_t)secondPayload;
            memcpy(combined, frameBuf, frameSize);
            combined[frameSize + 0] = (uint8_t)(magic & 0xFF);
            combined[frameSize + 1] = (uint8_t)((magic >> 8) & 0xFF);
            combined[frameSize + 2] = (uint8_t)((magic >> 16) & 0xFF);
            combined[frameSize + 3] = (uint8_t)((magic >> 24) & 0xFF);
            combined[frameSize + 4] = (uint8_t)(sz & 0xFF);
            combined[frameSize + 5] = (uint8_t)((sz >> 8) & 0xFF);
            combined[frameSize + 6] = (uint8_t)((sz >> 16) & 0xFF);
            combined[frameSize + 7] = (uint8_t)((sz >> 24) & 0xFF);
            if (secondPayload > 0) {
                memcpy(combined + frameSize + secondHeaderSize, payload, secondPayload);
            }
            free(ownedBuf);
            ownedBuf = combined;
            frameBuf = combined;
            frameSize = frameSize + secondHeaderSize + secondPayload;
        }
    }

    free(amplified);
    amplified = NULL;

    {
        unsigned const bigDst = ((capacitySeed >> 3) & 7u) == 0;
        unsigned const alignBlockDst = ((capacitySeed >> 6) & 7u) == 0;
        unsigned const forceSmallDst = ((capacitySeed >> 9) & 7u) == 0;
        unsigned const align64KDst = ((capacitySeed >> 12) & 7u) == 0;

        size_t dstCapacity;
        if (align64KDst) {
            uint32_t const off = (capacitySeed >> 15) % 3;
            dstCapacity = (size_t)(64 * 1024) - 1 + off;
        } else if (alignBlockDst) {
            size_t const blockSize = LZ4F_getBlockSize(prefs.frameInfo.blockSizeID);
            dstCapacity = LZ4F_isError(blockSize) ? (size_t)(64 * 1024) : blockSize;
        } else if (bigDst) {
            dstCapacity = (size_t)(256 * 1024);
        } else {
            size_t const maxCapacity = (frameSize < (16 * 1024 * 1024)) ?
                    (frameSize * 4 + 1024) : (16 * 1024 * 1024);
            dstCapacity = FUZZ_getRange_from_uint32(
                    capacitySeed, 0, (uint32_t)MIN(maxCapacity, 0xFFFFFFFFu));
        }
        if (forceSmallDst) {
            dstCapacity = MIN(dstCapacity, (size_t)4096);
        }

        uint8_t *dst = (uint8_t *)malloc(dstCapacity > 0 ? dstCapacity : 1);
        FUZZ_ASSERT(dst);

        {
            LZ4F_dctx *dctx = NULL;
            LZ4F_errorCode_t const dctxStatus =
                    LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
            if (LZ4F_isError(dctxStatus) || dctx == NULL) {
                free(dst);
                free(ownedBuf);
                FUZZ_dataProducer_free(producer);
                return 0;
            }

            {
                unsigned const useStableDst = (rngState >> 1) & 1u;
                unsigned const useNullOpts = ((rngState >> 2) & 7u) == 0;
                unsigned const skipChecksums = (rngState >> 3) & 1u;
                unsigned const chunkStrategy = (rngState >> 5) % 3;

                unsigned const useExtDict = ((rngState >> 13) & 7u) == 0;

                uint8_t extDictBuf[256];
                size_t extDictSize = 0;
                if (useExtDict) {
                    extDictSize = MIN(remaining, sizeof(extDictBuf));
                    if (extDictSize > 0) {
                        memcpy(extDictBuf, payload, extDictSize);
                    }
                }
                int firstDecompressCall = 1;

                LZ4F_decompressOptions_t opts;
                memset(&opts, 0, sizeof(opts));
                opts.stableDst = useStableDst ? 1 : 0;
                opts.skipChecksums = skipChecksums;

                {
                    size_t zeroSrc = 0;
                    size_t zeroDst = 0;
                    LZ4F_decompress(dctx, NULL, &zeroDst,
                                     frameBuf, &zeroSrc,
                                     useNullOpts ? NULL : &opts);
                }

                size_t growUsed = 0;

                const uint8_t *srcPtr = frameBuf;
                size_t srcRemaining = frameSize;

                int iterations = 0;
                const int maxIterations = 2000;

                while (iterations < maxIterations) {
                    if (srcRemaining == 0) {

                        break;
                    }

                    size_t want;
                    switch (chunkStrategy) {
                        case 0:
                            want = srcRemaining;
                            break;
                        case 1:
                            want = FUZZ_rand32(&rngState, 1,
                                    (uint32_t)MIN(srcRemaining, (size_t)4096));
                            break;
                        default:
                            want = FUZZ_rand32(&rngState, 1, 64);
                            break;
                    }
                    size_t srcSize = MIN(want, srcRemaining);

                    unsigned const useNullDst = ((rngState >> 9) & 15u) == 0;

                    void *callDst;
                    size_t callCap;
                    if (useNullDst) {
                        callDst = NULL;
                        callCap = 0;
                    } else if (useStableDst) {
                        callDst = dst + growUsed;
                        callCap = dstCapacity - growUsed;
                    } else {
                        callDst = dst;
                        callCap = dstCapacity;
                    }
                    {
                        size_t dstSize = callCap;

                        size_t const ret = (useExtDict && firstDecompressCall)
                                ? LZ4F_decompress_usingDict(
                                        dctx, callDst, &dstSize,
                                        srcPtr, &srcSize,
                                        extDictBuf, extDictSize,
                                        useNullOpts ? NULL : &opts)
                                : LZ4F_decompress(
                                        dctx, callDst, &dstSize,
                                        srcPtr, &srcSize,
                                        useNullOpts ? NULL : &opts);
                        firstDecompressCall = 0;

                        if (useStableDst && !useNullDst) {
                            growUsed += dstSize;
                        }

                        srcPtr += srcSize;
                        srcRemaining -= srcSize;

                        if (LZ4F_isError(ret)) {
                            break;
                        }
                        if (ret == 0) {

                            if (srcRemaining == 0) {
                                break;
                            }
                            iterations++;
                            continue;
                        }
                        if (srcSize == 0 && dstSize == 0) {

                            break;
                        }
                    }

                    iterations++;
                }
            }

            LZ4F_freeDecompressionContext(dctx);
        }

        free(dst);
    }

    free(ownedBuf);
    FUZZ_dataProducer_free(producer);

    return 0;
}