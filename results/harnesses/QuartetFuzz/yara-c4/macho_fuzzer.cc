#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <yara.h>

static const char* RULES =
    "import \"macho\"\n"
    "rule macho_cond {\n"
    "  strings:\n"
    "    $lit = \"MZMZ\"\n"
    "    $wide = \"HELLO\" wide\n"
    "    $nocase = \"needle\" nocase\n"
    "    $xor = \"secretsauce\" xor\n"
    "    $re = /ab.{1,10}cd/\n"
    "  condition:\n"
    "    (macho.ncmds > 0 or macho.number_of_segments >= 0) and\n"
    "    (macho.cputype == macho.CPU_TYPE_X86 or\n"
    "     macho.cputype == macho.CPU_TYPE_X86_64 or\n"
    "     macho.cputype == macho.CPU_TYPE_ARM64 or\n"
    "     macho.filetype == macho.MH_EXECUTE or\n"
    "     macho.filetype == macho.MH_OBJECT or\n"
    "     macho.entry_point >= 0 or\n"
    "     macho.nfat_arch >= 0 or\n"
    "     ext_int != 0 or ext_bool or ext_str == \"trigger\") or\n"
    "    (for any i in (0..macho.number_of_segments - 1): (\n"
    "       macho.segments[i].vmsize >= 0 and\n"
    "       macho.segments[i].nsects >= 0)) or\n"
    "    $lit or $wide or $nocase or $xor or $re\n"
    "}\n";

static YR_RULES* rules = NULL;

static int g_abort_on_match = 0;

static int scan_callback(
    YR_SCAN_CONTEXT* context,
    int message,
    void* message_data,
    void* user_data)
{

  if (message == CALLBACK_MSG_RULE_MATCHING)
  {
    YR_RULE* rule = (YR_RULE*) message_data;
    YR_STRING* string;

    yr_rule_strings_foreach(rule, string)
    {
      YR_MATCH* match;

      yr_string_matches_foreach(context, string, match)
      {

        volatile uint8_t sum = 0;
        for (int32_t i = 0; i < match->data_length; i++)
          sum += match->data[i];
        (void) sum;
      }
    }

    if (g_abort_on_match)
      return CALLBACK_ABORT;
  }

  (void) user_data;

  return CALLBACK_CONTINUE;
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
  YR_COMPILER* compiler;

  if (yr_initialize() != ERROR_SUCCESS)
    return 0;

  if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
    return 0;

  yr_compiler_define_integer_variable(compiler, "ext_int", 0);
  yr_compiler_define_boolean_variable(compiler, "ext_bool", 0);
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

  if (size < 5)
    return 0;

  uint8_t control = data[0];
  uint8_t control2 = data[1];
  uint8_t control3 = data[2];
  uint8_t control4 = data[3];
  uint8_t control5 = data[4];

  const uint8_t* buffer = data + 5;
  size_t buffer_size = size - 5;

  int flags = control & (SCAN_FLAGS_FAST_MODE | SCAN_FLAGS_PROCESS_MEMORY |
                          SCAN_FLAGS_NO_TRYCATCH |
                          SCAN_FLAGS_REPORT_RULES_MATCHING |
                          SCAN_FLAGS_REPORT_RULES_NOT_MATCHING);

  int timeout = control2 % 3;

  int64_t ext_int_val = (int64_t) (int8_t) control3;
  int ext_bool_val = control4 & 1;
  g_abort_on_match = control5 & 1;

  char ext_str_buf[17];
  size_t copy_len = buffer_size < 16 ? buffer_size : 16;
  memcpy(ext_str_buf, buffer, copy_len);
  ext_str_buf[copy_len] = '\0';

  yr_rules_define_integer_variable(rules, "ext_int", ext_int_val);
  yr_rules_define_boolean_variable(rules, "ext_bool", ext_bool_val);
  yr_rules_define_string_variable(rules, "ext_str", ext_str_buf);

  yr_rules_scan_mem(
      rules, buffer, buffer_size, flags, scan_callback, NULL, timeout);

  return 0;
}
