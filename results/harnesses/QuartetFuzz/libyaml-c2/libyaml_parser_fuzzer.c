#include "yaml.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef NDEBUG
#undef NDEBUG
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2) {
    return 0;
  }

  uint8_t encoding_selector = data[0] % 4;
  uint8_t nest_selector = data[1];
  data += 2;
  size -= 2;

  int max_nest = 4 + (nest_selector % 64);
  yaml_set_max_nest_level(max_nest);

  yaml_parser_t parser;
  yaml_event_t event;
  bool done = false;

  volatile size_t content_accum = 0;

  if (!yaml_parser_initialize(&parser)) {
    return 0;
  }

  yaml_parser_set_input_string(&parser, data, size);

  static const yaml_encoding_t encodings[4] = {
      YAML_ANY_ENCODING, YAML_UTF8_ENCODING, YAML_UTF16LE_ENCODING,
      YAML_UTF16BE_ENCODING};
  yaml_parser_set_encoding(&parser, encodings[encoding_selector]);

  while (!done) {
    if (!yaml_parser_parse(&parser, &event)) {

      content_accum += (size_t)parser.problem_mark.line;
      content_accum += (size_t)parser.problem_mark.column;
      break;
    }

    switch (event.type) {
      case YAML_STREAM_END_EVENT:
        done = true;
        break;
      case YAML_SCALAR_EVENT:
        if (event.data.scalar.value && event.data.scalar.length > 0) {
          content_accum += event.data.scalar.value[0];
          content_accum += event.data.scalar.length;
        }
        if (event.data.scalar.anchor) {
          content_accum += strlen((const char *)event.data.scalar.anchor);
        }
        if (event.data.scalar.tag) {
          content_accum += strlen((const char *)event.data.scalar.tag);
        }
        break;
      case YAML_ALIAS_EVENT:
        if (event.data.alias.anchor) {
          content_accum += strlen((const char *)event.data.alias.anchor);
        }
        break;
      case YAML_SEQUENCE_START_EVENT:
        if (event.data.sequence_start.anchor) {
          content_accum +=
              strlen((const char *)event.data.sequence_start.anchor);
        }
        if (event.data.sequence_start.tag) {
          content_accum += strlen((const char *)event.data.sequence_start.tag);
        }
        break;
      case YAML_MAPPING_START_EVENT:
        if (event.data.mapping_start.anchor) {
          content_accum +=
              strlen((const char *)event.data.mapping_start.anchor);
        }
        if (event.data.mapping_start.tag) {
          content_accum += strlen((const char *)event.data.mapping_start.tag);
        }
        break;
      case YAML_DOCUMENT_START_EVENT:
        if (event.data.document_start.version_directive) {
          content_accum +=
              (size_t)event.data.document_start.version_directive->major;
        }
        for (size_t i = 0;
             event.data.document_start.tag_directives.start + i <
             event.data.document_start.tag_directives.end;
             i++) {
          yaml_tag_directive_t *td =
              event.data.document_start.tag_directives.start + i;
          if (td->handle) {
            content_accum += strlen((const char *)td->handle);
          }
          if (td->prefix) {
            content_accum += strlen((const char *)td->prefix);
          }
        }
        break;
      default:
        content_accum += (size_t)event.start_mark.index;
        break;
    }

    yaml_event_delete(&event);
  }

  yaml_parser_delete(&parser);

  (void)content_accum;
  return 0;
}
