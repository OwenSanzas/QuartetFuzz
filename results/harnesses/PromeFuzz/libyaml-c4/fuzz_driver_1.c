#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <yaml.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Data == NULL || Size == 0) {
        return 0;
    }

    yaml_parser_t parser;

    if (!yaml_parser_initialize(&parser)) {
        return 0;
    }

    yaml_parser_set_input_string(&parser, Data, Size);

    yaml_token_t token;
    int done = 0;
    int token_count = 0;
    int max_tokens = 10000;

    while (!done && token_count < max_tokens) {
        if (!yaml_parser_scan(&parser, &token)) {

            break;
        }

        if (token.type == YAML_STREAM_END_TOKEN) {
            done = 1;
        }

        yaml_token_delete(&token);
        token_count++;
    }

    yaml_parser_delete(&parser);

    const char *filename = "./dummy_file";
    FILE *fp = fopen(filename, "wb");
    if (fp != NULL) {
        fwrite(Data, 1, Size, fp);
        fclose(fp);

        fp = fopen(filename, "rb");
        if (fp != NULL) {
            yaml_parser_t file_parser;
            if (yaml_parser_initialize(&file_parser)) {
                yaml_parser_set_input_file(&file_parser, fp);

                yaml_token_t file_token;
                int file_done = 0;
                int file_token_count = 0;

                while (!file_done && file_token_count < max_tokens) {
                    if (!yaml_parser_scan(&file_parser, &file_token)) {
                        break;
                    }

                    if (file_token.type == YAML_STREAM_END_TOKEN) {
                        file_done = 1;
                    }

                    yaml_token_delete(&file_token);
                    file_token_count++;
                }

                yaml_parser_delete(&file_parser);
            }
            fclose(fp);
        }
    }

    if (Size > 4) {
        yaml_parser_t encoding_parser;
        if (yaml_parser_initialize(&encoding_parser)) {
            yaml_parser_set_input_string(&encoding_parser, Data, Size);

            yaml_encoding_t enc = YAML_UTF8_ENCODING;
            uint8_t selector = Data[0] % 4;
            switch (selector) {
                case 0:
                    enc = YAML_ANY_ENCODING;
                    break;
                case 1:
                    enc = YAML_UTF8_ENCODING;
                    break;
                case 2:
                    enc = YAML_UTF16LE_ENCODING;
                    break;
                case 3:
                    enc = YAML_UTF16BE_ENCODING;
                    break;
            }

            yaml_parser_set_encoding(&encoding_parser, enc);

            yaml_token_t enc_token;
            int enc_done = 0;
            int enc_token_count = 0;

            while (!enc_done && enc_token_count < max_tokens) {
                if (!yaml_parser_scan(&encoding_parser, &enc_token)) {
                    break;
                }

                if (enc_token.type == YAML_STREAM_END_TOKEN) {
                    enc_done = 1;
                }

                yaml_token_delete(&enc_token);
                enc_token_count++;
            }

            yaml_parser_delete(&encoding_parser);
        }
    }

    return 0;
}