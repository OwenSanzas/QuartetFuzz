#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <yaml.h>

static char *
make_cstring(const uint8_t *data, size_t size)
{
    char *buf = (char *)malloc(size + 1);
    if (!buf) return NULL;
    if (size > 0) memcpy(buf, data, size);
    buf[size] = '\0';
    return buf;
}

static void
fuzz_manual_document(const uint8_t *data, size_t size)
{
    if (size == 0) return;

    uint8_t choice = data[0];
    const uint8_t *rest = data + 1;
    size_t rest_size = size - 1;

    char *scalar_text = make_cstring(rest, rest_size);
    if (!scalar_text) return;

    yaml_document_t document;
    memset(&document, 0, sizeof(document));

    if (!yaml_document_initialize(&document, NULL, NULL, NULL, 0, 0)) {
        free(scalar_text);
        return;
    }

    int root_id = 0;

    switch (choice % 3) {
        case 0: {

            root_id = yaml_document_add_scalar(&document,
                    NULL, (yaml_char_t *)scalar_text,
                    (int)strlen(scalar_text), YAML_PLAIN_SCALAR_STYLE);
            break;
        }
        case 1: {

            int seq_id = yaml_document_add_sequence(&document, NULL,
                    YAML_BLOCK_SEQUENCE_STYLE);
            if (seq_id) {
                int item1 = yaml_document_add_scalar(&document, NULL,
                        (yaml_char_t *)scalar_text,
                        (int)strlen(scalar_text), YAML_PLAIN_SCALAR_STYLE);
                if (item1) {
                    yaml_document_append_sequence_item(&document, seq_id, item1);
                }
                int item2 = yaml_document_add_scalar(&document, NULL,
                        (yaml_char_t *)"fuzz", 4, YAML_PLAIN_SCALAR_STYLE);
                if (item2) {
                    yaml_document_append_sequence_item(&document, seq_id, item2);
                }
            }
            root_id = seq_id;
            break;
        }
        case 2:
        default: {

            int map_id = yaml_document_add_mapping(&document, NULL,
                    YAML_BLOCK_MAPPING_STYLE);
            if (map_id) {
                int key_id = yaml_document_add_scalar(&document, NULL,
                        (yaml_char_t *)"key", 3, YAML_PLAIN_SCALAR_STYLE);
                int val_id = yaml_document_add_scalar(&document, NULL,
                        (yaml_char_t *)scalar_text,
                        (int)strlen(scalar_text), YAML_PLAIN_SCALAR_STYLE);
                if (key_id && val_id) {
                    yaml_document_append_mapping_pair(&document, map_id,
                            key_id, val_id);
                }
            }
            root_id = map_id;
            break;
        }
    }

    (void)root_id;

    yaml_emitter_t emitter;
    memset(&emitter, 0, sizeof(emitter));

    if (!yaml_emitter_initialize(&emitter)) {
        yaml_document_delete(&document);
        free(scalar_text);
        return;
    }

    FILE *out = fopen("./dummy_file", "wb");
    if (!out) {
        yaml_emitter_delete(&emitter);
        yaml_document_delete(&document);
        free(scalar_text);
        return;
    }

    yaml_emitter_set_output_file(&emitter, out);

    yaml_emitter_dump(&emitter, &document);

    fclose(out);
    yaml_emitter_delete(&emitter);
    free(scalar_text);
}

static void
fuzz_parsed_document(const uint8_t *data, size_t size)
{
    yaml_parser_t parser;
    yaml_document_t document;

    memset(&parser, 0, sizeof(parser));
    memset(&document, 0, sizeof(document));

    if (!yaml_parser_initialize(&parser)) {
        return;
    }

    yaml_parser_set_input_string(&parser, data, size);

    int loaded = yaml_parser_load(&parser, &document);
    yaml_parser_delete(&parser);

    if (!loaded) {

        yaml_document_delete(&document);
        return;
    }

    yaml_emitter_t emitter;
    memset(&emitter, 0, sizeof(emitter));

    if (!yaml_emitter_initialize(&emitter)) {
        yaml_document_delete(&document);
        return;
    }

    unsigned char output_buffer[65536];
    size_t written = 0;

    yaml_emitter_set_output_string(&emitter, output_buffer,
            sizeof(output_buffer), &written);

    yaml_emitter_dump(&emitter, &document);

    yaml_emitter_delete(&emitter);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size == 0) {
        return 0;
    }

    fuzz_parsed_document(Data, Size);

    fuzz_manual_document(Data, Size);

    return 0;
}