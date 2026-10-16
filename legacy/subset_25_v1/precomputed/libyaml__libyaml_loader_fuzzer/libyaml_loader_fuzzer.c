#include <yaml.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Copyright (c) 2017-2020 Ingy döt Net
 * Copyright (c) 2006-2016 Kirill Simonov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    yaml_parser_t parser;
    yaml_document_t document;
    int done = 0;

    if (!yaml_parser_initialize(&parser)) {
        return 0;
    }

    /* Set a reasonable nesting limit to avoid stack overflow or timeouts */
    yaml_set_max_nest_level(64);

    yaml_parser_set_input_string(&parser, data, size);

    while (!done) {
        if (!yaml_parser_load(&parser, &document)) {
            break;
        }

        done = (!yaml_document_get_root_node(&document));

        yaml_document_delete(&document);
    }

    yaml_parser_delete(&parser);

    return 0;
}
