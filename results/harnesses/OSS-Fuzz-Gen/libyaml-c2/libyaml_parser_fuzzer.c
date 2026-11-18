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

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    int simulate_failure;
    int failure_triggered;
} fuzz_input_t;

static int fuzz_read_handler(void *data, unsigned char *buffer, size_t size,
                              size_t *size_read) {
    fuzz_input_t *input = (fuzz_input_t *)data;

    if (input->simulate_failure && !input->failure_triggered) {
        input->failure_triggered = 1;
        *size_read = 0;
        return 0;
    }

    size_t remaining = input->size - input->pos;
    if (size > remaining) size = remaining;

    memcpy(buffer, input->data + input->pos, size);
    input->pos += size;
    *size_read = size;
    return 1;
}

static const char *const kSkeletons[] = {
    NULL,
    "%YAML 1.1\n%YAML 1.2\n---\n",
    "%YAML 2.0\n---\n",
    "%TAG !m! tag:example.com,2024:\n%TAG !m! tag:example.com,2024:\n---\n",
    "--- !m!foo\n",
    "? [a, b]\n: value\n? key\n: value\n",
    "&anchor [*anchor, *anchor]\n",
    "\"\\x41 \\u00e9 \\U0001F600 \\uD800 \\N \\_ \\L \\P\"\n",
    "k: |2-\n  text\n  more\nj: >1+\n note\n",
    "--- a\n...\n--- b\n...\n--- c\n",
    "--- !<tag:yaml.org,2002:str> foo\n",
    "foo:\n\t- bar\n",
};
#define NUM_SKELETONS (sizeof(kSkeletons) / sizeof(kSkeletons[0]))

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2)
    return 0;

  yaml_parser_t parser;
  yaml_event_t event;
  bool done = false;

  if (!yaml_parser_initialize(&parser))
    return 0;

  uint8_t sel = data[0] % 8;
  uint8_t mode;
  if (sel < 5) {
    mode = 0;
  } else if (sel == 5) {
    mode = 1;
  } else if (sel == 6) {
    mode = 2;
  } else {
    mode = 3;
  }

  int nest_level;
  if (data[0] & 0x80) {
    nest_level = 1000;
  } else {
    nest_level = 2 + ((data[0] >> 3) % 30);
  }
  yaml_set_max_nest_level(nest_level);

  size_t skeleton_index = 0;
  size_t header_consumed = 1;
  if (size >= 3) {
    skeleton_index = data[1] % NUM_SKELETONS;
    header_consumed = 2;
  }

  const uint8_t *payload = data + header_consumed;
  size_t payload_size = size - header_consumed;

  int use_skeleton = (skeleton_index != 0 && kSkeletons[skeleton_index] != NULL);

  uint8_t *combined = NULL;
  const uint8_t *content;
  size_t content_size;

  if (use_skeleton) {
    size_t skel_len = strlen(kSkeletons[skeleton_index]);
    combined = (uint8_t *)malloc(skel_len + payload_size);
    if (!combined) {
      yaml_parser_delete(&parser);
      return 0;
    }
    memcpy(combined, kSkeletons[skeleton_index], skel_len);
    if (payload_size > 0) {
      memcpy(combined + skel_len, payload, payload_size);
    }
    content = combined;
    content_size = skel_len + payload_size;
  } else {
    content = payload;
    content_size = payload_size;
  }

  fuzz_input_t input;
  input.data = content;
  input.size = content_size;
  input.pos = 0;
  input.failure_triggered = 0;

  input.simulate_failure = (!use_skeleton && mode == 3);

  yaml_parser_set_input(&parser, fuzz_read_handler, &input);

  if (!use_skeleton) {
    switch (mode) {
      case 0:

        break;
      case 1:
        yaml_parser_set_encoding(&parser, YAML_UTF16LE_ENCODING);
        break;
      case 2:
        yaml_parser_set_encoding(&parser, YAML_UTF16BE_ENCODING);
        break;
      default:
        yaml_parser_set_encoding(&parser, YAML_UTF8_ENCODING);
        break;
    }
  }

  while (!done) {
    if (!yaml_parser_parse(&parser, &event)) {

      yaml_event_delete(&event);
      break;
    }

    done = (event.type == YAML_STREAM_END_EVENT);

    yaml_event_delete(&event);
  }

  yaml_event_t extra_event;
  int rc = yaml_parser_parse(&parser, &extra_event);
  (void)rc;
  yaml_event_delete(&extra_event);

  yaml_parser_delete(&parser);

  if (combined) {
    free(combined);
  }

  return 0;
}