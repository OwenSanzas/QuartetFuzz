#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include <capstone/capstone.h>

#include "platform.h"

static const cs_mode sh_modes[] = {
    CS_MODE_SH2,
    CS_MODE_SH2A,
    CS_MODE_SH3,
    (cs_mode) (CS_MODE_SH4 | CS_MODE_SHFPU),
    (cs_mode) (CS_MODE_SH4A | CS_MODE_SHDSP),
    (cs_mode) (CS_MODE_SH2 | CS_MODE_BIG_ENDIAN),
    (cs_mode) (CS_MODE_SH4 | CS_MODE_BIG_ENDIAN),
};
#define SH_MODES_COUNT (sizeof(sh_modes) / sizeof(sh_modes[0]))

static size_t my_skipdata_cb(const uint8_t *code, size_t code_size,
                              size_t offset, void *user_data) {
    uintptr_t mode = (uintptr_t) user_data;
    size_t remaining = (offset < code_size) ? (code_size - offset) : 0;

    switch (mode % 4) {
        case 0:
            return 1;
        case 1:
            return 0;
        case 2:
            return code_size + 100;

        default:
            return (remaining > 4) ? 4 : 1;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    csh handle;
    cs_insn *insn = NULL;
    cs_err err;
    size_t count_result;
    unsigned int num_platforms;
    unsigned int num_total;
    unsigned int entry;
    cs_arch arch;
    cs_mode mode;
    const uint8_t *code;
    size_t code_size;
    uint64_t address;
    uint8_t flags;
    size_t req_count;
    size_t i;

    if (Size < 2) {
        return 0;
    }

    num_platforms = platform_len();
    num_total = num_platforms + (unsigned int) SH_MODES_COUNT;

    entry = (unsigned int) Data[0] % num_total;

    if (entry < num_platforms) {
        arch = platforms[entry].arch;
        mode = platforms[entry].mode;
    } else {
        arch = CS_ARCH_SH;
        mode = sh_modes[entry - num_platforms];
    }

    code = Data + 1;
    code_size = Size - 1;

    flags = (uint8_t) code_size;
    for (i = 0; i < code_size; i++) {
        flags ^= code[i];
        flags = (uint8_t) ((flags << 1) | (flags >> 7));
    }

    req_count = (size_t) (flags & 0x07);

    address = ((uint64_t) flags << 56) ^ 0x1000ULL;

    err = cs_open(arch, mode, &handle);
    if (err != CS_ERR_OK) {

        return 0;
    }

    cs_option(handle, CS_OPT_DETAIL, (flags & 0x04) ? CS_OPT_ON : CS_OPT_OFF);

    cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);

    switch ((flags >> 3) & 0x03) {
        case 0:
            break;
        case 1:
            cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
            break;
        case 2:
            cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_NOREGNAME);
            break;
        case 3:
            cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_MASM);
            break;
    }
    if (flags & 0x80) {
        cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_MOTOROLA);
    }

    if (flags & 0x10) {
        cs_option(handle, CS_OPT_UNSIGNED, CS_OPT_ON);
    }
    if (flags & 0x20) {
        cs_option(handle, CS_OPT_NO_BRANCH_OFFSET, CS_OPT_ON);
    }

    if (flags & 0x40) {
        cs_option(handle, CS_OPT_MODE, (size_t) mode);
    }

    cs_opt_skipdata skipdata_opt;
    memset(&skipdata_opt, 0, sizeof(skipdata_opt));
    skipdata_opt.mnemonic = "fuzzdata";
    skipdata_opt.callback = my_skipdata_cb;
    skipdata_opt.user_data = (void *) (uintptr_t) flags;
    cs_option(handle, CS_OPT_SKIPDATA_SETUP, (size_t) &skipdata_opt);

    cs_opt_mnem mnem_opt;
    mnem_opt.id = 1 + (unsigned int) (flags & 0x0f);
    mnem_opt.mnemonic = "fuzzmnem";
    cs_option(handle, CS_OPT_MNEMONIC, (size_t) &mnem_opt);
    mnem_opt.mnemonic = "fuzzmnem2";
    cs_option(handle, CS_OPT_MNEMONIC, (size_t) &mnem_opt);
    mnem_opt.mnemonic = NULL;
    cs_option(handle, CS_OPT_MNEMONIC, (size_t) &mnem_opt);

    count_result = cs_disasm(handle, code, code_size, address, req_count, &insn);

    if (count_result > 0) {
        cs_free(insn, count_result);
    }

    cs_close(&handle);

    return 0;
}