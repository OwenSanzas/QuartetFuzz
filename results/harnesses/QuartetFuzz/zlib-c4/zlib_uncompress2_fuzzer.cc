#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "zlib.h"

#define MAX_ALLOC (4 * 1024 * 1024)

#define put_byte(s, i, c) { (s)[i] = (unsigned char)(c); }

static void write_zlib_header(uint8_t *s, unsigned compression_method,
                               unsigned flags) {
  unsigned int header = (8   + ((flags) << 4)) << 8;
  header |= (compression_method << 6);
  header += 31 - (header % 31);
  put_byte(s, 0, (unsigned char)(header >> 8));
  put_byte(s, 1, (unsigned char)(header & 0xff));
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1)
    return 0;

  uint8_t mode = data[0];
  data++;
  size--;

  unsigned path = mode % 3;

  if (path == 0) {

    if (size < 3)
      return 0;
    int level = (int)(data[0] % 11) - 1;
    uint8_t corrupt_byte = data[1];
    uint8_t dest_hint = data[2];
    bool corrupt = (mode & 0x80) != 0;
    data += 3;
    size -= 3;

    uLong srcLen = (uLong)size;
    uLong boundLen = compressBound(srcLen);
    Bytef *comp = (Bytef *)malloc(boundLen > 0 ? boundLen : 1);
    if (!comp)
      return 0;

    uLong compLen = boundLen;
    int cret = compress2(comp, &compLen, data, srcLen, level);
    if (cret != Z_OK) {
      free(comp);
      return 0;
    }

    if (compLen > 0 && corrupt)
      comp[corrupt_byte % compLen] ^= (uint8_t)((mode >> 2) | 1);

    uLong destCap = (uLong)dest_hint * 64 + 1;
    if (destCap > MAX_ALLOC)
      destCap = MAX_ALLOC;
    Bytef *out = (Bytef *)malloc(destCap);
    if (!out) {
      free(comp);
      return 0;
    }

    uLong destLen = destCap;
    uLong sourceLen = compLen;
    int ret = uncompress2(out, &destLen, comp, &sourceLen);
    if (ret == Z_OK) {
      if (!corrupt) {

        assert(sourceLen == compLen);
        assert(destLen == srcLen);
        assert(destLen != srcLen || memcmp(out, data, (size_t)destLen) == 0);
      } else {
        volatile unsigned char sink = 0;
        for (uLong i = 0; i < destLen; i++)
          sink ^= out[i];
        (void)sink;
      }
    }

    z_size_t zdest = destCap;
    z_size_t zsrc = compLen;
    int ret2 = uncompress2_z(out, &zdest, comp, &zsrc);
    (void)ret2;

    uLong destLen3 = destCap;
    int ret3 = uncompress(out, &destLen3, comp, compLen);
    (void)ret3;

    free(out);
    free(comp);
  } else if (path == 1) {

    if (size < 2)
      return 0;
    uint16_t destHint = ((uint16_t)data[0] << 8) | data[1];
    data += 2;
    size -= 2;

    uLong destCap = destHint;
    if (destCap > MAX_ALLOC)
      destCap = MAX_ALLOC;
    Bytef *dest = (Bytef *)malloc(destCap > 0 ? destCap : 1);
    if (!dest)
      return 0;

    uLong destLen = destCap;
    uLong sourceLen = (uLong)size;
    int ret = uncompress2(dest, &destLen, data, &sourceLen);
    if (ret == Z_OK) {
      volatile unsigned char sink = 0;
      for (uLong i = 0; i < destLen; i++)
        sink ^= dest[i];
      (void)sink;
    }
    free(dest);
  } else {

    if (size < 2)
      return 0;
    unsigned compression_method = data[0] % 5;
    if (compression_method == 4)
      compression_method = 8;
    unsigned flags = data[1] & (2 << 4);
    data += 2;
    size -= 2;

    size_t copyLen = size + 2;
    uint8_t *copy = (uint8_t *)malloc(copyLen);
    if (!copy)
      return 0;
    if (size > 0)
      memcpy(copy + 2, data, size);
    write_zlib_header(copy, compression_method, flags);

    uLong destCap = (uLong)size + 256;
    if (destCap > MAX_ALLOC)
      destCap = MAX_ALLOC;
    Bytef *dest = (Bytef *)malloc(destCap);
    if (!dest) {
      free(copy);
      return 0;
    }

    uLong destLen = destCap;
    uLong sourceLen = (uLong)copyLen;
    int ret = uncompress2(dest, &destLen, copy, &sourceLen);
    if (ret == Z_OK) {
      volatile unsigned char sink = 0;
      for (uLong i = 0; i < destLen; i++)
        sink ^= dest[i];
      (void)sink;
    }
    free(dest);
    free(copy);
  }

  return 0;
}
