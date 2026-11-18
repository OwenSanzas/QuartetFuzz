#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

#include <climits>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>

extern "C" {
#include <yara.h>
#include <yara/rules.h>
#include <yara/compiler.h>
#include <yara/error.h>
}

static YR_RULES* g_rules = nullptr;
static bool g_initialized = false;
static bool g_init_failed = false;

static int scan_callback(
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

static void init_yara_rules()
{
    if (g_initialized || g_init_failed)
        return;

    g_initialized = true;

    if (yr_initialize() != ERROR_SUCCESS)
    {
        g_init_failed = true;
        return;
    }

    YR_COMPILER* compiler = nullptr;

    if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
    {
        g_init_failed = true;
        return;
    }

    const char* rule_source =
        "rule fuzz_rule_plain {\n"
        "  strings:\n"
        "    $a = \"fuzz\"\n"
        "    $b = \"test_string\"\n"
        "  condition:\n"
        "    $a or $b\n"
        "}\n"
        "rule fuzz_rule_hex {\n"
        "  strings:\n"
        "    $h1 = { AA BB CC DD }\n"
        "    $h2 = { 90 90 [1-4] 90 }\n"
        "  condition:\n"
        "    $h1 or $h2\n"
        "}\n"
        "rule fuzz_rule_regex {\n"
        "  strings:\n"
        "    $r1 = /abc.*def/\n"
        "    $r2 = /[0-9]{3,5}/\n"
        "  condition:\n"
        "    $r1 or $r2\n"
        "}\n"
        "rule fuzz_rule_modifiers {\n"
        "  strings:\n"
        "    $w = \"wide_test\" wide\n"
        "    $n = \"NoCaseTest\" nocase\n"
        "    $f = \"full_word\" fullword\n"
        "  condition:\n"
        "    $w or $n or $f\n"
        "}\n"
        "rule fuzz_rule_filesize {\n"
        "  condition:\n"
        "    filesize > 0 and filesize < 1000000\n"
        "}\n"
        "rule fuzz_rule_combo {\n"
        "  strings:\n"
        "    $c1 = \"combo1\"\n"
        "    $c2 = { DE AD BE EF }\n"
        "  condition:\n"
        "    all of them or filesize > 10\n"
        "}\n";

    int result = yr_compiler_add_string(compiler, rule_source, nullptr);

    if (result != 0)
    {
        yr_compiler_destroy(compiler);
        g_init_failed = true;
        return;
    }

    if (yr_compiler_get_rules(compiler, &g_rules) != ERROR_SUCCESS)
    {
        yr_compiler_destroy(compiler);
        g_init_failed = true;
        return;
    }

    yr_compiler_destroy(compiler);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{
    init_yara_rules();

    if (g_init_failed || g_rules == nullptr)
        return 0;

    if (Size == 0)
    {

        yr_rules_scan_mem(
            g_rules,
            Data,
            0,
            0,
            scan_callback,
            nullptr,
            0);
        return 0;
    }

    uint8_t selector = Data[0];
    int flags = 0;

    if (selector & 0x01)
        flags |= SCAN_FLAGS_FAST_MODE;
    if (selector & 0x02)
        flags |= SCAN_FLAGS_PROCESS_MEMORY;
    if (selector & 0x04)
        flags |= SCAN_FLAGS_REPORT_RULES_MATCHING;
    if (selector & 0x08)
        flags |= SCAN_FLAGS_REPORT_RULES_NOT_MATCHING;

    int timeout = (selector >> 4) & 0x0F;

    const uint8_t* buffer = Data + 1;
    size_t buffer_size = Size - 1;

    yr_rules_scan_mem(
        g_rules,
        buffer,
        buffer_size,
        flags,
        scan_callback,
        nullptr,
        timeout);

    yr_rules_scan_mem(
        g_rules,
        Data,
        Size,
        0,
        scan_callback,
        nullptr,
        0);

    return 0;
}