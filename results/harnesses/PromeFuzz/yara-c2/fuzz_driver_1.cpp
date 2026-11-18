#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <yara.h>
#include <yara/rules.h>
#include <yara/compiler.h>
#include <yara/error.h>
#include <yara/scan.h>

static YR_RULES *g_rules = nullptr;
static int g_initialized = 0;

static int dummy_callback(
    YR_SCAN_CONTEXT *context,
    int message,
    void *message_data,
    void *user_data)
{

  (void)context;
  (void)message;
  (void)message_data;
  (void)user_data;
  return CALLBACK_CONTINUE;
}

static void init_rules()
{
  if (g_initialized)
    return;

  g_initialized = 1;

  if (yr_initialize() != ERROR_SUCCESS)
    return;

  YR_COMPILER *compiler = nullptr;

  if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
    return;

  const char *rule_string =
      "rule test_rule_1 {\n"
      "  strings:\n"
      "    $a = \"test\"\n"
      "    $b = { 90 90 90 }\n"
      "    $c = /ab+c/\n"
      "  condition:\n"
      "    $a or $b or $c or filesize > 0\n"
      "}\n"
      "rule test_rule_2 {\n"
      "  strings:\n"
      "    $x = \"malware\"\n"
      "    $y = \"payload\"\n"
      "  condition:\n"
      "    $x and $y\n"
      "}\n"
      "rule test_rule_3 {\n"
      "  condition:\n"
      "    true\n"
      "}\n";

  int result = yr_compiler_add_string(compiler, rule_string, nullptr);

  if (result == 0)
  {
    yr_compiler_get_rules(compiler, &g_rules);
  }

  yr_compiler_destroy(compiler);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
  init_rules();

  if (g_rules == nullptr)
    return 0;

  if (Size == 0)
    return 0;

  uint8_t flag_selector = Data[0];
  int flags = 0;

  if (flag_selector & 0x01)
    flags |= SCAN_FLAGS_FAST_MODE;

  if (flag_selector & 0x02)
    flags |= SCAN_FLAGS_PROCESS_MEMORY;

  if (flag_selector & 0x04)
    flags |= SCAN_FLAGS_NO_TRYCATCH;

  int timeout = 1000000;

  const uint8_t *scan_buffer = Data;
  size_t scan_size = Size;

  if (Size > 1)
  {
    scan_buffer = Data + 1;
    scan_size = Size - 1;
  }

  yr_rules_scan_mem(
      g_rules,
      scan_buffer,
      scan_size,
      flags,
      dummy_callback,
      nullptr,
      timeout);

  yr_rules_scan_mem(
      g_rules,
      Data,
      Size,
      SCAN_FLAGS_FAST_MODE,
      dummy_callback,
      nullptr,
      1);

  yr_rules_scan_mem(
      g_rules,
      Data,
      0,
      0,
      dummy_callback,
      nullptr,
      timeout);

  return 0;
}