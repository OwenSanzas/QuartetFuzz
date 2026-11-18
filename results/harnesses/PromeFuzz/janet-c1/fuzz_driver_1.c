#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <janet.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    janet_init();

    JanetParser parser;
    janet_parser_init(&parser);

    for (size_t i = 0; i < Size; i++) {
        janet_parser_consume(&parser, Data[i]);

        enum JanetParserStatus status = janet_parser_status(&parser);

        if (status == JANET_PARSE_ERROR) {

            const char *err = janet_parser_error(&parser);
            (void) err;

            janet_parser_deinit(&parser);
            janet_parser_init(&parser);
            continue;
        }

        while (janet_parser_has_more(&parser)) {
            Janet value = janet_parser_produce(&parser);
            (void) value;
        }
    }

    janet_parser_eof(&parser);

    enum JanetParserStatus final_status = janet_parser_status(&parser);
    if (final_status == JANET_PARSE_ERROR) {
        const char *err = janet_parser_error(&parser);
        (void) err;
    }

    while (janet_parser_has_more(&parser)) {
        Janet value = janet_parser_produce(&parser);
        (void) value;
    }

    janet_parser_deinit(&parser);
    janet_deinit();

    return 0;
}