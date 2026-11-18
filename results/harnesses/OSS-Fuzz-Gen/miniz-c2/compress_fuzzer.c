#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2) {
    return 0;
  }

  int level = (int)(data[0] % 12) - 1;

  uint8_t mode = data[1] % 4;

  const unsigned char *source = data + 2;
  mz_ulong source_len = (mz_ulong)(size - 2);

  mz_ulong bound = mz_compressBound(source_len);
  if (bound == 0) {
    bound = 64;
  }

  mz_ulong dest_len;
  switch (mode) {
    case 0:

      dest_len = bound;
      break;
    case 1:

      dest_len = (bound > 16) ? (bound / 8) : 1;
      break;
    case 2:

      dest_len = (source_len > 0) ? 1 : bound;
      break;
    default:
      dest_len = bound;
      break;
  }

  if (dest_len == 0) {
    dest_len = 1;
  }

  unsigned char *dest = (unsigned char *)malloc(dest_len);
  if (!dest) {
    return 0;
  }

  mz_ulong actual_dest_len = dest_len;

  if (mode == 3) {
    actual_dest_len = ((mz_ulong)1 << 32) | 1;
  }

  int status = mz_compress2(dest, &actual_dest_len, source, source_len, level);
  (void)status;

  free(dest);

  return 0;
}