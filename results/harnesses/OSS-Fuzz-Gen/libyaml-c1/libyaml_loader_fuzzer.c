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

static const char *kTemplates[6] = {
    NULL,
    "%YAML 1.1\n",
    "%YAML 2.0\n",
    "%YAML 1.1\n%YAML 1.1\n",
    "%TAG ! tag:example.com,2000:\n",
    "%TAG ! tag:example.com,2000:\n%TAG ! tag:example.com,2000:\n",
};

static const char kBomUtf16LE[2] = { '\xff', '\xfe' };
static const char kBomUtf16BE[2] = { '\xfe', '\xff' };
static const char kBomUtf8[3]    = { '\xef', '\xbb', '\xbf' };

static const char *kTagHandleTemplates[2] = {
    "!y!foo\n",
    "%TAG !x! tag:example.com,2000:\n!y!foo\n",
};

static const char *kDocMarkerTemplates[2] = {
    "---\n",
    "---\n...\n---\n",
};

static const char kEscapeTemplate[] =
    "\"\\n\\t\\x41\\u0041\\U00000041\\\\\\\"\\0\\q\"\n";

static const char kAnchorAliasTemplate[] =
    "- &a foo\n"
    "- *a\n"
    "- &a bar\n";

static const char kComplexMappingTemplate[] =
    "? complex key\n"
    ": complex value\n"
    "nested: {a: [1, 2, {b: c}]}\n";

static const char kVerbatimTagTemplate[] =
    "!<tag:example.com,2000:%C3%A9> value\n";

static const char kBlockScalarTemplate[] =
    "lit: |2-\n"
    "  line one\n"
    "  line two\n"
    "fold: >+\n"
    "  folded line\n";

static const char kCollectionAnchorTemplate[] =
    "- &seq1 [1, 2, 3]\n"
    "- *seq1\n"
    "map: &map1 {x: 1}\n"
    "mapref: *map1\n";

static const char kUndefinedAliasTemplate[] = "*undefined\n";

static const unsigned char kBadUtf8[] = {
    0xC0, 0x80,
    0xED, 0xA0, 0x80,
    0xF4, 0x90, 0x80, 0x80,
    0xC2,
    '\n'
};

static const char kExplicitCollectionTagTemplate[] =
    "seq: !!seq &s [1, 2]\n"
    "map: !!map &m {a: 1}\n";

typedef struct {
  const uint8_t *data;
  size_t size;
  size_t pos;
  int fail_after_first;
} fail_ctx_t;

static int failing_read_handler(void *data, unsigned char *buffer,
                                 size_t size, size_t *size_read) {
  fail_ctx_t *ctx = (fail_ctx_t *)data;
  if (ctx->fail_after_first && ctx->pos > 0) {

    return 0;
  }
  size_t remaining = ctx->size - ctx->pos;
  size_t n = remaining < size ? remaining : size;
  if (n > 0) {
    memcpy(buffer, ctx->data + ctx->pos, n);
  }
  ctx->pos += n;
  *size_read = n;
  ctx->fail_after_first = 1;
  return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1) return 0;

  uint8_t mode = data[0] % 25;
  const uint8_t *body = data + 1;
  size_t body_size = size - 1;

  yaml_parser_t parser;
  yaml_document_t document;
  bool done = false;
  uint8_t *combined = NULL;
  fail_ctx_t fail_ctx;

  if (!yaml_parser_initialize(&parser))
    return 0;

  if (mode <= 5 && kTemplates[mode] != NULL) {

    size_t prefix_len = strlen(kTemplates[mode]);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, kTemplates[mode], prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else if (mode == 6) {

    yaml_parser_set_encoding(&parser, YAML_UTF16LE_ENCODING);
    yaml_parser_set_input_string(&parser, body, body_size);
  } else if (mode == 7) {

    yaml_parser_set_encoding(&parser, YAML_UTF16BE_ENCODING);
    yaml_parser_set_input_string(&parser, body, body_size);
  } else if (mode == 8 || mode == 9 || mode == 10) {

    const char *bom;
    size_t bom_len;
    if (mode == 8) {
      bom = kBomUtf16LE;
      bom_len = sizeof(kBomUtf16LE);
    } else if (mode == 9) {
      bom = kBomUtf16BE;
      bom_len = sizeof(kBomUtf16BE);
    } else {
      bom = kBomUtf8;
      bom_len = sizeof(kBomUtf8);
    }
    combined = (uint8_t *)malloc(bom_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, bom, bom_len);
    if (body_size > 0) {
      memcpy(combined + bom_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, bom_len + body_size);
  } else if (mode == 11 || mode == 12) {

    const char *tmpl = kTagHandleTemplates[mode - 11];
    size_t prefix_len = strlen(tmpl);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, tmpl, prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else if (mode == 13 || mode == 14) {

    const char *tmpl = kDocMarkerTemplates[mode - 13];
    size_t prefix_len = strlen(tmpl);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, tmpl, prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else if (mode == 15) {

    size_t prefix_len = strlen(kEscapeTemplate);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, kEscapeTemplate, prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else if (mode == 16) {

    size_t prefix_len = strlen(kAnchorAliasTemplate);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, kAnchorAliasTemplate, prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else if (mode == 17) {

    size_t prefix_len = strlen(kComplexMappingTemplate);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, kComplexMappingTemplate, prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else if (mode == 18) {

    size_t prefix_len = strlen(kVerbatimTagTemplate);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, kVerbatimTagTemplate, prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else if (mode == 19) {

    size_t prefix_len = strlen(kBlockScalarTemplate);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, kBlockScalarTemplate, prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else if (mode == 20) {

    size_t prefix_len = strlen(kCollectionAnchorTemplate);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, kCollectionAnchorTemplate, prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else if (mode == 21) {

    size_t prefix_len = strlen(kUndefinedAliasTemplate);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, kUndefinedAliasTemplate, prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else if (mode == 22) {

    fail_ctx.data = body;
    fail_ctx.size = body_size;
    fail_ctx.pos = 0;
    fail_ctx.fail_after_first = 0;
    yaml_parser_set_input(&parser, failing_read_handler, &fail_ctx);
  } else if (mode == 23) {

    size_t prefix_len = sizeof(kBadUtf8);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, kBadUtf8, prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else if (mode == 24) {

    size_t prefix_len = strlen(kExplicitCollectionTagTemplate);
    combined = (uint8_t *)malloc(prefix_len + body_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, kExplicitCollectionTagTemplate, prefix_len);
    if (body_size > 0) {
      memcpy(combined + prefix_len, body, body_size);
    }
    yaml_parser_set_input_string(&parser, combined, prefix_len + body_size);
  } else {

    yaml_parser_set_input_string(&parser, body, body_size);
  }

  while (!done) {
    if (!yaml_parser_load(&parser, &document)) {

      break;
    }

    done = (yaml_document_get_root_node(&document) == NULL);

    yaml_document_delete(&document);
  }

  yaml_parser_delete(&parser);
  free(combined);

  return 0;
}