#include <yaml.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * LibFuzzer harness for libyaml scanner.
 * It exercises the lexical analysis (scanning) of YAML input.
 */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    yaml_parser_t parser;
    yaml_token_t token;
    int done = 0;

    /* Initialize the parser object. */
    if (!yaml_parser_initialize(&parser)) {
        return 0;
    }

    /* Set the input string. */
    yaml_parser_set_input_string(&parser, data, size);

    /* Scan the input for tokens. */
    while (!done) {
        if (!yaml_parser_scan(&parser, &token)) {
            /* Scanning error, break the loop. */
            break;
        }

        /* Check if we reached the end of the stream. */
        done = (token.type == YAML_STREAM_END_TOKEN);

        /* Delete the token object. */
        yaml_token_delete(&token);
    }

    /* Destroy the parser object. */
    yaml_parser_delete(&parser);

    return 0;
}
