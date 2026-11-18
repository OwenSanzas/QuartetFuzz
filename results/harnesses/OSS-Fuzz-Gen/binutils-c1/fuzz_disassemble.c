#include "sysdep.h"
#include "bfd.h"
#include "dis-asm.h"
#include "disassemble.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_TEXT_SIZE 256
#define MAX_BUFFER_SIZE 16384
#define MAX_STEPS 4096
#define OPTS_SIZE 64

typedef struct
{
  char *buffer;
  size_t pos;
} SFILE;

static int
fuzz_disasm_null_styled_printf (void *stream,
             enum disassembler_style style,
             const char *format, ...)
{
  (void) stream;
  (void) style;
  (void) format;
  return 0;
}

static int
objdump_sprintf (void *vf, const char *format, ...)
{
  SFILE *f = (SFILE *) vf;
  size_t n;
  va_list args;

  va_start (args, format);
  if (f->pos >= MAX_TEXT_SIZE)
    {
      f->pos = 0;
      va_end (args);
      return 0;
    }
  n = vsnprintf (f->buffer + f->pos, MAX_TEXT_SIZE - f->pos, format, args);
  va_end (args);
  f->pos += n;
  return (int) n;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

  const size_t header_size =
      sizeof(uint32_t) + 1 + sizeof(uint64_t) + 1 + OPTS_SIZE;
  if (data == NULL || size < header_size + 1) {
    return 0;
  }

  size_t offset = 0;

  uint32_t arc_raw;
  memcpy(&arc_raw, data + offset, sizeof(arc_raw));
  offset += sizeof(arc_raw);

  uint8_t big_raw = data[offset];
  offset += 1;

  uint64_t mach_raw;
  memcpy(&mach_raw, data + offset, sizeof(mach_raw));
  offset += sizeof(mach_raw);

  uint8_t mach_mode = data[offset];
  offset += 1;

  char options[OPTS_SIZE + 1];
  memcpy(options, data + offset, OPTS_SIZE);
  options[OPTS_SIZE] = '\0';
  offset += OPTS_SIZE;

  const uint8_t *buf_data = data + offset;
  size_t buf_size = size - offset;
  if (buf_size > MAX_BUFFER_SIZE) {
    buf_size = MAX_BUFFER_SIZE;
  }
  if (buf_size == 0) {
    return 0;
  }

  enum bfd_architecture arc = (enum bfd_architecture) (arc_raw % (unsigned int) bfd_arch_last);
  bool big = (big_raw & 1) != 0;

  unsigned long mach;
  if (mach_mode & 1) {
    mach = (unsigned long) mach_raw;
  } else {
    mach = (unsigned long) (mach_raw & 0xFFUL);
  }

  if (bfd_lookup_arch(arc, mach) == NULL) {
    return 0;
  }

  bfd_init();

  bfd *abfd = NULL;
  char tmpname[] = "/tmp/fuzz-disas-XXXXXX";
  int fd = mkstemp(tmpname);
  if (fd >= 0) {
    ssize_t written = write(fd, buf_data, buf_size);
    (void) written;
    close(fd);

    bfd *maybe = bfd_openr(tmpname, NULL);
    if (maybe != NULL) {
      if (bfd_check_format(maybe, bfd_object)) {
        abfd = maybe;
      } else {
        bfd_close(maybe);
      }
    }
    remove(tmpname);
  }

  disassembler_ftype disasfunc = disassembler(arc, big, mach, abfd);
  if (disasfunc == NULL) {
    if (abfd != NULL) {
      bfd_close(abfd);
    }
    return 0;
  }

  char AssemblyText[MAX_TEXT_SIZE];
  struct disassemble_info disasm_info;
  SFILE s;

  init_disassemble_info (&disasm_info, stdout, (fprintf_ftype) fprintf,
                          fuzz_disasm_null_styled_printf);
  disasm_info.fprintf_func = objdump_sprintf;
  disasm_info.print_address_func = generic_print_address;
  disasm_info.display_endian = disasm_info.endian =
      big ? BFD_ENDIAN_BIG : BFD_ENDIAN_LITTLE;
  disasm_info.buffer = (bfd_byte *) buf_data;
  disasm_info.buffer_vma = 0x1000;
  disasm_info.buffer_length = buf_size;
  disasm_info.insn_info_valid = 0;
  disasm_info.disassembler_options = options;
  disasm_info.created_styled_output = false;

  s.buffer = AssemblyText;
  s.pos = 0;
  disasm_info.stream = &s;
  disasm_info.bytes_per_line = 0;

  disasm_info.flags |= USER_SPECIFIED_MACHINE_TYPE;
  disasm_info.arch = arc;
  disasm_info.mach = mach;

  disassemble_init_for_target(&disasm_info);

  int steps = 0;
  while (steps < MAX_STEPS) {
    s.pos = 0;
    int octets = disasfunc(disasm_info.buffer_vma, &disasm_info);
    if (octets < (int) disasm_info.octets_per_byte) {
      break;
    }
    if (disasm_info.buffer_length <= (size_t) octets) {
      break;
    }
    disasm_info.buffer += octets;
    disasm_info.buffer_vma += octets / disasm_info.octets_per_byte;
    disasm_info.buffer_length -= octets;
    steps++;
  }

  disassemble_free_target(&disasm_info);

  if (abfd != NULL) {
    bfd_close(abfd);
  }

  return 0;
}