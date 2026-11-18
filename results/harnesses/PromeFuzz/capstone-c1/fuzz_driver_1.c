#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <capstone/capstone.h>

static const cs_arch arch_list[] = {
    CS_ARCH_X86,
    CS_ARCH_ARM,
    CS_ARCH_AARCH64,
    CS_ARCH_MIPS,
    CS_ARCH_PPC,
    CS_ARCH_SPARC,
    CS_ARCH_SYSTEMZ,
    CS_ARCH_XCORE,
    CS_ARCH_M68K,
    CS_ARCH_TMS320C64X,
    CS_ARCH_M680X,
    CS_ARCH_EVM,
};

static const cs_mode mode_list[] = {
    CS_MODE_LITTLE_ENDIAN,
    CS_MODE_32,
    CS_MODE_64,
    CS_MODE_ARM,
    CS_MODE_16,
    CS_MODE_THUMB,
    CS_MODE_BIG_ENDIAN,
    CS_MODE_MIPS32,
    CS_MODE_MIPS64,
};

static void try_disasm(cs_arch arch, cs_mode mode, const uint8_t *code,
                        size_t code_size, uint64_t address, size_t count,
                        bool detail_on)
{
    csh handle;
    if (cs_open(arch, mode, &handle) != CS_ERR_OK) {
        return;
    }

    if (detail_on) {
        cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
    } else {
        cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);
    }

    cs_insn *insn = NULL;
    size_t num_insn = cs_disasm(handle, code, code_size, address, count, &insn);

    if (num_insn > 0) {
        for (size_t i = 0; i < num_insn; i++) {

            volatile unsigned int id = insn[i].id;
            volatile uint64_t addr = insn[i].address;
            volatile uint16_t size = insn[i].size;
            (void)id;
            (void)addr;
            (void)size;

            if (detail_on && insn[i].detail != NULL) {
                volatile uint8_t groups_count = insn[i].detail->groups_count;
                (void)groups_count;
            }
        }
        cs_free(insn, num_insn);
    } else {

        cs_errno(handle);
    }

    cs_close(&handle);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size < 1) {
        return 0;
    }

    uint8_t selector = Data[0];
    const uint8_t *code = Data + 1;
    size_t code_size = Size - 1;

    size_t arch_count = sizeof(arch_list) / sizeof(arch_list[0]);
    size_t mode_count = sizeof(mode_list) / sizeof(mode_list[0]);

    size_t arch_idx = selector % arch_count;
    size_t mode_idx = (selector / arch_count) % mode_count;

    cs_arch arch = arch_list[arch_idx];
    cs_mode mode = mode_list[mode_idx];

    uint64_t address = 0x1000;
    size_t count = 0;

    FILE *f = fopen("./dummy_file", "wb");
    if (f) {
        fwrite(Data, 1, Size, f);
        fclose(f);
    }

    try_disasm(arch, mode, code, code_size, address, count, false);

    try_disasm(arch, mode, code, code_size, address, count, true);

    if (code_size > 0) {
        try_disasm(arch, mode, code, code_size, address, 3, false);
    }

    if (Size > 2) {
        uint8_t selector2 = Data[Size - 1];
        size_t arch_idx2 = selector2 % arch_count;
        size_t mode_idx2 = (selector2 / arch_count) % mode_count;
        cs_arch arch2 = arch_list[arch_idx2];
        cs_mode mode2 = mode_list[mode_idx2];
        try_disasm(arch2, mode2, code, code_size, address, 0, true);
    }

    return 0;
}