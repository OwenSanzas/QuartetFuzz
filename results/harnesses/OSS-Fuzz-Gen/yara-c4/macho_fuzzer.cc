#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <yara.h>

static YR_RULES* g_rules_light = nullptr;
static YR_RULES* g_rules_rich = nullptr;
static YR_RULES* g_rules_weak = nullptr;

static const char* kLightRuleSource =
    "rule fuzzing_light_rule\n"
    "{\n"
    "  strings:\n"
    "    $a = \"fuzz\" nocase\n"
    "    $b = { 90 90 90 }\n"
    "    $c = /[a-z]+[0-9]+/\n"
    "    $d = \"wide_fuzz\" wide ascii\n"
    "    $e = \"fullword_fuzz\" fullword\n"
    "    $f = \"xor_fuzz\" xor\n"
    "    $g = \"base64_fuzz\" base64\n"
    "  condition:\n"
    "    any of them\n"
    "}\n"
    "\n"
    "rule fuzzing_light_rule2\n"
    "{\n"
    "  strings:\n"
    "    $x = \"MZ\"\n"
    "    $y = \"PK\"\n"
    "  condition:\n"
    "    $x at 0 or $y at 0 or filesize > 0\n"
    "}\n"
    "\n"
    "private rule fuzzing_light_private_rule\n"
    "{\n"
    "  condition:\n"
    "    true\n"
    "}\n"
    "\n"
    "rule fuzzing_light_rule3\n"
    "{\n"
    "  condition:\n"
    "    fuzzing_light_private_rule and\n"
    "    (\n"
    "      (for any i in (0..5): (i == i)) or\n"
    "      fuzzing_light_rule\n"
    "    )\n"
    "}\n"
    "\n"
    "rule fuzzing_light_external_rule\n"
    "{\n"
    "  condition:\n"
    "    test_int >= 0 or test_bool or test_string matches /.*/\n"
    "}\n"
    "\n"
    "rule fuzzing_light_rule_matches\n"
    "{\n"
    "  strings:\n"
    "    $re1 = /[A-Za-z]{2,10}[0-9]{1,5}/\n"
    "  condition:\n"
    "    $re1 or \"fuzz\" matches /f.*/\n"
    "}\n"
    "\n"
    "rule fuzzing_light_count_offset\n"
    "{\n"
    "  strings:\n"
    "    $s1 = \"aa\"\n"
    "  condition:\n"
    "    #s1 >= 0\n"
    "}\n";

static const char* kRichRuleSource =
    "import \"pe\"\n"
    "import \"elf\"\n"
    "import \"macho\"\n"
    "import \"dotnet\"\n"
    "import \"dex\"\n"
    "import \"math\"\n"
    "import \"time\"\n"
    "import \"string\"\n"
    "import \"console\"\n"
    "\n"
    "global rule fuzzing_global_rule\n"
    "{\n"
    "  condition:\n"
    "    true\n"
    "}\n"
    "\n"
    "rule fuzzing_rule_1\n"
    "{\n"
    "  strings:\n"
    "    $a = \"fuzz\" nocase\n"
    "    $b = { 90 90 90 }\n"
    "    $c = /[a-z]+[0-9]+/\n"
    "    $d = \"wide_fuzz\" wide ascii\n"
    "    $e = \"fullword_fuzz\" fullword\n"
    "    $f = \"xor_fuzz\" xor\n"
    "    $g = \"base64_fuzz\" base64\n"
    "  condition:\n"
    "    any of them\n"
    "}\n"
    "\n"
    "rule fuzzing_rule_2\n"
    "{\n"
    "  strings:\n"
    "    $x = \"MZ\"\n"
    "    $y = \"PK\"\n"
    "  condition:\n"
    "    $x at 0 or $y at 0 or filesize > 0\n"
    "}\n"
    "\n"
    "private rule fuzzing_private_rule\n"
    "{\n"
    "  condition:\n"
    "    true\n"
    "}\n"
    "\n"
    "rule fuzzing_rule_3\n"
    "{\n"
    "  condition:\n"
    "    fuzzing_private_rule and\n"
    "    (\n"
    "      (for any i in (0..5): (i == i)) or\n"
    "      fuzzing_rule_1\n"
    "    )\n"
    "}\n"
    "\n"
    "rule fuzzing_external_rule\n"
    "{\n"
    "  condition:\n"
    "    test_int >= 0 or test_bool or test_string matches /.*/\n"
    "}\n"
    "\n"
    "rule fuzzing_module_rule\n"
    "{\n"
    "  condition:\n"
    "    pe.is_pe or\n"
    "    pe.number_of_sections >= 0 or\n"
    "    elf.type == elf.ET_EXEC or\n"
    "    elf.number_of_sections >= 0 or\n"
    "    macho.magic >= 0 or\n"
    "    dotnet.is_dotnet or\n"
    "    dex.header.file_size >= 0 or\n"
    "    math.entropy(0, filesize) >= 0.0 or\n"
    "    time.now() >= 0 or\n"
    "    string.to_int(\"123\") == 123 or\n"
    "    console.log(0) >= 0\n"
    "}\n"
    "\n"
    "rule fuzzing_rule_matches\n"
    "{\n"
    "  strings:\n"
    "    $re1 = /[A-Za-z]{2,10}[0-9]{1,5}/\n"
    "  condition:\n"
    "    $re1 or \"fuzz\" matches /f.*/\n"
    "}\n"
    "\n"
    "rule fuzzing_count_offset\n"
    "{\n"
    "  strings:\n"
    "    $s1 = \"aa\"\n"
    "  condition:\n"
    "    #s1 >= 0\n"
    "}\n";

static const char* kWeakRuleSource =
    "rule fuzzing_weak_atom\n"
    "{\n"
    "  strings:\n"
    "    $weak = { ?? ?? ?? ?? }\n"
    "  condition:\n"
    "    $weak\n"
    "}\n";

static const char* kModuleImports[] = {
    "import \"pe\"\n",
    "import \"elf\"\n",
    "import \"macho\"\n",
    "import \"dotnet\"\n",
    "import \"dex\"\n",
    "import \"math\"\n",
    "import \"time\"\n",
    "import \"string\"\n",
    "import \"console\"\n",
};
static const size_t kNumModuleImports =
    sizeof(kModuleImports) / sizeof(kModuleImports[0]);

static const char* kGrammarTokens[] = {
    "rule", "r", "{", "}", "(", ")", ":", "\n", " ",
    "condition", "strings", "meta", "global", "private",
    "true", "false", "and", "or", "not",
    "matches", "contains", "at", "in", "for", "any", "all", "of", "them",
    "nocase", "wide", "ascii", "fullword", "xor", "base64",
    "filesize", "entrypoint", "uint8", "uint16", "uint32",
    "==", ">=", "<=", "!=", "+", "-", "*", "..",
    "$a", "$a=", "#a", "@a", "\"fuzz\"", "/[a-z]+[0-9]+/", "0", "1", "5",
    "import", "\"pe\"", "\"elf\"", "\"macho\"", "\"dotnet\"", "\"dex\"",
    "\"math\"", "\"time\"", "\"string\"", "\"console\"",
    "pe.is_pe", "elf.type", "math.entropy(0,filesize)", "time.now()",
};
static const size_t kNumGrammarTokens =
    sizeof(kGrammarTokens) / sizeof(kGrammarTokens[0]);

static const size_t kForceLargeTarget = 200001;

static int ScanCallback(
    YR_SCAN_CONTEXT* context,
    int message,
    void* message_data,
    void* user_data)
{
  (void) context;
  (void) message_data;

  const uint8_t mode = (user_data != nullptr)
      ? *reinterpret_cast<const uint8_t*>(user_data)
      : 0;

  switch (message)
  {
    case CALLBACK_MSG_TOO_SLOW_SCANNING:

      return (mode == 0) ? CALLBACK_ABORT : CALLBACK_CONTINUE;

    case CALLBACK_MSG_RULE_MATCHING:
    case CALLBACK_MSG_RULE_NOT_MATCHING:
    case CALLBACK_MSG_IMPORT_MODULE:
    case CALLBACK_MSG_MODULE_IMPORTED:

      if (mode == 1) return CALLBACK_ERROR;
      if (mode == 2) return CALLBACK_ABORT;
      return CALLBACK_CONTINUE;

    case CALLBACK_MSG_SCAN_FINISHED:
    case CALLBACK_MSG_TOO_MANY_MATCHES:
    case CALLBACK_MSG_CONSOLE_LOG:
    default:
      return CALLBACK_CONTINUE;
  }
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
  (void) argc;
  (void) argv;

  if (yr_initialize() != ERROR_SUCCESS)
    return 0;

  {
    YR_COMPILER* compiler = nullptr;

    if (yr_compiler_create(&compiler) == ERROR_SUCCESS)
    {
      yr_compiler_define_integer_variable(compiler, "test_int", 0);
      yr_compiler_define_boolean_variable(compiler, "test_bool", 0);
      yr_compiler_define_string_variable(compiler, "test_string", "");

      int errors = yr_compiler_add_string(compiler, kLightRuleSource, nullptr);

      if (errors == 0)
      {
        if (yr_compiler_get_rules(compiler, &g_rules_light) != ERROR_SUCCESS)
          g_rules_light = nullptr;
      }

      yr_compiler_destroy(compiler);
    }
  }

  {
    YR_COMPILER* compiler = nullptr;

    if (yr_compiler_create(&compiler) == ERROR_SUCCESS)
    {
      yr_compiler_define_integer_variable(compiler, "test_int", 0);
      yr_compiler_define_boolean_variable(compiler, "test_bool", 0);
      yr_compiler_define_string_variable(compiler, "test_string", "");

      int errors = yr_compiler_add_string(compiler, kRichRuleSource, nullptr);

      if (errors == 0)
      {
        if (yr_compiler_get_rules(compiler, &g_rules_rich) != ERROR_SUCCESS)
          g_rules_rich = nullptr;
      }

      yr_compiler_destroy(compiler);
    }
  }

  {
    YR_COMPILER* compiler = nullptr;

    if (yr_compiler_create(&compiler) == ERROR_SUCCESS)
    {
      int errors = yr_compiler_add_string(compiler, kWeakRuleSource, nullptr);

      if (errors == 0)
      {
        if (yr_compiler_get_rules(compiler, &g_rules_weak) != ERROR_SUCCESS)
          g_rules_weak = nullptr;
      }

      yr_compiler_destroy(compiler);
    }
  }

  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (g_rules_light == nullptr && g_rules_rich == nullptr &&
      g_rules_weak == nullptr)
    return 0;

  FuzzedDataProvider stream(data, size);

  const uint8_t magic_choice = stream.ConsumeIntegralInRange<uint8_t>(0, 5);

  const uint8_t rule_set_choice = stream.ConsumeIntegralInRange<uint8_t>(0, 99);

  YR_RULES* rules_to_use = nullptr;
  if (rule_set_choice < 30 && g_rules_rich != nullptr)
    rules_to_use = g_rules_rich;
  else if (g_rules_light != nullptr)
    rules_to_use = g_rules_light;
  else
    rules_to_use = g_rules_rich;

  if (rules_to_use == nullptr)
    rules_to_use = g_rules_weak;

  if (rules_to_use == nullptr)
    return 0;

  const int64_t ext_int_val = stream.ConsumeIntegral<int64_t>();
  const bool ext_bool_val = stream.ConsumeBool();

  const int flags = stream.PickValueInArray<int>(
      {0,
       SCAN_FLAGS_FAST_MODE,
       SCAN_FLAGS_PROCESS_MEMORY,
       SCAN_FLAGS_NO_TRYCATCH,
       SCAN_FLAGS_REPORT_RULES_MATCHING,
       SCAN_FLAGS_REPORT_RULES_NOT_MATCHING,
       SCAN_FLAGS_FAST_MODE | SCAN_FLAGS_NO_TRYCATCH,
       SCAN_FLAGS_PROCESS_MEMORY | SCAN_FLAGS_FAST_MODE,
       SCAN_FLAGS_REPORT_RULES_MATCHING | SCAN_FLAGS_REPORT_RULES_NOT_MATCHING});

  const uint8_t callback_mode = stream.ConsumeIntegralInRange<uint8_t>(0, 9);

  const bool force_large = stream.ConsumeIntegralInRange<uint8_t>(0, 99) == 0 &&
      g_rules_weak != nullptr;

  const int timeout =
      force_large ? 0 : stream.ConsumeIntegralInRange<int>(0, 5);

  bool do_dynamic_rule_fuzz = stream.ConsumeIntegralInRange<uint8_t>(0, 9) < 2;
  if (force_large)
    do_dynamic_rule_fuzz = false;

  const uint16_t import_mask = stream.ConsumeIntegral<uint16_t>();

  const int num_tokens = stream.ConsumeIntegralInRange<int>(0, 12);

  const size_t rule_len = stream.ConsumeIntegralInRange<size_t>(0, 256);

  std::string token_prefix;
  for (int i = 0; i < num_tokens; i++)
  {
    const size_t idx =
        stream.ConsumeIntegralInRange<size_t>(0, kNumGrammarTokens - 1);
    token_prefix += kGrammarTokens[idx];
    token_prefix += " ";
  }

  const std::string fuzzed_rule_text = stream.ConsumeBytesAsString(rule_len);
  const std::string ext_string_val = stream.ConsumeBytesAsString(16);

  std::vector<uint8_t> raw_buffer = stream.ConsumeRemainingBytes<uint8_t>();

  if (!force_large && raw_buffer.size() > 8192)
    raw_buffer.resize(8192);

  std::vector<uint8_t> buffer;
  switch (magic_choice)
  {
    case 1:
      buffer = {'M', 'Z'};
      break;
    case 2:
      buffer = {0x7f, 'E', 'L', 'F'};
      break;
    case 3:
      buffer = {0xCE, 0xFA, 0xED, 0xFE};
      break;
    case 4:
      buffer = {0xCF, 0xFA, 0xED, 0xFE};
      break;
    case 5:
      buffer = {'d', 'e', 'x', '\n', '0', '3', '5', '\0'};
      break;
    default:
      break;
  }
  buffer.insert(buffer.end(), raw_buffer.begin(), raw_buffer.end());

  if (force_large)
  {

    if (buffer.empty())
      buffer.push_back(0x41);

    const size_t target = kForceLargeTarget;

    std::vector<uint8_t> big;
    big.reserve(target);

    while (big.size() < target)
    {
      const size_t remaining = target - big.size();
      const size_t chunk = std::min(remaining, buffer.size());
      big.insert(big.end(), buffer.begin(), buffer.begin() + chunk);
    }

    buffer.swap(big);
  }

  const uint8_t* buffer_ptr = buffer.empty() ? nullptr : buffer.data();
  const size_t buffer_size = buffer.size();

  YR_RULES* scan_rules =
      (force_large && g_rules_weak != nullptr) ? g_rules_weak : rules_to_use;

  if (scan_rules != g_rules_weak)
  {
    yr_rules_define_integer_variable(scan_rules, "test_int", ext_int_val);
    yr_rules_define_boolean_variable(
        scan_rules, "test_bool", ext_bool_val ? 1 : 0);
    yr_rules_define_string_variable(
        scan_rules, "test_string", ext_string_val.c_str());
  }

  yr_rules_scan_mem(
      scan_rules,
      buffer_ptr,
      buffer_size,
      flags,
      ScanCallback,
      const_cast<uint8_t*>(&callback_mode),
      timeout);

  if (do_dynamic_rule_fuzz && (!fuzzed_rule_text.empty() || !token_prefix.empty()))
  {
    std::string dynamic_imports;
    for (size_t i = 0; i < kNumModuleImports; i++)
    {
      if (import_mask & (static_cast<uint16_t>(1) << i))
        dynamic_imports += kModuleImports[i];
    }

    const std::string dynamic_source =
        dynamic_imports + token_prefix + fuzzed_rule_text;

    YR_COMPILER* dyn_compiler = nullptr;

    if (yr_compiler_create(&dyn_compiler) == ERROR_SUCCESS)
    {
      yr_compiler_define_integer_variable(dyn_compiler, "test_int", ext_int_val);
      yr_compiler_define_boolean_variable(
          dyn_compiler, "test_bool", ext_bool_val ? 1 : 0);
      yr_compiler_define_string_variable(
          dyn_compiler, "test_string", ext_string_val.c_str());

      YR_RULES* dyn_rules = nullptr;
      const int dyn_errors = yr_compiler_add_string(
          dyn_compiler, dynamic_source.c_str(), nullptr);

      if (dyn_errors == 0 &&
          yr_compiler_get_rules(dyn_compiler, &dyn_rules) == ERROR_SUCCESS &&
          dyn_rules != nullptr)
      {
        yr_rules_scan_mem(
            dyn_rules,
            buffer_ptr,
            buffer_size,
            flags,
            ScanCallback,
            const_cast<uint8_t*>(&callback_mode),
            timeout);

        yr_rules_destroy(dyn_rules);
      }

      yr_compiler_destroy(dyn_compiler);
    }
  }

  return 0;
}