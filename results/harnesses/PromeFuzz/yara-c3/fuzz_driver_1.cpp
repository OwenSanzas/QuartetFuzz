#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>

extern "C" {
#include <yara.h>
#include <rules.h>
}

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

static bool g_initialized = false;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
  if (Size < 4)
    return 0;

  if (!g_initialized)
  {
    yr_initialize();
    g_initialized = true;
  }

  size_t split = Size / 2;
  if (split == 0)
    split = Size;

  char* rule_source = (char*) malloc(split + 1);
  if (rule_source == nullptr)
    return 0;

  memcpy(rule_source, Data, split);
  rule_source[split] = '\0';

  const uint8_t* buffer = Data + split;
  size_t buffer_size = Size - split;

  {
    FILE* fp = fopen("./dummy_file", "wb");
    if (fp != nullptr)
    {
      fwrite(buffer, 1, buffer_size, fp);
      fclose(fp);
    }
  }

  YR_COMPILER* compiler = nullptr;
  if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
  {
    free(rule_source);
    return 0;
  }

  int errors = yr_compiler_add_string(compiler, rule_source, nullptr);
  free(rule_source);

  if (errors > 0)
  {
    yr_compiler_destroy(compiler);
    return 0;
  }

  YR_RULES* rules = nullptr;
  if (yr_compiler_get_rules(compiler, &rules) != ERROR_SUCCESS)
  {
    yr_compiler_destroy(compiler);
    return 0;
  }

  void* user_data = nullptr;
  int timeout = 1;

  yr_rules_scan_mem(
      rules,
      buffer,
      buffer_size,
      0,
      dummy_callback,
      user_data,
      timeout);

  yr_rules_scan_mem(
      rules,
      buffer,
      buffer_size,
      SCAN_FLAGS_FAST_MODE,
      dummy_callback,
      user_data,
      timeout);

  yr_rules_scan_mem(
      rules,
      buffer,
      buffer_size,
      SCAN_FLAGS_REPORT_RULES_MATCHING,
      dummy_callback,
      user_data,
      timeout);

  yr_rules_scan_mem(
      rules,
      buffer,
      buffer_size,
      SCAN_FLAGS_REPORT_RULES_NOT_MATCHING,
      dummy_callback,
      user_data,
      timeout);

  yr_rules_destroy(rules);
  yr_compiler_destroy(compiler);

  return 0;
}