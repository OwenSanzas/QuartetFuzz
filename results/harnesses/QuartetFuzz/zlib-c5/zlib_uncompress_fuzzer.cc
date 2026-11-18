#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zlib.h"

#define put_byte(s, i, c) { (s)[i] = (unsigned char)(c); }

static void write_zlib_header(uint8_t *s, unsigned compression_method,
                               unsigned flags) {
  unsigned int header = (Z_DEFLATED + ((flags) << 4)) << 8;
  header |= (compression_method << 6);
  header += 31 - (header % 31);
  put_byte(s, 0, (unsigned char)(header >> 8));
  put_byte(s, 1, (unsigned char)(header & 0xff));
}

static uLong pick_dest_cap(unsigned mode, size_t base) {
  uLong cap = (uLong)base;
  if (mode == 0)
    cap = cap / 2;
  else if (mode == 2)
    cap = cap + 64;
  return cap;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 6)
    return 0;

  unsigned level_sel = data[0] % 11;
  unsigned compression_method = data[1] % 5;
  if (compression_method == 4)
    compression_method = 8;
  unsigned flags = data[2] & 0x20;
  unsigned dest_cap_mode1 = data[3] % 3;
  unsigned dest_cap_mode2 = (data[3] / 3) % 3;

  data += 4;
  size -= 4;

  if (size == 0 || size > (1u << 20))
    return 0;

  {
    uLong comprLen = compressBound((uLong)size);
    uint8_t *compr = (uint8_t *)malloc(comprLen);
    if (compr != NULL) {
      uLongf cLen = comprLen;
      int lvl = (level_sel == 10) ? Z_DEFAULT_COMPRESSION : (int)level_sel;
      int cret = compress2(compr, &cLen, data, (uLong)size, lvl);
      if (cret == Z_OK) {
        uLong dest_cap = pick_dest_cap(dest_cap_mode1, size);
        size_t alloc_sz = dest_cap > 0 ? (size_t)dest_cap : 1;
        uint8_t *uncompr = (uint8_t *)malloc(alloc_sz);
        if (uncompr != NULL) {
          uLongf uLen = (uLongf)dest_cap;
          int uret = uncompress(uncompr, &uLen, compr, cLen);
          if (uret == Z_OK) {
            size_t cmp_len = (uLen < size) ? (size_t)uLen : size;
            volatile int eq =
                (cmp_len == 0) || (memcmp(uncompr, data, cmp_len) == 0);
            (void)eq;
          }
          free(uncompr);
        }
      }
      free(compr);
    }
  }

  {
    size_t copyLen = size + 2;
    uint8_t *copy = (uint8_t *)malloc(copyLen);
    if (copy != NULL) {
      memcpy(copy + 2, data, size);
      write_zlib_header(copy, compression_method, flags);

      uLong dest_cap = pick_dest_cap(dest_cap_mode2, size + 16);
      size_t alloc_sz = dest_cap > 0 ? (size_t)dest_cap : 1;
      uint8_t *dest = (uint8_t *)malloc(alloc_sz);
      if (dest != NULL) {
        uLongf destLen = (uLongf)dest_cap;
        int ret = uncompress(dest, &destLen, copy, (uLong)copyLen);
        if (ret == Z_OK) {
          volatile unsigned char sink = 0;
          for (uLongf i = 0; i < destLen; i++)
            sink ^= dest[i];
          (void)sink;
        }
        free(dest);
      }
      free(copy);
    }
  }

  return 0;
}
