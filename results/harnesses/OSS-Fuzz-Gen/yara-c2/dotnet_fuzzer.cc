#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>

#include <yara.h>

const char* RULES =
    "import \"dotnet\" "
    "import \"pe\" "
    "import \"elf\" "
    "import \"macho\" "
    "import \"dex\" "
    "import \"math\" "
    "import \"time\" "
    "import \"string\" "
    "rule test_dotnet { "
    " condition: "
    "   dotnet.is_dotnet and "
    "   (dotnet.number_of_streams >= 0) and "
    "   (dotnet.number_of_guids >= 0) and "
    "   (dotnet.number_of_classes == 0 or "
    "      (for any c in dotnet.classes : (c.number_of_methods >= 0))) and "
    "   (dotnet.number_of_resources == 0 or "
    "      (for any r in dotnet.resources : (r.length >= 0))) and "
    "   (dotnet.number_of_assembly_refs == 0 or "
    "      (for any a in dotnet.assembly_refs : (a.name != \"\")))"
    "} "
    "rule test_pe { "
    " condition: "
    "   pe.is_pe and "
    "   (pe.number_of_sections >= 0) and "
    "   (pe.number_of_imports == 0 or "
    "      (for any i in pe.import_details : (i.number_of_functions >= 0))) and "
    "   (pe.number_of_resources == 0 or "
    "      (for any r in pe.resources : (r.length >= 0)))"
    "} "
    "rule test_elf { "
    " condition: "
    "   elf.number_of_sections >= 0 and "
    "   (for any s in elf.symtab : (s.name != \"\"))"
    "} "
    "rule test_macho { "
    " condition: "
    "   macho.number_of_segments >= 0 and "
    "   (for any seg in macho.segments : (seg.nsects >= 0))"
    "} "
    "rule test_dex { "
    " condition: "
    "   dex.header.magic != \"\" and dex.header.class_defs_size >= 0"
    "} "
    "rule test_math { "
    " condition: "
    "   math.entropy(0, filesize) >= 0.0"
    "} "
    "rule test_time { "
    " condition: "
    "   time.now() >= 0"
    "} "
    "rule test_string { "
    " condition: "
    "   string.to_int(\"0\") == 0"
    "} "
    "rule test_strings { "
    " strings: "
    "   $a1 = \"This program\" "
    "   $a2 = \"This program\" nocase "
    "   $a3 = \"This program\" wide "
    "   $a4 = \"This program\" fullword "
    "   $a5 = \"secret\" xor "
    "   $a6 = \"secret\" base64 "
    "   $h1 = { 4D 5A ?? ?? 00 00 } "
    "   $r1 = /MZ.{2}/ "
    " condition: "
    "   any of them or "
    "   all of ($a2,$a3) or "
    "   2 of ($a1,$a2,$a3,$a4) or "
    "   50% of them or "
    "   $h1 at 0 or "
    "   $r1 in (0..filesize) or "
    "   (#a1 > 0 and (for all i in (1..#a1) : (@a1[i] >= 0 and !a1[i] >= 0)))"
    "}";

YR_RULES* rules = NULL;

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
  YR_COMPILER* compiler;

  if (yr_initialize() != ERROR_SUCCESS)
    return 0;

  if (yr_compiler_create(&compiler) != ERROR_SUCCESS)
    return 0;

  if (yr_compiler_add_string(compiler, RULES, NULL) == 0)
    yr_compiler_get_rules(compiler, &rules);

  yr_compiler_destroy(compiler);

  return 0;
}

static int g_cb_return = CALLBACK_CONTINUE;

int callback(
    YR_SCAN_CONTEXT* context,
    int message,
    void* message_data,
    void* user_data)
{
  return g_cb_return;
}

static void put_u16le(std::vector<uint8_t>& v, size_t off, uint16_t val)
{
  if (v.size() < off + 2)
    v.resize(off + 2, 0);
  v[off] = (uint8_t) (val & 0xFF);
  v[off + 1] = (uint8_t) ((val >> 8) & 0xFF);
}

static void put_u32le(std::vector<uint8_t>& v, size_t off, uint32_t val)
{
  if (v.size() < off + 4)
    v.resize(off + 4, 0);
  v[off] = (uint8_t) (val & 0xFF);
  v[off + 1] = (uint8_t) ((val >> 8) & 0xFF);
  v[off + 2] = (uint8_t) ((val >> 16) & 0xFF);
  v[off + 3] = (uint8_t) ((val >> 24) & 0xFF);
}

static std::vector<uint8_t> make_elf_skeleton()
{
  const size_t elf_hdr_size = 52;
  const size_t sh_entsize = 40;
  const size_t sh_num = 3;
  const size_t sh_table_off = elf_hdr_size;
  const uint8_t strtab_data[] = {0x00, 's', 'y', 'm', '1', 0x00};
  const size_t strtab_off = sh_table_off + sh_entsize * sh_num;
  const size_t strtab_size = sizeof(strtab_data);
  const size_t symtab_off = strtab_off + strtab_size;
  const size_t symtab_size = 16;
  const size_t total_size = symtab_off + symtab_size;

  std::vector<uint8_t> v(total_size, 0);

  v[0] = 0x7f;
  v[1] = 'E';
  v[2] = 'L';
  v[3] = 'F';
  v[4] = 1;
  v[5] = 1;
  put_u16le(v, 16, 3);
  put_u32le(v, 32, (uint32_t) sh_table_off);
  put_u16le(v, 46, (uint16_t) sh_entsize);
  put_u16le(v, 48, (uint16_t) sh_num);
  put_u16le(v, 50, 1);

  size_t sec1 = sh_table_off + 1 * sh_entsize;
  put_u32le(v, sec1 + 0, 0);
  put_u32le(v, sec1 + 4, 3);
  put_u32le(v, sec1 + 16, (uint32_t) strtab_off);
  put_u32le(v, sec1 + 20, (uint32_t) strtab_size);
  put_u32le(v, sec1 + 32, 1);

  size_t sec2 = sh_table_off + 2 * sh_entsize;
  put_u32le(v, sec2 + 0, 0);
  put_u32le(v, sec2 + 4, 2);
  put_u32le(v, sec2 + 16, (uint32_t) symtab_off);
  put_u32le(v, sec2 + 20, (uint32_t) symtab_size);
  put_u32le(v, sec2 + 24, 1);
  put_u32le(v, sec2 + 32, 4);
  put_u32le(v, sec2 + 36, 16);

  memcpy(&v[strtab_off], strtab_data, strtab_size);

  put_u32le(v, symtab_off + 0, 1);
  put_u32le(v, symtab_off + 4, 0);
  put_u32le(v, symtab_off + 8, 0);
  v[symtab_off + 12] = 0x12;
  v[symtab_off + 13] = 0;
  put_u16le(v, symtab_off + 14, 0);

  return v;
}

static std::vector<uint8_t> make_macho_skeleton()
{
  const size_t header_size = 28;
  const size_t seg_off = header_size;
  const size_t seg_size = 56;
  const size_t sect_off = seg_off + seg_size;
  const size_t sect_size = 68;
  const size_t total_size = sect_off + sect_size;

  std::vector<uint8_t> v(total_size, 0);

  v[0] = 0xce;
  v[1] = 0xfa;
  v[2] = 0xed;
  v[3] = 0xfe;
  put_u32le(v, 16, 1);
  put_u32le(v, 20, (uint32_t) (seg_size + sect_size));

  put_u32le(v, seg_off + 0, 0x00000001);
  put_u32le(v, seg_off + 4, (uint32_t) (seg_size + sect_size));
  memcpy(&v[seg_off + 8], "__TEXT\0\0\0\0\0\0\0\0\0\0", 16);
  put_u32le(v, seg_off + 48, 1);

  memcpy(&v[sect_off + 0], "__text\0\0\0\0\0\0\0\0\0\0", 16);
  memcpy(&v[sect_off + 16], "__TEXT\0\0\0\0\0\0\0\0\0\0", 16);

  return v;
}

static std::vector<uint8_t> make_dex_skeleton()
{
  const size_t header_size = 112;
  const size_t string_ids_off = header_size;
  const size_t string_data_off = string_ids_off + 4;
  const size_t type_ids_off = string_data_off + 4;
  const size_t class_defs_off = type_ids_off + 4;
  const size_t total_size = class_defs_off + 32;

  std::vector<uint8_t> v(total_size, 0);

  const char magic[8] = {'d', 'e', 'x', '\n', '0', '3', '5', '\0'};
  memcpy(v.data(), magic, 8);

  put_u32le(v, 56, 1);
  put_u32le(v, 60, (uint32_t) string_ids_off);
  put_u32le(v, 64, 1);
  put_u32le(v, 68, (uint32_t) type_ids_off);
  put_u32le(v, 96, 1);
  put_u32le(v, 100, (uint32_t) class_defs_off);

  put_u32le(v, string_ids_off, (uint32_t) string_data_off);

  v[string_data_off + 0] = 0x03;
  memcpy(&v[string_data_off + 1], "Foo", 3);

  put_u32le(v, type_ids_off, 0);

  return v;
}

static std::vector<uint8_t> make_pe_skeleton()
{
  const uint32_t e_lfanew = 0x80;
  const size_t nt_sig_off = e_lfanew;
  const size_t file_hdr_off = nt_sig_off + 4;
  const size_t opt_hdr_off = file_hdr_off + 20;
  const size_t opt_hdr_size = 224;
  const size_t section_hdr_off = opt_hdr_off + opt_hdr_size;
  const size_t section_hdr_size = 40;
  const size_t body_off = section_hdr_off + section_hdr_size;

  const size_t imports_off = body_off;
  const size_t thunks_off = imports_off + 40;
  const size_t iibn_off = thunks_off + 8;
  const size_t dllname_off = iibn_off + 7;

  const size_t cli_hdr_off = dllname_off + 6;
  const size_t metadata_root_off = cli_hdr_off + 72;
  const size_t version_off = metadata_root_off + 16;
  const size_t flags_off = version_off + 4;
  const size_t num_streams_off = flags_off + 2;
  const size_t stream_headers_off = num_streams_off + 2;

  const size_t sh_tilde_off = stream_headers_off;
  const size_t sh_strings_off = sh_tilde_off + 12;
  const size_t sh_guid_off = sh_strings_off + 20;
  const size_t sh_blob_off = sh_guid_off + 16;
  const size_t sh_us_off = sh_blob_off + 16;
  const size_t tilde_off = sh_us_off + 12;

  const size_t strings_off = tilde_off + 38;
  const size_t guid_off = strings_off + 5;
  const size_t blob_off = guid_off + 16;
  const size_t us_off = blob_off + 4;
  const size_t total_size = us_off + 7;

  std::vector<uint8_t> v(total_size, 0);

  v[0] = 'M';
  v[1] = 'Z';
  put_u32le(v, 0x3C, e_lfanew);

  {
    const char stub[] = "This program cannot be run in DOS mode.";
    memcpy(&v[64], stub, sizeof(stub) - 1);
  }

  v[nt_sig_off + 0] = 'P';
  v[nt_sig_off + 1] = 'E';
  v[nt_sig_off + 2] = 0;
  v[nt_sig_off + 3] = 0;

  put_u16le(v, file_hdr_off + 0, 0x014c);
  put_u16le(v, file_hdr_off + 2, 1);
  put_u16le(v, file_hdr_off + 16, (uint16_t) opt_hdr_size);
  put_u16le(v, file_hdr_off + 18, 0);

  put_u16le(v, opt_hdr_off + 0, 0x010b);
  put_u32le(v, opt_hdr_off + 92, 16);

  const size_t data_dir_off = opt_hdr_off + 96;

  put_u32le(v, data_dir_off + 1 * 8 + 0, (uint32_t) imports_off);
  put_u32le(v, data_dir_off + 1 * 8 + 4, 40);

  put_u32le(v, data_dir_off + 14 * 8 + 0, (uint32_t) cli_hdr_off);
  put_u32le(v, data_dir_off + 14 * 8 + 4, 72);

  memcpy(&v[section_hdr_off], ".text\0\0\0", 8);
  put_u32le(v, section_hdr_off + 8, (uint32_t) total_size);
  put_u32le(v, section_hdr_off + 12, 0);
  put_u32le(v, section_hdr_off + 16, (uint32_t) total_size);
  put_u32le(v, section_hdr_off + 20, 0);

  put_u32le(v, imports_off + 0, (uint32_t) thunks_off);
  put_u32le(v, imports_off + 12, (uint32_t) dllname_off);
  put_u32le(v, imports_off + 16, (uint32_t) thunks_off);

  put_u32le(v, thunks_off + 0, (uint32_t) iibn_off);

  put_u16le(v, iibn_off + 0, 0);
  memcpy(&v[iibn_off + 2], "Func", 5);

  memcpy(&v[dllname_off], "A.dll", 6);

  put_u32le(v, cli_hdr_off + 0, 72);
  put_u16le(v, cli_hdr_off + 4, 2);
  put_u16le(v, cli_hdr_off + 6, 5);
  put_u32le(v, cli_hdr_off + 8, (uint32_t) metadata_root_off);
  put_u32le(
      v, cli_hdr_off + 12, (uint32_t) (total_size - metadata_root_off));

  put_u32le(v, metadata_root_off + 0, 0x424a5342);
  put_u16le(v, metadata_root_off + 4, 1);
  put_u16le(v, metadata_root_off + 6, 1);
  put_u32le(v, metadata_root_off + 8, 0);
  put_u32le(v, metadata_root_off + 12, 4);
  memcpy(&v[version_off], "1\0\0\0", 4);
  put_u16le(v, flags_off, 0);
  put_u16le(v, num_streams_off, 5);

  auto put_stream_header = [&](size_t off,
                                uint32_t rel_offset,
                                uint32_t size,
                                const char* name) {
    put_u32le(v, off + 0, rel_offset);
    put_u32le(v, off + 4, size);
    memcpy(&v[off + 8], name, strlen(name) + 1);

  };

  put_stream_header(
      sh_tilde_off, (uint32_t) (tilde_off - metadata_root_off), 38, "#~");
  put_stream_header(
      sh_strings_off,
      (uint32_t) (strings_off - metadata_root_off),
      5,
      "#Strings");
  put_stream_header(
      sh_guid_off, (uint32_t) (guid_off - metadata_root_off), 16, "#GUID");
  put_stream_header(
      sh_blob_off, (uint32_t) (blob_off - metadata_root_off), 4, "#Blob");
  put_stream_header(
      sh_us_off, (uint32_t) (us_off - metadata_root_off), 7, "#US");

  put_u32le(v, tilde_off + 0, 0);
  v[tilde_off + 4] = 2;
  v[tilde_off + 5] = 0;
  v[tilde_off + 6] = 0;
  v[tilde_off + 7] = 0;

  put_u32le(v, tilde_off + 8, 1);
  put_u32le(v, tilde_off + 12, 0);
  put_u32le(v, tilde_off + 16, 0);
  put_u32le(v, tilde_off + 20, 0);
  put_u32le(v, tilde_off + 24, 1);

  put_u16le(v, tilde_off + 28, 0);
  put_u16le(v, tilde_off + 30, 1);
  put_u16le(v, tilde_off + 32, 0);
  put_u16le(v, tilde_off + 34, 0);
  put_u16le(v, tilde_off + 36, 0);

  v[strings_off + 0] = 0x00;
  memcpy(&v[strings_off + 1], "Mod", 4);

  v[us_off + 0] = 0x00;
  v[us_off + 1] = 0x05;
  v[us_off + 2] = 'H';
  v[us_off + 3] = 0x00;
  v[us_off + 4] = 'i';
  v[us_off + 5] = 0x00;
  v[us_off + 6] = 0x00;

  return v;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  if (rules == NULL)
    return 0;

  if (size < 4)
    return 0;

  uint8_t flag_byte = data[size - 1];
  uint8_t cb_byte = data[size - 2];
  uint8_t timeout_byte = data[size - 3];
  uint8_t format_sel = data[size - 4] % 5;

  size_t body_size = size - 4;
  std::vector<uint8_t> buf(data, data + body_size);

  bool looks_like_known_format =
      (buf.size() >= 2 && buf[0] == 'M' && buf[1] == 'Z') ||
      (buf.size() >= 4 && buf[0] == 0x7f && buf[1] == 'E' && buf[2] == 'L' &&
       buf[3] == 'F') ||
      (buf.size() >= 4 &&
       ((buf[0] == 0xce && buf[1] == 0xfa && buf[2] == 0xed &&
         buf[3] == 0xfe) ||
        (buf[0] == 0xcf && buf[1] == 0xfa && buf[2] == 0xed &&
         buf[3] == 0xfe) ||
        (buf[0] == 0xfe && buf[1] == 0xed && buf[2] == 0xfa &&
         buf[3] == 0xce) ||
        (buf[0] == 0xfe && buf[1] == 0xed && buf[2] == 0xfa &&
         buf[3] == 0xcf))) ||
      (buf.size() >= 8 && memcmp(buf.data(), "dex\n035\0", 8) == 0);

  if (!looks_like_known_format)
  {
    std::vector<uint8_t> skel;

    switch (format_sel)
    {
    case 0:
      skel = make_elf_skeleton();
      break;
    case 1:
      skel = make_macho_skeleton();
      break;
    case 2:
      skel = make_dex_skeleton();
      break;
    case 3:
      skel = make_pe_skeleton();
      break;
    default:

      break;
    }

    skel.insert(skel.end(), buf.begin(), buf.end());
    buf.swap(skel);
  }

  int flags = (flag_byte &
               (SCAN_FLAGS_FAST_MODE | SCAN_FLAGS_PROCESS_MEMORY |
                SCAN_FLAGS_NO_TRYCATCH | SCAN_FLAGS_REPORT_RULES_MATCHING |
                SCAN_FLAGS_REPORT_RULES_NOT_MATCHING));

  flags |= SCAN_FLAGS_NO_TRYCATCH;

  g_cb_return = ((cb_byte % 4) == 0) ? CALLBACK_ABORT : CALLBACK_CONTINUE;

  int timeout = (timeout_byte % 2 == 0) ? 0 : 1;

  const uint8_t* buffer = buf.empty() ? NULL : buf.data();

  yr_rules_scan_mem(
      rules, buffer, buf.size(), flags, callback, NULL, timeout);

  return 0;
}