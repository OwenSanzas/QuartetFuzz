#include <fuzzer/FuzzedDataProvider.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include <yara.h>

static YR_RULES* g_rules = nullptr;

static std::vector<YR_RULES*> g_extra_rules;

static const char kRichRules[] =
    "import \"pe\"\n"
    "import \"elf\"\n"
    "import \"math\"\n"
    "import \"time\"\n"
    "import \"tests\"\n"
    "import \"dotnet\"\n"
    "import \"macho\"\n"
    "import \"dex\"\n"
    "import \"console\"\n"
    "import \"string\"\n"
    "\n"
    "rule pe_rule\n"
    "{\n"
    "    condition:\n"
    "        pe.is_pe and pe.number_of_sections > 0\n"
    "}\n"
    "\n"
    "rule pe_import_export_rule\n"
    "{\n"
    "    condition:\n"
    "        pe.number_of_imports >= 0 or pe.number_of_exports >= 0 or\n"
    "        pe.number_of_imported_functions >= 0 or\n"
    "        pe.rich_signature.length >= 0 or pe.pdb_path != \"\" or\n"
    "        pe.number_of_resources >= 0 or\n"
    "        pe.number_of_version_infos >= 0\n"
    "}\n"
    "\n"
    "rule elf_rule\n"
    "{\n"
    "    condition:\n"
    "        elf.type == elf.ET_EXEC or elf.type == elf.ET_DYN or\n"
    "        elf.number_of_sections >= 0 or\n"
    "        elf.dynamic_section_entries >= 0\n"
    "}\n"
    "\n"
    "rule dotnet_rule\n"
    "{\n"
    "    condition:\n"
    "        dotnet.is_dotnet and dotnet.number_of_streams > 0\n"
    "}\n"
    "\n"
    "rule macho_rule\n"
    "{\n"
    "    condition:\n"
    "        macho.number_of_segments > 0 or macho.entry_point >= 0\n"
    "}\n"
    "\n"
    "rule dex_rule\n"
    "{\n"
    "    condition:\n"
    "        dex.header.magic == dex.DEX_FILE_MAGIC_035 or\n"
    "        dex.header.file_size > 0\n"
    "}\n"
    "\n"
    "rule math_rule\n"
    "{\n"
    "    condition:\n"
    "        math.entropy(0, filesize) >= 0.0 or\n"
    "        math.in_range(1.0, 0.0, 2.0)\n"
    "}\n"
    "\n"
    "rule time_rule\n"
    "{\n"
    "    condition:\n"
    "        time.now() != 0 or time.now() == 0\n"
    "}\n"
    "\n"
    "rule tests_rule\n"
    "{\n"
    "    condition:\n"
    "        tests.isum(1, 2) == 3 or\n"
    "        tests.fsum(1.0, 2.0) > 0.0 or\n"
    "        tests.length(\"abc\") == 3 or\n"
    "        tests.foobar(1) != \"\" or\n"
    "        tests.integer_array[0] >= 0 or\n"
    "        tests.string_array[0] != \"\" or\n"
    "        tests.match(/a.c/, \"abc\") >= 0\n"
    "}\n"
    "\n"
    "rule string_rule\n"
    "{\n"
    "    strings:\n"
    "        $num = \"123\"\n"
    "    condition:\n"
    "        $num or string.to_int(\"123\") == 123 or string.length(\"abc\") == 3\n"
    "}\n"
    "\n"
    "rule console_rule\n"
    "{\n"
    "    condition:\n"
    "        console.log(\"hello\") == 1 or\n"
    "        console.log(\"key\", \"value\") == 1 or\n"
    "        console.log(1234) == 1 or\n"
    "        console.log(\"n\", 1234) == 1 or\n"
    "        console.log(1.5) == 1 or\n"
    "        console.log(\"f\", 1.5) == 1 or\n"
    "        console.hex(255) == 1 or\n"
    "        console.hex(\"h\", 255) == 1\n"
    "}\n"
    "\n"
    "private rule priv_regex_rule\n"
    "{\n"
    "    strings:\n"
    "        $r = /ab+c/ nocase wide ascii\n"
    "    condition:\n"
    "        $r\n"
    "}\n"
    "\n"
    "global rule glob_rule\n"
    "{\n"
    "    condition:\n"
    "        true\n"
    "}\n"
    "\n"
    "rule loop_rule\n"
    "{\n"
    "    strings:\n"
    "        $s1 = \"MZ\"\n"
    "        $s2 = \"PE\"\n"
    "        $h = { 4D 5A ?? ?? }\n"
    "        $x = \"secret\" xor\n"
    "        $b64 = \"flag\" base64\n"
    "    condition:\n"
    "        (for any i in (1..#s1) : (@s1[i] < filesize)) or\n"
    "        any of ($s2,$h,$x,$b64) or\n"
    "        $s1 at 0 or\n"
    "        $s1 in (0..10)\n"
    "}\n"
    "\n"
    "rule ext_rule\n"
    "{\n"
    "    condition:\n"
    "        my_int > 0 or my_bool or my_str contains \"a\"\n"
    "}\n"
    "\n"
    "rule complex_rule\n"
    "{\n"
    "    strings:\n"
    "        $a = \"a\"\n"
    "        $b = { 00 11 22 33 }\n"
    "    condition:\n"
    "        $a or $b or uint8(0) == 0x4D or uint16(0) == 0x5A4D or\n"
    "        uint32be(0) == 0x7F454C46 or filesize > 100\n"
    "}\n";

static const char kBitwiseRules[] =
    "import \"tests\"\n"
    "\n"
    "rule bitwise_rule\n"
    "{\n"
    "    condition:\n"
    "        (filesize & 0xFF) == (filesize & 0xFF) or\n"
    "        (filesize | 1) > 0 or\n"
    "        (filesize ^ 1) >= 0 or\n"
    "        (~filesize) != 0 or\n"
    "        (filesize << 1) >= 0 or\n"
    "        (filesize >> 1) >= 0 or\n"
    "        (filesize % 7) >= 0\n"
    "}\n"
    "\n"
    "rule percent_rule\n"
    "{\n"
    "    strings:\n"
    "        $p1 = \"MZ\"\n"
    "        $p2 = \"PE\"\n"
    "        $p3 = \"ELF\"\n"
    "    condition:\n"
    "        50% of them\n"
    "}\n"
    "\n"
    "rule entrypoint_rule\n"
    "{\n"
    "    condition:\n"
    "        entrypoint >= 0 or\n"
    "        int8(0) == int8(0) or\n"
    "        int16(0) == int16(0) or\n"
    "        int32(0) == int32(0) or\n"
    "        int16be(0) == int16be(0) or\n"
    "        int32be(0) == int32be(0)\n"
    "}\n"
    "\n"
    "rule string_ops_rule\n"
    "{\n"
    "    condition:\n"
    "        \"MZ\" icontains \"mz\" or\n"
    "        \"hello\" startswith \"he\" or\n"
    "        \"hello\" endswith \"lo\" or\n"
    "        \"ABC\" iequals \"abc\" or\n"
    "        \"hello\" istartswith \"HE\" or\n"
    "        \"hello\" iendswith \"LO\"\n"
    "}\n"
    "\n"
    "rule matches_rule\n"
    "{\n"
    "    condition:\n"
    "        \"hello123\" matches /hello[0-9]+/\n"
    "}\n"
    "\n"
    "rule dict_rule\n"
    "{\n"
    "    condition:\n"
    "        (for any k,v in tests.string_dict : (k != \"\")) or\n"
    "        (for all k,v in tests.string_dict : (v != \"\"))\n"
    "}\n";

static const char kPeFuncsRules[] =
    "import \"pe\"\n"
    "\n"
    "rule pe_funcs_rule\n"
    "{\n"
    "    condition:\n"
    "        pe.calculate_checksum() >= 0 or\n"
    "        pe.rich_signature.version(0) >= 0 or\n"
    "        pe.rich_signature.toolid(0) >= 0 or\n"
    "        pe.rich_signature.version(0, 0) >= 0 or\n"
    "        pe.rich_signature.toolid(0, 0) >= 0 or\n"
    "        pe.section_index(\".spec\") >= -1 or\n"
    "        pe.section_index(0) >= -1 or\n"
    "        pe.exports(\"Fn\") or\n"
    "        pe.exports(/F.*/) or\n"
    "        pe.exports(0) or\n"
    "        pe.exports_index(\"Fn\") >= -1 or\n"
    "        pe.exports_index(0) >= -1 or\n"
    "        pe.exports_index(/F.*/) >= -1 or\n"
    "        pe.imports(\"a.dll\", \"Fn\") or\n"
    "        pe.imports(\"a.dll\", 0) or\n"
    "        pe.imports(\"a.dll\") or\n"
    "        pe.imports(/a\\.dll/, /Fn/) or\n"
    "        pe.imports(pe.IMPORT_STANDARD, \"a.dll\", \"Fn\") or\n"
    "        pe.imports(pe.IMPORT_STANDARD, \"a.dll\", 0) or\n"
    "        pe.imports(pe.IMPORT_STANDARD, \"a.dll\") or\n"
    "        pe.imports(pe.IMPORT_STANDARD, /a\\.dll/, /Fn/) or\n"
    "        pe.import_rva(\"a.dll\", \"Fn\") >= -1 or\n"
    "        pe.import_rva(\"a.dll\", 0) >= -1 or\n"
    "        pe.delayed_import_rva(\"a.dll\", \"Fn\") >= -1 or\n"
    "        pe.delayed_import_rva(\"a.dll\", 0) >= -1 or\n"
    "        pe.locale(0) or\n"
    "        pe.language(0) or\n"
    "        pe.is_dll() or\n"
    "        pe.is_32bit() or\n"
    "        pe.is_64bit() or\n"
    "        pe.rva_to_offset(0) >= -1\n"
    "}\n";

static const char kElfFuncsRules[] =
    "import \"elf\"\n"
    "\n"
    "rule elf_funcs_rule\n"
    "{\n"
    "    condition:\n"
    "        elf.telfhash() != \"\" or elf.telfhash() == \"\"\n"
    "}\n";

static const char* kExtraRuleSets[] = {
    kBitwiseRules,
    kPeFuncsRules,
    kElfFuncsRules,
};

static int ScanCallback(
    YR_SCAN_CONTEXT* context,
    int message,
    void* message_data,
    void* user_data)
{
  (void) context;

  uint8_t* ctl = reinterpret_cast<uint8_t*>(user_data);

  switch (message)
  {
    case CALLBACK_MSG_CONSOLE_LOG:
    {

      const char* s = reinterpret_cast<const char*>(message_data);
      if (s != nullptr)
      {
        volatile size_t len = strlen(s);
        (void) len;
      }
      break;
    }
    case CALLBACK_MSG_IMPORT_MODULE:
    case CALLBACK_MSG_MODULE_IMPORTED:
    case CALLBACK_MSG_TOO_MANY_MATCHES:
    case CALLBACK_MSG_TOO_SLOW_SCANNING:
    case CALLBACK_MSG_SCAN_FINISHED:
      (void) message_data;
      break;
    default:
      break;
  }

  if (ctl != nullptr && (*ctl & 1) &&
      (message == CALLBACK_MSG_RULE_MATCHING ||
       message == CALLBACK_MSG_RULE_NOT_MATCHING))
  {
    return CALLBACK_ABORT;
  }

  return CALLBACK_CONTINUE;
}

static void AppendU16(std::vector<uint8_t>& buf, uint16_t v)
{
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
  buf.insert(buf.end(), p, p + sizeof(v));
}

static void AppendU32(std::vector<uint8_t>& buf, uint32_t v)
{
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
  buf.insert(buf.end(), p, p + sizeof(v));
}

static void AppendU64(std::vector<uint8_t>& buf, uint64_t v)
{
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
  buf.insert(buf.end(), p, p + sizeof(v));
}

static void AppendU32BE(std::vector<uint8_t>& buf, uint32_t v)
{
  buf.push_back((uint8_t) ((v >> 24) & 0xFF));
  buf.push_back((uint8_t) ((v >> 16) & 0xFF));
  buf.push_back((uint8_t) ((v >> 8) & 0xFF));
  buf.push_back((uint8_t) (v & 0xFF));
}

static void AppendBytes(std::vector<uint8_t>& buf, const char* s, size_t n)
{
  buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + n);
}

static void PatchU32(std::vector<uint8_t>& buf, size_t offset, uint32_t v)
{
  memcpy(&buf[offset], &v, sizeof(v));
}

static void PatchU16(std::vector<uint8_t>& buf, size_t offset, uint16_t v)
{
  memcpy(&buf[offset], &v, sizeof(v));
}

static std::vector<uint8_t> BuildPeBuffer(FuzzedDataProvider& stream)
{
  std::vector<uint8_t> buf;

  const uint32_t kDosStubSize = 0x80;
  std::vector<uint8_t> dos(kDosStubSize, 0);
  dos[0] = 'M';
  dos[1] = 'Z';
  uint32_t e_lfanew = kDosStubSize;
  memcpy(&dos[0x3C], &e_lfanew, sizeof(e_lfanew));

  uint32_t key = stream.ConsumeIntegral<uint32_t>();
  if (key == 0) key = 0xA5A5A5A5u;
  uint32_t dans_xored = 0x536e6144u ^ key;
  uint32_t pad1 = stream.ConsumeIntegral<uint32_t>() ^ key;
  uint32_t pad2 = stream.ConsumeIntegral<uint32_t>() ^ key;
  uint32_t pad3 = stream.ConsumeIntegral<uint32_t>() ^ key;
  memcpy(&dos[0x60], &dans_xored, 4);
  memcpy(&dos[0x64], &pad1, 4);
  memcpy(&dos[0x68], &pad2, 4);
  memcpy(&dos[0x6C], &pad3, 4);
  static const uint8_t kRich[4] = {'R', 'i', 'c', 'h'};
  memcpy(&dos[0x70], kRich, 4);
  memcpy(&dos[0x74], &key, 4);

  buf.insert(buf.end(), dos.begin(), dos.end());

  static const uint8_t pe_sig[4] = {'P', 'E', 0, 0};
  buf.insert(buf.end(), pe_sig, pe_sig + 4);

  bool pe32_plus = stream.ConsumeBool();

  uint16_t machine = stream.ConsumeBool() ? 0x014c   : 0x8664  ;
  uint16_t user_sections = stream.ConsumeIntegralInRange<uint16_t>(0, 3);
  uint16_t number_of_sections = (uint16_t) (user_sections + 1);
  uint32_t time_date_stamp = stream.ConsumeIntegral<uint32_t>();
  uint16_t size_of_optional_header =
      pe32_plus ? 240
                : 224  ;
  uint16_t characteristics = stream.ConsumeIntegral<uint16_t>();

  AppendU16(buf, machine);
  AppendU16(buf, number_of_sections);
  AppendU32(buf, time_date_stamp);
  AppendU32(buf, 0);
  AppendU32(buf, 0);
  AppendU16(buf, size_of_optional_header);
  AppendU16(buf, characteristics);

  AppendU16(buf, pe32_plus ? 0x20b : 0x10b);
  buf.push_back(stream.ConsumeIntegral<uint8_t>());
  buf.push_back(stream.ConsumeIntegral<uint8_t>());
  AppendU32(buf, stream.ConsumeIntegral<uint32_t>());
  AppendU32(buf, stream.ConsumeIntegral<uint32_t>());
  AppendU32(
      buf, stream.ConsumeIntegral<uint32_t>());
  AppendU32(buf, stream.ConsumeIntegral<uint32_t>());
  AppendU32(buf, 0x1000);

  if (!pe32_plus)
  {
    AppendU32(buf, 0x2000);
    AppendU32(buf, 0x400000);
  }
  else
  {
    AppendU64(buf, 0x140000000ULL);
  }

  AppendU32(buf, 0x1000);
  AppendU32(buf, 0x200);
  AppendU16(buf, stream.ConsumeIntegral<uint16_t>());
  AppendU16(buf, stream.ConsumeIntegral<uint16_t>());
  AppendU16(buf, stream.ConsumeIntegral<uint16_t>());
  AppendU16(buf, stream.ConsumeIntegral<uint16_t>());
  AppendU16(buf, stream.ConsumeIntegral<uint16_t>());
  AppendU16(buf, stream.ConsumeIntegral<uint16_t>());
  AppendU32(buf, 0);
  AppendU32(buf, 0x3000);
  AppendU32(buf, 0x400);
  AppendU32(buf, 0);
  AppendU16(buf, stream.ConsumeIntegralInRange<uint16_t>(0, 16));
  AppendU16(buf, stream.ConsumeIntegral<uint16_t>());

  if (!pe32_plus)
  {
    AppendU32(buf, 0x100000);
    AppendU32(buf, 0x1000);
    AppendU32(buf, 0x100000);
    AppendU32(buf, 0x1000);
  }
  else
  {
    AppendU64(buf, 0x100000);
    AppendU64(buf, 0x1000);
    AppendU64(buf, 0x100000);
    AppendU64(buf, 0x1000);
  }

  AppendU32(buf, 0);
  AppendU32(buf, 16);

  size_t data_dir_offset = buf.size();
  for (int i = 0; i < 16; i++)
  {
    AppendU32(buf, 0);
    AppendU32(buf, 0);
  }

  for (uint16_t i = 0; i < user_sections; i++)
  {
    uint8_t name[8] = {0};
    std::string sname = ".s" + std::to_string(i);
    memcpy(name, sname.data(), sname.size() < 8 ? sname.size() : 8);
    buf.insert(buf.end(), name, name + 8);
    AppendU32(buf, stream.ConsumeIntegral<uint32_t>());
    AppendU32(buf, 0x1000u * (i + 1));
    AppendU32(buf, stream.ConsumeIntegral<uint32_t>());
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU16(buf, 0);
    AppendU16(buf, 0);
    AppendU32(buf, stream.ConsumeIntegral<uint32_t>());
  }

  size_t special_header_offset = buf.size();
  {
    uint8_t name[8] = {'.', 's', 'p', 'e', 'c', 0, 0, 0};
    buf.insert(buf.end(), name, name + 8);
    AppendU32(buf, 0);
    AppendU32(buf, 0x1000u * (uint32_t) number_of_sections);
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU16(buf, 0);
    AppendU16(buf, 0);
    AppendU32(buf, 0);
  }
  uint32_t special_va = 0x1000u * (uint32_t) number_of_sections;

  while (buf.size() % 0x200 != 0) buf.push_back(0);
  uint32_t special_file_offset = (uint32_t) buf.size();

  std::vector<uint8_t> special;

  size_t import_start = special.size();
  {
    size_t desc_off = special.size();
    size_t thunk_off = desc_off + 40;
    size_t ibn_off = thunk_off + 8;
    size_t dllname_off = ibn_off + 6;

    AppendU32(special, special_va + (uint32_t) thunk_off);
    AppendU32(special, 0);
    AppendU32(special, 0);
    AppendU32(special, special_va + (uint32_t) dllname_off);
    AppendU32(special, special_va + (uint32_t) thunk_off);

    for (int k = 0; k < 5; k++) AppendU32(special, 0);

    AppendU32(special, special_va + (uint32_t) ibn_off);
    AppendU32(special, 0);

    AppendU16(special, 0);
    AppendBytes(special, "Fn", 2);
    special.push_back(0);
    special.push_back(0);

    AppendBytes(special, "a.dll", 5);
    special.push_back(0);
  }
  uint32_t import_rva = special_va + (uint32_t) import_start;
  uint32_t import_size = (uint32_t) (special.size() - import_start);

  size_t export_start = special.size();
  {
    size_t dir_off = special.size();
    size_t addrfunc_off = dir_off + 40;
    size_t addrnames_off = addrfunc_off + 4;
    size_t addrord_off = addrnames_off + 4;
    size_t dllname_off = addrord_off + 2;
    size_t funcname_off = dllname_off + 6;

    AppendU32(special, 0);
    AppendU32(special, stream.ConsumeIntegral<uint32_t>());
    AppendU16(special, 0);
    AppendU16(special, 0);
    AppendU32(special, special_va + (uint32_t) dllname_off);
    AppendU32(special, 1);
    AppendU32(special, 1);
    AppendU32(special, 1);
    AppendU32(special, special_va + (uint32_t) addrfunc_off);
    AppendU32(special, special_va + (uint32_t) addrnames_off);
    AppendU32(special, special_va + (uint32_t) addrord_off);

    AppendU32(special, special_va + (uint32_t) funcname_off);
    AppendU32(special, special_va + (uint32_t) funcname_off);
    AppendU16(special, 0);

    AppendBytes(special, "e.dll", 5);
    special.push_back(0);
    AppendBytes(special, "Ef", 2);
    special.push_back(0);
  }
  uint32_t export_rva = special_va + (uint32_t) export_start;
  uint32_t export_size = (uint32_t) (special.size() - export_start);

  size_t com_start = special.size();
  {
    size_t cli_off = special.size();
    size_t metadata_root_off = cli_off + 72;

    size_t streams_off = metadata_root_off + 16 + 4 + 2 + 2;

    size_t s_tilde_off = streams_off;
    size_t s_guid_off = s_tilde_off + 8 + 4;
    size_t s_strings_off = s_guid_off + 8 + 8;
    size_t s_blob_off = s_strings_off + 8 + 12;
    size_t s_us_off = s_blob_off + 8 + 8;
    size_t tilde_blob_off = s_us_off + 8 + 4;

    uint32_t metadata_root_rva = special_va + (uint32_t) metadata_root_off;
    uint32_t tilde_rel_offset =
        (uint32_t) (tilde_blob_off - metadata_root_off);
    uint32_t metadata_total_len =
        (uint32_t) (tilde_blob_off + 24 - metadata_root_off);

    AppendU32(special, 72);
    AppendU16(special, 2);
    AppendU16(special, 0);
    AppendU32(special, metadata_root_rva);
    AppendU32(special, metadata_total_len);
    AppendU32(special, 0);
    AppendU32(special, 0);
    AppendU32(special, 0); AppendU32(special, 0);
    AppendU32(special, 0); AppendU32(special, 0);
    AppendU64(special, 0);
    AppendU32(special, 0); AppendU32(special, 0);
    AppendU64(special, 0);
    AppendU64(special, 0);

    AppendU32(special, 0x424a5342u);
    AppendU16(special, 1);
    AppendU16(special, 1);
    AppendU32(special, 0);
    AppendU32(special, 4);
    AppendBytes(special, "v4\0\0", 4);

    AppendU16(special, 0);
    special.push_back(5);
    special.push_back(0);

    AppendU32(special, tilde_rel_offset);
    AppendU32(special, 24);
    AppendBytes(special, "#~\0\0", 4);

    AppendU32(special, 0);
    AppendU32(special, 0);
    AppendBytes(special, "#GUID\0\0\0", 8);

    AppendU32(special, 0);
    AppendU32(special, 0);
    AppendBytes(special, "#Strings\0\0\0\0", 12);

    AppendU32(special, 0);
    AppendU32(special, 0);
    AppendBytes(special, "#Blob\0\0\0", 8);

    AppendU32(special, 0);
    AppendU32(special, 0);
    AppendBytes(special, "#US\0", 4);

    AppendU32(special, 0);
    special.push_back(2);
    special.push_back(0);
    special.push_back(0);
    special.push_back(0);
    AppendU64(special, 0);
    AppendU64(special, 0);
  }
  uint32_t com_rva = special_va + (uint32_t) com_start;

  size_t rsrc_start = special.size();
  {

    AppendU32(special, 0);
    AppendU32(special, 0);
    AppendU16(special, 0);
    AppendU16(special, 0);
    AppendU16(special, 0);
    AppendU16(special, 1);
    AppendU32(special, stream.ConsumeIntegralInRange<uint32_t>(1, 24));
    AppendU32(special, 0x80000000u | 24u);

    AppendU32(special, 0);
    AppendU32(special, 0);
    AppendU16(special, 0);
    AppendU16(special, 0);
    AppendU16(special, 0);
    AppendU16(special, 1);
    AppendU32(special, stream.ConsumeIntegral<uint32_t>());
    AppendU32(special, 0x80000000u | 48u);

    AppendU32(special, 0);
    AppendU32(special, 0);
    AppendU16(special, 0);
    AppendU16(special, 0);
    AppendU16(special, 0);
    AppendU16(special, 1);
    AppendU32(special, stream.ConsumeIntegral<uint32_t>());
    AppendU32(special, 72);

    uint32_t content_rel_off = 88;
    AppendU32(
        special,
        special_va + (uint32_t) rsrc_start + content_rel_off);
    AppendU32(special, 16);
    AppendU32(special, 0);
    AppendU32(special, 0);

    AppendBytes(special, "RESOURCEDATA1234", 16);
  }
  uint32_t rsrc_rva = special_va + (uint32_t) rsrc_start;
  uint32_t rsrc_size = (uint32_t) (special.size() - rsrc_start);

  size_t debug_start = special.size();
  {
    size_t dbgdir_off = special.size();
    size_t cv_off = dbgdir_off + 28;

    AppendU32(special, 0);
    AppendU32(special, stream.ConsumeIntegral<uint32_t>());
    AppendU16(special, 0);
    AppendU16(special, 0);
    AppendU32(special, 2);
    AppendU32(special, 30);
    AppendU32(special, special_va + (uint32_t) cv_off);
    AppendU32(special, 0);

    AppendU32(special, 0x53445352u);
    AppendU32(special, stream.ConsumeIntegral<uint32_t>());
    AppendU32(special, stream.ConsumeIntegral<uint32_t>());
    AppendU32(special, stream.ConsumeIntegral<uint32_t>());
    AppendU32(special, stream.ConsumeIntegral<uint32_t>());
    AppendU32(special, stream.ConsumeIntegral<uint32_t>());
    AppendBytes(special, "a.pdb", 5);
    special.push_back(0);
  }
  uint32_t debug_rva = special_va + (uint32_t) debug_start;
  uint32_t debug_size = (uint32_t) (special.size() - debug_start);

  buf.insert(buf.end(), special.begin(), special.end());

  PatchU32(buf, special_header_offset + 8, (uint32_t) special.size());
  PatchU32(buf, special_header_offset + 16, (uint32_t) special.size());
  PatchU32(buf, special_header_offset + 20, special_file_offset);

  PatchU32(buf, data_dir_offset + 0 * 8 + 0, export_rva);
  PatchU32(buf, data_dir_offset + 0 * 8 + 4, export_size);
  PatchU32(buf, data_dir_offset + 1 * 8 + 0, import_rva);
  PatchU32(buf, data_dir_offset + 1 * 8 + 4, import_size);
  PatchU32(buf, data_dir_offset + 2 * 8 + 0, rsrc_rva);
  PatchU32(buf, data_dir_offset + 2 * 8 + 4, rsrc_size);
  PatchU32(buf, data_dir_offset + 6 * 8 + 0, debug_rva);
  PatchU32(buf, data_dir_offset + 6 * 8 + 4, debug_size);
  PatchU32(buf, data_dir_offset + 14 * 8 + 0, com_rva);
  PatchU32(buf, data_dir_offset + 14 * 8 + 4, 72);

  return buf;
}

static std::vector<uint8_t> BuildElfBuffer(FuzzedDataProvider& stream)
{
  std::vector<uint8_t> buf;

  bool is64 = stream.ConsumeBool();

  buf.push_back(0x7f);
  buf.push_back('E');
  buf.push_back('L');
  buf.push_back('F');
  buf.push_back(is64 ? 2 : 1);
  buf.push_back(1);
  buf.push_back(1);
  for (int i = 0; i < 8; i++) buf.push_back(0);
  buf.push_back(0);

  uint16_t e_type = stream.ConsumeIntegralInRange<uint16_t>(0, 4);
  uint16_t e_machine = stream.ConsumeIntegral<uint16_t>();
  uint32_t e_version = 1;
  uint64_t e_entry = stream.ConsumeIntegral<uint64_t>();
  const uint32_t kEhdrSize = is64 ? 64 : 52;
  const uint32_t kPhEntSize = is64 ? 56 : 32;
  const uint32_t kShEntSize = is64 ? 64 : 40;
  const uint32_t kDynEntSize = is64 ? 16 : 8;

  uint16_t extra_phnum = stream.ConsumeIntegralInRange<uint16_t>(0, 2);
  uint16_t e_phnum = (uint16_t) (extra_phnum + 1);

  uint16_t extra_shnum = stream.ConsumeIntegralInRange<uint16_t>(0, 3);
  uint16_t e_shnum = (uint16_t) (extra_shnum + 3);

  uint32_t e_phoff = kEhdrSize;
  uint32_t e_shoff = kEhdrSize + e_phnum * kPhEntSize;
  uint16_t e_shstrndx = 0;

  AppendU16(buf, e_type);
  AppendU16(buf, e_machine);
  AppendU32(buf, e_version);
  if (!is64)
  {
    AppendU32(buf, (uint32_t) e_entry);
    AppendU32(buf, e_phoff);
    AppendU32(buf, e_shoff);
  }
  else
  {
    AppendU64(buf, e_entry);
    AppendU64(buf, e_phoff);
    AppendU64(buf, e_shoff);
  }
  AppendU32(buf, 0);
  AppendU16(buf, static_cast<uint16_t>(kEhdrSize));
  AppendU16(buf, static_cast<uint16_t>(kPhEntSize));
  AppendU16(buf, e_phnum);
  AppendU16(buf, static_cast<uint16_t>(kShEntSize));
  AppendU16(buf, e_shnum);
  AppendU16(buf, e_shstrndx);

  uint32_t dynamic_array_off = e_shoff + e_shnum * kShEntSize;

  for (uint16_t i = 0; i < extra_phnum; i++)
  {
    uint32_t vaddr = stream.ConsumeIntegral<uint32_t>();
    uint32_t filesz = stream.ConsumeIntegral<uint32_t>();
    uint32_t memsz = stream.ConsumeIntegral<uint32_t>();
    uint32_t flags = stream.ConsumeIntegral<uint32_t>();

    if (!is64)
    {
      AppendU32(buf, 1);
      AppendU32(buf, 0);
      AppendU32(buf, vaddr);
      AppendU32(buf, 0);
      AppendU32(buf, filesz);
      AppendU32(buf, memsz);
      AppendU32(buf, flags);
      AppendU32(buf, 0x1000);
    }
    else
    {
      AppendU32(buf, 1);
      AppendU32(buf, flags);
      AppendU64(buf, 0);
      AppendU64(buf, vaddr);
      AppendU64(buf, 0);
      AppendU64(buf, filesz);
      AppendU64(buf, memsz);
      AppendU64(buf, 0x1000);
    }
  }

  uint32_t dyn_flags = stream.ConsumeIntegral<uint32_t>();
  if (!is64)
  {
    AppendU32(buf, 2);
    AppendU32(buf, dynamic_array_off);
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU32(buf, 2 * kDynEntSize);
    AppendU32(buf, 2 * kDynEntSize);
    AppendU32(buf, dyn_flags);
    AppendU32(buf, 4);
  }
  else
  {
    AppendU32(buf, 2);
    AppendU32(buf, dyn_flags);
    AppendU64(buf, dynamic_array_off);
    AppendU64(buf, 0);
    AppendU64(buf, 0);
    AppendU64(buf, 2 * kDynEntSize);
    AppendU64(buf, 2 * kDynEntSize);
    AppendU64(buf, 4);
  }

  AppendU32(buf, 0);
  AppendU32(buf, 3  );
  if (!is64)
  {
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU32(buf, 4);
  }
  else
  {
    AppendU64(buf, 0);
    AppendU64(buf, 0);
    AppendU64(buf, 0);
    AppendU64(buf, 4);
  }
  AppendU32(buf, 0);
  AppendU32(buf, 0);
  if (!is64)
  {
    AppendU32(buf, 0);
    AppendU32(buf, 0);
  }
  else
  {
    AppendU64(buf, 0);
    AppendU64(buf, 0);
  }

  uint32_t symtab_entsize = stream.ConsumeIntegral<uint32_t>();
  AppendU32(buf, 0);
  AppendU32(buf, 2  );
  if (!is64)
  {
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU32(buf, 0);
  }
  else
  {
    AppendU64(buf, 0);
    AppendU64(buf, 0);
    AppendU64(buf, 0);
    AppendU64(buf, 0);
  }
  AppendU32(buf, 0);
  AppendU32(buf, 0);
  if (!is64)
  {
    AppendU32(buf, 0);
    AppendU32(buf, symtab_entsize);
  }
  else
  {
    AppendU64(buf, 0);
    AppendU64(buf, symtab_entsize);
  }

  uint32_t dynsym_entsize = stream.ConsumeIntegral<uint32_t>();
  AppendU32(buf, 0);
  AppendU32(buf, 11  );
  if (!is64)
  {
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    AppendU32(buf, 0);
  }
  else
  {
    AppendU64(buf, 0);
    AppendU64(buf, 0);
    AppendU64(buf, 0);
    AppendU64(buf, 0);
  }
  AppendU32(buf, 0);
  AppendU32(buf, 0);
  if (!is64)
  {
    AppendU32(buf, 0);
    AppendU32(buf, dynsym_entsize);
  }
  else
  {
    AppendU64(buf, 0);
    AppendU64(buf, dynsym_entsize);
  }

  for (uint16_t i = 0; i < extra_shnum; i++)
  {
    uint32_t type = stream.ConsumeIntegralInRange<uint32_t>(0, 11);
    uint32_t flags = stream.ConsumeIntegral<uint32_t>();
    uint32_t size = stream.ConsumeIntegral<uint32_t>();

    AppendU32(buf, 0);
    AppendU32(buf, type);
    if (!is64)
    {
      AppendU32(buf, flags);
      AppendU32(buf, 0);
      AppendU32(buf, 0);
      AppendU32(buf, size);
    }
    else
    {
      AppendU64(buf, flags);
      AppendU64(buf, 0);
      AppendU64(buf, 0);
      AppendU64(buf, size);
    }
    AppendU32(buf, 0);
    AppendU32(buf, 0);
    if (!is64)
    {
      AppendU32(buf, 0);
      AppendU32(buf, 0);
    }
    else
    {
      AppendU64(buf, 0);
      AppendU64(buf, 0);
    }
  }

  uint64_t needed_val = stream.ConsumeIntegral<uint64_t>();
  if (!is64)
  {
    AppendU32(buf, 1  );
    AppendU32(buf, (uint32_t) needed_val);
    AppendU32(buf, 0  );
    AppendU32(buf, 0);
  }
  else
  {
    AppendU64(buf, 1  );
    AppendU64(buf, needed_val);
    AppendU64(buf, 0  );
    AppendU64(buf, 0);
  }

  return buf;
}

static std::vector<uint8_t> BuildMachOBuffer(FuzzedDataProvider& stream)
{
  std::vector<uint8_t> thin;

  bool is64 = stream.ConsumeBool();

  if (is64)
  {
    const uint32_t kSegCmdSize = 72;
    const uint32_t kSectSize = 80;
    const uint32_t kThreadCmdSize = 16 + 112;
    const uint32_t kMainCmdSize = 24;

    const uint32_t kSegFullSize = kSegCmdSize + kSectSize;
    const uint32_t ncmds = 3;
    const uint32_t sizeofcmds = kSegFullSize + kThreadCmdSize + kMainCmdSize;

    AppendU32(thin, 0xfeedfacf);
    AppendU32(thin, 0x01000007);
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, stream.ConsumeIntegralInRange<uint32_t>(1, 11));
    AppendU32(thin, ncmds);
    AppendU32(thin, sizeofcmds);
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, 0);

    AppendU32(thin, 0x19);
    AppendU32(thin, kSegFullSize);
    char segname[16] = {0};
    memcpy(segname, "__TEXT", 6);
    thin.insert(thin.end(), segname, segname + 16);
    AppendU64(thin, stream.ConsumeIntegral<uint64_t>());
    AppendU64(thin, stream.ConsumeIntegral<uint64_t>());
    AppendU64(thin, 0);
    AppendU64(thin, stream.ConsumeIntegral<uint64_t>());
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, 1);
    AppendU32(thin, 0);

    char sectname[16] = {0};
    memcpy(sectname, "__text", 6);
    thin.insert(thin.end(), sectname, sectname + 16);
    thin.insert(thin.end(), segname, segname + 16);
    AppendU64(thin, stream.ConsumeIntegral<uint64_t>());
    AppendU64(thin, stream.ConsumeIntegral<uint64_t>());
    AppendU32(thin, 0);
    AppendU32(thin, 0);
    AppendU32(thin, 0);
    AppendU32(thin, 0);
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, 0);
    AppendU32(thin, 0);
    AppendU32(thin, 0);

    AppendU32(thin, 0x05);
    AppendU32(thin, kThreadCmdSize);
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    for (int i = 0; i < 14; i++)
      AppendU64(thin, stream.ConsumeIntegral<uint64_t>());

    AppendU32(thin, 0x80000028u);
    AppendU32(thin, kMainCmdSize);
    AppendU64(thin, stream.ConsumeIntegral<uint64_t>());
    AppendU64(thin, stream.ConsumeIntegral<uint64_t>());
  }
  else
  {
    const uint32_t kSegCmdSize = 56;
    const uint32_t kSectSize = 68;
    const uint32_t kThreadCmdSize = 16 + 64;
    const uint32_t kMainCmdSize = 24;

    const uint32_t kSegFullSize = kSegCmdSize + kSectSize;
    const uint32_t ncmds = 3;
    const uint32_t sizeofcmds = kSegFullSize + kThreadCmdSize + kMainCmdSize;

    AppendU32(thin, 0xfeedface);
    AppendU32(thin, 0x00000007);
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, stream.ConsumeIntegralInRange<uint32_t>(1, 11));
    AppendU32(thin, ncmds);
    AppendU32(thin, sizeofcmds);
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());

    AppendU32(thin, 0x1);
    AppendU32(thin, kSegFullSize);
    char segname[16] = {0};
    memcpy(segname, "__TEXT", 6);
    thin.insert(thin.end(), segname, segname + 16);
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, 0);
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, 1);
    AppendU32(thin, 0);

    char sectname[16] = {0};
    memcpy(sectname, "__text", 6);
    thin.insert(thin.end(), sectname, sectname + 16);
    thin.insert(thin.end(), segname, segname + 16);
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, 0);
    AppendU32(thin, 0);
    AppendU32(thin, 0);
    AppendU32(thin, 0);
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, 0);
    AppendU32(thin, 0);

    AppendU32(thin, 0x05);
    AppendU32(thin, kThreadCmdSize);
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    AppendU32(thin, stream.ConsumeIntegral<uint32_t>());
    for (int i = 0; i < 16; i++)
      AppendU32(thin, stream.ConsumeIntegral<uint32_t>());

    AppendU32(thin, 0x80000028u);
    AppendU32(thin, kMainCmdSize);
    AppendU64(thin, stream.ConsumeIntegral<uint64_t>());
    AppendU64(thin, stream.ConsumeIntegral<uint64_t>());
  }

  if (!stream.ConsumeBool())
    return thin;

  std::vector<uint8_t> fat;
  AppendU32BE(fat, 0xcafebabeu);
  AppendU32BE(fat, 1);
  uint32_t arch_off = 8 + 20;
  AppendU32BE(fat, is64 ? 0x01000007u : 0x00000007u);
  AppendU32BE(fat, 0);
  AppendU32BE(fat, arch_off);
  AppendU32BE(fat, (uint32_t) thin.size());
  AppendU32BE(fat, 0x1000);
  fat.insert(fat.end(), thin.begin(), thin.end());
  return fat;
}

static std::vector<uint8_t> BuildDexBuffer(FuzzedDataProvider& stream)
{
  std::vector<uint8_t> buf;

  static const uint8_t magic[8] = {'d', 'e', 'x', '\n', '0', '3', '5', '\0'};
  buf.insert(buf.end(), magic, magic + 8);
  AppendU32(buf, stream.ConsumeIntegral<uint32_t>());
  for (int i = 0; i < 20; i++)
    buf.push_back(stream.ConsumeIntegral<uint8_t>());
  AppendU32(buf, stream.ConsumeIntegral<uint32_t>());
  AppendU32(buf, 112);
  AppendU32(buf, 0x12345678);
  AppendU32(buf, 0);
  AppendU32(buf, 0);
  AppendU32(buf, 0);
  AppendU32(buf, stream.ConsumeIntegralInRange<uint32_t>(0, 4));
  AppendU32(buf, 112);
  AppendU32(buf, stream.ConsumeIntegralInRange<uint32_t>(0, 4));
  AppendU32(buf, 112);
  AppendU32(buf, 0);
  AppendU32(buf, 0);
  AppendU32(buf, 0);
  AppendU32(buf, 0);
  AppendU32(buf, 0);
  AppendU32(buf, 0);
  AppendU32(buf, stream.ConsumeIntegralInRange<uint32_t>(0, 2));
  AppendU32(buf, 112);
  AppendU32(buf, 0);
  AppendU32(buf, 0);

  return buf;
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

  yr_compiler_define_integer_variable(compiler, "my_int", 0);
  yr_compiler_define_boolean_variable(compiler, "my_bool", 0);
  yr_compiler_define_string_variable(compiler, "my_str", "");

  if (yr_compiler_add_string(compiler, kRichRules, nullptr) == 0)
  {

    if (yr_compiler_get_rules(compiler, &g_rules) != ERROR_SUCCESS)
      g_rules = nullptr;
  }

  yr_compiler_destroy(compiler);

  for (size_t i = 0; i < sizeof(kExtraRuleSets) / sizeof(kExtraRuleSets[0]);
       i++)
  {
    YR_COMPILER* extra_compiler = nullptr;

    if (yr_compiler_create(&extra_compiler) != ERROR_SUCCESS)
      continue;

    if (yr_compiler_add_string(extra_compiler, kExtraRuleSets[i], nullptr) ==
        0)
    {
      YR_RULES* extra_rules = nullptr;

      if (yr_compiler_get_rules(extra_compiler, &extra_rules) ==
          ERROR_SUCCESS)
      {
        g_extra_rules.push_back(extra_rules);
      }
    }

    yr_compiler_destroy(extra_compiler);
  }

  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (g_rules == nullptr)
    return 0;

  FuzzedDataProvider stream(data, size);

  std::vector<uint8_t> buffer;

  switch (stream.ConsumeIntegralInRange<int>(0, 6))
  {
    case 1:
    {
      static const uint8_t kMzMagic[] = {'M', 'Z'};
      buffer.insert(buffer.end(), kMzMagic, kMzMagic + sizeof(kMzMagic));
      break;
    }
    case 2:
    {
      static const uint8_t kElfMagic[] = {0x7f, 'E', 'L', 'F'};
      buffer.insert(buffer.end(), kElfMagic, kElfMagic + sizeof(kElfMagic));
      break;
    }
    case 3:
    {
      buffer = BuildPeBuffer(stream);
      break;
    }
    case 4:
    {
      buffer = BuildElfBuffer(stream);
      break;
    }
    case 5:
    {
      buffer = BuildMachOBuffer(stream);
      break;
    }
    case 6:
    {
      buffer = BuildDexBuffer(stream);
      break;
    }
    default:
      break;
  }

  int flags = stream.ConsumeIntegral<uint8_t>() & 0x1F;
  if (stream.ConsumeBool())
    flags |= stream.ConsumeIntegral<int>();

  const int timeout = stream.ConsumeIntegralInRange<int>(0, 10);

  uint8_t ctl = stream.ConsumeIntegral<uint8_t>();

  int32_t ext_int = stream.ConsumeIntegral<int32_t>();
  bool ext_bool = stream.ConsumeBool();
  std::string ext_str = stream.ConsumeRandomLengthString(16);

  yr_rules_define_integer_variable(g_rules, "my_int", ext_int);
  yr_rules_define_boolean_variable(g_rules, "my_bool", ext_bool ? 1 : 0);
  yr_rules_define_string_variable(g_rules, "my_str", ext_str.c_str());

  std::vector<uint8_t> rest = stream.ConsumeRemainingBytes<uint8_t>();
  buffer.insert(buffer.end(), rest.begin(), rest.end());

  if ((ctl & 0x3F) == 0)
  {
    buffer.resize(210000, 0x41);
  }

  yr_rules_scan_mem(
      g_rules,
      buffer.data(),
      buffer.size(),
      flags,
      ScanCallback,
      &ctl,
      timeout);

  for (YR_RULES* extra_rules : g_extra_rules)
  {
    yr_rules_scan_mem(
        extra_rules,
        buffer.data(),
        buffer.size(),
        flags,
        ScanCallback,
        &ctl,
        timeout);
  }

  return 0;
}