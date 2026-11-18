#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <yara.h>

static const char* RULES_SRC =
    "import \"pe\"\n"
    "rule pe_sections_rule : pe_tag sections_tag {\n"
    "  meta:\n"
    "    author = \"fuzzer\"\n"
    "    revision = 1\n"
    "  condition:\n"
    "    pe.is_pe and pe.number_of_sections > 0 and\n"
    "    pe.sections[0].raw_data_size >= 0 and\n"
    "    pe.sections[0].virtual_size >= 0\n"
    "}\n";

static YR_RULES* rules = NULL;

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
  YR_COMPILER* compiler;

  if (yr_initialize() != ERROR_SUCCESS)
    return 0;

  if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
    return 0;

  if (yr_compiler_add_string(compiler, RULES_SRC, NULL) == 0)
    yr_compiler_get_rules(compiler, &rules);

  yr_compiler_destroy(compiler);

  return 0;
}

static int callback(
    YR_SCAN_CONTEXT* context,
    int message,
    void* message_data,
    void* user_data)
{
  if (message == CALLBACK_MSG_RULE_MATCHING || message == CALLBACK_MSG_RULE_NOT_MATCHING)
  {
    YR_RULE* rule = (YR_RULE*) message_data;
    const char* tag_name;
    YR_META* meta;
    YR_STRING* string;
    volatile size_t touched = 0;

    yr_rule_tags_foreach(rule, tag_name)
    {
      touched += strlen(tag_name);
    }

    yr_rule_metas_foreach(rule, meta)
    {
      if (meta->identifier != NULL)
        touched += strlen(meta->identifier);
      touched += (size_t) meta->integer;
    }

    yr_rule_strings_foreach(rule, string)
    {
      YR_MATCH* m;

      yr_string_matches_foreach(context, string, m)
      {
        touched += (size_t) m->match_length;

        if (m->data != NULL && m->data_length > 0)
          touched += m->data[0];
      }
    }

    (void) touched;
  }
  else if (message == CALLBACK_MSG_TOO_MANY_MATCHES)
  {
    return CALLBACK_CONTINUE;
  }

  return CALLBACK_CONTINUE;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (rules == NULL)
    return 0;

  if (size < 1)
    return 0;

  uint8_t flag_byte = data[0];
  const uint8_t* buf = data + 1;
  size_t buf_size = size - 1;

  int flags = SCAN_FLAGS_NO_TRYCATCH;

  if (flag_byte & 0x01)
    flags |= SCAN_FLAGS_FAST_MODE;
  if (flag_byte & 0x02)
    flags |= SCAN_FLAGS_PROCESS_MEMORY;
  if (flag_byte & 0x04)
    flags |= SCAN_FLAGS_REPORT_RULES_MATCHING;
  if (flag_byte & 0x08)
    flags |= SCAN_FLAGS_REPORT_RULES_NOT_MATCHING;

  int timeout = (flag_byte >> 6) & 0x03;

  yr_rules_scan_mem(rules, buf, buf_size, flags, callback, NULL, timeout);

  return 0;
}
