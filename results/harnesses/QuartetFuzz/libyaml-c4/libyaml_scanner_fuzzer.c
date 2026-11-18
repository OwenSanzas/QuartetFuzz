#include "yaml.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static volatile unsigned long g_sink;

static void consume_bytes(const unsigned char *p, size_t len) {
    unsigned long h = g_sink;
    size_t i;
    for (i = 0; i < len; i++) {
        h = h * 131u + p[i];
    }
    g_sink = h;
}

static void use_token(yaml_token_t *token) {
    switch (token->type) {
        case YAML_SCALAR_TOKEN:
            consume_bytes(token->data.scalar.value, token->data.scalar.length);
            g_sink += (unsigned long)token->data.scalar.style;
            break;
        case YAML_ANCHOR_TOKEN:
            consume_bytes(token->data.anchor.value,
                          token->data.anchor.value ? strlen((const char *)token->data.anchor.value) : 0);
            break;
        case YAML_ALIAS_TOKEN:
            consume_bytes(token->data.alias.value,
                          token->data.alias.value ? strlen((const char *)token->data.alias.value) : 0);
            break;
        case YAML_TAG_TOKEN:
            consume_bytes(token->data.tag.handle,
                          token->data.tag.handle ? strlen((const char *)token->data.tag.handle) : 0);
            consume_bytes(token->data.tag.suffix,
                          token->data.tag.suffix ? strlen((const char *)token->data.tag.suffix) : 0);
            break;
        case YAML_TAG_DIRECTIVE_TOKEN:
            consume_bytes(token->data.tag_directive.handle,
                          token->data.tag_directive.handle ? strlen((const char *)token->data.tag_directive.handle) : 0);
            consume_bytes(token->data.tag_directive.prefix,
                          token->data.tag_directive.prefix ? strlen((const char *)token->data.tag_directive.prefix) : 0);
            break;
        case YAML_VERSION_DIRECTIVE_TOKEN:
            g_sink += (unsigned long)token->data.version_directive.major;
            g_sink += (unsigned long)token->data.version_directive.minor;
            break;
        default:
            g_sink += (unsigned long)token->type;
            break;
    }

    g_sink += (unsigned long)token->start_mark.line + (unsigned long)token->end_mark.column;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) {
        return 0;
    }

    unsigned char mode = data[0];
    const unsigned char *doc = data + 1;
    size_t doc_size = size - 1;

    yaml_parser_t parser;
    yaml_token_t token;
    int done = 0;
    long iterations = 0;
    const long max_iterations = 200000;

    memset(&parser, 0, sizeof(parser));

    if (!yaml_parser_initialize(&parser)) {
        return 0;
    }

    switch (mode & 0x03) {
        case 1:
            yaml_parser_set_encoding(&parser, YAML_UTF8_ENCODING);
            break;
        case 2:
            yaml_parser_set_encoding(&parser, YAML_UTF16LE_ENCODING);
            break;
        case 3:
            yaml_parser_set_encoding(&parser, YAML_UTF16BE_ENCODING);
            break;
        default:

            break;
    }

    yaml_parser_set_input_string(&parser, doc, doc_size);

    while (!done && iterations < max_iterations) {
        iterations++;
        if (!yaml_parser_scan(&parser, &token)) {

            g_sink += (unsigned long)parser.error;
            if (parser.problem) {
                consume_bytes((const unsigned char *)parser.problem, strlen(parser.problem));
            }
            if (parser.context) {
                consume_bytes((const unsigned char *)parser.context, strlen(parser.context));
            }
            break;
        }

        use_token(&token);

        done = (token.type == YAML_STREAM_END_TOKEN);

        yaml_token_delete(&token);
    }

    yaml_parser_delete(&parser);

    return 0;
}
