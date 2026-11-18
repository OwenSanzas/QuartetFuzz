#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "zlib.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

  static const size_t kHeaderSize = 10;
  static const size_t kMaxSize = 1 << 20;

  if (size < kHeaderSize || size > kMaxSize) {
    return 0;
  }

  int level = (int)(data[0] % 11) - 1;
  int windowBitsBase = 8 + (int)(data[1] % 8);
  int wbMode = data[2] % 3;
  int windowBits = windowBitsBase;
  if (wbMode == 1) {
    windowBits = -windowBitsBase;
  } else if (wbMode == 2) {
    windowBits = windowBitsBase + 16;
  }
  int memLevel = 1 + (int)(data[3] % 9);
  int strategy = data[4] % 5;
  uint8_t flushSeed = data[5];
  uint8_t chunkSeed = data[6];
  uint8_t hcrcByte = data[7];
  uint8_t osByte = data[8];
  uint8_t paramSeed = data[9];

  const uint8_t *payload = data + kHeaderSize;
  size_t payloadSize = size - kHeaderSize;

  z_stream strm;
  memset(&strm, 0, sizeof(strm));
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;

  gz_header header;
  memset(&header, 0, sizeof(header));

  int ret = deflateInit2(&strm, level, Z_DEFLATED, windowBits, memLevel,
                          strategy);
  if (ret != Z_OK) {

    return 0;
  }

  {
    int good_length = (int)(data[0] % 258);
    int max_lazy = (int)(data[1] % 258);
    int nice_length = (int)(3 + (data[2] % 256));
    int max_chain = (int)(1 + (data[3] % 4096));
    (void)deflateTune(&strm, good_length, max_lazy, nice_length, max_chain);
  }

  if (wbMode == 1) {
    int primeBits = (int)(paramSeed % 17);
    int primeValue = (int)flushSeed;
    (void)deflatePrime(&strm, primeBits, primeValue);
  }

  if (wbMode != 2 && payloadSize > 0) {
    uInt dictLen = (uInt)(payloadSize < 64 ? payloadSize : 64);
    (void)deflateSetDictionary(&strm, payload, dictLen);
  }

  static const size_t kExtraCap = 2048;
  static const size_t kNameCap = 512;
  static const size_t kCommentCap = 512;
  uint8_t *extra_buf = NULL;
  uint8_t *name_buf = NULL;
  uint8_t *comment_buf = NULL;

  if (wbMode == 2) {
    extra_buf = (uint8_t *)malloc(kExtraCap);
    name_buf = (uint8_t *)malloc(kNameCap);
    comment_buf = (uint8_t *)malloc(kCommentCap);

    if (extra_buf == NULL || name_buf == NULL || comment_buf == NULL) {
      free(extra_buf);
      free(name_buf);
      free(comment_buf);
      deflateEnd(&strm);
      return 0;
    }

    memset(extra_buf, 0, kExtraCap);
    memset(name_buf, 0, kNameCap);
    memset(comment_buf, 0, kCommentCap);

    if (payloadSize > 0) {
      size_t n;

      size_t extraLen = payloadSize % (kExtraCap + 1);
      n = payloadSize < extraLen ? payloadSize : extraLen;
      if (n > 0) {
        size_t copyLen = n < payloadSize ? n : payloadSize;
        memcpy(extra_buf, payload, copyLen);
      }
      header.extra = extra_buf;
      header.extra_len = (uInt)extraLen;

      n = payloadSize < (kNameCap - 1) ? payloadSize : (kNameCap - 1);
      memcpy(name_buf, payload, n);
      for (size_t i = 0; i < n; i++) {
        if (name_buf[i] == 0) name_buf[i] = 1;
      }
      name_buf[kNameCap - 1] = '\0';
      header.name = name_buf;

      n = payloadSize < (kCommentCap - 1) ? payloadSize : (kCommentCap - 1);
      memcpy(comment_buf, payload, n);
      for (size_t i = 0; i < n; i++) {
        if (comment_buf[i] == 0) comment_buf[i] = 1;
      }
      comment_buf[kCommentCap - 1] = '\0';
      header.comment = comment_buf;

      header.hcrc = (int)(hcrcByte & 1);
      header.os = (int)osByte;
      header.time = (uLong)((uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                             ((uint32_t)data[2] << 16) |
                             ((uint32_t)data[3] << 24));
      header.text = (int)(osByte & 1);

      (void)deflateSetHeader(&strm, &header);
    }
  }

  uLong boundSize = deflateBound(&strm, (uLong)payloadSize) + 1024;
  uint8_t *outbuf = (uint8_t *)malloc((size_t)boundSize);
  if (outbuf == NULL) {
    free(extra_buf);
    free(name_buf);
    free(comment_buf);
    deflateEnd(&strm);
    return 0;
  }

  strm.next_in = (Bytef *)payload;
  strm.avail_in = (uInt)payloadSize;

  size_t outPos = 0;
  size_t iteration = 0;
  int flush = Z_NO_FLUSH;
  int last_flush_used = Z_NO_FLUSH;
  int safety_counter = 0;

  const int kMaxIterations = 2000;

  do {

    size_t remaining = (size_t)boundSize - outPos;
    uint8_t mixed = (uint8_t)(chunkSeed ^ (uint8_t)(iteration * 37));
    size_t chunk;
    switch (mixed % 4) {
      case 0:

        chunk = 1 + (size_t)(mixed % 64);
        break;
      case 1:

        chunk = 1 + (size_t)((mixed * 131u) % 4096u);
        break;
      case 2:

        chunk = 1 + (size_t)((mixed * 2111u) % 65536u);
        break;
      default:

        chunk = remaining;
        break;
    }
    if (chunk > remaining) {
      chunk = remaining;
    }
    if (chunk == 0) {

      if (remaining > 0) {
        chunk = remaining;
      } else {
        break;
      }
    }

    strm.next_out = outbuf + outPos;
    strm.avail_out = (uInt)chunk;

    if (payloadSize > 0 && (iteration % 7) == 3) {
      int newLevel = (int)(payload[iteration % payloadSize] % 12) - 1;
      int newStrategy =
          (int)(payload[(iteration + paramSeed) % payloadSize] % 5);
      (void)deflateParams(&strm, newLevel, newStrategy);
    }

    if (payloadSize > 0 && (iteration % 11) == 5) {
      (void)deflateSetDictionary(&strm, payload,
                                  (uInt)((payloadSize % 32) + 1));
    }

    if (strm.avail_in == 0) {

      if (last_flush_used != Z_FINISH &&
          ((paramSeed ^ (uint8_t)iteration) % 23) == 0) {
        int ret2 = deflate(&strm, Z_NO_FLUSH);
        (void)ret2;
      }

      flush = Z_FINISH;
    } else {
      int choice = (int)((flushSeed + iteration) % 5);
      switch (choice) {
        case 0:
          flush = Z_NO_FLUSH;
          break;
        case 1:
          flush = Z_PARTIAL_FLUSH;
          break;
        case 2:
          flush = Z_SYNC_FLUSH;
          break;
        case 3:
          flush = Z_FULL_FLUSH;
          break;
        default:
          flush = Z_BLOCK;
          break;
      }
    }

    if ((iteration % 29) == 0) {
      uInt savedAvailOut = strm.avail_out;
      strm.avail_out = 0;
      int r = deflate(&strm, flush);
      (void)r;
      strm.avail_out = savedAvailOut;
    }
    if ((iteration % 31) == 0) {
      Bytef *savedNextOut = strm.next_out;
      uInt savedAvailOut = strm.avail_out;
      strm.next_out = Z_NULL;
      strm.avail_out = 1;
      int r = deflate(&strm, flush);
      (void)r;
      strm.next_out = savedNextOut;
      strm.avail_out = savedAvailOut;
    }

    ret = deflate(&strm, flush);
    last_flush_used = flush;
    outPos = (size_t)(strm.next_out - outbuf);
    iteration++;
    safety_counter++;

    if ((iteration % 17) == 0) {
      unsigned pend = 0;
      int bits = 0;
      (void)deflatePending(&strm, &pend, &bits);
      int usedBits = 0;
      (void)deflateUsed(&strm, &usedBits);
      Bytef dictOut[32];
      uInt dictOutLen = 0;
      (void)deflateGetDictionary(&strm, dictOut, &dictOutLen);
    }

    if ((iteration % 13) == 0) {
      z_stream copy;
      memset(&copy, 0, sizeof(copy));
      if (deflateCopy(&copy, &strm) == Z_OK) {
        uint8_t tmp[256];
        copy.next_out = tmp;
        copy.avail_out = sizeof(tmp);
        int r = deflate(&copy, Z_FINISH);
        (void)r;
        deflateEnd(&copy);
      }
    }

    if (last_flush_used == Z_FINISH && ret == Z_OK && payloadSize > 0) {
      uint8_t tmpbuf[8];
      Bytef *savedNextOut = strm.next_out;
      uInt savedAvailOut = strm.avail_out;
      unsigned savedAvailIn = strm.avail_in;
      const Bytef *savedNextIn = strm.next_in;

      strm.next_out = tmpbuf;
      strm.avail_out = sizeof(tmpbuf);
      strm.avail_in = 1;
      strm.next_in = payload;
      int r = deflate(&strm, Z_FINISH);
      (void)r;

      strm.next_out = savedNextOut;
      strm.avail_out = savedAvailOut;
      strm.avail_in = savedAvailIn;
      strm.next_in = savedNextIn;
    }

    if (ret == Z_STREAM_ERROR || ret == Z_BUF_ERROR) {
      break;
    }
  } while (ret != Z_STREAM_END && safety_counter < kMaxIterations);

  if (ret == Z_STREAM_END) {
    uint8_t extra[8];
    Bytef *savedNextOut = strm.next_out;
    uInt savedAvailOut = strm.avail_out;
    unsigned savedAvailIn = strm.avail_in;

    strm.next_out = extra;
    strm.avail_out = sizeof(extra);
    strm.avail_in = 0;
    int r = deflate(&strm, Z_NO_FLUSH);
    (void)r;

    strm.next_out = savedNextOut;
    strm.avail_out = savedAvailOut;
    strm.avail_in = savedAvailIn;
  }

  deflateEnd(&strm);
  free(outbuf);
  free(extra_buf);
  free(name_buf);
  free(comment_buf);

  return 0;
}