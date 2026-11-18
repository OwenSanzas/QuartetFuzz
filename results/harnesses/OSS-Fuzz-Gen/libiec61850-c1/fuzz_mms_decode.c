#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#include "iec61850_server.h"
#include "hal_thread.h"
#include "lib_memory.h"

static void take(const uint8_t **data, size_t *size, uint8_t *out, int want) {
    int n = (*size < (size_t)want) ? (int)*size : want;
    if (n > 0) {
        memcpy(out, *data, n);
        *data += n;
        *size -= n;
    }
    if (n < want)
        memset(out + n, 0, want - n);
}

static uint8_t nextByte(const uint8_t **data, size_t *size) {
    uint8_t b = 0;
    take(data, size, &b, 1);
    return b;
}

static int encodeBerLength(uint8_t *out, int pos, int length) {
    if (length < 0)
        length = 0;

    if (length < 128) {
        out[pos++] = (uint8_t)length;
    }
    else if (length < 256) {
        out[pos++] = 0x81;
        out[pos++] = (uint8_t)length;
    }
    else if (length < 65536) {
        out[pos++] = 0x82;
        out[pos++] = (uint8_t)((length >> 8) & 0xff);
        out[pos++] = (uint8_t)(length & 0xff);
    }
    else {
        out[pos++] = 0x83;
        out[pos++] = (uint8_t)((length >> 16) & 0xff);
        out[pos++] = (uint8_t)((length >> 8) & 0xff);
        out[pos++] = (uint8_t)(length & 0xff);
    }

    return pos;
}

static int encodeBerLengthOverlong(uint8_t *out, int pos, int length) {
    out[pos++] = 0x84;
    out[pos++] = (uint8_t)((length >> 24) & 0xff);
    out[pos++] = (uint8_t)((length >> 16) & 0xff);
    out[pos++] = (uint8_t)((length >> 8) & 0xff);
    out[pos++] = (uint8_t)(length & 0xff);
    return pos;
}

static int buildScalarElement(const uint8_t **data, size_t *size, uint8_t *out, int cap) {
    if (cap < 6)
        return -1;

    static const uint8_t tags[] = {
        0x80, 0x83, 0x84, 0x85, 0x86, 0x87, 0x89, 0x8a, 0x8c, 0x90, 0x91, 0x00
    };

    uint8_t selTag = nextByte(data, size);
    uint8_t tag = tags[selTag % (sizeof(tags) / sizeof(tags[0]))];

    uint8_t lenSel = nextByte(data, size);
    int len;

    switch (tag) {
        case 0x87:
            len = (lenSel & 0x01) ? 9 : 5;
            break;
        case 0x91:
            len = 8;
            break;
        case 0x8c:
            len = (lenSel & 0x01) ? 6 : 4;
            break;
        case 0x85:
        case 0x86:
            len = 1 + (lenSel % 8);
            break;
        case 0x83:
            len = 1;
            break;
        case 0x84:
            len = 1 + (lenSel % 5);

            break;
        case 0x80:
            len = 1 + (lenSel % 4);
            break;
        case 0x00:
            len = 0;
            break;
        default:
            len = lenSel % 17;
            break;
    }

    uint8_t mutateSel = nextByte(data, size);
    if (mutateSel & 0x80) {
        len = (len + (mutateSel & 0x0f)) % 20;
        if (len < 0)
            len = 0;
    }

    if (len > 32)
        len = 32;

    uint8_t overlongSel = nextByte(data, size);
    int useOverlong = ((overlongSel & 0xE0) == 0xE0);

    if (useOverlong) {
        if (cap < 1 + 5)
            return -1;

        int pos = 0;
        out[pos++] = tag;
        pos = encodeBerLengthOverlong(out, pos, len);
        return pos;
    }

    uint8_t payload[32];
    take(data, size, payload, len);

    int lenFieldSize = (len < 128) ? 1 : (len < 256 ? 2 : (len < 65536 ? 3 : 4));

    if (cap < 1 + lenFieldSize + len)
        return -1;

    int pos = 0;
    out[pos++] = tag;
    pos = encodeBerLength(out, pos, len);
    if (len > 0) {
        memcpy(out + pos, payload, len);
        pos += len;
    }

    return pos;
}

static int buildInvalidTagElement(const uint8_t **data, size_t *size, uint8_t *out, int cap) {
    if (cap < 2)
        return -1;

    static const uint8_t invalidTags[] = { 0x81, 0x82, 0x88, 0x8b, 0x8d, 0x8e, 0x8f, 0x92, 0xff };

    uint8_t selTag = nextByte(data, size);
    uint8_t tag = invalidTags[selTag % (sizeof(invalidTags) / sizeof(invalidTags[0]))];

    uint8_t lenSel = nextByte(data, size);
    int len = lenSel % 17;

    if (len > 32)
        len = 32;

    uint8_t payload[32];
    take(data, size, payload, len);

    int lenFieldSize = (len < 128) ? 1 : (len < 256 ? 2 : (len < 65536 ? 3 : 4));

    if (cap < 1 + lenFieldSize + len)
        return -1;

    int pos = 0;
    out[pos++] = tag;
    pos = encodeBerLength(out, pos, len);
    if (len > 0) {
        memcpy(out + pos, payload, len);
        pos += len;
    }

    return pos;
}

static int buildExtendedTagChild(const uint8_t **data, size_t *size, uint8_t *out, int cap) {
    if (cap < 3)
        return -1;

    uint8_t b = nextByte(data, size);
    uint8_t tagHigh = b & 0xE0;
    out[0] = tagHigh | 0x1f;
    out[1] = nextByte(data, size);
    out[2] = 0x00;

    return 3;
}

static int buildRandomElement(const uint8_t **data, size_t *size, uint8_t *out, int cap, int depth) {
    if (cap < 2)
        return -1;

    uint8_t sel = nextByte(data, size);

    if (depth < 8 && (sel & 0x03) == 0) {
        uint8_t tag = (sel & 0x04) ? 0xa1 : 0xa2;
        uint8_t childCountByte = nextByte(data, size);
        int childCount = 1 + (childCountByte % 3);

        uint8_t payload[256];
        int payloadLen = 0;

        int i;
        for (i = 0; i < childCount; i++) {
            if (*size == 0)
                break;

            int remainingCap = (int)sizeof(payload) - payloadLen;
            if (remainingCap <= 0)
                break;

            uint8_t extSel = nextByte(data, size);
            int childLen;

            if ((extSel & 0xF0) == 0xF0) {

                childLen = buildExtendedTagChild(data, size, payload + payloadLen, remainingCap);
            }
            else {
                childLen = buildRandomElement(data, size, payload + payloadLen, remainingCap, depth + 1);
            }

            if (childLen < 0)
                break;

            payloadLen += childLen;
        }

        int lenFieldSize = (payloadLen < 128) ? 1 : (payloadLen < 256 ? 2 : (payloadLen < 65536 ? 3 : 4));

        if (cap < 1 + lenFieldSize + payloadLen)
            return -1;

        int pos = 0;
        out[pos++] = tag;
        pos = encodeBerLength(out, pos, payloadLen);
        if (payloadLen > 0) {
            memcpy(out + pos, payload, payloadLen);
            pos += payloadLen;
        }

        return pos;
    }
    else {
        return buildScalarElement(data, size, out, cap);
    }
}

static int buildIndefiniteElement(const uint8_t **data, size_t *size, uint8_t *out, int cap, int depth) {
    if (cap < 4)
        return -1;

    static const uint8_t outerTags[] = { 0xa1, 0xa2, 0x83, 0x85, 0x89, 0x8a };
    uint8_t tagSel = nextByte(data, size);
    uint8_t tag = outerTags[tagSel % (sizeof(outerTags) / sizeof(outerTags[0]))];

    uint8_t childCountByte = nextByte(data, size);
    int childCount = 1 + (childCountByte % 3);

    uint8_t content[512];
    int contentLen = 0;

    int i;
    for (i = 0; i < childCount; i++) {
        if (*size == 0)
            break;

        int remainingCap = (int)sizeof(content) - contentLen;
        if (remainingCap <= 2)
            break;

        uint8_t extSel = nextByte(data, size);
        int childLen;

        if ((extSel & 0xF0) == 0xF0) {

            childLen = buildExtendedTagChild(data, size, content + contentLen, remainingCap);
        }
        else if (depth < 4) {
            childLen = buildRandomElement(data, size, content + contentLen, remainingCap, depth + 1);
        }
        else {
            childLen = buildScalarElement(data, size, content + contentLen, remainingCap);
        }

        if (childLen < 0)
            break;

        contentLen += childLen;
    }

    uint8_t wellFormedSel = nextByte(data, size);
    int wellFormed = (wellFormedSel & 0x01);

    int needed = 1   + 1   + contentLen + (wellFormed ? 2 : 0);
    if (cap < needed)
        return -1;

    int pos = 0;
    out[pos++] = tag;
    out[pos++] = 0x80;
    if (contentLen > 0) {
        memcpy(out + pos, content, contentLen);
        pos += contentLen;
    }
    if (wellFormed) {
        out[pos++] = 0x00;
        out[pos++] = 0x00;
    }

    return pos;
}

static int buildDeepNest(const uint8_t **data, size_t *size, uint8_t *out, int cap) {
    uint8_t leaf[40];
    int leafLen = buildScalarElement(data, size, leaf, (int)sizeof(leaf));
    if (leafLen < 0)
        return -1;

    uint8_t cur[8192];
    if (leafLen > (int)sizeof(cur))
        return -1;

    memcpy(cur, leaf, leafLen);
    int curLen = leafLen;

    uint8_t depthByte = nextByte(data, size);
    int depthCount = 1 + (depthByte % 40);

    int i;
    for (i = 0; i < depthCount; i++) {
        uint8_t tagSel = nextByte(data, size);
        uint8_t tag = (tagSel & 0x01) ? 0xa1 : 0xa2;

        int lenFieldSize = (curLen < 128) ? 1 : (curLen < 256 ? 2 : (curLen < 65536 ? 3 : 4));

        if (1 + lenFieldSize + curLen > (int)sizeof(cur) - 8)
            break;

        uint8_t wrapped[8192];
        int pos = 0;
        wrapped[pos++] = tag;
        pos = encodeBerLength(wrapped, pos, curLen);
        memcpy(wrapped + pos, cur, curLen);
        pos += curLen;

        memcpy(cur, wrapped, pos);
        curLen = pos;
    }

    if (curLen > cap)
        return -1;

    memcpy(out, cur, curLen);
    return curLen;
}

static int buildDeepIndefiniteNest(const uint8_t **data, size_t *size, uint8_t *out, int cap) {
    uint8_t leaf[40];
    int leafLen = buildScalarElement(data, size, leaf, (int)sizeof(leaf));
    if (leafLen < 0)
        return -1;

    uint8_t cur[8192];
    if (leafLen > (int)sizeof(cur))
        return -1;

    memcpy(cur, leaf, leafLen);
    int curLen = leafLen;

    uint8_t depthByte = nextByte(data, size);
    int depthCount = 1 + (depthByte % 40);

    int i;
    for (i = 0; i < depthCount; i++) {
        uint8_t tagSel = nextByte(data, size);
        uint8_t tag = (tagSel & 0x01) ? 0xa1 : 0xa2;

        uint8_t wfSel = nextByte(data, size);
        int wellFormed = (wfSel & 0x01);

        int needed = 1   + 1   + curLen + (wellFormed ? 2 : 0);

        if (needed > (int)sizeof(cur) - 8)
            break;

        uint8_t wrapped[8192];
        int pos = 0;
        wrapped[pos++] = tag;
        wrapped[pos++] = 0x80;
        memcpy(wrapped + pos, cur, curLen);
        pos += curLen;
        if (wellFormed) {
            wrapped[pos++] = 0x00;
            wrapped[pos++] = 0x00;
        }

        memcpy(cur, wrapped, pos);
        curLen = pos;
    }

    if (curLen > cap)
        return -1;

    memcpy(out, cur, curLen);
    return curLen;
}

static int buildVeryDeepIndefiniteNest(const uint8_t **data, size_t *size, uint8_t *out, int cap) {
    uint8_t leaf[40];
    int leafLen = buildScalarElement(data, size, leaf, (int)sizeof(leaf));
    if (leafLen < 0)
        return -1;

    uint8_t cur[8192];
    if (leafLen > (int)sizeof(cur))
        return -1;

    memcpy(cur, leaf, leafLen);
    int curLen = leafLen;

    uint8_t depthByte = nextByte(data, size);

    int depthCount = 55 + (depthByte % 64);

    int i;
    for (i = 0; i < depthCount; i++) {
        uint8_t tagSel = nextByte(data, size);
        uint8_t tag = (tagSel & 0x01) ? 0xa1 : 0xa2;

        int needed = 1   + 1   + curLen + 2  ;
        if (needed > (int)sizeof(cur) - 8)
            break;

        uint8_t wrapped[8192];
        int pos = 0;
        wrapped[pos++] = tag;
        wrapped[pos++] = 0x80;
        memcpy(wrapped + pos, cur, curLen);
        pos += curLen;
        wrapped[pos++] = 0x00;
        wrapped[pos++] = 0x00;

        memcpy(cur, wrapped, pos);
        curLen = pos;
    }

    if (curLen > cap)
        return -1;

    memcpy(out, cur, curLen);
    return curLen;
}

static int buildMidDeepIndefiniteNest(const uint8_t **data, size_t *size, uint8_t *out, int cap) {
    uint8_t leaf[40];
    int leafLen = buildScalarElement(data, size, leaf, (int)sizeof(leaf));
    if (leafLen < 0)
        return -1;

    uint8_t cur[8192];
    if (leafLen > (int)sizeof(cur))
        return -1;

    memcpy(cur, leaf, leafLen);
    int curLen = leafLen;

    uint8_t depthByte = nextByte(data, size);

    int depthCount = 26 + (depthByte % 15);

    int i;
    for (i = 0; i < depthCount; i++) {
        uint8_t tagSel = nextByte(data, size);
        uint8_t tag = (tagSel & 0x01) ? 0xa1 : 0xa2;

        int needed = 1   + 1   + curLen + 2  ;
        if (needed > (int)sizeof(cur) - 8)
            break;

        uint8_t wrapped[8192];
        int pos = 0;
        wrapped[pos++] = tag;
        wrapped[pos++] = 0x80;
        memcpy(wrapped + pos, cur, curLen);
        pos += curLen;
        wrapped[pos++] = 0x00;
        wrapped[pos++] = 0x00;

        memcpy(cur, wrapped, pos);
        curLen = pos;
    }

    if (curLen > cap)
        return -1;

    memcpy(out, cur, curLen);
    return curLen;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (data == NULL || size == 0) {
        return 0;
    }

    if (size > (size_t)INT_MAX) {
        return 0;
    }

    uint8_t modeByte = data[0];
    const uint8_t *rest = data + 1;
    size_t restSize = size - 1;

    uint8_t buffer[8192];
    int bufferLength;

    int mode = modeByte % 7;

    if (mode == 0 || restSize == 0) {

        size_t copyLen = restSize;
        if (copyLen > sizeof(buffer))
            copyLen = sizeof(buffer);
        if (copyLen > 0)
            memcpy(buffer, rest, copyLen);
        bufferLength = (int)copyLen;
    }
    else if (mode == 1) {

        bufferLength = buildRandomElement(&rest, &restSize, buffer, (int)sizeof(buffer), 0);
        if (bufferLength < 0)
            return 0;
    }
    else if (mode == 2) {

        bufferLength = buildDeepNest(&rest, &restSize, buffer, (int)sizeof(buffer));
        if (bufferLength < 0)
            return 0;
    }
    else if (mode == 3) {

        if (modeByte & 0x04) {
            bufferLength = buildDeepIndefiniteNest(&rest, &restSize, buffer, (int)sizeof(buffer));
        }
        else {
            bufferLength = buildIndefiniteElement(&rest, &restSize, buffer, (int)sizeof(buffer), 0);
        }
        if (bufferLength < 0)
            return 0;
    }
    else if (mode == 4) {

        bufferLength = buildVeryDeepIndefiniteNest(&rest, &restSize, buffer, (int)sizeof(buffer));
        if (bufferLength < 0)
            return 0;
    }
    else if (mode == 5) {

        bufferLength = buildMidDeepIndefiniteNest(&rest, &restSize, buffer, (int)sizeof(buffer));
        if (bufferLength < 0)
            return 0;
    }
    else {

        bufferLength = buildInvalidTagElement(&rest, &restSize, buffer, (int)sizeof(buffer));
        if (bufferLength < 0)
            return 0;
    }

    if (bufferLength <= 0) {
        return 0;
    }

    uint8_t prefixLenByte = nextByte(&rest, &restSize);
    int prefixLen = prefixLenByte % 16;

    uint8_t finalBuffer[8192 + 16];

    if (prefixLen + bufferLength > (int)sizeof(finalBuffer)) {
        prefixLen = 0;
    }

    int total = 0;
    int pfi;
    for (pfi = 0; pfi < prefixLen; pfi++)
        finalBuffer[total++] = nextByte(&rest, &restSize);

    memcpy(finalBuffer + total, buffer, bufferLength);
    total += bufferLength;

    int bufPos = prefixLen;
    int endBufPos = 0;

    MmsValue *mmsValue = MmsValue_decodeMmsData(finalBuffer, bufPos, total, &endBufPos);
    if (mmsValue == NULL) {
        return 0;
    }

    int dataSize = MmsValue_encodeMmsData(mmsValue, NULL, 0, false);
    if (dataSize > 0) {
        uint8_t *mmsBuffer = (uint8_t *)GLOBAL_MALLOC(dataSize);
        if (mmsBuffer != NULL) {
            MmsValue_encodeMmsData(mmsValue, mmsBuffer, 0, true);
            GLOBAL_FREEMEM(mmsBuffer);
        }
    }

    char printBuffer[4096];
    MmsValue_printToBuffer(mmsValue, printBuffer, sizeof(printBuffer));

    MmsValue_delete(mmsValue);

    return 0;
}