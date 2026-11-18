#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <yara.h>

static const char* RULES =
    "import \"dotnet\"\n"
    "rule test {\n"
    "  strings:\n"
    "    $mz = \"MZ\"\n"
    "    $pe = \"PE\"\n"
    "  condition:\n"
    "    dotnet.is_dotnet and (\n"
    "      dotnet.number_of_streams >= 0 or\n"
    "      dotnet.number_of_classes >= 0 or\n"
    "      dotnet.number_of_resources >= 0 or\n"
    "      dotnet.number_of_guids >= 0 or\n"
    "      dotnet.number_of_assembly_refs >= 0 or\n"
    "      dotnet.number_of_modulerefs >= 0 or\n"
    "      dotnet.number_of_user_strings >= 0 or\n"
    "      dotnet.number_of_constants >= 0 or\n"
    "      dotnet.number_of_field_offsets >= 0 or\n"
    "      dotnet.assembly.version.major >= 0 or\n"
    "      dotnet.assembly.name != \"\" or\n"
    "      dotnet.module_name != \"\" or\n"
    "      dotnet.typelib != \"\" or\n"
    "      ($mz at 0) or\n"
    "      $pe or\n"
    "      (dotnet.number_of_classes > 0 and\n"
    "       dotnet.classes[0].number_of_methods > 0 and\n"
    "       dotnet.classes[0].methods[0].number_of_parameters >= 0)\n"
    "    )\n"
    "}\n"
    "rule test_not_dotnet {\n"
    "  condition:\n"
    "    not dotnet.is_dotnet\n"
    "}\n";

static YR_RULES* rules = NULL;

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
  YR_COMPILER* compiler = NULL;

  if (yr_initialize() != ERROR_SUCCESS)
    return 0;

  if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
    return 0;

  if (yr_compiler_add_string(compiler, RULES, NULL) == 0)
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

  if (message == CALLBACK_MSG_RULE_MATCHING ||
      message == CALLBACK_MSG_RULE_NOT_MATCHING)
  {
    YR_RULE* rule = (YR_RULE*) message_data;

    if (rule != NULL)
    {
      if (rule->identifier != NULL)
      {
        volatile size_t len = strlen(rule->identifier);
        (void) len;
      }

      YR_STRING* s;
      volatile uint64_t checksum = 0;

      yr_rule_strings_foreach(rule, s)
      {
        YR_MATCH* m;

        yr_string_matches_foreach(context, s, m)
        {
          checksum += (uint64_t) m->offset + (uint64_t) m->match_length;

          if (m->data != NULL && m->data_length > 0)
            checksum += m->data[0];
        }
      }

      (void) checksum;
    }
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

  if (size < 2)
    return 0;

  int flags = data[0] &
      (SCAN_FLAGS_FAST_MODE | SCAN_FLAGS_PROCESS_MEMORY |
       SCAN_FLAGS_NO_TRYCATCH | SCAN_FLAGS_REPORT_RULES_MATCHING |
       SCAN_FLAGS_REPORT_RULES_NOT_MATCHING);

  int timeout = data[1] & 1;

  const uint8_t* buffer = data + 2;
  size_t buffer_size = size - 2;

  yr_rules_scan_mem(
      rules, buffer, buffer_size, flags, callback, NULL, timeout);

  return 0;
}
