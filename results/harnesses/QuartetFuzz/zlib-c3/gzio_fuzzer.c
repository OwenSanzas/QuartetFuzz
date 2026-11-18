#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "zlib.h"

static const char *kRawPath = "/tmp/gzio_fuzzer_raw.gz";
static const char *kWritePath = "/tmp/gzio_fuzzer_write.gz";

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 6)
    return 0;

  uint8_t ctrl1 = data[0];
  uint8_t ctrl2 = data[1];
  uint8_t ctrl3 = data[2];
  uint8_t ctrl4 = data[3];
  uint8_t ctrl5 = data[4];
  uint8_t ctrl6 = data[5];
  const uint8_t *payload = data + 6;
  size_t payload_len = size - 6;

  {
    FILE *f = fopen(kRawPath, "wb");
    if (f != NULL) {
      if (payload_len > 0)
        fwrite(payload, 1, payload_len, f);
      fclose(f);

      char mode[8];
      int mi = 0;
      mode[mi++] = 'r';

      if (ctrl1 & 0x1)
        mode[mi++] = 'T';
      else if (ctrl1 & 0x2)
        mode[mi++] = 'G';
      mode[mi] = '\0';

      gzFile gf = gzopen(kRawPath, mode);
      if (gf != NULL) {

        unsigned want = 8 + (unsigned)(ctrl2 % 64);
        gzbuffer(gf, want);

        int direct = gzdirect(gf);
        (void)direct;

        char rbuf[256];
        int rret;
        long total = 0;
        do {
          rret = gzread(gf, rbuf, sizeof(rbuf));
          if (rret > 0)
            total += rret;
        } while (rret > 0 && total < (1 << 20));

        z_off_t pos = gztell(gf);
        (void)pos;
        if (ctrl3 & 0x1)
          gzseek(gf, (z_off_t)(ctrl4 % 128), SEEK_SET);
        else
          gzrewind(gf);

        int c = gzgetc(gf);
        if (c != -1)
          gzungetc(c, gf);

        char linebuf[128];
        gzgets(gf, linebuf, (int)sizeof(linebuf));

        int errnum = 0;
        const char *msg = gzerror(gf, &errnum);
        (void)msg;
        gzclearerr(gf);

        gzclose(gf);
      }
    }
  }

  {
    int level = ctrl2 % 10;
    char strategy = 0;
    switch (ctrl3 % 5) {
    case 0:
      strategy = 0;
      break;
    case 1:
      strategy = 'f';
      break;
    case 2:
      strategy = 'h';
      break;
    case 3:
      strategy = 'R';
      break;
    case 4:
      strategy = 'F';
      break;
    }

    char wmode[8];
    int wi = 0;
    wmode[wi++] = 'w';
    wmode[wi++] = (char)('0' + level);
    if (strategy)
      wmode[wi++] = strategy;
    if (ctrl4 & 0x1)
      wmode[wi++] = 'T';
    wmode[wi] = '\0';

    int fd = open(kWritePath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
      gzFile gw = gzdopen(fd, wmode);
      if (gw == NULL) {
        close(fd);
      } else {
        unsigned want = 8 + (unsigned)(ctrl4 % 64);
        gzbuffer(gw, want);

        if (payload_len > 0) {
          size_t off = 0;
          size_t chunk = 1 + (size_t)(ctrl1 % 64);
          while (off < payload_len) {
            size_t n = payload_len - off;
            if (n > chunk)
              n = chunk;
            gzwrite(gw, payload + off, (unsigned)n);
            off += n;
          }
        }
        gzputc(gw, (int)ctrl5);
        gzputs(gw, "gzio_fuzzer");
        gzprintf(gw, "-%d-", (int)ctrl1);

        int sp_strategy;
        switch (ctrl6 % 4) {
        case 0:
          sp_strategy = Z_DEFAULT_STRATEGY;
          break;
        case 1:
          sp_strategy = Z_FILTERED;
          break;
        case 2:
          sp_strategy = Z_HUFFMAN_ONLY;
          break;
        default:
          sp_strategy = Z_RLE;
          break;
        }
        gzsetparams(gw, (ctrl6 % 10), sp_strategy);

        int flush_mode = (int)(ctrl5 % (Z_FINISH + 1));
        gzflush(gw, flush_mode);

        gzclose(gw);
      }
    }

    if (ctrl6 & 0x1) {
      gzFile ga = gzopen(kWritePath, "ab");
      if (ga != NULL) {
        gzputs(ga, "appended");
        gzclose(ga);
      }
    }

    gzFile gr = gzopen(kWritePath, "rb");
    if (gr != NULL) {
      char rbuf[256];
      int rret;
      long total = 0;
      do {
        rret = gzread(gr, rbuf, sizeof(rbuf));
        if (rret > 0)
          total += rret;
      } while (rret > 0 && total < (1 << 20));

      z_off_t pos = gztell(gr);
      (void)pos;
      gzseek(gr, 0, SEEK_SET);
      gzrewind(gr);

      gzclose(gr);
    }
  }

  unlink(kRawPath);
  unlink(kWritePath);

  return 0;
}
