#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "zlib.h"

static const char *kReadPath  = "/dev/shm/gzopen_fuzz_read_target";
static const char *kWritePath = "/dev/shm/gzopen_fuzz_write_target";

static void write_gzip_like_member(FILE *f, const uint8_t *payload,
                                    size_t payload_len, unsigned char flg) {
  unsigned char hdr[10] = {
    0x1f, 0x8b, 0x08, flg,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03
  };
  fwrite(hdr, 1, sizeof(hdr), f);
  if (payload_len > 0) {
    fwrite(payload, 1, payload_len, f);
  }
  unsigned long crc = crc32(0L, Z_NULL, 0);
  if (payload_len > 0) {
    crc = crc32(crc, payload, (uInt)payload_len);
  }
  unsigned char trailer[8];
  trailer[0] = (unsigned char)(crc & 0xff);
  trailer[1] = (unsigned char)((crc >> 8) & 0xff);
  trailer[2] = (unsigned char)((crc >> 16) & 0xff);
  trailer[3] = (unsigned char)((crc >> 24) & 0xff);
  unsigned long isize = (unsigned long)payload_len;
  trailer[4] = (unsigned char)(isize & 0xff);
  trailer[5] = (unsigned char)((isize >> 8) & 0xff);
  trailer[6] = (unsigned char)((isize >> 16) & 0xff);
  trailer[7] = (unsigned char)((isize >> 24) & 0xff);
  fwrite(trailer, 1, sizeof(trailer), f);
}

static void exercise_common_accessors(gzFile gf, uint8_t b3, uint8_t b4) {

  gzrewind(gf);

  int16_t wide = (int16_t)(((uint16_t)b3 << 8) | (uint16_t)b4);
  z_off_t seek_off = (z_off_t)wide;
  gzseek(gf, seek_off, (b3 & 1) ? SEEK_CUR : SEEK_SET);

  gztell(gf);
  gzoffset(gf);
  gzeof(gf);
  int errnum = 0;
  gzerror(gf, &errnum);
  gzclearerr(gf);
  gzdirect(gf);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

  if (size < 5) {
    return 0;
  }

  uint8_t b0 = data[0];
  uint8_t b1 = data[1];
  uint8_t b2 = data[2];
  uint8_t b3 = data[3];
  uint8_t b4 = data[4];

  const uint8_t *remaining = data + 5;
  size_t remaining_size = size - 5;

  static const size_t kMaxSize = 1 << 13;
  if (remaining_size > kMaxSize) {
    remaining_size = kMaxSize;
  }

  if ((b0 % 32) == 0) {
    gzFile gf = gzopen(NULL, "rb");
    if (gf != NULL) {
      gzclose(gf);
    }
    return 0;
  }
  if ((b0 % 32) == 1) {
    gzFile gf = gzopen(kReadPath, NULL);
    if (gf != NULL) {
      gzclose(gf);
    }
    return 0;
  }

  static const char base_chars[3] = {'r', 'w', 'a'};
  int has_base = (b0 % 8) != 0;

  char base = has_base ? base_chars[b0 % 3] : 0;

  static const char candidates[] =
      {'f', 'h', 'R', 'F', 'T', 'G', 'N', 'x', 'e', 'b', '+'};
  static const size_t kNumCandidates = sizeof(candidates);

  char mode[16];
  size_t idx = 0;
  if (has_base) {
    mode[idx++] = base;
  }
  mode[idx++] = (char)('0' + (b1 % 10));

  uint8_t pick_bytes[3] = {b2, b3, b4};
  for (int i = 0; i < 3 && idx < sizeof(mode) - 1; i++) {
    if ((pick_bytes[i] % 2) == 0) {
      mode[idx++] = candidates[pick_bytes[i] % kNumCandidates];
    }
  }
  mode[idx] = '\0';

  if (has_base && idx < sizeof(mode) - 1) {
    if (base == 'r' && (b1 % 3 == 0)) {
      int already = 0;
      for (size_t i = 0; i < idx; i++) {
        if (mode[i] == 'G') { already = 1; break; }
      }
      if (!already) {
        mode[idx++] = 'G';
        mode[idx] = '\0';
      }
    } else if (base != 'r' && (b1 % 3 == 0) && idx < sizeof(mode) - 1) {
      int already = 0;
      for (size_t i = 0; i < idx; i++) {
        if (mode[i] == 'T') { already = 1; break; }
      }
      if (!already) {
        mode[idx++] = 'T';
        mode[idx] = '\0';
      }
    }
  }

  if (!has_base) {

    gzFile gf = gzopen(kReadPath, mode);
    if (gf != NULL) {

      gzclose(gf);
    }
    return 0;
  }

  if (base == 'r') {

    int seed_strategy = b4 % 4;

    if (seed_strategy == 0) {

      FILE *seed = fopen(kReadPath, "wb");
      if (seed != NULL) {
        write_gzip_like_member(seed, remaining, remaining_size,
                                (unsigned char)b2);
        fclose(seed);
      }
    } else if (seed_strategy == 1) {

      FILE *seed = fopen(kReadPath, "wb");
      if (seed != NULL) {
        unsigned char hdr[10] = {
          0x1f, 0x8b, 0x08, (unsigned char)b2,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x03
        };
        fwrite(hdr, 1, sizeof(hdr), seed);
        if (remaining_size > 0) {
          fwrite(remaining, 1, remaining_size, seed);
        }
        fclose(seed);
      }
    } else if (seed_strategy == 2) {

      FILE *seed = fopen(kReadPath, "wb");
      if (seed != NULL) {
        if (remaining_size > 0) {
          fwrite(remaining, 1, remaining_size, seed);
        }
        fclose(seed);
      }
    } else {

      FILE *seed = fopen(kReadPath, "wb");
      if (seed != NULL) {
        size_t half = remaining_size / 2;
        write_gzip_like_member(seed, remaining, half, (unsigned char)b2);
        unsigned char hdr2[10] = {
          0x1f, 0x8b, 0x08, (unsigned char)b3,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x03
        };
        fwrite(hdr2, 1, sizeof(hdr2), seed);
        if (remaining_size > half) {
          fwrite(remaining + half, 1, remaining_size - half, seed);
        }
        fclose(seed);
      }
    }

    gzFile gf = gzopen(kReadPath, mode);
    if (gf != NULL) {

      if ((b4 % 16) == 0) {
        gzclose_r(gf);
        return 0;
      }

      gzbuffer(gf, 8 + (unsigned)(b2 % 64));

      int c;
      int getc_calls = 0;
      while ((c = gzgetc(gf)) != -1 && getc_calls < 512) {
        getc_calls++;
      }

      gzungetc('X', gf);

      char line[64];
      int gets_calls = 0;
      while (gzgets(gf, line, sizeof(line)) != NULL && gets_calls < 64) {
        gets_calls++;
      }

      unsigned char fbuf[64];
      gzfread(fbuf, 1, sizeof(fbuf), gf);

      exercise_common_accessors(gf, b3, b4);

      char buf[256];
      int rlen;
      do {
        rlen = gzread(gf, buf, (unsigned)sizeof(buf));
      } while (rlen > 0);

      gzbuffer(gf, 4096);

      if ((b2 & 1) == 0) {
        gzclose_r(gf);
      } else {
        gzclose(gf);
      }
    }
  } else {

    gzFile gf = gzopen(kWritePath, mode);
    if (gf != NULL) {

      if ((b4 % 16) == 0) {
        gzclose_w(gf);
        return 0;
      }

      gzbuffer(gf, 8 + (unsigned)(b2 % 64));

      gzputc(gf, 'A');
      gzputs(gf, "fuzz");
      gzprintf(gf, "%d", b1);

      gzsetparams(gf, b2 % 10, b3 % 5);
      gzputs(gf, "mid1");
      gzsetparams(gf, (b2 + 3) % 10, (b3 + 1) % 5);
      gzputs(gf, "mid2");

      static const int flushes[] = {Z_NO_FLUSH, Z_PARTIAL_FLUSH,
                                     Z_SYNC_FLUSH, Z_FULL_FLUSH,
                                     Z_BLOCK, Z_FINISH};
      static const size_t kNumFlushes = sizeof(flushes) / sizeof(flushes[0]);

      size_t half = remaining_size / 2;
      if (half > 0) {
        gzwrite(gf, remaining, (unsigned)half);
      }
      gzflush(gf, flushes[b3 % kNumFlushes]);

      if (remaining_size > half) {
        gzwrite(gf, remaining + half, (unsigned)(remaining_size - half));
      }
      gzflush(gf, flushes[b4 % kNumFlushes]);

      if ((b1 % 4) == 0) {
        gzflush(gf, -1);
      }

      exercise_common_accessors(gf, b3, b4);

      if ((b2 & 1) == 0) {
        gzclose_w(gf);
      } else {
        gzclose(gf);
      }
    }
  }

  return 0;
}