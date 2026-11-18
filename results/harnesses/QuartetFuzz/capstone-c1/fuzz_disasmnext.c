#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <capstone/capstone.h>
#include "platform.h"

static size_t skipdata_cb(const uint8_t *code, size_t code_size,
			   size_t offset, void *user_data)
{
	(void)code;
	(void)code_size;
	(void)user_data;

	return 1 + (offset % 4);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	if (Size < 5) {

		return 0;
	}

	uint8_t sel = Data[0];
	uint8_t flags = Data[1];
	uint8_t count_hint = Data[2];
	uint8_t mnem_id_hint = Data[3];

	const uint8_t *code = Data + 4;
	size_t code_size = Size - 4;
	if (code_size > 0x1000) {

		code_size = 0x1000;
	}

	unsigned int pidx = get_platform_entry(sel);
	cs_arch arch = platforms[pidx].arch;
	cs_mode mode = platforms[pidx].mode;

	csh handle;
	cs_err err = cs_open(arch, mode, &handle);
	if (err != CS_ERR_OK) {
		return 0;
	}

	bool detail_on = (flags & 0x1) != 0;
	cs_option(handle, CS_OPT_DETAIL, detail_on ? CS_OPT_ON : CS_OPT_OFF);

	if (flags & 0x2) {
		cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
	} else if (flags & 0x4) {
		cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_NOREGNAME);
	}

	if (flags & 0x8) {
		cs_option(handle, CS_OPT_UNSIGNED, CS_OPT_ON);
	}

	cs_opt_skipdata skipdata_setup;
	memset(&skipdata_setup, 0, sizeof(skipdata_setup));
	if (flags & 0x10) {
		cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);
		if (flags & 0x20) {
			skipdata_setup.mnemonic = ".byte";
			skipdata_setup.callback = skipdata_cb;
			skipdata_setup.user_data = NULL;
			cs_option(handle, CS_OPT_SKIPDATA_SETUP,
				  (uintptr_t)&skipdata_setup);
		}
	}

	if (flags & 0x40) {
		cs_opt_mnem mnem;
		mnem.id = mnem_id_hint ? mnem_id_hint : 1;
		mnem.mnemonic = "fuzzmn";
		cs_option(handle, CS_OPT_MNEMONIC, (uintptr_t)&mnem);
	}

	uint64_t address = ((uint64_t)flags << 32) | count_hint;

	size_t count = (count_hint & 0x1) ? 0 : (size_t)((count_hint >> 1) + 1);

	cs_insn *insn = NULL;
	size_t n = cs_disasm(handle, code, code_size, address, count, &insn);

	if (n > 0) {
		for (size_t i = 0; i < n; i++) {
			cs_insn *ip = &insn[i];

			volatile size_t acc = 0;
			acc += strlen(ip->mnemonic);
			acc += strlen(ip->op_str);
			for (int b = 0; b < ip->size && b < 16; b++) {
				acc += ip->bytes[b];
			}
			acc += ip->illegal ? 1 : 0;
			acc += ip->is_alias ? 1 : 0;
			acc += ip->usesAliasDetails ? 1 : 0;
			acc += (size_t)(ip->alias_id & 0xff);
			(void)acc;

			const char *name = cs_insn_name(handle, ip->id);
			(void)name;

			if (detail_on && ip->id != 0 && ip->detail != NULL) {
				cs_detail *d = ip->detail;

				for (uint8_t r = 0; r < d->regs_read_count; r++) {
					const char *rn =
						cs_reg_name(handle,
							    d->regs_read[r]);
					(void)rn;
					cs_reg_read(handle, ip,
						    d->regs_read[r]);
				}
				for (uint8_t r = 0; r < d->regs_write_count; r++) {
					const char *rn =
						cs_reg_name(handle,
							    d->regs_write[r]);
					(void)rn;
					cs_reg_write(handle, ip,
						     d->regs_write[r]);
				}
				for (uint8_t g = 0; g < d->groups_count; g++) {
					const char *gn =
						cs_group_name(handle,
							      d->groups[g]);
					(void)gn;
					cs_insn_group(handle, ip,
						      d->groups[g]);
				}

				cs_regs regs_read, regs_write;
				uint8_t rr_count = 0, rw_count = 0;
				cs_regs_access(handle, ip, regs_read, &rr_count,
						regs_write, &rw_count);
			}
		}
		cs_free(insn, n);
	} else {

		cs_err e = cs_errno(handle);
		(void)e;
	}

	cs_close(&handle);
	return 0;
}
