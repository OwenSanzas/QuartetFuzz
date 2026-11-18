#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <yara.h>
#include <yara/rules.h>

#include <yara/pe.h>
#include <yara/pe_utils.h>
#include <yara/elf.h>
#include <yara/dex.h>
#include <yara/macho.h>
#include <yara/dotnet.h>

static const char* kRuleSource =
    "import \"pe\"\n"
    "import \"elf\"\n"
    "import \"math\"\n"
    "import \"time\"\n"
    "import \"console\"\n"
    "import \"string\"\n"
    "import \"dotnet\"\n"
    "import \"dex\"\n"
    "import \"macho\"\n"

    "rule strings_rule {"
    "  strings:"
    "    $a = \"test\" nocase wide ascii"
    "    $b = { 90 90 90 ?? 90 }"
    "    $c = /a.*b/"
    "    $d = \"xorme\" xor"
    "    $e = \"b64me\" base64"
    "    $f = \"wideme\" wide"
    "    $g = \"fullme\" fullword"
    "  condition:"
    "    (any of them) or"
    "    (for any i in (1..#a): (@a[i] > 0))"
    "}"

    "rule pe_rule {"
    "  condition:"
    "    pe.is_pe and ("
    "      pe.number_of_sections > 0 or"
    "      pe.exports(\"a\") or"
    "      pe.exports(/a/) or"
    "      pe.imports(\"a.dll\") or"
    "      pe.imports(\"a.dll\", \"b\") or"
    "      pe.imports(/a/i, /b/i) or"
    "      pe.is_32bit() or"
    "      pe.is_64bit() or"
    "      pe.language(1033) or"
    "      pe.locale(1033) or"
    "      pe.rva_to_offset(0) >= 0 or"
    "      pe.calculate_checksum() > 0 or"
    "      pe.rich_signature.toolid(1) >= 0 or"
    "      pe.section_index(\"a\") >= 0 or"
    "      pe.section_index(0) >= 0"
    "    )"
    "}"

    "rule pdb_ops_rule {"
    "  condition:"
    "    pe.is_pe and ("
    "      pe.pdb_path matches /test\\.pdb/i or"
    "      pe.pdb_path contains \"test\" or"
    "      pe.pdb_path icontains \"TEST\" or"
    "      pe.pdb_path startswith \"test\" or"
    "      pe.pdb_path istartswith \"TEST\" or"
    "      pe.pdb_path endswith \".pdb\" or"
    "      pe.pdb_path iequals \"TEST.PDB\" or"
    "      pe.pdb_path > \"\" or"
    "      pe.pdb_path >= pe.pdb_path or"
    "      pe.pdb_path < \"zzzzzzzz\" or"
    "      pe.pdb_path <= pe.pdb_path or"
    "      pe.pdb_path == pe.pdb_path or"
    "      pe.pdb_path != \"\""
    "    )"
    "}"

    "rule version_rule {"
    "  condition:"
    "    pe.is_pe and ("
    "      pe.number_of_resources > 0 or"
    "      pe.resources[0].type == pe.RESOURCE_TYPE_VERSION or"
    "      pe.version_info[\"a\"] != \"\""
    "    )"
    "}"

    "rule delay_import_rule {"
    "  condition:"
    "    pe.is_pe and ("
    "      pe.number_of_delayed_imports > 0 or"
    "      pe.number_of_delayed_imported_functions > 0"
    "    )"
    "}"

    "rule elf_rule {"
    "  condition:"
    "    (elf.type == elf.ET_EXEC or elf.type == elf.ET_DYN) or"
    "    elf.telfhash() != \"\""
    "}"

    "rule dotnet_rule {"
    "  condition:"
    "    dotnet.is_dotnet and ("
    "      dotnet.number_of_streams > 0 or"
    "      dotnet.number_of_resources > 0 or"
    "      dotnet.number_of_guids > 0 or"
    "      dotnet.number_of_user_strings > 0"
    "    )"
    "}"

    "rule dex_rule {"
    "  condition:"
    "    dex.header.file_size > 0 or"
    "    dex.has_class(\"a\") or"
    "    dex.has_method(\"a\")"
    "}"

    "rule macho_rule {"
    "  condition:"
    "    macho.ncmds > 0 or"
    "    macho.file_index_for_arch(0) >= 0 or"
    "    macho.entry_point_for_arch(0) >= 0 or"
    "    macho.entry_point >= 0"
    "}"

    "rule math_rule {"
    "  condition:"
    "    math.entropy(0, 4096) >= 0.0 or"
    "    math.in_range(math.mean(0, 4096), 0.0, 255.0) or"
    "    math.deviation(0, 4096, math.mean(0, 4096)) >= 0.0 or"
    "    math.monte_carlo_pi(0, 4096) >= 0.0 or"
    "    math.serial_correlation(0, 4096) >= -1.0 or"
    "    math.mode(0, 4096) >= 0 or"
    "    math.count(0) >= 0 or"
    "    math.percentage(0) >= 0.0"
    "}"

    "rule misc_rule {"
    "  condition:"
    "    time.now() > 0 and"
    "    string.length(\"test\") > 0 and"
    "    string.to_int(\"123\") > 0 and"
    "    console.log(\"x\") >= 0"
    "}";

static const char* kExtraRuleSource =
    "import \"pe\"\n"

    "global rule global_gate { condition: pe.is_pe }"
    "private rule priv_helper { condition: true }"
    "rule depends_on_other { condition: priv_helper }"

    "rule bitwise_rule {"
    "  condition:"
    "    (((filesize & 0xff) | 0x1) ^ 0x2) >= 0 or"
    "    (filesize << 1) >= 0 or"
    "    (filesize >> 1) >= 0 or"
    "    (filesize % 7) >= 0 or"
    "    (~filesize) < 0"
    "}"

    "rule percent_rule {"
    "  strings:"
    "    $x = \"a\""
    "    $y = \"b\""
    "    $z = \"c\""
    "  condition:"
    "    50% of them or"
    "    (any of them at 0) or"
    "    ($x at 0) or"
    "    ($x in (0..10))"
    "}"

    "rule ext_rule {"
    "  condition:"
    "    ext_int > 0 or"
    "    ext_bool or"
    "    ext_float >= 0.0 or"
    "    ext_str matches /a/ or"
    "    ext_str != \"\""
    "}";

static YR_RULES* g_rules = nullptr;

static int scan_callback(
    YR_SCAN_CONTEXT* context,
    int message,
    void* message_data,
    void* user_data)
{
  (void) context;
  (void) message_data;

  if (message == CALLBACK_MSG_TOO_SLOW_SCANNING)
  {

    uint8_t choice =
        (user_data != nullptr) ? *reinterpret_cast<uint8_t*>(user_data) : 0;
    if ((choice & 0x0F) == 0)
      return CALLBACK_CONTINUE;
    return CALLBACK_ABORT;
  }

  if (user_data != nullptr &&
      (message == CALLBACK_MSG_RULE_MATCHING ||
       message == CALLBACK_MSG_RULE_NOT_MATCHING))
  {
    uint8_t choice = *reinterpret_cast<uint8_t*>(user_data);
    uint8_t selector = choice & 0x3;

    if (selector == 1)
      return CALLBACK_ABORT;
    if (selector == 2)
      return CALLBACK_ERROR;
  }

  return CALLBACK_CONTINUE;
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
  (void) argc;
  (void) argv;

  if (yr_initialize() != ERROR_SUCCESS)
    return 0;

  YR_COMPILER* compiler = nullptr;

  if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
    return 0;

  yr_compiler_define_integer_variable(compiler, "ext_int", 0);
  yr_compiler_define_boolean_variable(compiler, "ext_bool", 0);
  yr_compiler_define_float_variable(compiler, "ext_float", 0.0);
  yr_compiler_define_string_variable(compiler, "ext_str", "");

  int errors = yr_compiler_add_string(compiler, kRuleSource, nullptr);
  errors += yr_compiler_add_string(compiler, kExtraRuleSource, "extra");

  if (errors == 0)
  {

    yr_compiler_get_rules(compiler, &g_rules);
  }

  yr_compiler_destroy(compiler);

  return 0;
}

template <typename T>
static void AppendPOD(std::vector<uint8_t>& buf, const T& v)
{
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
  buf.insert(buf.end(), p, p + sizeof(T));
}

static void AppendBytes(std::vector<uint8_t>& buf, const void* data, size_t len)
{
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
  buf.insert(buf.end(), p, p + len);
}

static std::vector<uint8_t> BuildMinimalPe(FuzzedDataProvider& fdp, bool is64)
{

  const int kRichVersionCount = 2;
  std::vector<uint8_t> rich_bytes;

  uint32_t rich_key = fdp.ConsumeIntegral<uint32_t>() | 1u;

  AppendPOD(rich_bytes, (uint32_t) (0x536e6144u ^ rich_key));
  AppendPOD(rich_bytes, (uint32_t) (0u ^ rich_key));
  AppendPOD(rich_bytes, (uint32_t) (0u ^ rich_key));
  AppendPOD(rich_bytes, (uint32_t) (0u ^ rich_key));

  for (int i = 0; i < kRichVersionCount; i++)
  {
    uint32_t id_version = fdp.ConsumeIntegral<uint32_t>();
    uint32_t times = fdp.ConsumeIntegral<uint32_t>();
    AppendPOD(rich_bytes, (uint32_t) (id_version ^ rich_key));
    AppendPOD(rich_bytes, (uint32_t) (times ^ rich_key));
  }

  AppendPOD(rich_bytes, (uint32_t) 0x68636952u);
  AppendPOD(rich_bytes, (uint32_t) rich_key);

  const DWORD rich_size = (DWORD) rich_bytes.size();

  const DWORD header_total = (DWORD) (
      sizeof(IMAGE_DOS_HEADER) + rich_size + sizeof(DWORD) +
      sizeof(IMAGE_FILE_HEADER) +
      (is64 ? sizeof(IMAGE_OPTIONAL_HEADER64) : sizeof(IMAGE_OPTIONAL_HEADER32)) +
      sizeof(IMAGE_SECTION_HEADER));

  auto rva_of = [&](size_t body_off) -> DWORD {
    return header_total + (DWORD) body_off;
  };

  std::vector<uint8_t> body;

  size_t imp_desc_off = body.size();
  body.resize(body.size() + sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2, 0);

  size_t thunk_entry_size = is64 ? 8 : 4;
  size_t thunk_off = body.size();
  body.resize(body.size() + thunk_entry_size * 2, 0);

  size_t ibn_off = body.size();
  body.push_back(0);
  body.push_back(0);
  body.push_back('a');
  body.push_back(0);

  size_t dllname_off = body.size();
  static const char kDllName[] = "a.dll";
  AppendBytes(body, kDllName, sizeof(kDllName));

  {
    IMAGE_IMPORT_DESCRIPTOR desc;
    memset(&desc, 0, sizeof(desc));
    desc.OriginalFirstThunk = rva_of(thunk_off);
    desc.TimeDateStamp = 0;
    desc.ForwarderChain = 0;
    desc.Name = rva_of(dllname_off);
    desc.FirstThunk = rva_of(thunk_off);
    memcpy(&body[imp_desc_off], &desc, sizeof(desc));
  }

  if (is64)
  {
    uint64_t v = rva_of(ibn_off);
    memcpy(&body[thunk_off], &v, 8);
  }
  else
  {
    uint32_t v = rva_of(ibn_off);
    memcpy(&body[thunk_off], &v, 4);
  }

  DWORD import_rva = rva_of(imp_desc_off);
  DWORD import_size = (DWORD) (sizeof(IMAGE_IMPORT_DESCRIPTOR) * 2);

  size_t exp_dir_off = body.size();
  body.resize(body.size() + sizeof(IMAGE_EXPORT_DIRECTORY), 0);

  size_t func_arr_off = body.size();
  body.resize(body.size() + 4, 0);

  size_t names_arr_off = body.size();
  body.resize(body.size() + 4, 0);

  size_t ords_arr_off = body.size();
  body.resize(body.size() + 2, 0);

  size_t exp_func_name_off = body.size();
  body.push_back('a');
  body.push_back(0);

  size_t exp_dll_name_off = body.size();
  body.push_back('a');
  body.push_back(0);

  {
    IMAGE_EXPORT_DIRECTORY exp;
    memset(&exp, 0, sizeof(exp));
    exp.Name = rva_of(exp_dll_name_off);
    exp.Base = 1;
    exp.NumberOfFunctions = 1;
    exp.NumberOfNames = 1;
    exp.AddressOfFunctions = rva_of(func_arr_off);
    exp.AddressOfNames = rva_of(names_arr_off);
    exp.AddressOfNameOrdinals = rva_of(ords_arr_off);
    memcpy(&body[exp_dir_off], &exp, sizeof(exp));
  }
  {
    uint32_t v = rva_of(exp_dir_off);
    memcpy(&body[func_arr_off], &v, 4);
    uint32_t v2 = rva_of(exp_func_name_off);
    memcpy(&body[names_arr_off], &v2, 4);
    uint16_t v3 = 0;
    memcpy(&body[ords_arr_off], &v3, 2);
  }

  DWORD export_rva = rva_of(exp_dir_off);
  DWORD export_size = (DWORD) (body.size() - exp_dir_off);

  size_t cli_off = body.size();
  body.resize(body.size() + sizeof(CLI_HEADER), 0);

  size_t metadata_off = body.size();
  body.resize(body.size() + sizeof(NET_METADATA), 0);

  size_t version_off = body.size();
  static const char kVersion[4] = {'0', 0, 0, 0};
  AppendBytes(body, kVersion, sizeof(kVersion));

  size_t flags_off = body.size();
  body.resize(body.size() + 4, 0);
  body[flags_off + 2] = 2;

  size_t guid_header_off = body.size();
  body.resize(body.size() + 8, 0);
  static const char kGuidName[8] = {'#', 'G', 'U', 'I', 'D', 0, 0, 0};
  AppendBytes(body, kGuidName, sizeof(kGuidName));

  size_t us_header_off = body.size();
  body.resize(body.size() + 8, 0);
  static const char kUsName[4] = {'#', 'U', 'S', 0};
  AppendBytes(body, kUsName, sizeof(kUsName));

  size_t guid_data_off = body.size();
  static const uint8_t kGuidData[16] = {
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  AppendBytes(body, kGuidData, sizeof(kGuidData));

  size_t us_data_off = body.size();
  static const uint8_t kUsData[] = {
      0x00,
      0x05,
      'a', 0x00, 'b', 0x00,
      0x01};
  AppendBytes(body, kUsData, sizeof(kUsData));

  {
    uint32_t v = (uint32_t) (guid_data_off - metadata_off);
    memcpy(&body[guid_header_off], &v, 4);
    v = (uint32_t) sizeof(kGuidData);
    memcpy(&body[guid_header_off + 4], &v, 4);

    v = (uint32_t) (us_data_off - metadata_off);
    memcpy(&body[us_header_off], &v, 4);
    v = (uint32_t) sizeof(kUsData);
    memcpy(&body[us_header_off + 4], &v, 4);
  }

  {
    CLI_HEADER cli;
    memset(&cli, 0, sizeof(cli));
    cli.Size = sizeof(CLI_HEADER);
    cli.MajorRuntimeVersion = 2;
    cli.MinorRuntimeVersion = 0;
    cli.MetaData.VirtualAddress = rva_of(metadata_off);
    cli.MetaData.Size = (DWORD) (sizeof(NET_METADATA) + sizeof(kVersion));
    memcpy(&body[cli_off], &cli, sizeof(cli));
  }
  {
    NET_METADATA md;
    memset(&md, 0, sizeof(md));
    md.Magic = NET_METADATA_MAGIC;
    md.MajorVersion = 1;
    md.MinorVersion = 1;
    md.Reserved = 0;
    md.Length = (DWORD) sizeof(kVersion);
    memcpy(&body[metadata_off], &md, sizeof(md));
  }

  DWORD com_rva = rva_of(cli_off);
  DWORD com_size = sizeof(CLI_HEADER);
  (void) version_off;

  size_t debug_dir_off = body.size();
  body.resize(body.size() + sizeof(IMAGE_DEBUG_DIRECTORY), 0);

  size_t cv_record_off = body.size();
  static const char kPdbPath[] = "test.pdb";

  size_t cv_record_size = 4 + 16 + 4 + sizeof(kPdbPath);
  body.resize(body.size() + cv_record_size, 0);
  {
    uint32_t sig = 0x53445352u;
    memcpy(&body[cv_record_off], &sig, 4);
    memcpy(&body[cv_record_off + 24], kPdbPath, sizeof(kPdbPath));
  }
  {
    IMAGE_DEBUG_DIRECTORY dbg;
    memset(&dbg, 0, sizeof(dbg));
    dbg.Characteristics = 0;
    dbg.TimeDateStamp = fdp.ConsumeIntegral<uint32_t>();
    dbg.MajorVersion = 0;
    dbg.MinorVersion = 0;
    dbg.Type = IMAGE_DEBUG_TYPE_CODEVIEW;
    dbg.SizeOfData = (DWORD) cv_record_size;
    dbg.AddressOfRawData = rva_of(cv_record_off);
    dbg.PointerToRawData = header_total + (DWORD) cv_record_off;
    memcpy(&body[debug_dir_off], &dbg, sizeof(dbg));
  }

  DWORD debug_rva = rva_of(debug_dir_off);
  DWORD debug_size = (DWORD) sizeof(IMAGE_DEBUG_DIRECTORY);

  size_t delay_desc_off = body.size();

  body.resize(body.size() + sizeof(IMAGE_DELAYLOAD_DESCRIPTOR) * 2, 0);

  size_t delay_name_table_off = body.size();
  body.resize(body.size() + thunk_entry_size * 2, 0);

  size_t delay_iat_table_off = body.size();
  body.resize(body.size() + thunk_entry_size * 2, 0);

  size_t delay_ibn_off = body.size();
  body.push_back(0);
  body.push_back(0);
  body.push_back('a');
  body.push_back(0);

  size_t delay_dllname_off = body.size();
  static const char kDelayDllName[] = "d.dll";
  AppendBytes(body, kDelayDllName, sizeof(kDelayDllName));

  {
    IMAGE_DELAYLOAD_DESCRIPTOR desc;
    memset(&desc, 0, sizeof(desc));
    desc.Attributes.AllAttributes = 1;
    desc.DllNameRVA = rva_of(delay_dllname_off);
    desc.ModuleHandleRVA = 0;
    desc.ImportAddressTableRVA = rva_of(delay_iat_table_off);
    desc.ImportNameTableRVA = rva_of(delay_name_table_off);
    desc.BoundImportAddressTableRVA = 0;
    desc.UnloadInformationTableRVA = 0;
    desc.TimeDateStamp = 0;
    memcpy(&body[delay_desc_off], &desc, sizeof(desc));
  }

  if (is64)
  {
    uint64_t v = rva_of(delay_ibn_off);
    memcpy(&body[delay_name_table_off], &v, 8);
    uint64_t fv = 1;
    memcpy(&body[delay_iat_table_off], &fv, 8);
  }
  else
  {
    uint32_t v = rva_of(delay_ibn_off);
    memcpy(&body[delay_name_table_off], &v, 4);
    uint32_t fv = 1;
    memcpy(&body[delay_iat_table_off], &fv, 4);
  }

  DWORD delay_import_rva = rva_of(delay_desc_off);
  DWORD delay_import_size = (DWORD) sizeof(IMAGE_DELAYLOAD_DESCRIPTOR);

  size_t rsrc_level0_off = body.size();
  body.resize(
      body.size() + sizeof(IMAGE_RESOURCE_DIRECTORY) +
          sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY),
      0);

  size_t rsrc_level1_off = body.size();
  body.resize(
      body.size() + sizeof(IMAGE_RESOURCE_DIRECTORY) +
          sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY),
      0);

  size_t rsrc_level2_off = body.size();
  body.resize(
      body.size() + sizeof(IMAGE_RESOURCE_DIRECTORY) +
          sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY),
      0);

  size_t rsrc_data_entry_off = body.size();
  body.resize(body.size() + sizeof(IMAGE_RESOURCE_DATA_ENTRY), 0);

  size_t version_info_off = body.size();

  std::vector<uint8_t> vi(200, 0);

  auto put_u16 = [&](size_t off, uint16_t v) {
    vi[off] = (uint8_t) (v & 0xFF);
    vi[off + 1] = (uint8_t) (v >> 8);
  };
  auto put_wstr = [&](size_t off, const char* s) {
    size_t i = 0;
    for (; s[i] != '\0'; i++)
    {
      vi[off + i * 2] = (uint8_t) s[i];
      vi[off + i * 2 + 1] = 0;
    }
    vi[off + i * 2] = 0;
    vi[off + i * 2 + 1] = 0;
  };

  put_u16(0, 200);
  put_u16(2, 0);
  put_u16(4, 0);
  put_wstr(6, "VS_VERSION_INFO");

  const size_t kStringFileInfoOff = 92;
  put_u16(kStringFileInfoOff, 100);
  put_u16(kStringFileInfoOff + 2, 0);
  put_u16(kStringFileInfoOff + 4, 1);
  put_wstr(kStringFileInfoOff + 6, "StringFileInfo");

  const size_t kStringTableOff = kStringFileInfoOff + 36;
  put_u16(kStringTableOff, 38);
  put_u16(kStringTableOff + 2, 0);
  put_u16(kStringTableOff + 4, 1);
  put_wstr(kStringTableOff + 6, "040904B0");

  const size_t kStringOff = kStringTableOff + 24;
  put_u16(kStringOff, 14);
  put_u16(kStringOff + 2, 2);
  put_u16(kStringOff + 4, 1);
  put_wstr(kStringOff + 6, "a");
  put_wstr(kStringOff + 10, "b");

  AppendBytes(body, vi.data(), vi.size());

  DWORD resource_rva = rva_of(rsrc_level0_off);
  DWORD resource_size = (DWORD) (body.size() - rsrc_level0_off);

  {
    IMAGE_RESOURCE_DATA_ENTRY data_entry;
    memset(&data_entry, 0, sizeof(data_entry));
    data_entry.OffsetToData = rva_of(version_info_off);
    data_entry.Size = (DWORD) vi.size();
    data_entry.CodePage = 0;
    data_entry.Reserved = 0;
    memcpy(&body[rsrc_data_entry_off], &data_entry, sizeof(data_entry));
  }
  {
    IMAGE_RESOURCE_DIRECTORY dir2;
    memset(&dir2, 0, sizeof(dir2));
    dir2.NumberOfIdEntries = 1;
    memcpy(&body[rsrc_level2_off], &dir2, sizeof(dir2));

    IMAGE_RESOURCE_DIRECTORY_ENTRY entry2;
    entry2.Name = 1033;
    entry2.OffsetToData = (DWORD) (rsrc_data_entry_off - rsrc_level0_off);
    memcpy(
        &body[rsrc_level2_off + sizeof(IMAGE_RESOURCE_DIRECTORY)],
        &entry2,
        sizeof(entry2));
  }
  {
    IMAGE_RESOURCE_DIRECTORY dir1;
    memset(&dir1, 0, sizeof(dir1));
    dir1.NumberOfIdEntries = 1;
    memcpy(&body[rsrc_level1_off], &dir1, sizeof(dir1));

    IMAGE_RESOURCE_DIRECTORY_ENTRY entry1;
    entry1.Name = 1;
    entry1.OffsetToData =
        (DWORD) ((rsrc_level2_off - rsrc_level0_off) | 0x80000000u);
    memcpy(
        &body[rsrc_level1_off + sizeof(IMAGE_RESOURCE_DIRECTORY)],
        &entry1,
        sizeof(entry1));
  }
  {
    IMAGE_RESOURCE_DIRECTORY dir0;
    memset(&dir0, 0, sizeof(dir0));
    dir0.NumberOfIdEntries = 1;
    memcpy(&body[rsrc_level0_off], &dir0, sizeof(dir0));

    IMAGE_RESOURCE_DIRECTORY_ENTRY entry0;
    entry0.Name = 16;
    entry0.OffsetToData =
        (DWORD) ((rsrc_level1_off - rsrc_level0_off) | 0x80000000u);
    memcpy(
        &body[rsrc_level0_off + sizeof(IMAGE_RESOURCE_DIRECTORY)],
        &entry0,
        sizeof(entry0));
  }

  std::vector<uint8_t> tail = fdp.ConsumeRemainingBytes<uint8_t>();
  body.insert(body.end(), tail.begin(), tail.end());

  if (body.empty())
    body.resize(1, 0);

  std::vector<uint8_t> buf;
  buf.reserve(header_total + body.size());

  IMAGE_DOS_HEADER dos;
  memset(&dos, 0, sizeof(dos));
  dos.e_magic = IMAGE_DOS_SIGNATURE;
  dos.e_lfanew = (LONG) (sizeof(IMAGE_DOS_HEADER) + rich_size);
  AppendPOD(buf, dos);

  buf.insert(buf.end(), rich_bytes.begin(), rich_bytes.end());

  DWORD sig = IMAGE_NT_SIGNATURE;
  AppendPOD(buf, sig);

  IMAGE_FILE_HEADER file_header;
  memset(&file_header, 0, sizeof(file_header));
  file_header.Machine = fdp.ConsumeIntegral<uint16_t>();
  file_header.NumberOfSections = 1;
  file_header.TimeDateStamp = fdp.ConsumeIntegral<uint32_t>();
  file_header.SizeOfOptionalHeader = (WORD) (
      is64 ? sizeof(IMAGE_OPTIONAL_HEADER64) : sizeof(IMAGE_OPTIONAL_HEADER32));
  file_header.Characteristics = fdp.ConsumeIntegral<uint16_t>();
  AppendPOD(buf, file_header);

  if (is64)
  {
    IMAGE_OPTIONAL_HEADER64 oh;
    memset(&oh, 0, sizeof(oh));
    oh.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    oh.AddressOfEntryPoint = fdp.ConsumeIntegral<uint32_t>();
    oh.ImageBase = fdp.ConsumeIntegral<uint64_t>();
    oh.SectionAlignment = 1;
    oh.FileAlignment = 1;
    oh.SizeOfImage = header_total + (DWORD) body.size();
    oh.SizeOfHeaders = header_total;
    oh.Subsystem = fdp.ConsumeIntegral<uint16_t>();
    oh.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = import_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = import_size;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = export_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = export_size;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress = com_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].Size = com_size;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress = debug_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size = debug_size;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress =
        delay_import_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size =
        delay_import_size;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress =
        resource_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].Size = resource_size;
    AppendPOD(buf, oh);
  }
  else
  {
    IMAGE_OPTIONAL_HEADER32 oh;
    memset(&oh, 0, sizeof(oh));
    oh.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    oh.AddressOfEntryPoint = fdp.ConsumeIntegral<uint32_t>();
    oh.ImageBase = fdp.ConsumeIntegral<uint32_t>();
    oh.SectionAlignment = 1;
    oh.FileAlignment = 1;
    oh.SizeOfImage = header_total + (DWORD) body.size();
    oh.SizeOfHeaders = header_total;
    oh.Subsystem = fdp.ConsumeIntegral<uint16_t>();
    oh.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = import_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = import_size;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = export_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = export_size;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress = com_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].Size = com_size;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress = debug_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size = debug_size;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress =
        delay_import_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size =
        delay_import_size;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress =
        resource_rva;
    oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].Size = resource_size;
    AppendPOD(buf, oh);
  }

  IMAGE_SECTION_HEADER section;
  memset(&section, 0, sizeof(section));
  memcpy(section.Name, ".body\0\0", 8);
  section.VirtualAddress = header_total;
  section.PointerToRawData = header_total;
  section.Misc.VirtualSize = (DWORD) body.size();
  section.SizeOfRawData = (DWORD) body.size();
  AppendPOD(buf, section);

  buf.insert(buf.end(), body.begin(), body.end());
  return buf;
}

static std::vector<uint8_t> BuildMinimalElf32(FuzzedDataProvider& fdp)
{
  std::vector<uint8_t> buf(sizeof(elf32_header_t), 0);

  size_t sh_off = buf.size();
  buf.resize(buf.size() + 5 * sizeof(elf32_section_header_t), 0);

  size_t ph_off = buf.size();
  buf.resize(buf.size() + sizeof(elf32_program_header_t), 0);

  size_t symtab_off = buf.size();
  buf.resize(buf.size() + sizeof(elf32_sym_t), 0);

  size_t strtab_off = buf.size();
  static const char kStrtab[] = "\0sym";
  AppendBytes(buf, kStrtab, sizeof(kStrtab));

  size_t dynsym_off = buf.size();
  buf.resize(buf.size() + sizeof(elf32_sym_t), 0);

  size_t dynstr_off = buf.size();
  static const char kDynstr[] = "\0dyn";
  AppendBytes(buf, kDynstr, sizeof(kDynstr));

  size_t dynamic_off = buf.size();
  buf.resize(buf.size() + 2 * sizeof(elf32_dyn_t), 0);
  {
    elf32_dyn_t d0;
    d0.tag = ELF_DT_NEEDED;
    d0.val = 0;
    elf32_dyn_t d1;
    d1.tag = ELF_DT_NULL;
    d1.val = 0;
    memcpy(&buf[dynamic_off], &d0, sizeof(d0));
    memcpy(&buf[dynamic_off + sizeof(elf32_dyn_t)], &d1, sizeof(d1));
  }

  {
    elf32_program_header_t ph;
    memset(&ph, 0, sizeof(ph));
    ph.type = ELF_PT_DYNAMIC;
    ph.offset = (elf32_off_t) dynamic_off;
    ph.virt_addr = 0;
    ph.phys_addr = 0;
    ph.file_size = (elf32_word_t) (2 * sizeof(elf32_dyn_t));
    ph.mem_size = 0x1000;
    ph.flags = 0;
    ph.alignment = 0;
    memcpy(&buf[ph_off], &ph, sizeof(ph));
  }

  {
    elf32_sym_t s;
    memset(&s, 0, sizeof(s));
    s.name = 1;
    s.value = 0;
    s.size = 0;
    s.info = 0x12;
    s.other = 0;
    s.shndx = 0;
    memcpy(&buf[symtab_off], &s, sizeof(s));
    memcpy(&buf[dynsym_off], &s, sizeof(s));
  }

  {
    elf32_section_header_t sections[5];
    memset(sections, 0, sizeof(sections));
    sections[1].type = ELF_SHT_SYMTAB;
    sections[1].offset = (elf32_off_t) symtab_off;
    sections[1].size = (elf32_word_t) sizeof(elf32_sym_t);
    sections[1].link = 2;
    sections[1].entry_size = (elf32_word_t) sizeof(elf32_sym_t);

    sections[2].type = ELF_SHT_STRTAB;
    sections[2].offset = (elf32_off_t) strtab_off;
    sections[2].size = (elf32_word_t) sizeof(kStrtab);

    sections[3].type = ELF_SHT_DYNSYM;
    sections[3].offset = (elf32_off_t) dynsym_off;
    sections[3].size = (elf32_word_t) sizeof(elf32_sym_t);
    sections[3].link = 4;
    sections[3].entry_size = (elf32_word_t) sizeof(elf32_sym_t);

    sections[4].type = ELF_SHT_STRTAB;
    sections[4].offset = (elf32_off_t) dynstr_off;
    sections[4].size = (elf32_word_t) sizeof(kDynstr);

    memcpy(&buf[sh_off], sections, sizeof(sections));
  }

  {
    elf32_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.ident.magic = ELF_MAGIC;
    hdr.ident._class = ELF_CLASS_32;
    hdr.ident.data = ELF_DATA_2LSB;
    hdr.ident.version = 1;
    hdr.ident.nident = 0;
    hdr.type = ELF_ET_EXEC;
    hdr.machine = fdp.ConsumeIntegral<uint16_t>();
    hdr.version = 1;
    hdr.entry = 1;
    hdr.ph_offset = (elf32_off_t) ph_off;
    hdr.sh_offset = (elf32_off_t) sh_off;
    hdr.flags = 0;
    hdr.header_size = (elf32_half_t) sizeof(elf32_header_t);
    hdr.ph_entry_size = (elf32_half_t) sizeof(elf32_program_header_t);
    hdr.ph_entry_count = 1;
    hdr.sh_entry_size = (elf32_half_t) sizeof(elf32_section_header_t);
    hdr.sh_entry_count = 5;
    hdr.sh_str_table_index = 2;
    memcpy(&buf[0], &hdr, sizeof(hdr));
  }

  std::vector<uint8_t> tail = fdp.ConsumeRemainingBytes<uint8_t>();
  buf.insert(buf.end(), tail.begin(), tail.end());
  return buf;
}

static std::vector<uint8_t> BuildMinimalElf64(FuzzedDataProvider& fdp)
{
  std::vector<uint8_t> buf(sizeof(elf64_header_t), 0);

  size_t sh_off = buf.size();
  buf.resize(buf.size() + 5 * sizeof(elf64_section_header_t), 0);

  size_t ph_off = buf.size();
  buf.resize(buf.size() + sizeof(elf64_program_header_t), 0);

  size_t symtab_off = buf.size();
  buf.resize(buf.size() + sizeof(elf64_sym_t), 0);

  size_t strtab_off = buf.size();
  static const char kStrtab[] = "\0sym";
  AppendBytes(buf, kStrtab, sizeof(kStrtab));

  size_t dynsym_off = buf.size();
  buf.resize(buf.size() + sizeof(elf64_sym_t), 0);

  size_t dynstr_off = buf.size();
  static const char kDynstr[] = "\0dyn";
  AppendBytes(buf, kDynstr, sizeof(kDynstr));

  size_t dynamic_off = buf.size();
  buf.resize(buf.size() + 2 * sizeof(elf64_dyn_t), 0);
  {
    elf64_dyn_t d0;
    d0.tag = ELF_DT_NEEDED;
    d0.val = 0;
    elf64_dyn_t d1;
    d1.tag = ELF_DT_NULL;
    d1.val = 0;
    memcpy(&buf[dynamic_off], &d0, sizeof(d0));
    memcpy(&buf[dynamic_off + sizeof(elf64_dyn_t)], &d1, sizeof(d1));
  }

  {
    elf64_program_header_t ph;
    memset(&ph, 0, sizeof(ph));
    ph.type = ELF_PT_DYNAMIC;
    ph.flags = 0;
    ph.offset = (elf64_off_t) dynamic_off;
    ph.virt_addr = 0;
    ph.phys_addr = 0;
    ph.file_size = (elf64_xword_t) (2 * sizeof(elf64_dyn_t));
    ph.mem_size = 0x1000;
    ph.alignment = 0;
    memcpy(&buf[ph_off], &ph, sizeof(ph));
  }

  {
    elf64_sym_t s;
    memset(&s, 0, sizeof(s));
    s.name = 1;
    s.info = 0x12;
    s.other = 0;
    s.shndx = 0;
    s.value = 0;
    s.size = 0;
    memcpy(&buf[symtab_off], &s, sizeof(s));
    memcpy(&buf[dynsym_off], &s, sizeof(s));
  }

  {
    elf64_section_header_t sections[5];
    memset(sections, 0, sizeof(sections));
    sections[1].type = ELF_SHT_SYMTAB;
    sections[1].offset = (elf64_off_t) symtab_off;
    sections[1].size = (elf64_xword_t) sizeof(elf64_sym_t);
    sections[1].link = 2;
    sections[1].entry_size = (elf64_xword_t) sizeof(elf64_sym_t);

    sections[2].type = ELF_SHT_STRTAB;
    sections[2].offset = (elf64_off_t) strtab_off;
    sections[2].size = (elf64_xword_t) sizeof(kStrtab);

    sections[3].type = ELF_SHT_DYNSYM;
    sections[3].offset = (elf64_off_t) dynsym_off;
    sections[3].size = (elf64_xword_t) sizeof(elf64_sym_t);
    sections[3].link = 4;
    sections[3].entry_size = (elf64_xword_t) sizeof(elf64_sym_t);

    sections[4].type = ELF_SHT_STRTAB;
    sections[4].offset = (elf64_off_t) dynstr_off;
    sections[4].size = (elf64_xword_t) sizeof(kDynstr);

    memcpy(&buf[sh_off], sections, sizeof(sections));
  }

  {
    elf64_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.ident.magic = ELF_MAGIC;
    hdr.ident._class = ELF_CLASS_64;
    hdr.ident.data = ELF_DATA_2LSB;
    hdr.ident.version = 1;
    hdr.ident.nident = 0;
    hdr.type = ELF_ET_EXEC;
    hdr.machine = fdp.ConsumeIntegral<uint16_t>();
    hdr.version = 1;
    hdr.entry = 1;
    hdr.ph_offset = (elf64_off_t) ph_off;
    hdr.sh_offset = (elf64_off_t) sh_off;
    hdr.flags = 0;
    hdr.header_size = (elf64_half_t) sizeof(elf64_header_t);
    hdr.ph_entry_size = (elf64_half_t) sizeof(elf64_program_header_t);
    hdr.ph_entry_count = 1;
    hdr.sh_entry_size = (elf64_half_t) sizeof(elf64_section_header_t);
    hdr.sh_entry_count = 5;
    hdr.sh_str_table_index = 2;
    memcpy(&buf[0], &hdr, sizeof(hdr));
  }

  std::vector<uint8_t> tail = fdp.ConsumeRemainingBytes<uint8_t>();
  buf.insert(buf.end(), tail.begin(), tail.end());
  return buf;
}

static std::vector<uint8_t> BuildMinimalDex(FuzzedDataProvider& fdp)
{
  std::vector<uint8_t> buf(sizeof(dex_header_t), 0);

  size_t string_ids_off = buf.size();
  buf.resize(buf.size() + sizeof(string_id_item_t), 0);

  size_t type_ids_off = buf.size();
  buf.resize(buf.size() + sizeof(type_id_item_t), 0);

  size_t method_ids_off = buf.size();
  buf.resize(buf.size() + sizeof(method_id_item_t), 0);

  size_t class_defs_off = buf.size();
  buf.resize(buf.size() + sizeof(class_id_item_t), 0);

  size_t class_data_off = buf.size();

  static const uint8_t kClassData[] = {
      0x00, 0x00, 0x00, 0x01,
      0x00, 0x00, 0x00,
      0, 0, 0, 0, 0, 0, 0, 0, 0
  };
  AppendBytes(buf, kClassData, sizeof(kClassData));

  size_t string_data_off = buf.size();
  static const uint8_t kStringData[] = {0x01, 'a'};
  AppendBytes(buf, kStringData, sizeof(kStringData));

  {
    string_id_item_t s;
    s.string_data_offset = (uint32_t) string_data_off;
    memcpy(&buf[string_ids_off], &s, sizeof(s));
  }
  {
    type_id_item_t t;
    t.descriptor_idx = 0;
    memcpy(&buf[type_ids_off], &t, sizeof(t));
  }
  {
    method_id_item_t m;
    m.class_idx = 0;
    m.proto_idx = 0;
    m.name_idx = 0;
    memcpy(&buf[method_ids_off], &m, sizeof(m));
  }
  {
    class_id_item_t c;
    memset(&c, 0, sizeof(c));
    c.class_idx = 0;
    c.access_flags = 0;
    c.super_class_idx = 0xFFFFFFFFu;
    c.interfaces_offset = 0;
    c.source_file_idx = 0xFFFFFFFFu;
    c.annotations_offset = 0;
    c.class_data_offset = (uint32_t) class_data_off;
    c.static_values_offset = 0;
    memcpy(&buf[class_defs_off], &c, sizeof(c));
  }
  {
    dex_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "dex\n035\x00", 8);
    hdr.checksum = fdp.ConsumeIntegral<uint32_t>();
    hdr.header_size = (uint32_t) sizeof(dex_header_t);
    hdr.endian_tag = 0x12345678;
    hdr.link_size = 0;
    hdr.link_offset = 0;
    hdr.map_offset = 0;
    hdr.string_ids_size = 1;
    hdr.string_ids_offset = (uint32_t) string_ids_off;
    hdr.type_ids_size = 1;
    hdr.type_ids_offset = (uint32_t) type_ids_off;
    hdr.proto_ids_size = 0;
    hdr.proto_ids_offset = 0;
    hdr.field_ids_size = 0;
    hdr.field_ids_offset = 0;
    hdr.method_ids_size = 1;
    hdr.method_ids_offset = (uint32_t) method_ids_off;
    hdr.class_defs_size = 1;
    hdr.class_defs_offset = (uint32_t) class_defs_off;
    hdr.data_size = 0;
    hdr.data_offset = 0;
    hdr.file_size = (uint32_t) buf.size();
    memcpy(&buf[0], &hdr, sizeof(hdr));
  }

  std::vector<uint8_t> tail = fdp.ConsumeRemainingBytes<uint8_t>();
  buf.insert(buf.end(), tail.begin(), tail.end());
  return buf;
}

static std::vector<uint8_t> BuildMinimalMachO(FuzzedDataProvider& fdp, bool is64)
{
  std::vector<uint8_t> buf;

  if (is64)
  {
    yr_mach_header_64_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = MH_MAGIC_64;
    hdr.cputype = CPU_TYPE_X86_64;
    hdr.cpusubtype = 0;
    hdr.filetype = MH_EXECUTE;
    hdr.flags = 0;
    hdr.reserved = 0;

    yr_segment_command_64_t seg;
    memset(&seg, 0, sizeof(seg));
    seg.cmd = LC_SEGMENT_64;
    seg.cmdsize = (uint32_t) sizeof(seg);
    seg.vmaddr = fdp.ConsumeIntegral<uint64_t>();
    seg.vmsize = fdp.ConsumeIntegral<uint64_t>();
    seg.fileoff = 0;
    seg.filesize = 0;
    seg.maxprot = 7;
    seg.initprot = 7;
    seg.nsects = 0;
    seg.flags = 0;

    yr_entry_point_command_t ep;
    memset(&ep, 0, sizeof(ep));
    ep.cmd = LC_MAIN;
    ep.cmdsize = (uint32_t) sizeof(ep);
    ep.entryoff = fdp.ConsumeIntegral<uint64_t>();
    ep.stacksize = fdp.ConsumeIntegral<uint64_t>();

    yr_thread_command_t thread_cmd;
    memset(&thread_cmd, 0, sizeof(thread_cmd));
    thread_cmd.cmd = LC_UNIXTHREAD;
    thread_cmd.flavor = 4;
    thread_cmd.count = 0;

    yr_x86_thread_state64_t state64;
    memset(&state64, 0, sizeof(state64));
    state64.rip = fdp.ConsumeIntegral<uint64_t>();

    thread_cmd.cmdsize = (uint32_t) (sizeof(thread_cmd) + sizeof(state64));

    hdr.ncmds = 3;
    hdr.sizeofcmds =
        (uint32_t) (sizeof(seg) + sizeof(ep) + sizeof(thread_cmd) +
                    sizeof(state64));

    AppendPOD(buf, hdr);
    AppendPOD(buf, seg);
    AppendPOD(buf, ep);
    AppendPOD(buf, thread_cmd);
    AppendPOD(buf, state64);
  }
  else
  {
    yr_mach_header_32_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = MH_MAGIC;
    hdr.cputype = CPU_TYPE_I386;
    hdr.cpusubtype = 0;
    hdr.filetype = MH_EXECUTE;
    hdr.flags = 0;

    yr_segment_command_32_t seg;
    memset(&seg, 0, sizeof(seg));
    seg.cmd = LC_SEGMENT;
    seg.cmdsize = (uint32_t) sizeof(seg);
    seg.vmaddr = fdp.ConsumeIntegral<uint32_t>();
    seg.vmsize = fdp.ConsumeIntegral<uint32_t>();
    seg.fileoff = 0;
    seg.filesize = 0;
    seg.maxprot = 7;
    seg.initprot = 7;
    seg.nsects = 0;
    seg.flags = 0;

    yr_thread_command_t thread_cmd32;
    memset(&thread_cmd32, 0, sizeof(thread_cmd32));
    thread_cmd32.cmd = LC_UNIXTHREAD;
    thread_cmd32.flavor = 1;
    thread_cmd32.count = 0;

    yr_x86_thread_state_t state32;
    memset(&state32, 0, sizeof(state32));
    state32.eip = fdp.ConsumeIntegral<uint32_t>();

    thread_cmd32.cmdsize =
        (uint32_t) (sizeof(thread_cmd32) + sizeof(state32));

    hdr.ncmds = 2;
    hdr.sizeofcmds =
        (uint32_t) (sizeof(seg) + sizeof(thread_cmd32) + sizeof(state32));

    AppendPOD(buf, hdr);
    AppendPOD(buf, seg);
    AppendPOD(buf, thread_cmd32);
    AppendPOD(buf, state32);
  }

  if (buf.size() < sizeof(yr_mach_header_64_t))
    buf.resize(sizeof(yr_mach_header_64_t), 0);

  std::vector<uint8_t> tail = fdp.ConsumeRemainingBytes<uint8_t>();
  buf.insert(buf.end(), tail.begin(), tail.end());
  return buf;
}

static std::vector<uint8_t> BuildMinimalMachOFat(FuzzedDataProvider& fdp)
{
  yr_mach_header_64_t thdr;
  memset(&thdr, 0, sizeof(thdr));
  thdr.magic = MH_MAGIC_64;
  thdr.cputype = CPU_TYPE_X86_64;
  thdr.cpusubtype = 0;
  thdr.filetype = MH_EXECUTE;
  thdr.ncmds = 0;
  thdr.sizeofcmds = 0;
  thdr.flags = 0;
  thdr.reserved = 0;

  std::vector<uint8_t> thin;
  AppendPOD(thin, thdr);
  thin.resize(thin.size() + 16, 0);

  std::vector<uint8_t> buf;

  auto push_be32 = [&](std::vector<uint8_t>& v, uint32_t x) {
    uint8_t b[4] = {
        (uint8_t) (x >> 24), (uint8_t) (x >> 16), (uint8_t) (x >> 8),
        (uint8_t) x};
    v.insert(v.end(), b, b + 4);
  };
  auto push_be64 = [&](std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 7; i >= 0; i--) v.push_back((uint8_t) (x >> (i * 8)));
  };

  push_be32(buf, FAT_MAGIC_64);
  push_be32(buf, 1);

  const size_t kArchEntrySize = 32;
  size_t payload_offset = buf.size() + kArchEntrySize;

  push_be32(buf, CPU_TYPE_X86_64);
  push_be32(buf, 0);
  push_be64(buf, (uint64_t) payload_offset);
  push_be64(buf, (uint64_t) thin.size());
  push_be32(buf, 0);
  push_be32(buf, 0);

  buf.insert(buf.end(), thin.begin(), thin.end());

  std::vector<uint8_t> tail = fdp.ConsumeRemainingBytes<uint8_t>();
  buf.insert(buf.end(), tail.begin(), tail.end());
  return buf;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (g_rules == nullptr)
    return 0;

  FuzzedDataProvider stream(data, size);

  int raw_flags = stream.ConsumeIntegral<int>();
  int flags = raw_flags &
      (SCAN_FLAGS_FAST_MODE | SCAN_FLAGS_PROCESS_MEMORY |
       SCAN_FLAGS_NO_TRYCATCH | SCAN_FLAGS_REPORT_RULES_MATCHING |
       SCAN_FLAGS_REPORT_RULES_NOT_MATCHING);

  int timeout = stream.ConsumeIntegralInRange<int>(0, 5);

  uint8_t callback_choice = stream.ConsumeIntegral<uint8_t>();

  int64_t ext_int_value = stream.ConsumeIntegral<int64_t>();
  int ext_bool_value = stream.ConsumeBool() ? 1 : 0;
  double ext_float_value = stream.ConsumeFloatingPoint<double>();
  std::string ext_str_value = stream.ConsumeRandomLengthString(16);

  yr_rules_define_integer_variable(g_rules, "ext_int", ext_int_value);
  yr_rules_define_boolean_variable(g_rules, "ext_bool", ext_bool_value);
  yr_rules_define_float_variable(g_rules, "ext_float", ext_float_value);
  yr_rules_define_string_variable(g_rules, "ext_str", ext_str_value.c_str());

  uint8_t format_selector = (uint8_t) (stream.ConsumeIntegral<uint8_t>() % 9);

  bool tile_buffer = stream.ConsumeIntegralInRange<int>(0, 31) == 0;

  std::vector<uint8_t> buffer;
  switch (format_selector)
  {
  case 1:
    buffer = BuildMinimalPe(stream,  false);
    break;
  case 2:
    buffer = BuildMinimalPe(stream,  true);
    break;
  case 3:
    buffer = BuildMinimalElf32(stream);
    break;
  case 4:
    buffer = BuildMinimalElf64(stream);
    break;
  case 5:
    buffer = BuildMinimalDex(stream);
    break;
  case 6:
    buffer = BuildMinimalMachO(stream,  false);
    break;
  case 7:
    buffer = BuildMinimalMachO(stream,  true);
    break;
  case 8:
    buffer = BuildMinimalMachOFat(stream);
    break;
  default:

    buffer = stream.ConsumeRemainingBytes<uint8_t>();
    break;
  }

  if (tile_buffer && !buffer.empty())
  {
    const size_t kTargetSize = 200100;
    std::vector<uint8_t> small = std::move(buffer);
    buffer.clear();
    buffer.reserve(kTargetSize);

    while (buffer.size() < kTargetSize)
    {
      size_t remaining = kTargetSize - buffer.size();
      size_t chunk = small.size() < remaining ? small.size() : remaining;
      buffer.insert(buffer.end(), small.begin(), small.begin() + chunk);
    }
  }

  yr_rules_scan_mem(
      g_rules,
      buffer.data(),
      buffer.size(),
      flags,
      scan_callback,
      &callback_choice,
      timeout);

  return 0;
}