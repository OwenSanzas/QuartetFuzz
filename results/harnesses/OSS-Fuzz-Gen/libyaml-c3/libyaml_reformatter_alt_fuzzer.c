#include "yaml.h"
#include "yaml_write_handler.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#undef NDEBUG
#endif

#define MAX_SCALAR_LEN 200

typedef struct {
  const uint8_t *data;
  size_t size;
  size_t pos;
} cursor_t;

static uint8_t cursor_byte(cursor_t *c) {
  if (c->pos < c->size) return c->data[c->pos++];
  return 0;
}

static size_t cursor_remaining(const cursor_t *c) {
  return c->pos < c->size ? c->size - c->pos : 0;
}

static const uint8_t *cursor_ptr(const cursor_t *c) {
  return c->data + c->pos;
}

static void cursor_advance(cursor_t *c, size_t n) {
  c->pos += n;
  if (c->pos > c->size) c->pos = c->size;
}

static size_t build_scalar_text(const uint8_t *data, size_t avail,
                                 uint8_t *out, size_t out_cap) {
  static const char alphabet[] =
      "abcXYZ 09\n\t-:#,[]{}&*!|>'\"%@`?";
  size_t n = avail < out_cap ? avail : out_cap;
  size_t i;
  for (i = 0; i < n; i++) {
    out[i] = (uint8_t)alphabet[data[i] % (sizeof(alphabet) - 1)];
  }
  return n;
}

static size_t append_utf8(uint8_t *buf, uint32_t cp) {
  if (cp < 0x80) {
    buf[0] = (uint8_t)cp;
    return 1;
  } else if (cp < 0x800) {
    buf[0] = (uint8_t)(0xC0 | (cp >> 6));
    buf[1] = (uint8_t)(0x80 | (cp & 0x3F));
    return 2;
  } else if (cp < 0x10000) {
    buf[0] = (uint8_t)(0xE0 | (cp >> 12));
    buf[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = (uint8_t)(0x80 | (cp & 0x3F));
    return 3;
  } else {
    buf[0] = (uint8_t)(0xF0 | (cp >> 18));
    buf[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (uint8_t)(0x80 | (cp & 0x3F));
    return 4;
  }
}

static int build_synthetic_document(yaml_document_t *doc,
                                     const uint8_t *data, size_t size) {
  cursor_t cur;
  cur.data = data;
  cur.size = size;
  cur.pos = 0;

  uint8_t ctrl = cursor_byte(&cur);

  yaml_version_directive_t vd;
  vd.major = 1;
  vd.minor = (ctrl & 1) ? 1 : 2;
  yaml_version_directive_t *vdp = (ctrl & 2) ? &vd : NULL;

  int has_tagdir = (ctrl & 4) != 0;
  int custom_tagdir = (ctrl & 0x40) != 0;
  yaml_tag_directive_t tag_dirs[1];
  if (has_tagdir) {
    if (custom_tagdir) {
      tag_dirs[0].handle = (yaml_char_t *)"!e!";
      tag_dirs[0].prefix = (yaml_char_t *)"tag:example.com,2000:";
    } else {
      tag_dirs[0].handle = (yaml_char_t *)"!!";
      tag_dirs[0].prefix = (yaml_char_t *)"tag:yaml.org,2002:";
    }
  }

  int start_implicit = (ctrl & 8) != 0;
  int end_implicit = (ctrl & 16) != 0;

  if (!yaml_document_initialize(doc, vdp,
                                 has_tagdir ? tag_dirs : NULL,
                                 has_tagdir ? tag_dirs + 1 : NULL,
                                 start_implicit, end_implicit)) {
    return 0;
  }

  static const yaml_scalar_style_t scalar_styles[] = {
      YAML_PLAIN_SCALAR_STYLE, YAML_SINGLE_QUOTED_SCALAR_STYLE,
      YAML_DOUBLE_QUOTED_SCALAR_STYLE, YAML_LITERAL_SCALAR_STYLE,
      YAML_FOLDED_SCALAR_STYLE};
  static const yaml_sequence_style_t sequence_styles[] = {
      YAML_BLOCK_SEQUENCE_STYLE, YAML_FLOW_SEQUENCE_STYLE};
  static const yaml_mapping_style_t mapping_styles[] = {
      YAML_BLOCK_MAPPING_STYLE, YAML_FLOW_MAPPING_STYLE};

  uint8_t b1 = cursor_byte(&cur);
  uint8_t b2 = cursor_byte(&cur);
  uint8_t b3 = cursor_byte(&cur);
  uint8_t b4 = cursor_byte(&cur);

  static const yaml_char_t *custom_tags[] = {
      (yaml_char_t *)"!mytag",
      (yaml_char_t *)"!my tag:with/special*chars \x01",
      (yaml_char_t *)"tag:example.com,2000:custom",
      (yaml_char_t *)"x-custom-scheme:thing",
      (yaml_char_t *)"!e!widget",
      (yaml_char_t *)"",
  };
  const yaml_char_t *custom_tag = NULL;
  if (ctrl & 32) {
    if (custom_tagdir && (b4 & 2)) {

      custom_tag = custom_tags[4];
    } else {
      custom_tag = custom_tags[b4 % 6];
    }
  }

  uint8_t root_kind = b1 % 3;

  if (root_kind == 2) {

    uint8_t buf[MAX_SCALAR_LEN];
    size_t avail = cursor_remaining(&cur);
    size_t want = 1 + (b2 % MAX_SCALAR_LEN);
    size_t cap = want < avail ? want : avail;
    size_t n = build_scalar_text(cursor_ptr(&cur), avail, buf, cap);
    cursor_advance(&cur, n);
    if (n == 0) {
      buf[0] = 'x';
      n = 1;
    }
    int node = yaml_document_add_scalar(doc, custom_tag, buf, (int)n,
                                         scalar_styles[b3 % 5]);
    if (!node && custom_tag) {

      yaml_document_add_scalar(doc, NULL, buf, (int)n, scalar_styles[b3 % 5]);
    }
    return 1;
  }

  const yaml_char_t *tag_for_seq = (b1 & 1) ? custom_tag : NULL;
  const yaml_char_t *tag_for_map = (b1 & 2) ? custom_tag : NULL;

  int root = (root_kind == 1)
      ? yaml_document_add_sequence(doc, tag_for_seq,
                                    sequence_styles[b1 % 2])
      : yaml_document_add_mapping(doc, tag_for_map,
                                   mapping_styles[b1 % 2]);
  if (!root) {

    root = (root_kind == 1)
        ? yaml_document_add_sequence(doc, NULL, sequence_styles[b1 % 2])
        : yaml_document_add_mapping(doc, NULL, mapping_styles[b1 % 2]);
  }
  if (!root) return 1;

  uint8_t shared_buf[MAX_SCALAR_LEN + 8];
  size_t shared_len;
  {
    size_t avail = cursor_remaining(&cur);
    size_t want = 20 + (b2 % (MAX_SCALAR_LEN - 20 + 1));
    size_t cap = want < avail ? want : avail;
    shared_len = build_scalar_text(cursor_ptr(&cur), avail, shared_buf, cap);
    cursor_advance(&cur, shared_len);
    if ((b3 & 1) && shared_len + 4 <= sizeof(shared_buf)) {

      static const uint32_t codepoints[] = {0x00E9, 0x2028, 0x1F600, 0x0041};
      shared_len += append_utf8(shared_buf + shared_len,
                                 codepoints[b3 % 4]);
    }
  }
  if (shared_len == 0) {
    shared_buf[0] = 'x';
    shared_len = 1;
  }

  int shared = yaml_document_add_scalar(doc, custom_tag, shared_buf,
                                         (int)shared_len,
                                         scalar_styles[b3 % 5]);
  if (!shared) {
    shared = yaml_document_add_scalar(doc, NULL, shared_buf, (int)shared_len,
                                       scalar_styles[b3 % 5]);
  }

  uint8_t longkey_buf[200];
  size_t longkey_len = 0;
  if (ctrl & 64) {
    size_t avail = cursor_remaining(&cur);
    size_t want = 130 + (b4 % 60);
    size_t cap = want < avail ? want : avail;
    longkey_len = build_scalar_text(cursor_ptr(&cur), avail, longkey_buf, cap);
    cursor_advance(&cur, longkey_len);
  }

  int key = longkey_len
      ? yaml_document_add_scalar(doc, NULL, longkey_buf, (int)longkey_len,
                                  scalar_styles[b2 % 5])
      : yaml_document_add_scalar(doc, NULL, (yaml_char_t *)"k", 1,
                                  scalar_styles[b2 % 5]);

  int seq = yaml_document_add_sequence(doc, NULL, sequence_styles[b1 % 2]);

  if (shared && seq) {

    yaml_document_append_sequence_item(doc, seq, shared);
    yaml_document_append_sequence_item(doc, seq, shared);
  }

  int empty_seq = 0, empty_map = 0;
  if (ctrl & 128) {
    empty_seq = yaml_document_add_sequence(doc, NULL, sequence_styles[b2 % 2]);
  }
  if (b4 & 1) {
    empty_map = yaml_document_add_mapping(doc, NULL, mapping_styles[b2 % 2]);
  }

  int key_collection = 0;
  if (cursor_remaining(&cur) > 0) {
    uint8_t kc = cursor_byte(&cur);
    if (kc & 1) {
      key_collection = yaml_document_add_sequence(doc, NULL,
                                                    sequence_styles[kc % 2]);
    } else {
      key_collection = yaml_document_add_mapping(doc, NULL,
                                                   mapping_styles[kc % 2]);
    }
  }

  int extra_count = 1 + (b2 % 6);
  int i;
  for (i = 0; i < extra_count && seq && cursor_remaining(&cur) > 0; i++) {
    uint8_t kind_byte = cursor_byte(&cur);
    uint8_t kind = kind_byte % 4;
    if (kind == 0) {
      if (cursor_remaining(&cur) > 0) {
        uint8_t v = cursor_byte(&cur);
        int node = yaml_document_add_scalar(doc, NULL, &v, 1,
                                             scalar_styles[v % 5]);
        if (node) yaml_document_append_sequence_item(doc, seq, node);
      }
    } else if (kind == 1) {
      uint8_t buf[64];
      size_t avail = cursor_remaining(&cur);
      size_t want = 1 + (kind_byte % 63);
      size_t cap = want < avail ? want : avail;
      size_t n = build_scalar_text(cursor_ptr(&cur), avail, buf, cap);
      cursor_advance(&cur, n);
      if (n > 0) {
        int node = yaml_document_add_scalar(doc, NULL, buf, (int)n,
                                             scalar_styles[kind_byte % 5]);
        if (node) yaml_document_append_sequence_item(doc, seq, node);
      }
    } else if (kind == 2) {
      int sub = yaml_document_add_sequence(doc, NULL,
                                            sequence_styles[kind_byte % 2]);
      if (sub) {
        int sub_items = 1 + (kind_byte % 2);
        int j;
        for (j = 0; j < sub_items && cursor_remaining(&cur) > 0; j++) {
          uint8_t v = cursor_byte(&cur);
          int node = yaml_document_add_scalar(doc, NULL, &v, 1,
                                               scalar_styles[v % 5]);
          if (node) yaml_document_append_sequence_item(doc, sub, node);
        }
        yaml_document_append_sequence_item(doc, seq, sub);
      }
    } else {
      int sub = yaml_document_add_mapping(doc, NULL,
                                           mapping_styles[kind_byte % 2]);
      if (sub) {
        if (cursor_remaining(&cur) >= 2) {
          uint8_t kv = cursor_byte(&cur);
          uint8_t vv = cursor_byte(&cur);
          int subkey = yaml_document_add_scalar(doc, NULL, &kv, 1,
                                                 scalar_styles[kv % 5]);
          int subval = yaml_document_add_scalar(doc, NULL, &vv, 1,
                                                 scalar_styles[vv % 5]);
          if (subkey && subval) {
            yaml_document_append_mapping_pair(doc, sub, subkey, subval);
          }
        }
        yaml_document_append_sequence_item(doc, seq, sub);
      }
    }
  }

  if (root_kind == 1) {

    if (key) yaml_document_append_sequence_item(doc, root, key);
    if (seq) yaml_document_append_sequence_item(doc, root, seq);
    if (empty_seq) yaml_document_append_sequence_item(doc, root, empty_seq);
    if (empty_map) yaml_document_append_sequence_item(doc, root, empty_map);
    if (key_collection)
      yaml_document_append_sequence_item(doc, root, key_collection);
    return 1;
  }

  if (key && seq) {
    yaml_document_append_mapping_pair(doc, root, key, seq);
  }
  if (empty_seq) {
    int k2 = yaml_document_add_scalar(doc, NULL, (yaml_char_t *)"es", 2,
                                       scalar_styles[b3 % 5]);
    if (k2) yaml_document_append_mapping_pair(doc, root, k2, empty_seq);
  }
  if (empty_map) {
    int k3 = yaml_document_add_scalar(doc, NULL, (yaml_char_t *)"em", 2,
                                       scalar_styles[b4 % 5]);
    if (k3) yaml_document_append_mapping_pair(doc, root, k3, empty_map);
  }
  if (key_collection) {

    int kv = yaml_document_add_scalar(doc, NULL, (yaml_char_t *)"cv", 2,
                                       scalar_styles[(b1 + b3) % 5]);
    if (kv) yaml_document_append_mapping_pair(doc, root, key_collection, kv);
  }

  if (shared && (ctrl & 32)) {
    int k4 = yaml_document_add_scalar(doc, NULL, (yaml_char_t *)"al", 2,
                                       scalar_styles[(b1 + b2) % 5]);
    if (k4) yaml_document_append_mapping_pair(doc, root, k4, shared);
  }

  return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 7)
    return 0;

  uint8_t mode = data[0];
  int doc_count = 1 + ((mode >> 1) & 3);
  data += 1;
  size -= 1;

  if (size < 6)
    return 0;

  size_t cfg_off = size - 6;
  const uint8_t *cfg = data + cfg_off;

  bool is_canonical = cfg[0] & 1;
  bool is_unicode = cfg[1] & 1;

  static const yaml_encoding_t encodings[] = {
      YAML_ANY_ENCODING, YAML_UTF8_ENCODING,
      YAML_UTF16LE_ENCODING, YAML_UTF16BE_ENCODING};
  yaml_encoding_t encoding = encodings[cfg[2] % 4];

  int indent = 2 + (cfg[3] % 8);
  int width = (cfg[4] % 2) ? -1 : (cfg[4] * 4);

  static const yaml_break_t breaks[] = {
      YAML_ANY_BREAK, YAML_CR_BREAK, YAML_LN_BREAK, YAML_CRLN_BREAK};
  yaml_break_t line_break = breaks[cfg[5] % 4];

  size_t body_size = cfg_off;
  const uint8_t *body = data;

  yaml_emitter_t emitter;
  if (!yaml_emitter_initialize(&emitter))
    return 0;

  yaml_output_buffer_t out = { NULL,  0,  1000};
  yaml_emitter_set_output(&emitter, yaml_write_handler, &out);

  yaml_emitter_set_canonical(&emitter, is_canonical);
  yaml_emitter_set_unicode(&emitter, is_unicode);

  yaml_emitter_set_encoding(&emitter, encoding);
  yaml_emitter_set_indent(&emitter, indent);
  yaml_emitter_set_width(&emitter, width);
  yaml_emitter_set_break(&emitter, line_break);

  if (mode & 1) {

    int d;
    for (d = 0; d < doc_count && body_size > 0; d++) {

      size_t chunk = body_size / (size_t)(doc_count - d);
      if (chunk == 0) chunk = body_size;
      yaml_document_t document;
      if (build_synthetic_document(&document, body, chunk)) {
        if (!yaml_emitter_dump(&emitter, &document)) {
          break;
        }
      }
      body += chunk;
      body_size -= chunk;
    }
  } else {

    yaml_parser_t parser;

    if (!yaml_parser_initialize(&parser)) {
      free(out.buf);
      yaml_emitter_delete(&emitter);
      return 0;
    }

    yaml_parser_set_input_string(&parser, body, body_size);

    bool done = false;
    while (!done) {
      yaml_document_t document;

      if (!yaml_parser_load(&parser, &document)) {
        break;
      }

      if (!yaml_document_get_root_node(&document)) {
        done = true;
      }

      if (!yaml_emitter_dump(&emitter, &document)) {
        break;
      }
    }

    yaml_parser_delete(&parser);
  }

  free(out.buf);
  yaml_emitter_delete(&emitter);

  return 0;
}