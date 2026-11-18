#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    yaml_parser_t parser;

    if (!yaml_parser_initialize(&parser)) {
        return 0;
    }

    yaml_parser_set_input_string(&parser, Data, Size);

    int done = 0;
    int iterations = 0;
    const int max_iterations = 64;

    while (!done && iterations < max_iterations) {
        yaml_document_t document;
        memset(&document, 0, sizeof(document));

        int result = yaml_parser_load(&parser, &document);

        if (!result) {

            yaml_document_delete(&document);
            done = 1;
            break;
        }

        yaml_node_t *root = yaml_document_get_root_node(&document);
        if (root == NULL) {

            yaml_document_delete(&document);
            done = 1;
            break;
        }

        int node_count = 0;
        yaml_node_t *node;
        while ((node = yaml_document_get_node(&document, node_count + 1)) != NULL) {
            node_count++;
            if (node_count > 1000) {
                break;
            }
        }

        yaml_document_delete(&document);
        iterations++;
    }

    FILE *fp = fopen("./dummy_file", "wb");
    if (fp != NULL) {
        fwrite(Data, 1, Size, fp);
        fclose(fp);

        FILE *rfp = fopen("./dummy_file", "rb");
        if (rfp != NULL) {
            yaml_parser_t file_parser;
            if (yaml_parser_initialize(&file_parser)) {
                yaml_parser_set_input_file(&file_parser, rfp);

                int file_done = 0;
                int file_iterations = 0;
                const int file_max_iterations = 64;

                while (!file_done && file_iterations < file_max_iterations) {
                    yaml_document_t file_document;
                    memset(&file_document, 0, sizeof(file_document));

                    int file_result = yaml_parser_load(&file_parser, &file_document);

                    if (!file_result) {
                        yaml_document_delete(&file_document);
                        file_done = 1;
                        break;
                    }

                    yaml_node_t *file_root = yaml_document_get_root_node(&file_document);
                    if (file_root == NULL) {
                        yaml_document_delete(&file_document);
                        file_done = 1;
                        break;
                    }

                    yaml_document_delete(&file_document);
                    file_iterations++;
                }

                yaml_parser_delete(&file_parser);
            }
            fclose(rfp);
        }
    }

    yaml_parser_delete(&parser);

    return 0;
}