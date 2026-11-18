#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "llhttp.h"

typedef struct {
  uint8_t mode;
  unsigned hash;
} fuzz_ctx_t;

static unsigned hash_span(const char *at, size_t length) {
  unsigned h = 2166136261u;
  size_t i;
  for (i = 0; i < length; i++) {
    h ^= (unsigned char) at[i];
    h *= 16777619u;
  }
  return h;
}

static int span_cb(llhttp_t *parser, const char *at, size_t length,
                    unsigned bit) {
  fuzz_ctx_t *ctx = (fuzz_ctx_t *) parser->data;
  unsigned h = hash_span(at, length);
  if (ctx != NULL) {
    ctx->hash ^= h;
    if ((ctx->mode & bit) != 0 && (h & 1) != 0) {
      return -1;
    }
  }
  return 0;
}

static int on_protocol(llhttp_t *p, const char *at, size_t len) { return span_cb(p, at, len, 0x01); }
static int on_url(llhttp_t *p, const char *at, size_t len) { return span_cb(p, at, len, 0x02); }
static int on_status(llhttp_t *p, const char *at, size_t len) { return span_cb(p, at, len, 0x04); }
static int on_method(llhttp_t *p, const char *at, size_t len) { return span_cb(p, at, len, 0x08); }
static int on_version(llhttp_t *p, const char *at, size_t len) { return span_cb(p, at, len, 0x10); }
static int on_header_field(llhttp_t *p, const char *at, size_t len) { return span_cb(p, at, len, 0x20); }
static int on_header_value(llhttp_t *p, const char *at, size_t len) { return span_cb(p, at, len, 0x40); }
static int on_body(llhttp_t *p, const char *at, size_t len) { return span_cb(p, at, len, 0x80); }
static int on_chunk_extension_name(llhttp_t *p, const char *at, size_t len) { return span_cb(p, at, len, 0x01); }
static int on_chunk_extension_value(llhttp_t *p, const char *at, size_t len) { return span_cb(p, at, len, 0x02); }

static int notify_cb(llhttp_t *parser, unsigned bit) {
  fuzz_ctx_t *ctx = (fuzz_ctx_t *) parser->data;
  if (ctx != NULL && (ctx->mode & bit) != 0) {
    return HPE_PAUSED;
  }
  return 0;
}

static int on_message_begin(llhttp_t *p) { return notify_cb(p, 0x01); }
static int on_message_complete(llhttp_t *p) { return notify_cb(p, 0x02); }
static int on_protocol_complete(llhttp_t *p) { return notify_cb(p, 0x04); }
static int on_url_complete(llhttp_t *p) { return notify_cb(p, 0x08); }
static int on_status_complete(llhttp_t *p) { return notify_cb(p, 0x10); }
static int on_method_complete(llhttp_t *p) { return notify_cb(p, 0x20); }
static int on_version_complete(llhttp_t *p) { return notify_cb(p, 0x40); }
static int on_header_field_complete(llhttp_t *p) { return notify_cb(p, 0x80); }
static int on_header_value_complete(llhttp_t *p) { return notify_cb(p, 0x01); }
static int on_chunk_extension_name_complete(llhttp_t *p) { return notify_cb(p, 0x02); }
static int on_chunk_extension_value_complete(llhttp_t *p) { return notify_cb(p, 0x04); }
static int on_chunk_complete(llhttp_t *p) { return notify_cb(p, 0x08); }
static int on_reset(llhttp_t *p) { return notify_cb(p, 0x10); }

static int on_chunk_header(llhttp_t *p) { return notify_cb(p, 0x20); }

static int on_headers_complete(llhttp_t *parser) {
  fuzz_ctx_t *ctx = (fuzz_ctx_t *) parser->data;
  unsigned sel;

  (void) llhttp_get_type(parser);
  (void) llhttp_get_http_major(parser);
  (void) llhttp_get_http_minor(parser);
  (void) llhttp_get_method(parser);
  (void) llhttp_get_status_code(parser);
  (void) llhttp_get_upgrade(parser);
  (void) llhttp_message_needs_eof(parser);
  (void) llhttp_should_keep_alive(parser);
  (void) llhttp_method_name((llhttp_method_t) llhttp_get_method(parser));

  if (ctx == NULL) return 0;

  sel = ctx->mode & 0x03;
  if (sel == 0) return 0;
  if (sel == 1) return 1;
  if (sel == 2) return 2;
  return -1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 5) {
    return 0;
  }

  uint8_t ctrl = data[0];
  uint8_t chunk_ctrl = data[1];
  uint8_t mode = data[2];
  uint8_t extra_ctrl = data[3];

  const char *stream = (const char *) (data + 4);
  size_t stream_len = size - 4;

  llhttp_settings_t settings;
  llhttp_settings_init(&settings);

  settings.on_message_begin = on_message_begin;
  settings.on_protocol = on_protocol;
  settings.on_url = on_url;
  settings.on_status = on_status;
  settings.on_method = on_method;
  settings.on_version = on_version;
  settings.on_header_field = on_header_field;
  settings.on_header_value = on_header_value;
  settings.on_chunk_extension_name = on_chunk_extension_name;
  settings.on_chunk_extension_value = on_chunk_extension_value;
  settings.on_headers_complete = on_headers_complete;
  settings.on_body = on_body;
  settings.on_message_complete = on_message_complete;
  settings.on_protocol_complete = on_protocol_complete;
  settings.on_url_complete = on_url_complete;
  settings.on_status_complete = on_status_complete;
  settings.on_method_complete = on_method_complete;
  settings.on_version_complete = on_version_complete;
  settings.on_header_field_complete = on_header_field_complete;
  settings.on_header_value_complete = on_header_value_complete;
  settings.on_chunk_extension_name_complete = on_chunk_extension_name_complete;
  settings.on_chunk_extension_value_complete = on_chunk_extension_value_complete;
  settings.on_chunk_header = on_chunk_header;
  settings.on_chunk_complete = on_chunk_complete;
  settings.on_reset = on_reset;

  if (ctrl & 0x80) {
    settings.on_protocol = NULL;
    settings.on_protocol_complete = NULL;
    settings.on_chunk_extension_name = NULL;
    settings.on_chunk_extension_value = NULL;
    settings.on_chunk_extension_name_complete = NULL;
    settings.on_chunk_extension_value_complete = NULL;
  }

  llhttp_type_t type;
  switch (ctrl & 0x03) {
    case 0: type = HTTP_BOTH; break;
    case 1: type = HTTP_REQUEST; break;
    default: type = HTTP_RESPONSE; break;
  }

  llhttp_t parser;
  llhttp_init(&parser, type, &settings);

  fuzz_ctx_t ctx;
  ctx.mode = mode;
  ctx.hash = 0;
  parser.data = &ctx;

  llhttp_set_lenient_headers(&parser, (ctrl & 0x04) != 0);
  llhttp_set_lenient_chunked_length(&parser, (ctrl & 0x08) != 0);
  llhttp_set_lenient_keep_alive(&parser, (ctrl & 0x10) != 0);
  llhttp_set_lenient_transfer_encoding(&parser, (ctrl & 0x20) != 0);
  llhttp_set_lenient_version(&parser, (ctrl & 0x40) != 0);
  llhttp_set_lenient_data_after_close(&parser, (extra_ctrl & 0x01) != 0);

  llhttp_set_lenient_optional_lf_after_cr(&parser, (chunk_ctrl & 0x10) != 0);
  llhttp_set_lenient_optional_cr_before_lf(&parser, (chunk_ctrl & 0x20) != 0);
  llhttp_set_lenient_optional_crlf_after_chunk(&parser, (chunk_ctrl & 0x40) != 0);
  llhttp_set_lenient_spaces_after_chunk_size(&parser, (chunk_ctrl & 0x80) != 0);

  unsigned nchunks = (chunk_ctrl & 0x03) + 1;
  size_t base = stream_len / nchunks;
  if (base == 0 && stream_len > 0) {
    base = stream_len;
    nchunks = 1;
  }

  const char *cursor = stream;
  size_t remaining = stream_len;
  size_t pass;

  for (pass = 0; pass < nchunks && remaining > 0; pass++) {
    size_t this_len = (pass + 1 == nchunks) ? remaining : base;
    if (this_len > remaining) this_len = remaining;

    llhttp_errno_t err = llhttp_execute(&parser, cursor, this_len);

    cursor += this_len;
    remaining -= this_len;

    if (err == HPE_PAUSED) {
      llhttp_resume(&parser);
    } else if (err == HPE_PAUSED_UPGRADE) {
      llhttp_resume_after_upgrade(&parser);
    } else if (err != HPE_OK) {

      (void) llhttp_get_error_reason(&parser);
      (void) llhttp_get_error_pos(&parser);
      (void) llhttp_errno_name(err);
      break;
    }
  }

  if (chunk_ctrl & 0x04) {
    llhttp_errno_t ferr = llhttp_finish(&parser);
    (void) ferr;
  }

  (void) llhttp_get_errno(&parser);
  (void) llhttp_get_type(&parser);
  (void) llhttp_should_keep_alive(&parser);
  (void) llhttp_get_error_reason(&parser);

  if ((chunk_ctrl & 0x08) != 0 && stream_len > 0) {
    llhttp_reset(&parser);
    ctx.hash = 0;
    llhttp_errno_t err2 = llhttp_execute(&parser, stream, stream_len);
    if (err2 == HPE_PAUSED) {
      llhttp_resume(&parser);
    } else if (err2 == HPE_PAUSED_UPGRADE) {
      llhttp_resume_after_upgrade(&parser);
    } else if (err2 != HPE_OK) {
      (void) llhttp_get_error_reason(&parser);
      (void) llhttp_get_error_pos(&parser);
      (void) llhttp_errno_name(err2);
    }
    (void) llhttp_get_errno(&parser);
    (void) llhttp_should_keep_alive(&parser);
  }

  parser.data = NULL;

  return 0;
}
