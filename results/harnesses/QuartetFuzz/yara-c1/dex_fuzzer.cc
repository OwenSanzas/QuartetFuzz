#include <cstdio>
#include <cstring>
#include <stddef.h>
#include <stdint.h>
#include <yara.h>

static const char* RULES =
    "import \"dex\"\n"
    "rule dex_main\n"
    "{\n"
    "  strings:\n"
    "    $lit = \"classes.dex\" nocase\n"
    "    $hex = { 00 00 00 00 }\n"
    "    $re = /L[A-Za-z0-9_\\/]+;/\n"
    "  condition:\n"
    "    ext_int >= 0 and\n"
    "    (\n"
    "      dex.header.magic == dex.DEX_FILE_MAGIC_035 or\n"
    "      dex.header.magic == dex.DEX_FILE_MAGIC_036 or\n"
    "      dex.header.magic == dex.DEX_FILE_MAGIC_037 or\n"
    "      dex.header.magic == dex.DEX_FILE_MAGIC_038 or\n"
    "      dex.header.magic == dex.DEX_FILE_MAGIC_039 or\n"
    "      dex.header.checksum >= 0 or\n"
    "      dex.header.file_size >= 0 or\n"
    "      dex.header.data_size >= 0 or\n"
    "      dex.number_of_methods >= 0 or\n"
    "      dex.number_of_fields >= 0 or\n"
    "      dex.map_list.size >= 0 or\n"
    "      dex.has_method(\"<init>\") or\n"
    "      dex.has_method(\"Ljava/lang/Object;\", \"<init>\") or\n"
    "      dex.has_method(/init/) or\n"
    "      dex.has_method(/L.*;/, /.*/ ) or\n"
    "      dex.has_class(\"Ljava/lang/Object;\") or\n"
    "      dex.has_class(/L.*;/) or\n"
    "      any of them or\n"
    "      ext_str contains \"a\"\n"
    "    )\n"
    "}\n"
    "rule dex_tagged : tagA tagB\n"
    "{\n"
    "  meta:\n"
    "    author = \"fuzz\"\n"
    "    info = \"dex harness\"\n"
    "  strings:\n"
    "    $m1 = \"secret\" nocase\n"
    "    $m2 = { 90 90 ?? 90 }\n"
    "    $m3 = /[0-9]{3,6}/\n"
    "  condition:\n"
    "    any of them or dex.header.endian_tag == 0x12345678\n"
    "}\n";

static YR_RULES* rules = NULL;

struct ScanState
{
  uint8_t control;
};

static void walk_rule(YR_SCAN_CONTEXT* context, YR_RULE* rule)
{
  const char* tag;
  YR_META* meta;
  YR_STRING* string;
  size_t touched = 0;

  yr_rule_tags_foreach(rule, tag) { touched += strlen(tag); }

  yr_rule_metas_foreach(rule, meta)
  {
    if (meta->identifier != NULL)
      touched += strlen(meta->identifier);
  }

  yr_rule_strings_foreach(rule, string)
  {
    YR_MATCH* match;

    yr_string_matches_foreach(context, string, match)
    {
      if (match->data != NULL && match->data_length > 0)
      {
        volatile uint8_t sink = match->data[0];
        touched += sink;
      }
    }
  }

  (void) touched;
}

static int callback(
    YR_SCAN_CONTEXT* context,
    int message,
    void* message_data,
    void* user_data)
{
  ScanState* state = (ScanState*) user_data;

  switch (message)
  {
  case CALLBACK_MSG_MODULE_IMPORTED:
  {

    YR_OBJECT* object = (YR_OBJECT*) message_data;
    yr_object_print_data(object, 0, 1);
    printf("\n");
    break;
  }
  case CALLBACK_MSG_RULE_MATCHING:
  case CALLBACK_MSG_RULE_NOT_MATCHING:
  {
    YR_RULE* rule = (YR_RULE*) message_data;
    walk_rule(context, rule);

    if (message == CALLBACK_MSG_RULE_MATCHING && state != NULL &&
        (state->control & 0x80) != 0)
      return CALLBACK_ABORT;

    break;
  }
  case CALLBACK_MSG_TOO_MANY_MATCHES:
  case CALLBACK_MSG_TOO_SLOW_SCANNING:
  {

    YR_STRING* string = (YR_STRING*) message_data;

    if (string != NULL && context != NULL && context->rules != NULL)
    {
      YR_RULE* rule = &context->rules->rules_table[string->rule_idx];
      volatile size_t len = 0;

      if (rule->identifier != NULL)
        len += strlen(rule->identifier);
      if (string->identifier != NULL)
        len += strlen(string->identifier);

      (void) len;
    }

    break;
  }
  case CALLBACK_MSG_CONSOLE_LOG:
  {
    const char* text = (const char*) message_data;

    if (text != NULL)
      printf("%s\n", text);

    break;
  }
  default:
    break;
  }

  return CALLBACK_CONTINUE;
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{

  freopen("/dev/null", "w", stdout);

  if (yr_initialize() != ERROR_SUCCESS)
    return 0;

  YR_COMPILER* compiler = NULL;

  if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
    return 0;

  yr_compiler_define_integer_variable(compiler, "ext_int", 0);
  yr_compiler_define_string_variable(compiler, "ext_str", "");

  if (yr_compiler_add_string(compiler, RULES, NULL) == 0)
    yr_compiler_get_rules(compiler, &rules);

  yr_compiler_destroy(compiler);

  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (rules == NULL)
    return 0;

  uint8_t control = (size > 0) ? data[size - 1] : 0;

  int flags = SCAN_FLAGS_NO_TRYCATCH |
              (control &
               (SCAN_FLAGS_FAST_MODE | SCAN_FLAGS_PROCESS_MEMORY |
                SCAN_FLAGS_REPORT_RULES_MATCHING |
                SCAN_FLAGS_REPORT_RULES_NOT_MATCHING));

  int timeout = (control & 0x40) ? 1 : 0;

  yr_rules_define_integer_variable(rules, "ext_int", (int64_t) control);

  char ext_str[16];
  size_t n = size < 15 ? size : 15;

  for (size_t i = 0; i < n; i++)
  {
    uint8_t b = data[i];
    ext_str[i] = (char) (32 + (b % 95));
  }

  ext_str[n] = '\0';

  yr_rules_define_string_variable(rules, "ext_str", ext_str);

  ScanState state;
  state.control = control;

  yr_rules_scan_mem(rules, data, size, flags, callback, &state, timeout);

  return 0;
}
