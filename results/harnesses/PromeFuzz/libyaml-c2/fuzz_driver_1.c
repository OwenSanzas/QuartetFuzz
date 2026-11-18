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

    {
        yaml_parser_t parser;
        yaml_event_t event;

        if (!yaml_parser_initialize(&parser)) {
            return 0;
        }

        yaml_parser_set_input_string(&parser, Data, Size);

        int done = 0;
        int iterations = 0;
        const int max_iterations = 10000;

        while (!done && iterations < max_iterations) {
            int status = yaml_parser_parse(&parser, &event);
            iterations++;

            if (!status) {

                break;
            }

            if (event.type == YAML_STREAM_END_EVENT) {
                yaml_event_delete(&event);
                done = 1;
                break;
            }

            yaml_event_delete(&event);
        }

        yaml_parser_delete(&parser);
    }

    {
        FILE *fp = fopen("./dummy_file", "wb");
        if (fp != NULL) {
            fwrite(Data, 1, Size, fp);
            fclose(fp);

            FILE *rf = fopen("./dummy_file", "rb");
            if (rf != NULL) {
                yaml_parser_t parser2;
                yaml_event_t event2;

                if (yaml_parser_initialize(&parser2)) {
                    yaml_parser_set_input_file(&parser2, rf);

                    int done2 = 0;
                    int iterations2 = 0;
                    const int max_iterations2 = 10000;

                    while (!done2 && iterations2 < max_iterations2) {
                        int status2 = yaml_parser_parse(&parser2, &event2);
                        iterations2++;

                        if (!status2) {
                            break;
                        }

                        if (event2.type == YAML_STREAM_END_EVENT) {
                            yaml_event_delete(&event2);
                            done2 = 1;
                            break;
                        }

                        yaml_event_delete(&event2);
                    }

                    yaml_parser_delete(&parser2);
                }

                fclose(rf);
            }
        }
    }

    return 0;
}