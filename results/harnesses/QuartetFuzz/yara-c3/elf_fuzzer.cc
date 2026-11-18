#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <yara.h>

static const char* RULES =
    "import \"elf\"\n"
    "rule test {\n"
    "  strings:\n"
    "    $a = \"ELF\"\n"
    "    $b = { 7f 45 4c 46 }\n"
    "    $c = /[A-Za-z0-9_\\.]{3,}/\n"
    "  condition:\n"
    "    $a or $b or $c or ext_int == 1234 or\n"
    "    elf.type == elf.ET_EXEC or elf.type == elf.ET_DYN or\n"
    "    elf.type == elf.ET_REL or elf.type == elf.ET_NONE or\n"
    "    elf.type == elf.ET_CORE or\n"
    "    elf.machine == elf.EM_X86_64 or elf.machine == elf.EM_386 or\n"
    "    elf.machine == elf.EM_ARM or elf.machine == elf.EM_AARCH64 or\n"
    "    elf.number_of_sections > 0 or elf.number_of_segments > 0 or\n"
    "    elf.entry_point > 0 or elf.symtab_entries > 0 or\n"
    "    elf.dynsym_entries > 0 or elf.dynamic_section_entries > 0 or\n"
    "    (elf.number_of_sections > 0 and\n"
    "     for any i in (0..elf.number_of_sections - 1) :\n"
    "       (elf.sections[i].name contains \"a\" or\n"
    "        elf.sections[i].size > 0 or\n"
    "        elf.sections[i].type == elf.SHT_PROGBITS)) or\n"
    "    (elf.number_of_segments > 0 and\n"
    "     for any i in (0..elf.number_of_segments - 1) :\n"
    "       (elf.segments[i].type == elf.PT_LOAD or\n"
    "        elf.segments[i].memory_size > 0)) or\n"
    "    (elf.dynamic_section_entries > 0 and\n"
    "     for any i in (0..elf.dynamic_section_entries - 1) :\n"
    "       (elf.dynamic[i].type == elf.DT_NEEDED)) or\n"
    "    (elf.symtab_entries > 0 and\n"
    "     for any i in (0..elf.symtab_entries - 1) :\n"
    "       (elf.symtab[i].name contains \"a\" or elf.symtab[i].value > 0)) or\n"
    "    (elf.dynsym_entries > 0 and\n"
    "     for any i in (0..elf.dynsym_entries - 1) :\n"
    "       (elf.dynsym[i].name contains \"a\" or elf.dynsym[i].value > 0))\n"
    "}\n";

static YR_RULES* rules = NULL;

typedef struct
{
  int mode;
  uint64_t sink;
} SCAN_CTX;

static int callback(
    YR_SCAN_CONTEXT* context,
    int message,
    void* message_data,
    void* user_data)
{
  SCAN_CTX* ctx = (SCAN_CTX*) user_data;

  if (message == CALLBACK_MSG_RULE_MATCHING ||
      message == CALLBACK_MSG_RULE_NOT_MATCHING)
  {
    YR_RULE* rule = (YR_RULE*) message_data;
    YR_STRING* string;

    yr_rule_strings_foreach(rule, string)
    {
      YR_MATCH* match;

      yr_string_matches_foreach(context, string, match)
      {
        if (match->data != NULL && match->data_length > 0)
          ctx->sink += (uint64_t) match->data[0] +
                       (uint64_t) match->data_length;
      }
    }
  }

  if (ctx->mode == 1)
    return CALLBACK_ABORT;
  if (ctx->mode == 2)
    return CALLBACK_ERROR;

  return CALLBACK_CONTINUE;
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
  YR_COMPILER* compiler = NULL;

  if (yr_initialize() != ERROR_SUCCESS)
    return 0;

  if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
    return 0;

  yr_compiler_define_integer_variable(compiler, "ext_int", 0);

  if (yr_compiler_add_string(compiler, RULES, NULL) == 0)
    yr_compiler_get_rules(compiler, &rules);

  yr_compiler_destroy(compiler);

  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (rules == NULL || size < 4)
    return 0;

  uint8_t flags_byte = data[0];
  uint8_t timeout_byte = data[1];
  uint8_t mode_byte = data[2];
  uint8_t ext_byte = data[3];

  const uint8_t* buffer = data + 4;
  size_t buffer_size = size - 4;

  int flags = (int) (flags_byte & 0x1F) | SCAN_FLAGS_NO_TRYCATCH;

  int timeout = timeout_byte % 5;

  SCAN_CTX ctx;
  ctx.mode = mode_byte % 3;
  ctx.sink = 0;

  yr_rules_define_integer_variable(rules, "ext_int", (int64_t) ext_byte);

  yr_rules_scan_mem(
      rules, buffer, buffer_size, flags, callback, &ctx, timeout);

  return 0;
}
