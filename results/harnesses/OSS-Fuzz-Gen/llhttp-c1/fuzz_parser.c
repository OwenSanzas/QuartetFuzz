#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "llhttp.h"

typedef struct {
  const uint8_t *ctrl;
  size_t ctrl_len;
  size_t idx;
} fuzz_ctx_t;

static int next_ctrl(fuzz_ctx_t *ctx) {
  if (ctx->ctrl_len == 0) return 0;
  uint8_t b = ctx->ctrl[ctx->idx % ctx->ctrl_len];
  ctx->idx++;
  return b;
}

static int cb_ret_generic(llhttp_t *parser) {
  fuzz_ctx_t *ctx = (fuzz_ctx_t *)parser->data;
  switch (next_ctrl(ctx) % 8) {
    case 0: return 21;
    case 1: return -1;
    default: return 0;
  }
}

static int cb_headers_complete(llhttp_t *parser) {
  fuzz_ctx_t *ctx = (fuzz_ctx_t *)parser->data;
  switch (next_ctrl(ctx) % 10) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 21;
    case 3: return -1;
    default: return 0;
  }
}

static int cb_data_generic(llhttp_t *parser, const char *at, size_t length) {
  (void)at; (void)length;
  fuzz_ctx_t *ctx = (fuzz_ctx_t *)parser->data;
  return (next_ctrl(ctx) % 6 == 0) ? -1 : 0;
}

#define MSG_CAP (65536u)
#define MAX_BODY (8192u)

static char g_msg[MSG_CAP];

static void msg_append(char *buf, size_t cap, size_t *pos,
                        const char *src, size_t len) {
  if (*pos >= cap) return;
  size_t avail = cap - *pos;
  size_t n = (len < avail) ? len : avail;
  if (n > 0) memcpy(buf + *pos, src, n);
  *pos += n;
}

static void msg_append_str(char *buf, size_t cap, size_t *pos,
                            const char *s) {
  msg_append(buf, cap, pos, s, strlen(s));
}

static void build_url(char *buf, size_t cap, size_t *pos,
                       uint8_t sel, int is_connect,
                       const uint8_t *filler, size_t filler_len) {
  if (is_connect) {

    msg_append_str(buf, cap, pos, "example.com:443");
    return;
  }
  switch (sel % 6) {
    case 0:
      msg_append_str(buf, cap, pos, "/");
      break;
    case 1:
      msg_append_str(buf, cap, pos, "/path/to/resource?x=1&y=2");
      break;
    case 2:
      msg_append_str(buf, cap, pos, "/a#frag");
      break;
    case 3:
      msg_append_str(buf, cap, pos,
                      "http://user:pass@host.example.com:8080/p?q#f");
      break;
    case 4:
      msg_append_str(buf, cap, pos, "http://[::1]:8080/");
      break;
    default: {

      size_t n = filler_len < 16 ? filler_len : 16;
      size_t i;
      msg_append_str(buf, cap, pos, "/");
      for (i = 0; i < n; i++) {
        uint8_t c = filler[i];
        if (c == '\r' || c == '\n') c = '_';
        msg_append(buf, cap, pos, (const char *)&c, 1);
      }
      break;
    }
  }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 12) return 0;

  llhttp_type_t type;
  switch (data[0] % 3) {
    case 0: type = HTTP_BOTH; break;
    case 1: type = HTTP_REQUEST; break;
    default: type = HTTP_RESPONSE; break;
  }
  uint8_t lenient_bits1 = data[1];
  uint8_t lenient_bits2 = data[2];
  uint8_t mode          = (uint8_t)(data[3] % 5);
  uint8_t header_mask   = data[4];
  uint8_t method_sel    = data[5];
  uint8_t misc_sel      = data[6];
  uint8_t perturb_sel   = (uint8_t)(data[7] % 4);
  uint8_t extra_sel     = data[8];

  size_t header_off = 9;
  size_t remaining = size - header_off;

  size_t ctrl_len = remaining / 8;
  if (ctrl_len == 0) ctrl_len = 1;
  if (ctrl_len >= remaining) ctrl_len = remaining > 1 ? remaining - 1 : 0;

  const uint8_t *ctrl = data + header_off;
  const uint8_t *filler = data + header_off + ctrl_len;
  size_t filler_len = remaining - ctrl_len;

  fuzz_ctx_t ctx = { ctrl, ctrl_len, 0 };

  llhttp_settings_t settings;
  llhttp_settings_init(&settings);

  settings.on_message_begin                  = cb_ret_generic;
  settings.on_message_complete               = cb_ret_generic;
  settings.on_protocol_complete               = cb_ret_generic;
  settings.on_url_complete                   = cb_ret_generic;
  settings.on_status_complete                = cb_ret_generic;
  settings.on_method_complete                = cb_ret_generic;
  settings.on_version_complete               = cb_ret_generic;
  settings.on_header_field_complete          = cb_ret_generic;
  settings.on_header_value_complete          = cb_ret_generic;
  settings.on_chunk_extension_name_complete  = cb_ret_generic;
  settings.on_chunk_extension_value_complete = cb_ret_generic;
  settings.on_chunk_header                   = cb_ret_generic;
  settings.on_chunk_complete                 = cb_ret_generic;
  settings.on_reset                          = cb_ret_generic;
  settings.on_headers_complete                = cb_headers_complete;

  settings.on_protocol             = cb_data_generic;
  settings.on_url                  = cb_data_generic;
  settings.on_status               = cb_data_generic;
  settings.on_method                = cb_data_generic;
  settings.on_version               = cb_data_generic;
  settings.on_header_field          = cb_data_generic;
  settings.on_header_value          = cb_data_generic;
  settings.on_body                  = cb_data_generic;
  settings.on_chunk_extension_name  = cb_data_generic;
  settings.on_chunk_extension_value = cb_data_generic;

  llhttp_t parser;
  llhttp_init(&parser, type, &settings);
  parser.data = &ctx;

  llhttp_set_lenient_headers(&parser,                    (lenient_bits1 >> 0) & 1);
  llhttp_set_lenient_chunked_length(&parser,              (lenient_bits1 >> 1) & 1);
  llhttp_set_lenient_keep_alive(&parser,                  (lenient_bits1 >> 2) & 1);
  llhttp_set_lenient_transfer_encoding(&parser,           (lenient_bits1 >> 3) & 1);
  llhttp_set_lenient_version(&parser,                     (lenient_bits1 >> 4) & 1);
  llhttp_set_lenient_data_after_close(&parser,            (lenient_bits1 >> 5) & 1);
  llhttp_set_lenient_optional_lf_after_cr(&parser,        (lenient_bits1 >> 6) & 1);
  llhttp_set_lenient_optional_cr_before_lf(&parser,       (lenient_bits1 >> 7) & 1);
  llhttp_set_lenient_optional_crlf_after_chunk(&parser,   (lenient_bits2 >> 0) & 1);
  llhttp_set_lenient_spaces_after_chunk_size(&parser,     (lenient_bits2 >> 1) & 1);
  llhttp_set_lenient_header_value_relaxed(&parser,        (lenient_bits2 >> 2) & 1);

  const char *p;
  const char *end;

  if (mode == 0) {

    p = (const char *)filler;
    end = p + filler_len;
  } else {
    size_t pos = 0;
    int is_response = (type == HTTP_RESPONSE);

    static const char *kMethods[] = {
      "GET", "POST", "PUT", "DELETE", "HEAD", "CONNECT", "OPTIONS", "TRACE",
      "PATCH", "PROPFIND", "MKCOL", "COPY", "MOVE", "LOCK", "UNLOCK",
      "ANNOUNCE", "DESCRIBE", "SETUP", "PLAY", "PAUSE", "TEARDOWN"
    };
    const size_t kMethodsCount = sizeof(kMethods) / sizeof(kMethods[0]);

    static const char *kVersions[] = { "HTTP/1.1", "HTTP/1.0", "HTTP/0.9" };

    static const struct { int code; const char *reason; } kStatuses[] = {
      {200, "OK"}, {201, "Created"}, {204, "No Content"},
      {301, "Moved Permanently"}, {400, "Bad Request"}, {404, "Not Found"},
      {500, "Internal Server Error"}, {101, "Switching Protocols"}
    };
    const size_t kStatusesCount = sizeof(kStatuses) / sizeof(kStatuses[0]);

    static const char *kOptionalHeaders[] = {
      "Host: example.com\r\n",
      "Connection: close\r\n",
      "Connection: keep-alive\r\n",
      "Accept: */*\r\n",
      "X-Custom-Header: value\r\n",
      "Cookie: a=b\r\n",
      "User-Agent: fuzz\r\n",
    };
    const size_t kOptionalHeadersCount = sizeof(kOptionalHeaders) / sizeof(kOptionalHeaders[0]);

    const char *version = kVersions[misc_sel % 3];

    int http09 = (!is_response && mode != 3 && (extra_sel & 0x03) == 0);
    int unknown_method = (!is_response && mode != 3 && (extra_sel & 0x0C) == 0x0C);
    int chunk_ext_mode = (extra_sel >> 4) & 0x3;
    int dup_header_mode = (extra_sel >> 6) & 0x3;

    if (is_response) {
      char linebuf[96];
      int n;
      if (mode == 3) {
        n = snprintf(linebuf, sizeof(linebuf), "%s 101 Switching Protocols\r\n", version);
      } else {
        size_t status_idx = misc_sel % kStatusesCount;
        n = snprintf(linebuf, sizeof(linebuf), "%s %d %s\r\n", version,
                     kStatuses[status_idx].code, kStatuses[status_idx].reason);
      }
      if (n > 0) msg_append(g_msg, MSG_CAP, &pos, linebuf, (size_t)n);
    } else {
      char custom_method[8];
      const char *method;
      int is_connect;

      if (mode == 3) {
        method = "CONNECT";
        is_connect = 1;
      } else if (unknown_method && filler_len >= 4) {
        size_t i;
        for (i = 0; i < 4; i++) {
          custom_method[i] = (char)('A' + (filler[i] % 26));
        }
        custom_method[4] = '\0';
        method = custom_method;
        is_connect = 0;
      } else {
        method = kMethods[method_sel % kMethodsCount];
        is_connect = (strcmp(method, "CONNECT") == 0);
      }

      msg_append_str(g_msg, MSG_CAP, &pos, method);
      msg_append_str(g_msg, MSG_CAP, &pos, " ");
      build_url(g_msg, MSG_CAP, &pos, misc_sel, is_connect, filler, filler_len);
      if (!http09) {
        msg_append_str(g_msg, MSG_CAP, &pos, " ");
        msg_append_str(g_msg, MSG_CAP, &pos, version);
      }
      msg_append_str(g_msg, MSG_CAP, &pos, "\r\n");
    }

    {
      size_t i;
      for (i = 0; i < kOptionalHeadersCount; i++) {
        if (header_mask & (1u << i)) {
          msg_append_str(g_msg, MSG_CAP, &pos, kOptionalHeaders[i]);
        }
      }

      if (header_mask & 0x80) {
        msg_append_str(g_msg, MSG_CAP, &pos, "X-Folded: part1\r\n  part2 continued\r\n");
      }
    }

    if (mode == 3) {

      msg_append_str(g_msg, MSG_CAP, &pos, "Connection: upgrade\r\n");
      msg_append_str(g_msg, MSG_CAP, &pos, "Upgrade: websocket\r\n");
    }

    size_t body_len = (filler_len < MAX_BODY) ? filler_len : MAX_BODY;

    if (mode == 2 || mode == 4) {
      msg_append_str(g_msg, MSG_CAP, &pos, "Transfer-Encoding: chunked\r\n");
    } else if (mode == 1) {
      size_t header_len_value;
      switch (perturb_sel) {
        case 0: header_len_value = body_len; break;
        case 1: header_len_value = body_len + 1; break;
        case 2: header_len_value = (body_len > 0) ? body_len - 1 : 0; break;
        default: header_len_value = body_len * 2 + 7; break;
      }
      char clbuf[64];
      int n = snprintf(clbuf, sizeof(clbuf), "Content-Length: %zu\r\n", header_len_value);
      if (n > 0) msg_append(g_msg, MSG_CAP, &pos, clbuf, (size_t)n);
    }

    if (dup_header_mode == 1) {
      msg_append_str(g_msg, MSG_CAP, &pos, "Content-Length: 5\r\n");
      msg_append_str(g_msg, MSG_CAP, &pos, "Content-Length: 7\r\n");
    } else if (dup_header_mode == 2 && mode == 1) {
      msg_append_str(g_msg, MSG_CAP, &pos, "Transfer-Encoding: chunked\r\n");
    } else if (dup_header_mode == 3) {
      msg_append_str(g_msg, MSG_CAP, &pos, "Transfer-Encoding: chunked\r\n");
      msg_append_str(g_msg, MSG_CAP, &pos, "Transfer-Encoding: identity\r\n");
    }

    msg_append_str(g_msg, MSG_CAP, &pos, "\r\n");

    if (mode == 2 || mode == 4) {
      size_t chunk1 = body_len / 2;
      size_t chunk2 = body_len - chunk1;
      if (chunk1 > 0) {
        char hb[32];
        int n = snprintf(hb, sizeof(hb), "%zx", chunk1);
        if (n > 0) msg_append(g_msg, MSG_CAP, &pos, hb, (size_t)n);

        if (chunk_ext_mode == 1) {
          msg_append_str(g_msg, MSG_CAP, &pos, ";ext=val");
        } else if (chunk_ext_mode == 2) {
          msg_append_str(g_msg, MSG_CAP, &pos, ";ext=\"quoted \\\" value\"");
        } else if (chunk_ext_mode == 3) {
          msg_append_str(g_msg, MSG_CAP, &pos, "; a=1;b=\"x\"");
        }
        msg_append_str(g_msg, MSG_CAP, &pos, "\r\n");
        msg_append(g_msg, MSG_CAP, &pos, (const char *)filler, chunk1);
        msg_append_str(g_msg, MSG_CAP, &pos, "\r\n");
      }
      if (chunk2 > 0) {
        char hb[32];
        int n = snprintf(hb, sizeof(hb), "%zx", chunk2);
        if (n > 0) msg_append(g_msg, MSG_CAP, &pos, hb, (size_t)n);
        if (chunk_ext_mode == 1) {
          msg_append_str(g_msg, MSG_CAP, &pos, ";ext2=val2");
        }
        msg_append_str(g_msg, MSG_CAP, &pos, "\r\n");
        msg_append(g_msg, MSG_CAP, &pos, (const char *)(filler + chunk1), chunk2);
        msg_append_str(g_msg, MSG_CAP, &pos, "\r\n");
      }
      msg_append_str(g_msg, MSG_CAP, &pos, "0");
      if (chunk_ext_mode == 3) {
        msg_append_str(g_msg, MSG_CAP, &pos, ";final=1");
      }
      msg_append_str(g_msg, MSG_CAP, &pos, "\r\n");
      if (mode == 4) {
        msg_append_str(g_msg, MSG_CAP, &pos, "X-Trailer: done\r\n");
      }
      msg_append_str(g_msg, MSG_CAP, &pos, "\r\n");
    } else if (mode == 1) {
      msg_append(g_msg, MSG_CAP, &pos, (const char *)filler, body_len);
    } else if (mode == 3) {

      msg_append(g_msg, MSG_CAP, &pos, (const char *)filler, body_len);
    }

    p = g_msg;
    end = g_msg + pos;
  }

  int iterations = 0;
  while (p < end && iterations++ < 10000) {
    llhttp_errno_t err = llhttp_execute(&parser, p, (size_t)(end - p));

    if (err == HPE_PAUSED) {
      const char *epos = llhttp_get_error_pos(&parser);
      llhttp_resume(&parser);
      if (epos == NULL || epos <= p) break;
      p = epos;
      continue;
    }
    if (err == HPE_PAUSED_UPGRADE) {
      const char *epos = llhttp_get_error_pos(&parser);
      llhttp_resume_after_upgrade(&parser);
      if (epos == NULL || epos <= p) break;
      p = epos;
      continue;
    }

    break;
  }

  llhttp_finish(&parser);

  return 0;
}