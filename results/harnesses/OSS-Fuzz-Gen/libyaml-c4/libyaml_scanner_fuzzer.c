#include "yaml.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef NDEBUG
#undef NDEBUG
#endif

typedef struct {
  const uint8_t *data;
  size_t size;
  size_t pos;

  int fail_mode;
} read_ctx_t;

static int custom_read_handler(void *data, unsigned char *buffer,
                                size_t size, size_t *size_read) {
  read_ctx_t *ctx = (read_ctx_t *)data;

  if (ctx->fail_mode == 1) {
    ctx->fail_mode = 0;
    return 0;
  }
  if (ctx->fail_mode == 2 && ctx->pos >= ctx->size / 2) {
    ctx->fail_mode = 0;
    return 0;
  }

  size_t remaining = ctx->size - ctx->pos;
  size_t to_copy = remaining < size ? remaining : size;

  if (to_copy > 64) to_copy = 64;
  if (to_copy > 0) {
    memcpy(buffer, ctx->data + ctx->pos, to_copy);
  }
  ctx->pos += to_copy;
  *size_read = to_copy;
  return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  yaml_parser_t parser;
  yaml_token_t token;
  bool done = false;
  uint8_t *utf16_buf = NULL;

  if (!yaml_parser_initialize(&parser))
    return 0;

  uint8_t enc_sel = 0;
  uint8_t method_sel = 0;
  if (size > 0) {
    enc_sel = data[size - 1];
    size--;
  }
  if (size > 0) {
    method_sel = data[size - 1];
    size--;
  }

  bool utf16_le = false;
  bool utf16_be = false;

  bool utf16_raw = (enc_sel & 0x08) != 0;

  switch (enc_sel & 0x07) {
    case 0:
      yaml_parser_set_encoding(&parser, YAML_UTF16LE_ENCODING);
      utf16_le = true;
      break;
    case 1:
      yaml_parser_set_encoding(&parser, YAML_UTF16BE_ENCODING);
      utf16_be = true;
      break;
    default:

      break;
  }

  int input_method = 0;
  {
    int sel = method_sel % 16;
    if (sel == 0) {
      input_method = 2;
    } else if (sel == 1) {
      input_method = 3;
    } else if (sel < 12) {
      input_method = 0;
    } else {
      input_method = 1;
    }
  }

  const uint8_t *effective_data = data;
  size_t effective_size = size;
  if ((utf16_le || utf16_be) && size > 0) {
    if (utf16_raw) {

      effective_data = data;
      effective_size = size;
    } else {
      size_t wlen = size * 2;
      utf16_buf = (uint8_t *)malloc(wlen);
      if (utf16_buf) {
        for (size_t i = 0; i < size; i++) {
          if (utf16_le) {
            utf16_buf[2 * i] = data[i];
            utf16_buf[2 * i + 1] = 0x00;
          } else {
            utf16_buf[2 * i] = 0x00;
            utf16_buf[2 * i + 1] = data[i];
          }
        }
        effective_data = utf16_buf;
        effective_size = wlen;
      }
    }
  }

  read_ctx_t ctx;
  ctx.data = effective_data;
  ctx.size = effective_size;
  ctx.pos = 0;
  ctx.fail_mode = 0;

  switch (input_method) {
    case 0:
      yaml_parser_set_input_string(&parser, effective_data, effective_size);
      break;
    case 1:
      yaml_parser_set_input(&parser, custom_read_handler, &ctx);
      break;
    case 2:
      ctx.fail_mode = 1;
      yaml_parser_set_input(&parser, custom_read_handler, &ctx);
      break;
    case 3:
      ctx.fail_mode = 2;
      yaml_parser_set_input(&parser, custom_read_handler, &ctx);
      break;
    default:
      yaml_parser_set_input_string(&parser, effective_data, effective_size);
      break;
  }

  while (!done) {
    if (!yaml_parser_scan(&parser, &token))
      break;

    done = (token.type == YAML_STREAM_END_TOKEN);

    yaml_token_delete(&token);
  }

  yaml_parser_delete(&parser);

  if (utf16_buf) {
    free(utf16_buf);
  }

  return 0;
}