#if defined(_MSC_VER) && _MSC_VER < 1900
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>
#include <stddef.h>

#include <capstone/capstone.h>

#include "platform.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);

static FILE *outfile = NULL;

struct platform extra_platforms[] = {

	{ CS_ARCH_SH,
	  (cs_mode)(CS_MODE_LITTLE_ENDIAN | CS_MODE_SH4 | CS_MODE_SHFPU),
	  "SuperH SH4", "sh4" },
	{ CS_ARCH_SH, (cs_mode)(CS_MODE_BIG_ENDIAN | CS_MODE_SH2), "SuperH SH2 BE",
	  "sh2be" },
	{ CS_ARCH_SH, (cs_mode)(CS_MODE_SH2A | CS_MODE_SHDSP), "SuperH SH2A+DSP",
	  "sh2adsp" },
#if CS_API_MAJOR >= 6
	{ CS_ARCH_ALPHA, CS_MODE_LITTLE_ENDIAN, "Alpha LE", "alpha" },
	{ CS_ARCH_ALPHA, CS_MODE_BIG_ENDIAN, "Alpha BE", "alphabe" },
	{ CS_ARCH_HPPA, (cs_mode)(CS_MODE_HPPA_11 | CS_MODE_LITTLE_ENDIAN),
	  "HPPA 1.1 LE", "hppa11" },
	{ CS_ARCH_HPPA, (cs_mode)(CS_MODE_HPPA_20 | CS_MODE_BIG_ENDIAN),
	  "HPPA 2.0 BE", "hppa20be" },
	{ CS_ARCH_HPPA, (cs_mode)(CS_MODE_HPPA_20W | CS_MODE_LITTLE_ENDIAN),
	  "HPPA 2.0W LE", "hppa20w" },
	{ CS_ARCH_LOONGARCH, CS_MODE_LOONGARCH32, "LoongArch32", "loongarch32" },
	{ CS_ARCH_LOONGARCH, CS_MODE_LOONGARCH64, "LoongArch64", "loongarch64" },
	{ CS_ARCH_ARC, CS_MODE_LITTLE_ENDIAN, "ARC LE", "arc" },
#endif
};
#define EXTRA_PLATFORM_COUNT \
	(sizeof(extra_platforms) / sizeof(extra_platforms[0]))

static size_t fuzz_skipdata_cb(const uint8_t *code, size_t code_size,
				size_t offset, void *user_data)
{
	uint8_t ctrl = *(uint8_t *)user_data;

	(void)code;
	(void)code_size;
	(void)offset;

	if (ctrl & 0x80)
		return 0;

	return (size_t)((ctrl & 0x0F) + 1);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	csh handle;
	cs_insn *all_insn;
	cs_detail *detail;
	cs_err err;
	unsigned int i;

	if (Size < 2) {

		return 0;
	} else if (Size > 0x1000) {

		Size = 0x1000;
	}

	if (outfile == NULL) {

		outfile = fopen("/dev/null", "w");
		if (outfile == NULL) {
			return 0;
		}
	}

	unsigned int base_len = platform_len();
	unsigned int total_len = base_len + (unsigned int)EXTRA_PLATFORM_COUNT;
	unsigned int sel = (unsigned int)Data[0] % total_len;

	cs_arch arch;
	cs_mode mode;

	if (sel < base_len) {
		i = sel;
		arch = platforms[i].arch;
		mode = platforms[i].mode;
	} else {
		arch = extra_platforms[sel - base_len].arch;
		mode = extra_platforms[sel - base_len].mode;
	}

	err = cs_open(arch, mode, &handle);
	if (err) {
		return 0;
	}

	uint8_t opt_byte = Data[1];

	cs_option(handle, CS_OPT_DETAIL,
		  (opt_byte & 0x01) ? CS_OPT_ON : CS_OPT_OFF);

	if (opt_byte & 0x02) {
		cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);
	}

	if (opt_byte & 0x04) {
		cs_option(handle, CS_OPT_UNSIGNED, CS_OPT_ON);
	}

	if (opt_byte & 0x80) {
		cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
	} else if (opt_byte & 0x40) {
		cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_NOREGNAME);
	} else if (opt_byte & 0x20) {
		cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_MASM);
	} else if (opt_byte & 0x10) {
		cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_MOTOROLA);
	} else if (opt_byte & 0x08) {
		cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_CS_REG_ALIAS);
	}

	uint64_t address = 0x1000;
	size_t count = 0;
	size_t header_len = 2;

	if (Size >= 10) {
		memcpy(&address, Data + 2, 8);
		header_len = 10;
	}

	if (Size >= 11) {

		count = Data[10] % 8;
		header_len = 11;
	}

	uint8_t opt_byte2 = 0;
	uint8_t extra_byte = 0;
	static uint8_t skip_ctrl_byte;
	static cs_opt_skipdata skip_opt;
	static cs_opt_mnem mnem_opt;
	static char mnem_str[16];

	if (Size >= 12) {
		opt_byte2 = Data[11];
		header_len = 12;
	}
	if (Size >= 13) {
		extra_byte = Data[12];
		header_len = 13;
	}

	if ((opt_byte & 0x02) && (opt_byte2 & 0x01)) {

		skip_ctrl_byte = extra_byte;
		skip_opt.mnemonic = NULL;
		skip_opt.callback = fuzz_skipdata_cb;
		skip_opt.user_data = &skip_ctrl_byte;
		cs_option(handle, CS_OPT_SKIPDATA_SETUP, (size_t)&skip_opt);
	}

	if (opt_byte2 & 0x02) {

		mnem_opt.id = extra_byte ? extra_byte : 1;
		snprintf(mnem_str, sizeof(mnem_str), "fz%u",
			 (unsigned int)extra_byte);
		mnem_opt.mnemonic = mnem_str;
		cs_option(handle, CS_OPT_MNEMONIC, (size_t)&mnem_opt);

		if (extra_byte & 0x01) {

			mnem_opt.mnemonic = NULL;
			cs_option(handle, CS_OPT_MNEMONIC, (size_t)&mnem_opt);
		}
	}

	if (opt_byte2 & 0x04) {

		cs_option(handle, CS_OPT_ONLY_OFFSET_BRANCH, CS_OPT_ON);
	}

	if (opt_byte2 & 0x08) {

		cs_option(handle, CS_OPT_LITBASE, (size_t)address);
	}

	if (Size >= 14) {

		uint8_t mode_byte = Data[13];
		cs_option(handle, CS_OPT_MODE,
			  (size_t)(mode ^ (cs_mode)(mode_byte & 0x07)));
		header_len = 14;
	}

	const uint8_t *code = Data + header_len;
	size_t code_size = Size - header_len;

	size_t num_insn = cs_disasm(handle, code, code_size, address, count, &all_insn);

	if (num_insn) {
		size_t j;
		unsigned int n;

		for (j = 0; j < num_insn; j++) {
			cs_insn *insn = &(all_insn[j]);
			fprintf(outfile,
				"0x%" PRIx64 ":\t%s\t\t%s // insn-ID: %u, insn-mnem: %s\n",
				insn->address, insn->mnemonic, insn->op_str,
				insn->id, cs_insn_name(handle, insn->id));

			detail = insn->detail;

			if (detail != NULL) {
				if (detail->regs_read_count > 0) {
					fprintf(outfile, "\tImplicit registers read: ");
					for (n = 0; n < detail->regs_read_count; n++) {
						fprintf(outfile, "%s ",
							cs_reg_name(handle, detail->regs_read[n]));
					}
				}

				if (detail->regs_write_count > 0) {
					fprintf(outfile, "\tImplicit registers modified: ");
					for (n = 0; n < detail->regs_write_count; n++) {
						fprintf(outfile, "%s ",
							cs_reg_name(handle, detail->regs_write[n]));
					}
				}

				if (detail->groups_count > 0) {
					fprintf(outfile, "\tThis instruction belongs to groups: ");
					for (n = 0; n < detail->groups_count; n++) {
						fprintf(outfile, "%s ",
							cs_group_name(handle, detail->groups[n]));
					}
				}
			}
		}

		fprintf(outfile, "0x%" PRIx64 ":\n",
			all_insn[j - 1].address + all_insn[j - 1].size);
		cs_free(all_insn, num_insn);
	}

	cs_close(&handle);

	return 0;
}