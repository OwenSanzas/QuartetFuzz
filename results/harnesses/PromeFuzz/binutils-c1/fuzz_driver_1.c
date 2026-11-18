#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "dis-asm.h"
#include "bfd.h"

static int
nop_fprintf (void *stream, const char *fmt, ...)
{
  (void) stream;
  (void) fmt;
  return 0;
}

static int
nop_fprintf_styled (void *stream, enum disassembler_style style,
                     const char *fmt, ...)
{
  (void) stream;
  (void) style;
  (void) fmt;
  return 0;
}

static bool
get_valid_mach (enum bfd_architecture arc, unsigned long mach_in,
                unsigned long *mach_out)
{
  const bfd_arch_info_type *ainfo = bfd_lookup_arch (arc, mach_in);

  if (ainfo == NULL)
    ainfo = bfd_lookup_arch (arc, 0);

  if (ainfo == NULL)
    return false;

  *mach_out = ainfo->mach;
  return true;
}

static void
try_disassemble (enum bfd_architecture arc, bool big, unsigned long mach,
                  bfd *abfd, const uint8_t *buf, size_t buf_len)
{
  unsigned long valid_mach;

  if (!get_valid_mach (arc, mach, &valid_mach))
    {

      (void) disassembler (arc, big, mach, abfd);
      return;
    }

  disassembler_ftype fn = disassembler (arc, big, valid_mach, abfd);
  if (fn == NULL)
    return;

  if (buf_len == 0)
    return;

  disassemble_info info;

  INIT_DISASSEMBLE_INFO (info, NULL, nop_fprintf, nop_fprintf_styled);

  info.arch = arc;
  info.mach = valid_mach;
  info.endian = big ? BFD_ENDIAN_BIG : BFD_ENDIAN_LITTLE;
  info.endian_code = info.endian;

  info.buffer = (bfd_byte *) buf;
  info.buffer_vma = 0;
  info.buffer_length = (unsigned int) buf_len;
  info.section = NULL;

  bfd_vma addr = 0;
  int iterations = 0;
  while (addr < buf_len && iterations < 8)
    {
      int consumed = fn (addr, &info);
      if (consumed <= 0)
        consumed = 1;
      addr += (bfd_vma) consumed;
      iterations++;
    }

  disassemble_free_target (&info);
}

int
LLVMFuzzerTestOneInput (const uint8_t *Data, size_t Size)
{
  if (Size < 8)
    return 0;

  uint8_t arc_byte = Data[0];
  uint8_t big_byte = Data[1];
  uint32_t mach_raw;
  memcpy (&mach_raw, Data + 2, sizeof (mach_raw));

  enum bfd_architecture arc =
    (enum bfd_architecture) (arc_byte % (unsigned int) bfd_arch_last);
  bool big = (big_byte & 1) != 0;
  unsigned long mach = (unsigned long) mach_raw;

  const uint8_t *code_data = Data + 6;
  size_t code_len = Size - 6;

  try_disassemble (arc, big, mach, NULL, code_data, code_len);

  FILE *f = fopen ("./dummy_file", "wb");
  if (f != NULL)
    {
      fwrite (Data, 1, Size, f);
      fclose (f);

      bfd_init ();

      bfd *abfd = bfd_openr ("./dummy_file", NULL);
      if (abfd != NULL)
        {
          bool fmt_ok = bfd_check_format (abfd, bfd_object);

          if (fmt_ok && abfd->arch_info != NULL)
            {
              enum bfd_architecture abfd_arc = abfd->arch_info->arch;
              unsigned long abfd_mach = abfd->arch_info->mach;

              try_disassemble (abfd_arc, big, abfd_mach, abfd,
                                code_data, code_len);
            }

          bfd_close (abfd);
        }
    }

  if (Size >= 16)
    {
      uint8_t arc_byte2 = Data[8];
      uint8_t big_byte2 = Data[9];
      uint32_t mach_raw2;
      memcpy (&mach_raw2, Data + 10, sizeof (mach_raw2));

      enum bfd_architecture arc2 =
        (enum bfd_architecture) (arc_byte2 % (unsigned int) bfd_arch_last);
      bool big2 = (big_byte2 & 1) != 0;
      unsigned long mach2 = (unsigned long) mach_raw2;

      const uint8_t *code_data2 = Data + 14;
      size_t code_len2 = Size - 14;

      try_disassemble (arc2, big2, mach2, NULL, code_data2, code_len2);
    }

  return 0;
}