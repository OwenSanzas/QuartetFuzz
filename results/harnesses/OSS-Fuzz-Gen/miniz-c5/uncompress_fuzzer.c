#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

static void run_uncompress(const unsigned char *src, mz_ulong src_len) {
  mz_ulong dest_len = src_len * 4 + 1024;
  const mz_ulong kMaxDestLen = 64 * 1024 * 1024;
  if (dest_len > kMaxDestLen) {
    dest_len = kMaxDestLen;
  }

  unsigned char *dest_buf = (unsigned char *)malloc(dest_len);
  if (dest_buf == NULL) {
    return;
  }

  mz_ulong actual_dest_len = dest_len;
  int status = mz_uncompress(dest_buf, &actual_dest_len, src, src_len);
  (void)status;

  free(dest_buf);
}

static void run_bitflip_corruptions(const unsigned char *comp, mz_ulong comp_len,
                                     const uint8_t *data, size_t size) {
  if (comp_len < 3 || size < 2) return;
  unsigned char *tmp = (unsigned char *)malloc(comp_len);
  if (!tmp) return;
  for (int i = 0; i < 4 && (size_t)(i * 2 + 1) < size; i++) {
    memcpy(tmp, comp, comp_len);
    size_t byte_off = 2 + (data[i * 2] % (comp_len - 2));
    int bit = data[i * 2 + 1] % 8;
    tmp[byte_off] ^= (unsigned char)(1u << bit);
    run_uncompress(tmp, comp_len);
  }
  free(tmp);
}

static void run_with_strategy(const uint8_t *data, size_t size, int level, int strategy) {
  if (size == 0) return;

  mz_stream strm;
  memset(&strm, 0, sizeof(strm));
  if (mz_deflateInit2(&strm, level, MZ_DEFLATED, MZ_DEFAULT_WINDOW_BITS,
                       9  , strategy) != MZ_OK) {
    return;
  }

  mz_ulong bound = mz_compressBound((mz_ulong)size) + 64;
  unsigned char *comp_buf = (unsigned char *)malloc(bound);
  if (!comp_buf) {
    mz_deflateEnd(&strm);
    return;
  }

  strm.next_in = data;
  strm.avail_in = (unsigned int)size;
  strm.next_out = comp_buf;
  strm.avail_out = (unsigned int)bound;

  int st = mz_deflate(&strm, MZ_FINISH);
  mz_ulong comp_len = bound - strm.avail_out;
  mz_deflateEnd(&strm);

  if (st == MZ_STREAM_END && comp_len > 0) {

    run_uncompress(comp_buf, comp_len);

    if (comp_len > 2) {
      unsigned char *c2 = (unsigned char *)malloc(comp_len);
      if (c2) {
        memcpy(c2, comp_buf, comp_len);
        c2[2] ^= 0x07;
        run_uncompress(c2, comp_len);
        free(c2);
      }
    }

    if (comp_len >= 4) {
      unsigned char *c3 = (unsigned char *)malloc(comp_len);
      if (c3) {
        memcpy(c3, comp_buf, comp_len);
        c3[comp_len - 1] ^= 0xFF;
        run_uncompress(c3, comp_len);
        free(c3);
      }
    }

    run_bitflip_corruptions(comp_buf, comp_len, data, size);
  }
  free(comp_buf);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size == 0) {
    return 0;
  }

  run_uncompress(data, (mz_ulong)size);

  {
    size_t copy_len = size + 2;
    unsigned char *copy = (unsigned char *)malloc(copy_len);
    if (copy) {
      memcpy(copy + 2, data, size);

      unsigned level_flags = 0;
      unsigned w_bits = 8;
      unsigned header = (8   + ((w_bits - 8) << 4)) << 8;
      header |= (level_flags << 6);
      header += 31 - (header % 31);
      copy[0] = (unsigned char)(header >> 8);
      copy[1] = (unsigned char)(header & 0xff);

      run_uncompress(copy, (mz_ulong)copy_len);
      free(copy);
    }
  }

  {
    mz_ulong plain_len = (mz_ulong)size;
    mz_ulong comp_bound = mz_compressBound(plain_len) + 64;
    unsigned char *comp_buf = (unsigned char *)malloc(comp_bound);
    if (comp_buf) {
      mz_ulong comp_len = comp_bound;

      int level = (int)(data[0] % 10);
      int cstatus = mz_compress2(comp_buf, &comp_len, data, plain_len, level);
      if (cstatus == MZ_OK) {
        run_uncompress(comp_buf, comp_len);

        if (comp_len > 0) {
          unsigned char *flip_copy = (unsigned char *)malloc(comp_len);
          if (flip_copy) {
            memcpy(flip_copy, comp_buf, comp_len);
            flip_copy[comp_len / 2] ^= 0xFF;
            run_uncompress(flip_copy, comp_len);
            free(flip_copy);
          }
        }

        run_bitflip_corruptions(comp_buf, comp_len, data, size);
      }
      free(comp_buf);
    }
  }

  {
    mz_ulong plain_len = (mz_ulong)size;
    mz_ulong comp_bound = mz_compressBound(plain_len) + 64;
    unsigned char *comp_buf = (unsigned char *)malloc(comp_bound);
    if (comp_buf) {
      mz_ulong comp_len = comp_bound;
      int cstatus = mz_compress(comp_buf, &comp_len, data, plain_len);
      if (cstatus == MZ_OK && plain_len > 1) {
        mz_ulong small_dest_len = plain_len / 2;
        unsigned char *small_dest = (unsigned char *)malloc(small_dest_len > 0 ? small_dest_len : 1);
        if (small_dest) {
          mz_ulong actual_len = small_dest_len > 0 ? small_dest_len : 1;
          int status = mz_uncompress(small_dest, &actual_len, comp_buf, comp_len);
          (void)status;
          free(small_dest);
        }
      }
      free(comp_buf);
    }
  }

  {
    static const int strategies[] = {MZ_DEFAULT_STRATEGY, MZ_FILTERED,
                                      MZ_HUFFMAN_ONLY, MZ_RLE, MZ_FIXED};
    int strategy = strategies[data[0] % 5];
    int level = (int)((size > 1 ? data[1] : data[0]) % 10);
    run_with_strategy(data, size, level, strategy);
  }

  {
    mz_ulong plain_len = (mz_ulong)size;
    mz_ulong comp_bound = mz_compressBound(plain_len) + 64;
    unsigned char *comp_buf = (unsigned char *)malloc(comp_bound);
    if (comp_buf) {
      mz_ulong comp_len = comp_bound;
      if (mz_compress(comp_buf, &comp_len, data, plain_len) == MZ_OK) {

        {
          unsigned char dummy = 0;
          mz_ulong actual_len = 0;
          int status = mz_uncompress(&dummy, &actual_len, comp_buf, comp_len);
          (void)status;
        }

        {
          unsigned char small_dest[1];
          mz_ulong actual_len = 1;
          int status = mz_uncompress(small_dest, &actual_len, comp_buf, comp_len);
          (void)status;
        }

        if (plain_len > 1) {
          mz_ulong small_len = plain_len - 1;
          unsigned char *small_dest = (unsigned char *)malloc(small_len);
          if (small_dest) {
            mz_ulong actual_len = small_len;
            int status = mz_uncompress(small_dest, &actual_len, comp_buf, comp_len);
            (void)status;
            free(small_dest);
          }
        }
      }
      free(comp_buf);
    }
  }

  if (sizeof(mz_ulong) > 4) {
    unsigned char tiny_src[4] = {0, 0, 0, 0};
    unsigned char tiny_dest[4];

    {
      mz_ulong dl = (mz_ulong)0x100000000ULL;
      int status = mz_uncompress(tiny_dest, &dl, tiny_src, (mz_ulong)sizeof(tiny_src));
      (void)status;
    }

    {
      mz_ulong dl2 = (mz_ulong)sizeof(tiny_dest);
      int status = mz_uncompress(tiny_dest, &dl2, tiny_src, (mz_ulong)0x100000000ULL);
      (void)status;
    }
  }

  {
    unsigned char dest[16];
    mz_ulong dlen;

    {
      unsigned char s[] = {0x78, 0x9c, 0x07};
      dlen = sizeof(dest);
      int status = mz_uncompress(dest, &dlen, s, sizeof(s));
      (void)status;
    }

    {
      unsigned char s[] = {0x78, 0x9c, 0x01, 0x05, 0x00, 0x05, 0x00};
      dlen = sizeof(dest);
      int status = mz_uncompress(dest, &dlen, s, sizeof(s));
      (void)status;
    }

    {
      unsigned char s[] = {0x78, 0x20, 0x00, 0x00, 0x00, 0x00};
      dlen = sizeof(dest);
      int status = mz_uncompress(dest, &dlen, s, sizeof(s));
      (void)status;
    }
  }

  return 0;
}