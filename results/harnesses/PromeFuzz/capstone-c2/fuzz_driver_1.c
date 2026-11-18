#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <capstone/capstone.h>

static void try_disasm(cs_arch arch, cs_mode mode, const uint8_t *data, size_t size, uint64_t address, size_t count, bool detail_on) {
    csh handle;
    cs_err err = cs_open(arch, mode, &handle);
    if (err != CS_ERR_OK) {
        return;
    }

    if (detail_on) {
        cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
    } else {
        cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);
    }

    cs_insn *insn = NULL;
    size_t num_insn = cs_disasm(handle, data, size, address, count, &insn);

    if (num_insn > 0 && insn != NULL) {
        for (size_t i = 0; i < num_insn; i++) {
            cs_insn *cur = &insn[i];
            volatile unsigned int id = cur->id;
            volatile uint64_t addr = cur->address;
            volatile uint16_t sz = cur->size;
            (void)id;
            (void)addr;
            (void)sz;

            if (detail_on && cur->detail != NULL) {
                cs_detail *det = cur->detail;
                volatile uint8_t groups_count = det->groups_count;
                (void)groups_count;
                for (uint8_t g = 0; g < det->groups_count && g < 32; g++) {
                    volatile uint8_t grp = det->groups[g];
                    (void)grp;
                }
            }
        }
        cs_free(insn, num_insn);
    } else {
        cs_errno(handle);
    }

    cs_close(&handle);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size < 4) {
        return 0;
    }

    uint8_t byte0 = Data[0];
    uint8_t byte1 = Data[1];
    uint8_t byte2 = Data[2];
    uint8_t byte3 = Data[3];

    const uint8_t *code = Data + 4;
    size_t code_size = Size - 4;

    uint64_t address = ((uint64_t)byte0 << 8) | byte1;
    size_t count = (byte2 % 8);
    bool detail_on = (byte3 & 0x1) != 0;

    uint8_t arch_choice = byte3 % 8;

    switch (arch_choice) {
        case 0:
            try_disasm(CS_ARCH_X86, CS_MODE_32, code, code_size, address, count, detail_on);
            try_disasm(CS_ARCH_X86, CS_MODE_64, code, code_size, address, count, detail_on);
            try_disasm(CS_ARCH_X86, CS_MODE_16, code, code_size, address, count, detail_on);
            break;
        case 1:
            try_disasm(CS_ARCH_ARM, CS_MODE_ARM, code, code_size, address, count, detail_on);
            try_disasm(CS_ARCH_ARM, CS_MODE_THUMB, code, code_size, address, count, detail_on);
            break;
        case 2:
            try_disasm(CS_ARCH_ARM64, CS_MODE_ARM, code, code_size, address, count, detail_on);
            break;
        case 3:
            try_disasm(CS_ARCH_MIPS, CS_MODE_MIPS32, code, code_size, address, count, detail_on);
            try_disasm(CS_ARCH_MIPS, CS_MODE_MIPS64, code, code_size, address, count, detail_on);
            break;
        case 4:
            try_disasm(CS_ARCH_PPC, CS_MODE_32, code, code_size, address, count, detail_on);
            try_disasm(CS_ARCH_PPC, CS_MODE_64, code, code_size, address, count, detail_on);
            break;
        case 5:
            try_disasm(CS_ARCH_SPARC, CS_MODE_BIG_ENDIAN, code, code_size, address, count, detail_on);
            break;
        case 6:
            try_disasm(CS_ARCH_SYSZ, CS_MODE_BIG_ENDIAN, code, code_size, address, count, detail_on);
            break;
        case 7:
        default:
            try_disasm(CS_ARCH_RISCV, CS_MODE_RISCV32, code, code_size, address, count, detail_on);
            try_disasm(CS_ARCH_RISCV, CS_MODE_RISCV64, code, code_size, address, count, detail_on);
            break;
    }

    FILE *fp = fopen("./dummy_file", "wb");
    if (fp != NULL) {
        fwrite(Data, 1, Size, fp);
        fclose(fp);
    }

    return 0;
}