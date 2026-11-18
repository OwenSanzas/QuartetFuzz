#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <capstone/capstone.h>

struct cs_fuzz_platform {
    cs_arch arch;
    cs_mode mode;
};

static const struct cs_fuzz_platform g_platforms[] = {
    { CS_ARCH_X86, CS_MODE_32 },
    { CS_ARCH_X86, CS_MODE_64 },
    { CS_ARCH_X86, CS_MODE_16 },
    { CS_ARCH_ARM, CS_MODE_ARM },
    { CS_ARCH_ARM, CS_MODE_THUMB },
    { CS_ARCH_ARM, (cs_mode)(CS_MODE_ARM + CS_MODE_V8) },
    { CS_ARCH_ARM, (cs_mode)(CS_MODE_THUMB + CS_MODE_V8) },
    { CS_ARCH_ARM, (cs_mode)(CS_MODE_THUMB + CS_MODE_MCLASS) },
    { CS_ARCH_ARM64, (cs_mode)0 },
    { CS_ARCH_MIPS, (cs_mode)(CS_MODE_MIPS32 + CS_MODE_BIG_ENDIAN) },
    { CS_ARCH_MIPS, (cs_mode)(CS_MODE_MIPS32 + CS_MODE_MICRO) },
    { CS_ARCH_MIPS, CS_MODE_MIPS64 },
    { CS_ARCH_MIPS, CS_MODE_MIPS32 },
    { CS_ARCH_MIPS, (cs_mode)(CS_MODE_MIPS64 + CS_MODE_BIG_ENDIAN) },
    { CS_ARCH_MIPS, (cs_mode)(CS_MODE_MIPS32 + CS_MODE_MICRO + CS_MODE_BIG_ENDIAN) },
    { CS_ARCH_MIPS, (cs_mode)(CS_MODE_MIPS32R6 + CS_MODE_BIG_ENDIAN) },
    { CS_ARCH_MIPS, (cs_mode)(CS_MODE_MIPS32R6 + CS_MODE_MICRO + CS_MODE_BIG_ENDIAN) },
    { CS_ARCH_MIPS, CS_MODE_MIPS32R6 },
    { CS_ARCH_MIPS, (cs_mode)(CS_MODE_MIPS32R6 + CS_MODE_MICRO) },
    { CS_ARCH_PPC, (cs_mode)(CS_MODE_64 | CS_MODE_BIG_ENDIAN) },
    { CS_ARCH_PPC, (cs_mode)(CS_MODE_64 | CS_MODE_BIG_ENDIAN | CS_MODE_QPX) },
    { CS_ARCH_SPARC, CS_MODE_BIG_ENDIAN },
    { CS_ARCH_SPARC, (cs_mode)(CS_MODE_BIG_ENDIAN + CS_MODE_V9) },
    { CS_ARCH_SYSZ, (cs_mode)0 },
    { CS_ARCH_XCORE, (cs_mode)0 },
    { CS_ARCH_M68K, (cs_mode)0 },
    { CS_ARCH_M68K, CS_MODE_M68K_040 },
    { CS_ARCH_M680X, (cs_mode)CS_MODE_M680X_6809 },
    { CS_ARCH_M680X, (cs_mode)CS_MODE_M680X_6800 },
    { CS_ARCH_M680X, (cs_mode)CS_MODE_M680X_6801 },
    { CS_ARCH_M680X, (cs_mode)CS_MODE_M680X_6805 },
    { CS_ARCH_M680X, (cs_mode)CS_MODE_M680X_6808 },
    { CS_ARCH_M680X, (cs_mode)CS_MODE_M680X_6811 },
    { CS_ARCH_M680X, (cs_mode)CS_MODE_M680X_CPU12 },
    { CS_ARCH_M680X, (cs_mode)CS_MODE_M680X_6301 },
    { CS_ARCH_M680X, (cs_mode)CS_MODE_M680X_6309 },
    { CS_ARCH_M680X, (cs_mode)CS_MODE_M680X_HCS08 },
    { CS_ARCH_EVM, (cs_mode)0 },
    { CS_ARCH_MOS65XX, (cs_mode)0 },
    { CS_ARCH_TMS320C64X, CS_MODE_BIG_ENDIAN },
    { CS_ARCH_WASM, (cs_mode)0 },
    { CS_ARCH_BPF, (cs_mode)(CS_MODE_LITTLE_ENDIAN | CS_MODE_BPF_CLASSIC) },
    { CS_ARCH_BPF, (cs_mode)(CS_MODE_LITTLE_ENDIAN | CS_MODE_BPF_EXTENDED) },
    { CS_ARCH_BPF, (cs_mode)(CS_MODE_BIG_ENDIAN | CS_MODE_BPF_CLASSIC) },
    { CS_ARCH_BPF, (cs_mode)(CS_MODE_BIG_ENDIAN | CS_MODE_BPF_EXTENDED) },
    { CS_ARCH_RISCV, CS_MODE_RISCV32 },
    { CS_ARCH_RISCV, CS_MODE_RISCV64 },
    { CS_ARCH_TRICORE, (cs_mode)(CS_MODE_32 | CS_MODE_BIG_ENDIAN | CS_MODE_TRICORE_110) },
    { CS_ARCH_TRICORE, (cs_mode)(CS_MODE_32 | CS_MODE_BIG_ENDIAN | CS_MODE_TRICORE_160) },
};

#define NUM_PLATFORMS (sizeof(g_platforms) / sizeof(g_platforms[0]))

static void consume_insn(csh handle, cs_insn *insn) {
    volatile size_t acc = 0;

    acc += (size_t)insn->address;
    acc += insn->size;
    acc += insn->bytes[0];
    acc += strlen(insn->mnemonic);
    acc += strlen(insn->op_str);
    (void)cs_insn_name(handle, insn->id);

    if (insn->detail) {
        cs_detail *detail = insn->detail;
        int n;
        for (n = 0; n < detail->regs_read_count; n++) {
            const char *name = cs_reg_name(handle, detail->regs_read[n]);
            if (name) acc += strlen(name);
        }
        for (n = 0; n < detail->regs_write_count; n++) {
            const char *name = cs_reg_name(handle, detail->regs_write[n]);
            if (name) acc += strlen(name);
        }
        for (n = 0; n < detail->groups_count; n++) {
            const char *name = cs_group_name(handle, detail->groups[n]);
            if (name) acc += strlen(name);
            (void)cs_insn_group(handle, insn, detail->groups[n]);
        }

        cs_regs regs_read, regs_write;
        uint8_t regs_read_count = 0, regs_write_count = 0;
        cs_regs_access(handle, insn, regs_read, &regs_read_count,
                        regs_write, &regs_write_count);
        for (n = 0; n < regs_read_count; n++) {
            cs_reg_read(handle, insn, regs_read[n]);
        }
        for (n = 0; n < regs_write_count; n++) {
            cs_reg_write(handle, insn, regs_write[n]);
        }

        cs_op_type optypes[3] = { CS_OP_REG, CS_OP_IMM, CS_OP_MEM };
        for (n = 0; n < 3; n++) {
            int cnt = cs_op_count(handle, insn, optypes[n]);
            if (cnt > 0) {
                cs_op_index(handle, insn, optypes[n], cnt);
            }
        }
    }
    (void)acc;
}

static size_t skipdata_cb(const uint8_t *code, size_t code_size,
                           size_t offset, void *user_data) {
    (void)code;
    (void)user_data;
    if (offset + 1 >= code_size) return 0;
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4) return 0;

    uint8_t ctrl0 = data[0];
    uint8_t ctrl1 = data[1];
    uint8_t ctrl2 = data[2];
    uint8_t ctrl3 = data[3];

    const uint8_t *code = data + 4;
    size_t code_size = size - 4;

    struct cs_fuzz_platform p = g_platforms[ctrl0 % NUM_PLATFORMS];

    csh handle;
    cs_err err = cs_open(p.arch, p.mode, &handle);
    if (err != CS_ERR_OK) {
        return 0;
    }

    unsigned syntax_sel = ctrl1 & 0x3;
    cs_opt_value syntax;
    switch (syntax_sel) {
        case 0: syntax = CS_OPT_SYNTAX_DEFAULT; break;
        case 1: syntax = CS_OPT_SYNTAX_INTEL; break;
        case 2: syntax = CS_OPT_SYNTAX_ATT; break;
        default: syntax = CS_OPT_SYNTAX_NOREGNAME; break;
    }
    cs_option(handle, CS_OPT_SYNTAX, syntax);

    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

    if (ctrl1 & 0x4) {
        cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);
        if (ctrl1 & 0x8) {
            cs_opt_skipdata setup;
            memset(&setup, 0, sizeof(setup));
            setup.mnemonic = ".byte";
            setup.callback = skipdata_cb;
            setup.user_data = NULL;
            cs_option(handle, CS_OPT_SKIPDATA_SETUP, (size_t)&setup);
        }
    }
    if (ctrl1 & 0x10) {
        cs_option(handle, CS_OPT_UNSIGNED, CS_OPT_ON);
    }
    if (ctrl1 & 0x20) {
        cs_option(handle, CS_OPT_NO_BRANCH_OFFSET, CS_OPT_ON);
    }

    char mnem_buf[32];
    if (ctrl3 == 0xABu) {
        cs_opt_mnem mnem;
        size_t len = code_size < sizeof(mnem_buf) - 1 ? code_size : sizeof(mnem_buf) - 1;
        memcpy(mnem_buf, code, len);
        mnem_buf[len] = '\0';
        mnem.id = ctrl2;
        mnem.mnemonic = mnem_buf;
        cs_option(handle, CS_OPT_MNEMONIC, (size_t)&mnem);
    }

    uint64_t address = ((uint64_t)ctrl2) << 8 | ctrl3;

    size_t count_req = 0;
    if ((ctrl1 & 0xC0) == 0xC0) {
        count_req = (ctrl2 % 8) + 1;
    }

    cs_insn *all_insn = NULL;
    size_t count = cs_disasm(handle, code, code_size, address, count_req, &all_insn);

    if (count > 0 && all_insn != NULL) {
        size_t j;
        for (j = 0; j < count; j++) {
            consume_insn(handle, &all_insn[j]);
        }
        cs_free(all_insn, count);
    } else {

        (void)cs_errno(handle);
    }

    if (ctrl1 & 0x40) {
        cs_insn *iter_insn = cs_malloc(handle);
        if (iter_insn) {
            const uint8_t *icode = code;
            size_t isize = code_size;
            uint64_t iaddr = address;
            int iterations = 0;
            while (iterations < 64 &&
                   cs_disasm_iter(handle, &icode, &isize, &iaddr, iter_insn)) {
                consume_insn(handle, iter_insn);
                iterations++;
            }
            cs_free(iter_insn, 1);
        }
    }

    cs_mode mode2 = (cs_mode)(p.mode ^ 1u);
    if (cs_option(handle, CS_OPT_MODE, mode2) == CS_ERR_OK) {
        size_t half = code_size / 2;
        cs_insn *all_insn2 = NULL;
        size_t count2 = cs_disasm(handle, code + half, code_size - half,
                                   address + half, 0, &all_insn2);
        if (count2 > 0 && all_insn2 != NULL) {
            size_t j;
            for (j = 0; j < count2; j++) {
                consume_insn(handle, &all_insn2[j]);
            }
            cs_free(all_insn2, count2);
        }
    }

    cs_close(&handle);
    return 0;
}
