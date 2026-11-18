#include <climits>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <yara.h>
#include <yara/rules.h>
#include <yara/compiler.h>
#include <yara/error.h>

static int dummy_callback(
    YR_SCAN_CONTEXT* context,
    int message,
    void* message_data,
    void* user_data)
{
  (void)context;
  (void)message;
  (void)message_data;
  (void)user_data;
  return CALLBACK_CONTINUE;
}

static const char* g_rule_source =
    "rule fuzz_rule\n"
    "{\n"
    "    strings:\n"
    "        $a = \"fuzz\"\n"
    "        $b = { 00 01 02 03 }\n"
    "    condition:\n"
    "        $a or $b or filesize > 0\n"
    "}\n";

static int g_initialized = 0;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{
  if (!g_initialized)
  {
    if (yr_initialize() != ERROR_SUCCESS)
      return 0;
    g_initialized = 1;
  }

  YR_COMPILER* compiler = NULL;
  if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
    return 0;

  int compile_errors = yr_compiler_add_string(compiler, g_rule_source, NULL);
  if (compile_errors != 0)
  {
    yr_compiler_destroy(compiler);
    return 0;
  }

  YR_RULES* rules = NULL;
  if (yr_compiler_get_rules(compiler, &rules) != ERROR_SUCCESS)
  {
    yr_compiler_destroy(compiler);
    return 0;
  }

  FILE* f = fopen("./dummy_file", "wb");
  if (f != NULL)
  {
    fwrite(Data, 1, Size, f);
    fclose(f);
  }

  uint8_t* buffer = NULL;
  size_t buffer_size = Size;

  if (Size > 0)
  {
    buffer = (uint8_t*)malloc(Size);
    if (buffer != NULL)
      memcpy(buffer, Data, Size);
  }
  else
  {
    buffer = (uint8_t*)malloc(1);
    buffer_size = 0;
  }

  if (buffer != NULL)
  {

    yr_rules_scan_mem(
        rules,
        buffer,
        buffer_size,
        0,
        dummy_callback,
        NULL,
        1);

    yr_rules_scan_mem(
        rules,
        buffer,
        buffer_size,
        SCAN_FLAGS_FAST_MODE,
        dummy_callback,
        NULL,
        1);

    free(buffer);
  }

  yr_rules_destroy(rules);
  yr_compiler_destroy(compiler);

  return 0;
}