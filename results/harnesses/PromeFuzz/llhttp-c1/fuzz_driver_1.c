#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "llhttp.h"

static int on_message_begin(llhttp_t* p) { return 0; }
static int on_url(llhttp_t* p, const char* at, size_t length) { return 0; }
static int on_status(llhttp_t* p, const char* at, size_t length) { return 0; }
static int on_header_field(llhttp_t* p, const char* at, size_t length) { return 0; }
static int on_header_value(llhttp_t* p, const char* at, size_t length) { return 0; }
static int on_headers_complete(llhttp_t* p) { return 0; }
static int on_body(llhttp_t* p, const char* at, size_t length) { return 0; }
static int on_message_complete(llhttp_t* p) { return 0; }
static int on_chunk_header(llhttp_t* p) { return 0; }
static int on_chunk_complete(llhttp_t* p) { return 0; }
static int on_url_complete(llhttp_t* p) { return 0; }
static int on_status_complete(llhttp_t* p) { return 0; }
static int on_header_field_complete(llhttp_t* p) { return 0; }
static int on_header_value_complete(llhttp_t* p) { return 0; }
static int on_method_complete(llhttp_t* p) { return 0; }
static int on_version_complete(llhttp_t* p) { return 0; }
static int on_chunk_extension_name_complete(llhttp_t* p) { return 0; }
static int on_chunk_extension_value_complete(llhttp_t* p) { return 0; }
static int on_reset(llhttp_t* p) { return 0; }
static int on_protocol_complete(llhttp_t* p) { return 0; }

static void setup_settings(llhttp_settings_t* settings) {
  llhttp_settings_init(settings);
  settings->on_message_begin = on_message_begin;
  settings->on_url = on_url;
  settings->on_status = on_status;
  settings->on_header_field = on_header_field;
  settings->on_header_value = on_header_value;
  settings->on_headers_complete = on_headers_complete;
  settings->on_body = on_body;
  settings->on_message_complete = on_message_complete;
  settings->on_chunk_header = on_chunk_header;
  settings->on_chunk_complete = on_chunk_complete;
  settings->on_url_complete = on_url_complete;
  settings->on_status_complete = on_status_complete;
  settings->on_header_field_complete = on_header_field_complete;
  settings->on_header_value_complete = on_header_value_complete;
#if defined(LLHTTP_API_VERSION_MAJOR)
  settings->on_method_complete = on_method_complete;
  settings->on_version_complete = on_version_complete;
  settings->on_chunk_extension_name_complete = on_chunk_extension_name_complete;
  settings->on_chunk_extension_value_complete = on_chunk_extension_value_complete;
  settings->on_reset = on_reset;
  settings->on_protocol_complete = on_protocol_complete;
#endif
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size < 1) return 0;

  llhttp_settings_t settings;
  setup_settings(&settings);

  llhttp_type_t types[3] = {HTTP_BOTH, HTTP_REQUEST, HTTP_RESPONSE};
  llhttp_type_t type = types[Data[0] % 3];

  const uint8_t *payload = Data + 1;
  size_t payload_len = Size - 1;

  llhttp_t parser;
  llhttp_init(&parser, type, &settings);

  size_t split = payload_len / 2;

  llhttp_errno_t err = llhttp_execute(&parser, (const char*)payload, split);

  if (err == HPE_PAUSED || err == HPE_PAUSED_UPGRADE || err == HPE_PAUSED_H2_UPGRADE) {
    llhttp_resume(&parser);
  }

  if (err == HPE_OK || err == HPE_PAUSED || err == HPE_PAUSED_UPGRADE || err == HPE_PAUSED_H2_UPGRADE) {
    err = llhttp_execute(&parser, (const char*)(payload + split), payload_len - split);
    if (err == HPE_PAUSED || err == HPE_PAUSED_UPGRADE || err == HPE_PAUSED_H2_UPGRADE) {
      llhttp_resume(&parser);
    }
  }

  llhttp_finish(&parser);

  llhttp_init(&parser, type, &settings);
  err = llhttp_execute(&parser, (const char*)payload, payload_len);
  if (err == HPE_PAUSED || err == HPE_PAUSED_UPGRADE || err == HPE_PAUSED_H2_UPGRADE) {
    llhttp_resume(&parser);
  }

  return 0;
}