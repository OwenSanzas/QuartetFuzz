#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "zlib.h"

static uint8_t take_byte(const uint8_t **p, size_t *len) {
  if (*len == 0) return 0;
  uint8_t v = **p;
  (*p)++;
  (*len)--;
  return v;
}

static int run_deflate_session(z_stream *c_stream, const uint8_t *in_data,
                                size_t in_len, const uint8_t **p,
                                size_t *remaining, uint8_t **out_buf,
                                size_t *out_len) {
  size_t out_cap = 128 + 2 * (size_t)deflateBound(c_stream, (uLong)in_len);
  uint8_t *buf = (uint8_t *)malloc(out_cap);
  if (buf == NULL) return 0;

  c_stream->next_out = buf;
  c_stream->avail_out = (uInt)out_cap;

  size_t fed = 0;
  int round = 0;
  int stream_broken = 0;
  int params_switches = 0;
  int err;

  while (fed < in_len && round < 16) {
    uint8_t chunk_sel = take_byte(p, remaining);
    size_t left = in_len - fed;
    size_t chunk = left ? (size_t)(chunk_sel % left + 1) : 0;
    if (chunk == 0) chunk = left;

    c_stream->next_in = (z_const Bytef *)(in_data + fed);
    c_stream->avail_in = (uInt)chunk;

    uint8_t flush_sel = take_byte(p, remaining);
    int flush = flush_sel % 6;

    if ((chunk_sel & 0x80) && round > 0 && params_switches < 3) {
      int new_level = (int)((chunk_sel >> 3) % 10);
      int new_strategy = (int)(flush_sel % 5);
      int perr = deflateParams(c_stream, new_level, new_strategy);
      if (perr != Z_OK && perr != Z_BUF_ERROR) {
        stream_broken = 1;
        break;
      }
      params_switches++;
    }

    err = deflate(c_stream, flush);
    if (err == Z_STREAM_ERROR) {
      stream_broken = 1;
      break;
    }
    fed += chunk;

    if (c_stream->avail_out == 0) {
      size_t used = out_cap - c_stream->avail_out;
      size_t grow = out_cap * 2;
      uint8_t *grown = (uint8_t *)realloc(buf, grow);
      if (grown == NULL) {
        stream_broken = 1;
        break;
      }
      buf = grown;
      out_cap = grow;
      c_stream->next_out = buf + used;
      c_stream->avail_out = (uInt)(out_cap - used);
    }
    round++;
  }

  if (!stream_broken) {
    c_stream->next_in = Z_NULL;
    c_stream->avail_in = 0;
    int finish_rounds = 0;
    for (;;) {
      err = deflate(c_stream, Z_FINISH);
      if (err == Z_STREAM_END) break;
      if (err != Z_OK && err != Z_BUF_ERROR) {
        stream_broken = 1;
        break;
      }
      if (c_stream->avail_out == 0 && finish_rounds < 32) {
        size_t used = out_cap - c_stream->avail_out;
        size_t grow = out_cap * 2;
        uint8_t *grown = (uint8_t *)realloc(buf, grow);
        if (grown == NULL) {
          stream_broken = 1;
          break;
        }
        buf = grown;
        out_cap = grow;
        c_stream->next_out = buf + used;
        c_stream->avail_out = (uInt)(out_cap - used);
      } else {
        break;
      }
      finish_rounds++;
    }
  }

  *out_buf = buf;
  *out_len = (size_t)c_stream->total_out;
  return !stream_broken;
}

static void verify_roundtrip(const uint8_t *compressed, size_t compressed_len,
                              int inflate_wbits, const uint8_t *orig,
                              size_t orig_len, int wrap_is_gzip,
                              const uint8_t *dict_buf, size_t dict_len) {
  if (compressed_len == 0) return;

  z_stream d_stream;
  memset(&d_stream, 0, sizeof(d_stream));
  d_stream.zalloc = Z_NULL;
  d_stream.zfree = Z_NULL;
  d_stream.opaque = Z_NULL;
  d_stream.next_in = (z_const Bytef *)compressed;
  d_stream.avail_in = (uInt)compressed_len;

  if (inflateInit2(&d_stream, inflate_wbits) != Z_OK) return;

  size_t unc_cap = orig_len > 0 ? orig_len + 64 : 64;
  uint8_t *unc_buf = (uint8_t *)malloc(unc_cap);
  if (unc_buf != NULL) {
    int derr;
    size_t total = 0;
    int steps = 0;
    do {
      if (total >= unc_cap) {
        size_t grow = unc_cap * 2;
        uint8_t *grown = (uint8_t *)realloc(unc_buf, grow);
        if (grown == NULL) break;
        unc_buf = grown;
        unc_cap = grow;
      }
      d_stream.next_out = unc_buf + total;
      d_stream.avail_out = (uInt)(unc_cap - total);
      derr = inflate(&d_stream, Z_NO_FLUSH);
      total = unc_cap - d_stream.avail_out;
      if (derr == Z_NEED_DICT && !wrap_is_gzip && dict_len > 0) {
        inflateSetDictionary(&d_stream, dict_buf, (uInt)dict_len);
        derr = Z_OK;
      }
      steps++;
    } while (derr == Z_OK && d_stream.avail_in > 0 && steps < 1024);

    if (derr == Z_STREAM_END && total == orig_len && orig_len > 0) {
      volatile int same = (memcmp(unc_buf, orig, orig_len) == 0);
      (void)same;
    }
    free(unc_buf);
  }
  inflateEnd(&d_stream);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

  if (size < 9 || size > (64u << 10))
    return 0;

  const uint8_t *p = data;
  size_t remaining = size;

  uint8_t b_level = take_byte(&p, &remaining);
  uint8_t b_wbits = take_byte(&p, &remaining);
  uint8_t b_mem = take_byte(&p, &remaining);
  uint8_t b_strategy = take_byte(&p, &remaining);
  uint8_t b_split = take_byte(&p, &remaining);
  uint8_t b_dict_len = take_byte(&p, &remaining);
  uint8_t b_header_flags = take_byte(&p, &remaining);
  uint8_t b_prime = take_byte(&p, &remaining);
  uint8_t b_do_reset = take_byte(&p, &remaining);

  int level = (int)(b_level % 11) - 1;
  int strategy = b_strategy % 5;
  int memLevel = 1 + (b_mem % 9);

  int wbits_mode = b_wbits % 3;
  int windowBits;
  if (wbits_mode == 0) {
    windowBits = -(9 + (b_wbits % 7));
  } else if (wbits_mode == 1) {
    windowBits = 9 + (b_wbits % 7);
  } else {
    windowBits = 9 + (b_wbits % 7) + 16;
  }
  int wrap_is_gzip = (wbits_mode == 2);
  int wrap_is_raw = (wbits_mode == 0);
  int inflate_wbits = wrap_is_raw ? -(9 + (b_wbits % 7))
                     : wrap_is_gzip ? (9 + (b_wbits % 7) + 16)
                     : (9 + (b_wbits % 7));

  z_stream c_stream;
  memset(&c_stream, 0, sizeof(c_stream));
  c_stream.zalloc = Z_NULL;
  c_stream.zfree = Z_NULL;
  c_stream.opaque = Z_NULL;

  int err = deflateInit2(&c_stream, level, Z_DEFLATED, windowBits, memLevel,
                          strategy);
  if (err != Z_OK) {

    return 0;
  }

  size_t dict_len = b_dict_len;
  if (dict_len > remaining) dict_len = remaining;
  uint8_t dict_buf[256];
  if (!wrap_is_gzip && dict_len > 0) {
    if (dict_len > sizeof(dict_buf)) dict_len = sizeof(dict_buf);
    memcpy(dict_buf, p, dict_len);

    (void)deflateSetDictionary(&c_stream, dict_buf, (uInt)dict_len);
  }
  p += dict_len;
  remaining -= dict_len;

  gz_header head;
  memset(&head, 0, sizeof(head));
  uint8_t name_buf[32];
  uint8_t comment_buf[32];
  uint8_t extra_buf[32];
  if (wrap_is_gzip) {
    head.text = (b_header_flags & 0x01) ? 1 : 0;
    head.time = 0x01020304;
    head.os = 3;
    head.hcrc = (b_header_flags & 0x02) ? 1 : 0;

    if (b_header_flags & 0x04) {
      size_t name_len = take_byte(&p, &remaining) % (sizeof(name_buf) - 1);
      if (name_len > remaining) name_len = remaining;
      memcpy(name_buf, p, name_len);
      name_buf[name_len] = '\0';
      p += name_len;
      remaining -= name_len;
      head.name = name_buf;
    }
    if (b_header_flags & 0x08) {
      size_t comment_len =
          take_byte(&p, &remaining) % (sizeof(comment_buf) - 1);
      if (comment_len > remaining) comment_len = remaining;
      memcpy(comment_buf, p, comment_len);
      comment_buf[comment_len] = '\0';
      p += comment_len;
      remaining -= comment_len;
      head.comment = comment_buf;
    }
    if (b_header_flags & 0x10) {
      size_t extra_len = take_byte(&p, &remaining) % sizeof(extra_buf);
      if (extra_len > remaining) extra_len = remaining;
      memcpy(extra_buf, p, extra_len);
      p += extra_len;
      remaining -= extra_len;
      head.extra = extra_buf;
      head.extra_len = (uInt)extra_len;
    }
    (void)deflateSetHeader(&c_stream, &head);
  }

  if (b_prime != 0) {
    int bits = (b_prime % 16) + 1;
    (void)deflatePrime(&c_stream, bits, (int)b_prime);
  }

  const uint8_t *in_data = p;
  size_t in_len = remaining;
  size_t first_len = in_len;
  if (b_do_reset & 1) {
    first_len = in_len ? (size_t)(b_split % in_len + 1) : 0;
  }

  uint8_t *out1 = NULL;
  size_t out1_len = 0;
  int ok1 = run_deflate_session(&c_stream, in_data, first_len, &p, &remaining,
                                 &out1, &out1_len);

  if (ok1) {
    verify_roundtrip(out1, out1_len, inflate_wbits, in_data, first_len,
                      wrap_is_gzip, dict_buf, dict_len);
  }

  uint8_t *out2 = NULL;
  size_t out2_len = 0;
  int ok2 = 0;
  if ((b_do_reset & 1) && first_len < in_len) {
    if (deflateReset(&c_stream) == Z_OK) {
      const uint8_t *second_data = in_data + first_len;
      size_t second_len = in_len - first_len;
      ok2 = run_deflate_session(&c_stream, second_data, second_len, &p,
                                 &remaining, &out2, &out2_len);
      if (ok2) {
        verify_roundtrip(out2, out2_len, inflate_wbits, second_data,
                          second_len, wrap_is_gzip, dict_buf, dict_len);
      }
    }
  }

  deflateEnd(&c_stream);

  free(out1);
  free(out2);
  return 0;
}
