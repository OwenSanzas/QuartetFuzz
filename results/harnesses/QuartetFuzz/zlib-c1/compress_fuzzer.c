#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zlib.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2) {
    return 0;
  }

  int level = (int)(data[0] % 11) - 1;

  uint8_t sizeCtrl = data[1];

  const Bytef *source = (const Bytef *)(data + 2);
  uLong sourceLen = (uLong)(size - 2);

  uLong bound = compressBound(sourceLen);
  if (bound == (uLong)-1) {

    return 0;
  }

  uLong destLen;
  switch (sizeCtrl % 4) {
    case 0:
      destLen = bound;
      break;
    case 1:
      destLen = bound / 2;
      break;
    case 2:
      destLen = sourceLen;
      break;
    default:
      destLen = bound + (uLong)sizeCtrl;
      break;
  }

  Bytef *dest = (Bytef *)malloc(destLen > 0 ? destLen : 1);
  if (dest == NULL) {
    return 0;
  }

  uLongf destLenIO = (uLongf)destLen;
  int ret = compress2(dest, &destLenIO, source, sourceLen, level);

  if (ret == Z_OK) {

    uLong decompCap = sourceLen > 0 ? sourceLen : 1;
    Bytef *decomp = (Bytef *)malloc(decompCap);
    if (decomp != NULL) {
      uLongf decompLen = (uLongf)decompCap;
      uLong compressedUsed = destLenIO;
      int uret = uncompress2(decomp, &decompLen, dest, &compressedUsed);
      if (uret == Z_OK && decompLen == sourceLen && sourceLen > 0) {

        if (memcmp(decomp, source, sourceLen) != 0) {

        }
      }
      free(decomp);
    }
  }

  free(dest);
  return 0;
}
