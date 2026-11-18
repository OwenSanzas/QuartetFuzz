#include <fuzzer/FuzzedDataProvider.h>
#include <string>
#include <stdint.h>

#include "jq.h"
#include "jv.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fdp(data, size);

  std::string prog_payload = fdp.ConsumeRandomLengthString();
  std::string args_key = fdp.ConsumeRandomLengthString();
  std::string args_val_payload = fdp.ConsumeRandomLengthString();
  std::string input_payload = fdp.ConsumeRandomLengthString();
  uint8_t use_args_obj = fdp.ConsumeIntegral<uint8_t>();
  uint8_t flags_byte = fdp.ConsumeIntegral<uint8_t>();
  int flags = flags_byte & 0x3;

  jq_state *jq = jq_init();
  if (jq == NULL)
    return 0;

  jq_set_attr(jq, jv_string("JQ_ORIGIN"), jv_string("/tmp/"));

  jv args_value = jv_parse(args_val_payload.c_str());
  if (!jv_is_valid(args_value)) {
    jv_free(args_value);
    args_value = jv_null();
  }
  jv args;
  if (use_args_obj & 1) {
    args = jv_object_set(jv_object(), jv_string(args_key.c_str()), args_value);
  } else {
    jv entry = jv_object_set(jv_object(), jv_string("name"), jv_string(args_key.c_str()));
    entry = jv_object_set(entry, jv_string("value"), args_value);
    args = jv_array_append(jv_array(), entry);
  }

  int compiled = jq_compile_args(jq, prog_payload.c_str(), args);

  if (compiled) {
    jv input = jv_parse(input_payload.c_str());
    if (!jv_is_valid(input)) {

      jv_free(input);
      input = jv_null();
    }

    jq_start(jq, input, flags);

    jv result = jv_invalid();
    int count = 0;
    const int kMaxResults = 1000;
    while (count < kMaxResults) {
      result = jq_next(jq);
      if (!jv_is_valid(result))
        break;

      if (jv_get_kind(result) == JV_KIND_STRING) {
        size_t len = (size_t)jv_string_length_bytes(jv_copy(result));
        const char *s = jv_string_value(result);
        volatile unsigned long checksum = 0;
        for (size_t i = 0; i < len; i++)
          checksum += (unsigned char)s[i];
        (void)checksum;
        jv_free(result);
      } else {
        jv dumped = jv_dump_string(jv_copy(result), 0);
        jv_free(dumped);
        jv_free(result);
      }
      count++;
    }

    if (count < kMaxResults) {

      if (jv_invalid_has_msg(jv_copy(result))) {
        jv msg = jv_invalid_get_msg(result);
        jv_free(msg);
      } else {
        jv_free(result);
      }
    }

    if (jq_halted(jq)) {
      jv_free(jq_get_exit_code(jq));
      jv_free(jq_get_error_message(jq));
    }
  }

  jq_teardown(&jq);
  return 0;
}
