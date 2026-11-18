#include <stdint.h>
#include <janet.h>

int __lsan_is_turned_off(void) {
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

    janet_init();

    JanetParser parser;
    janet_parser_init(&parser);

    JanetTryState tstate;
    if (janet_try(&tstate) == JANET_SIGNAL_OK) {
        for (size_t i = 0; i < size; i++) {
            enum JanetParserStatus status = janet_parser_status(&parser);

            if (status == JANET_PARSE_DEAD) {

                break;
            }

            if (status == JANET_PARSE_ERROR) {

                (void) janet_parser_error(&parser);
            }

            janet_parser_consume(&parser, data[i]);

            while (janet_parser_has_more(&parser)) {
                Janet form = janet_parser_produce(&parser);
                (void) form;
            }
        }

        if (janet_parser_status(&parser) != JANET_PARSE_DEAD) {
            janet_parser_eof(&parser);
            while (janet_parser_has_more(&parser)) {
                Janet form = janet_parser_produce(&parser);
                (void) form;
            }
        }
    }
    janet_restore(&tstate);

    janet_parser_deinit(&parser);

    janet_deinit();

    return 0;
}