#include "yaml.h"
#include "yaml_write_handler.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WALK_DEPTH 256
#define MAX_DOCUMENTS 64

static void walk_node(yaml_document_t *document, int index, int depth) {
    if (depth > MAX_WALK_DEPTH)
        return;

    yaml_node_t *node = yaml_document_get_node(document, index);
    if (!node)
        return;

    switch (node->type) {
    case YAML_SCALAR_NODE:

        if (node->data.scalar.value && node->data.scalar.length > 0) {
            volatile unsigned char sink = node->data.scalar.value[0];
            sink ^= node->data.scalar.value[node->data.scalar.length - 1];
            (void)sink;
        }
        break;
    case YAML_SEQUENCE_NODE: {
        yaml_node_item_t *item;
        for (item = node->data.sequence.items.start;
             item < node->data.sequence.items.top; item++) {
            walk_node(document, *item, depth + 1);
        }
        break;
    }
    case YAML_MAPPING_NODE: {
        yaml_node_pair_t *pair;
        for (pair = node->data.mapping.pairs.start;
             pair < node->data.mapping.pairs.top; pair++) {
            walk_node(document, pair->key, depth + 1);
            walk_node(document, pair->value, depth + 1);
        }
        break;
    }
    default:
        break;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2)
        return 0;

    uint8_t control = data[0];
    data += 1;
    size -= 1;

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser))
        return 0;

    if (control & 0x4) {
        yaml_encoding_t enc;
        switch (control & 0x3) {
        case 0: enc = YAML_UTF8_ENCODING; break;
        case 1: enc = YAML_UTF16LE_ENCODING; break;
        case 2: enc = YAML_UTF16BE_ENCODING; break;
        default: enc = YAML_ANY_ENCODING; break;
        }
        yaml_parser_set_encoding(&parser, enc);
    }

    yaml_parser_set_input_string(&parser, data, size);

    yaml_emitter_t emitter;
    yaml_output_buffer_t out = {NULL, 0, 1000};
    int emitter_ok = yaml_emitter_initialize(&emitter);
    if (emitter_ok) {
        yaml_emitter_set_canonical(&emitter, control & 0x8);
        yaml_emitter_set_unicode(&emitter, control & 0x10);
        yaml_emitter_set_output(&emitter, yaml_write_handler, &out);

        if (!yaml_emitter_open(&emitter)) {
            yaml_emitter_delete(&emitter);
            emitter_ok = 0;
        }
    }

    int document_count = 0;
    while (document_count < MAX_DOCUMENTS) {
        yaml_document_t document;

        if (!yaml_parser_load(&parser, &document)) {

            break;
        }

        yaml_node_t *root = yaml_document_get_root_node(&document);
        if (!root) {

            yaml_document_delete(&document);
            break;
        }

        walk_node(&document, 1, 0);

        if (emitter_ok) {
            if (!yaml_emitter_dump(&emitter, &document)) {

                break;
            }
        } else {
            yaml_document_delete(&document);
        }

        document_count++;
    }

    if (emitter_ok) {
        yaml_emitter_close(&emitter);
        yaml_emitter_delete(&emitter);
    }
    free(out.buf);

    yaml_parser_delete(&parser);

    return 0;
}
