#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <yara.h>
#include <fuzzer/FuzzedDataProvider.h>

static YR_RULES* g_rules = NULL;
static YR_COMPILER* g_compiler = NULL;

struct CallbackCtx
{
  uint8_t decision_bits;
};

static int tunable_callback(
    YR_SCAN_CONTEXT* context,
    int message,
    void* message_data,
    void* user_data)
{
  CallbackCtx* ctx = (CallbackCtx*) user_data;
  uint8_t bits = ctx ? ctx->decision_bits : 0;

  switch (message)
  {
  case CALLBACK_MSG_RULE_MATCHING:
  case CALLBACK_MSG_RULE_NOT_MATCHING:
    if (bits & 0x1) return CALLBACK_ABORT;
    if (bits & 0x2) return CALLBACK_ERROR;
    return CALLBACK_CONTINUE;

  case CALLBACK_MSG_TOO_MANY_MATCHES:

    return (bits & 0x4) ? CALLBACK_ERROR : CALLBACK_CONTINUE;

  case CALLBACK_MSG_TOO_SLOW_SCANNING:

    return (bits & 0x8) ? CALLBACK_ERROR : CALLBACK_CONTINUE;

  case CALLBACK_MSG_IMPORT_MODULE:
  case CALLBACK_MSG_MODULE_IMPORTED:

    return (bits & 0x10) ? CALLBACK_ERROR : CALLBACK_CONTINUE;

  case CALLBACK_MSG_CONSOLE_LOG:
  case CALLBACK_MSG_SCAN_FINISHED:
  default:
    return CALLBACK_CONTINUE;
  }
}

static void compiler_callback(
    int error_level,
    const char* file_name,
    int line_number,
    const YR_RULE* rule,
    const char* message,
    void* user_data)
{

  fprintf(
      stderr,
      "YARA compile %s:%d: %s\n",
      file_name ? file_name : "<string>",
      line_number,
      message ? message : "");
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
  yr_initialize();

  const char* rule_string =
      "import \"pe\" "
      "import \"elf\" "
      "import \"math\" "
      "import \"time\" "
      "import \"dotnet\" "
      "import \"dex\" "
      "import \"macho\" "
      "import \"console\" "
      "import \"string\" "
      "rule fuzz_rule_1 { "
      "  strings: "
      "    $a = \"test\" nocase wide ascii fullword "
      "    $b = { 4D 5A [1-4] 90 ?? } "
      "    $c = /abc[0-9]+/ "
      "    $d = \"secret\" xor "
      "    $e = \"QUJD\" base64 "
      "  condition: "
      "    any of them or $a at 0 or $c in (0..filesize) "
      "} "
      "rule fuzz_rule_2 { "
      "  strings: "
      "    $x = \"a\" "
      "  condition: "
      "    #x > 0 or filesize > 100 "
      "} "
      "rule fuzz_rule_pe { "
      "  condition: "
      "    pe.is_pe and (pe.number_of_sections > 0 or pe.entry_point >= 0) "
      "} "
      "rule fuzz_rule_elf { "
      "  condition: "
      "    elf.type == elf.ET_EXEC or elf.type == elf.ET_DYN or true "
      "} "
      "rule fuzz_rule_math { "
      "  condition: "
      "    math.entropy(0, filesize) >= 0.0 "
      "} "
      "rule fuzz_rule_dotnet { "
      "  condition: "
      "    dotnet.is_dotnet or true "
      "} "
      "rule fuzz_rule_dex { "
      "  condition: "
      "    dex.header.checksum >= 0 or true "
      "} "
      "rule fuzz_rule_macho { "
      "  condition: "
      "    macho.ncmds >= 0 or true "
      "} "
      "rule fuzz_rule_loop { "
      "  strings: $s = \"a\" "
      "  condition: "
      "    for any i in (1..#s) : ( @s[i] >= 0 ) "
      "} "
      "rule fuzz_rule_console_string { "
      "  condition: "
      "    console.log(\"fuzz\") >= 0 and "
      "    (string.to_int(\"123\") == 123 or string.to_int(\"bad\") == 123) "
      "} "
      "rule fuzz_rule_ops { "
      "  condition: "
      "    (filesize % 7) == 0 or (filesize & 0xff) != 0 or "
      "    (filesize << 1) > 0 or (filesize >> 1) >= 0 or "
      "    not (filesize < 0) or defined filesize "
      "} "
      "rule fuzz_rule_strops { "
      "  strings: "
      "    $a = \"test\" "
      "  condition: "
      "    (pe.rich_signature.clear_data contains \"a\") or "
      "    (pe.rich_signature.clear_data matches /t.st/) or "
      "    $a or "
      "    (console.log(\"x\") >= 0 and "
      "     \"abc\" startswith \"a\" and \"abc\" endswith \"c\" and "
      "     \"ABC\" icontains \"b\") "
      "} "
      "private rule fuzz_rule_private { condition: true } "
      "global rule fuzz_rule_global { condition: true } "
      "rule fuzz_rule_iter_pe { "
      "  condition: "
      "    (pe.number_of_sections > 0 and "
      "     for any i in (0..pe.number_of_sections - 1) : ( "
      "       pe.sections[i].raw_data_size >= 0 "
      "     )) "
      "    or (for any section in pe.sections : ( section.virtual_size >= 0 )) "
      "    or true "
      "} "
      "rule fuzz_rule_iter_dotnet { "
      "  condition: "
      "    (dotnet.number_of_streams > 0 and "
      "     for any i in (0..dotnet.number_of_streams - 1) : ( "
      "       dotnet.streams[i].size >= 0 "
      "     )) "
      "    or true "
      "} "
      "rule fuzz_rule_stringset { "
      "  strings: "
      "    $x1 = \"a\" "
      "    $x2 = \"b\" "
      "    $x3 = \"c\" "
      "  condition: "
      "    (for any of ($x*) : ( $ )) or (for all of ($x*) : ( $ )) "
      "} "
      "rule fuzz_rule_math_time { "
      "  condition: "
      "    math.mean(0, filesize) >= 0.0 and "
      "    math.deviation(0, filesize, math.mean(0, filesize)) >= 0.0 and "
      "    time.now() >= 0 "
      "}";

  if (yr_compiler_create(&g_compiler) != ERROR_SUCCESS)
  {
    fprintf(stderr, "FATAL: yr_compiler_create() failed, aborting fuzz target init\n");
    abort();
  }

  yr_compiler_set_callback(g_compiler, compiler_callback, NULL);

  if (yr_compiler_add_string(g_compiler, rule_string, NULL) != 0)
  {

    fprintf(
        stderr,
        "FATAL: rule_string failed to compile, aborting fuzz target init\n");
    yr_compiler_destroy(g_compiler);
    g_compiler = NULL;
    abort();
  }

  if (yr_compiler_get_rules(g_compiler, &g_rules) != ERROR_SUCCESS)
  {
    fprintf(
        stderr,
        "FATAL: yr_compiler_get_rules() failed, aborting fuzz target init\n");
    yr_compiler_destroy(g_compiler);
    g_compiler = NULL;
    g_rules = NULL;
    abort();
  }

  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (g_rules == NULL)
    return 0;

  if (size < 1)
  {

    CallbackCtx ctx = {0};
    yr_rules_scan_mem(
        g_rules,
        data,
        size,
        0,
        tunable_callback,
        &ctx,
        1);
    return 0;
  }

  FuzzedDataProvider fdp(data, size);

  uint8_t flag_byte = fdp.ConsumeIntegral<uint8_t>();
  int flags = 0;
  if (flag_byte & 0x1) flags |= SCAN_FLAGS_FAST_MODE;
  if (flag_byte & 0x2) flags |= SCAN_FLAGS_NO_TRYCATCH;
  if (flag_byte & 0x4) flags |= SCAN_FLAGS_REPORT_RULES_MATCHING;
  if (flag_byte & 0x8) flags |= SCAN_FLAGS_REPORT_RULES_NOT_MATCHING;
  if (flag_byte & 0x10) flags |= SCAN_FLAGS_PROCESS_MEMORY;

  int timeout = fdp.ConsumeIntegralInRange<int>(0, 2);

  CallbackCtx ctx;
  ctx.decision_bits = fdp.ConsumeIntegral<uint8_t>();

  std::vector<uint8_t> buffer = fdp.ConsumeRemainingBytes<uint8_t>();

  yr_rules_scan_mem(
      g_rules,
      buffer.data(),
      buffer.size(),
      flags,
      tunable_callback,
      &ctx,
      timeout);

  return 0;
}