#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zlib.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2) {
    return 0;
  }

  uint8_t level_selector = data[0];
  uint8_t destlen_selector = data[1];

  int level_table[16] = {Z_DEFAULT_COMPRESSION, 0, 1, 2, 3,
                          4, 5, 6, 6, 6, 6, 6,
                          7, 8, -5, 100};
  int level = level_table[level_selector % 16];

  const Bytef *source = NULL;
  uLong sourceLen = (uLong)(size - 2);

  const uLong kWindowSize = 1u << 16;
  const uLong kSmallCap = kWindowSize + 4096;
  const uLong kLargeCap = 4 * kWindowSize;
  uLong cap = (level >= 0 && level <= 3) ? kLargeCap : kSmallCap;
  if (sourceLen > cap) {
    sourceLen = cap;
  }

  if (sourceLen > 0) {
    source = (const Bytef *)(data + 2);
  }

  uLong bound = compressBound(sourceLen);

  uLongf destLen;
  int mode = destlen_selector % 5;
  if (mode == 0) {
    destLen = 0;
  } else if (mode == 1 || mode == 2) {
    if (bound > 1) {
      destLen = 1 + (uLongf)(((uint64_t)destlen_selector * (bound - 1)) / 255);
    } else {
      destLen = (uLongf)bound;
    }
  } else {
    destLen = (uLongf)bound;
  }

  size_t alloc_size = (destLen == 0) ? 1 : (size_t)destLen;
  Bytef *dest = (Bytef *)malloc(alloc_size);
  if (!dest) {
    return 0;
  }

  int ret = compress2(dest, &destLen, source, sourceLen, level);
  (void)ret;

  free(dest);

  return 0;
}