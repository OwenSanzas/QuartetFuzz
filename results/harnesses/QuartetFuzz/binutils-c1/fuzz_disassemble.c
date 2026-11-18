#include "sysdep.h"
#include "bfd.h"
#include "dis-asm.h"
#include "disassemble.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define MAX_TEXT_SIZE 256
#define OPTIONS_SIZE 64

typedef struct
{
  char *buffer;
  size_t pos;
} SFILE;

static int
fuzz_disasm_null_styled_printf (void *stream ATTRIBUTE_UNUSED,
				 enum disassembler_style style ATTRIBUTE_UNUSED,
				 const char *format ATTRIBUTE_UNUSED, ...)
{
  return 0;
}

static int
objdump_sprintf (void *vf, const char *format, ...)
{
  SFILE *f = (SFILE *) vf;
  va_list args;
  int n;

  if (f->pos >= MAX_TEXT_SIZE - 1)
    {
      f->pos = 0;
      return 0;
    }

  va_start (args, format);
  n = vsnprintf (f->buffer + f->pos, MAX_TEXT_SIZE - f->pos, format, args);
  va_end (args);

  if (n > 0)
    f->pos += (size_t) n;
  return n;
}

static const enum bfd_architecture kArches[] = {
  bfd_arch_aarch64,
  bfd_arch_alpha,
  bfd_arch_arc,
  bfd_arch_arm,
  bfd_arch_avr,
  bfd_arch_bfin,
  bfd_arch_cr16,
  bfd_arch_cris,
  bfd_arch_crx,
  bfd_arch_csky,
  bfd_arch_d10v,
  bfd_arch_d30v,
  bfd_arch_dlx,
  bfd_arch_h8300,
  bfd_arch_hppa,
  bfd_arch_i386,
  bfd_arch_iamcu,
  bfd_arch_ia64,
  bfd_arch_ip2k,
  bfd_arch_bpf,
  bfd_arch_epiphany,
  bfd_arch_fr30,
  bfd_arch_kvx,
  bfd_arch_lm32,
  bfd_arch_m32r,
  bfd_arch_m68hc11,
  bfd_arch_m68hc12,
  bfd_arch_m9s12x,
  bfd_arch_m9s12xg,
  bfd_arch_s12z,
  bfd_arch_m68k,
  bfd_arch_mt,
  bfd_arch_microblaze,
  bfd_arch_msp430,
  bfd_arch_nds32,
  bfd_arch_nfp,
  bfd_arch_ns32k,
  bfd_arch_mcore,
  bfd_arch_mep,
  bfd_arch_metag,
  bfd_arch_mips,
  bfd_arch_mmix,
  bfd_arch_mn10200,
  bfd_arch_mn10300,
  bfd_arch_or1k,
  bfd_arch_pdp11,
  bfd_arch_pj,
  bfd_arch_powerpc,
  bfd_arch_rs6000,
  bfd_arch_pru,
  bfd_arch_riscv,
  bfd_arch_rl78,
  bfd_arch_rx,
  bfd_arch_s390,
  bfd_arch_score,
  bfd_arch_sh,
  bfd_arch_sparc,
  bfd_arch_spu,
  bfd_arch_tic30,
  bfd_arch_tic4x,
  bfd_arch_tic54x,
  bfd_arch_tic6x,
  bfd_arch_ft32,
  bfd_arch_v850,
  bfd_arch_wasm32,
  bfd_arch_xgate,
  bfd_arch_xstormy16,
  bfd_arch_xtensa,
  bfd_arch_z80,
  bfd_arch_z8k,
  bfd_arch_vax,
  bfd_arch_visium,
  bfd_arch_frv,
  bfd_arch_moxie,
  bfd_arch_iq2000,
  bfd_arch_m32c,
  bfd_arch_tilegx,
  bfd_arch_tilepro,
  bfd_arch_loongarch,
};

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{

  const size_t header = 1 + 1 + 8 + OPTIONS_SIZE;
  if (size < header + 4)
    return 0;

  if (size > header + 4096)
    size = header + 4096;

  uint8_t archsel = data[0];
  uint8_t bigsel = data[1];
  uint64_t machval;
  memcpy (&machval, data + 2, 8);

  char options[OPTIONS_SIZE];
  memcpy (options, data + 10, OPTIONS_SIZE - 1);
  options[OPTIONS_SIZE - 1] = '\0';

  const uint8_t *buf = data + header;
  size_t buflen = size - header;

  enum bfd_architecture arch =
    kArches[archsel % (sizeof (kArches) / sizeof (kArches[0]))];
  bool big = (bigsel & 1) != 0;
  unsigned long mach = (unsigned long) machval;

  char assembly_text[MAX_TEXT_SIZE];
  SFILE s;
  struct disassemble_info info;

  init_disassemble_info (&info, stdout, (fprintf_ftype) fprintf,
			  fuzz_disasm_null_styled_printf);
  info.fprintf_func = objdump_sprintf;
  info.print_address_func = generic_print_address;
  info.display_endian = info.endian = big ? BFD_ENDIAN_BIG : BFD_ENDIAN_LITTLE;
  info.buffer = (bfd_byte *) buf;
  info.buffer_vma = 0x1000;
  info.buffer_length = buflen;
  info.disassembler_options = options;
  info.flags |= USER_SPECIFIED_MACHINE_TYPE;
  info.arch = arch;
  info.mach = mach;

  s.buffer = assembly_text;
  s.pos = 0;
  info.stream = &s;

  if (bfd_lookup_arch (info.arch, info.mach) == NULL)
    return 0;

  disassembler_ftype disasfunc = disassembler (info.arch, big, info.mach, NULL);
  if (disasfunc == NULL)
    return 0;

  disassemble_init_for_target (&info);

  int iterations = 0;
  while (iterations < 4096)
    {
      s.pos = 0;
      int octets = disasfunc (info.buffer_vma, &info);
      iterations++;

      if (octets < (int) info.octets_per_byte)
	break;
      if (info.buffer_length <= (size_t) octets)
	break;

      info.buffer += octets;
      info.buffer_vma += octets / info.octets_per_byte;
      info.buffer_length -= octets;
    }

  disassemble_free_target (&info);

  return 0;
}
