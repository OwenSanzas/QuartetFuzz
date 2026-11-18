## target: cs_disasm

### P2.0 Main path to keep (every input) — read first
**Claim:** The call sequence that reaches the most of cs_disasm's core is: `cs_open(arch, mode)` over a broad arch/mode table → `cs_option(CS_OPT_SYNTAX, input-chosen syntax)` → `cs_option(CS_OPT_DETAIL, CS_OPT_ON)` (always) + `CS_OPT_SKIPDATA`/`CS_OPT_UNSIGNED` from input bits → `cs_disasm(handle, code, size, address, count, &insn)` → consume insns (`cs_insn_name`, `cs_reg_name`/`cs_group_name` over detail) → `cs_free` → `cs_option(CS_OPT_MODE, other mode of same arch)` → second `cs_disasm` on remaining bytes → `cs_free` → `cs_close`. The syntax option selects a different printer per instruction and is the single largest coverage driver after arch/mode.
**Rule for the harness:** keep every step of this main path on every input, fed with the full fuzz input; Logic-Group entries and the other sections of this report only add calls on top of it (selected by spare input bits), they never replace or dilute a main-path step.
**Evidence:**
> arch/X86/X86Module.c:61-90: `case CS_OPT_SYNTAX: ... case CS_OPT_SYNTAX_ATT: handle->printer = X86_ATT_printInst; handle->syntax = CS_OPT_SYNTAX_ATT;` vs `CS_OPT_SYNTAX_INTEL/DEFAULT: handle->printer = X86_Intel_printInst;` and `CS_OPT_SYNTAX_MASM`.
> arch/ARM/ARMModule.c:55-57: `case CS_OPT_SYNTAX: ARM_getRegName(handle, (int)value); handle->syntax = (int)value;` (NOREGNAME register naming).
> arch/PowerPC/PPCInstPrinter.c:1190: `if (MI->csh->syntax == CS_OPT_SYNTAX_NOREGNAME)`; arch/MOS65XX/MOS65XXModule.c:64-73 `CS_OPT_SYNTAX_MOTOROLA`.
> include/capstone/capstone.h:279-299: `CS_OPT_SYNTAX`, `CS_OPT_MODE`, `CS_OPT_UNSIGNED`, `CS_OPT_NO_BRANCH_OFFSET`, `CS_OPT_SYNTAX_{DEFAULT,INTEL,ATT,NOREGNAME,MASM,MOTOROLA}`.
> tests/test_x86.c:392-393, tests/test_detail.c:95-96, tests/test_iter.c:97-98, tests/test_basic.c:118 (`CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT`); tests/test_arm.c:222,255 (NOREGNAME); tests/test_mos65xx.c:191 (MOTOROLA); tests/test_x86.c:424-426 applies the per-platform option then `CS_OPT_DETAIL, CS_OPT_ON`.
> cs.c:824-829 + arch/X86/X86Module.c:53-59, arch/ARM/ARMModule.c:46-53: `CS_OPT_MODE` changes the mode / decoder of a live handle (validated against `arch_disallowed_mode_mask`).
> suite/fuzz/platform.c: the project's arch/mode table (~50 entries incl. TMS320C64X, M680X variants, SH, TRICORE, BPF classic/extended).

### P2.1 Init Sequence
**Claim:** Before calling `cs_disasm`, the caller must (1) optionally call `cs_option(0, CS_OPT_MEM, ...)` to install a custom allocator (rarely used by fuzz targets — if skipped, capstone's default allocator from `cs_open`'s check is used), then (2) call `cs_open(arch, mode, &handle)` to get a valid `csh`, and (3) optionally call `cs_option(handle, ...)` (e.g. `CS_OPT_DETAIL`, `CS_OPT_SKIPDATA`, `CS_OPT_SKIPDATA_SETUP`, `CS_OPT_UNSIGNED`, `CS_OPT_MNEMONIC`) to configure behavior. `cs_open` must succeed (return `CS_ERR_OK`) before `cs_disasm` is called with that handle.
**Evidence:**
> cs.c:474-477: `if (!cs_mem_malloc || !cs_mem_calloc || !cs_mem_realloc || !cs_mem_free || !cs_vsnprintf) return CS_ERR_MEMSETUP;` inside `cs_open` — shows memory hooks are set by default at static init (see `cs.c` globals) so `cs_option(CS_OPT_MEM,...)` is optional in practice.
> cs.c:479-514: `cs_open` validates `arch`/`mode`, allocates `cs_struct` via `cs_mem_calloc`, calls `arch_configs[arch].arch_init(ud)` which installs `handle->disasm`/`handle->printer`/`handle->insn_id` function pointers that `cs_disasm` later calls.
> cs.c:889-894: `cs_disasm` casts `ud` to `struct cs_struct *`; `if (!handle) return 0;` — a zero/invalid handle from a failed `cs_open` yields 0 instructions, no crash, but is not a valid prior state.
> suite/fuzz/fuzz_harness.c:161-170: real caller order — `cs_open(...)` → `cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON)` → `cs_disasm(handle, buf_ptr, buf_ptr_size, address, 0, &all_insn)`.
> tests/test_skipdata.c:115-128: order — `cs_open` → `cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON)` → `cs_option(handle, CS_OPT_SKIPDATA_SETUP, (size_t)&skipdata)` → `cs_disasm(...)`.

### P2.2 Parameter Construction
**Claim:** `cs_disasm(csh ud, const uint8_t *buffer, size_t size, uint64_t offset, size_t count, cs_insn **insn)` requires: `ud` a valid handle from `cs_open`; `buffer` a non-NULL pointer to at least `size` readable bytes (raw machine code, arbitrary/untrusted bytes, no required header or magic); `size` the number of bytes available (can be 0, loop simply doesn't run); `offset` the base virtual address assigned to the first decoded instruction (arbitrary uint64, used only for relative-branch target computation and reporting, not validated); `count` = 0 means "decode as many instructions as fit in buffer" (no limit), count > 0 stops after that many instructions decoded (also used to presize cache, capped by `INSN_CACHE_SIZE`); `insn` must be a valid `cs_insn **` output parameter (non-NULL) that the function will set to a freshly allocated array (or NULL on failure/0 instructions).
**Evidence:**
> capstone.h:656 (target decl) and cs.c:870 definition signature: `size_t CAPSTONE_API cs_disasm(csh ud, const uint8_t *buffer, size_t size, uint64_t offset, size_t count, cs_insn **insn)`.
> cs.c:902-905: `#ifdef CAPSTONE_USE_SYS_DYN_MEM if (count > 0 && count <= INSN_CACHE_SIZE) cache_size = (unsigned int) count; #endif` — count only affects initial cache sizing, not a hard validation.
> cs.c:922: `while (size > 0) { ... }` — size==0 simply disassembles nothing and returns 0 with `*insn` left untouched unless `total` freed (see P2.4).
> cs.c:1021-1023: `c++; if (count > 0 && c == count) break;` — count>0 stops the loop early.
> suite/fuzz/fuzz_harness.c:169-170: `uint64_t address = 0x1000; size_t count = cs_disasm(handle, buf_ptr, buf_ptr_size, address, 0, &all_insn);` — real caller passes `count=0` (decode everything) and an arbitrary address.

### P2.3 Object Lifecycle
**Claim:** Lifecycle is strictly create → configure → use (possibly repeatedly) → destroy: `cs_open` creates the handle; `cs_option` configures it (can be called multiple times, even interleaved with multiple `cs_disasm`/`cs_disasm_iter` calls on the same handle); `cs_disasm` (or `cs_disasm_iter`) uses it; `cs_free` releases the per-call result array; `cs_close` destroys the handle itself. The handle from `cs_open` may be reused across many `cs_disasm` calls.
**Evidence:**
> cs.c:470-515 `cs_open` (create), cs.c:711+ `cs_option` (configure), cs.c:870 `cs_disasm` (use), cs.c:1088-1098 `cs_free` (free result), cs.c:518-547 `cs_close` (destroy handle).
> suite/fuzz/fuzz_harness.c:161-219: full sequence `cs_open` → `cs_option(DETAIL)` → `cs_disasm` → (consume `all_insn`) → `cs_free(all_insn, count)` → `cs_close(&handle)`.
> tests/test_iter.c:279-341: same handle reused in a loop over platforms, each iteration: `cs_open` → `cs_option` → `cs_malloc` → `cs_disasm_iter` loop → `cs_free(insn,1)` → `cs_close`.

### P2.4 Return Value Handling
**Claim:** `cs_disasm` returns the number of instructions successfully disassembled (`size_t c`, which may be 0). A return of 0 means no instructions were disassembled — `*insn` is set to `NULL` in that case and nothing needs to be freed. On allocation failure mid-stream, it returns 0, sets `*insn = NULL`, sets `handle->errnum = CS_ERR_MEM`, and the caller should call `cs_errno(handle)` to learn why. On success (`return value > 0`), `*insn` points to a `cs_mem_realloc`-sized/`cs_mem_calloc`-allocated array of `cs_insn` of exactly that many elements, which the caller owns and must free with `cs_free`.
**Evidence:**
> cs.c:1057-1060: `if (!c) { cs_mem_free(total); total = NULL; } ... *insn = total; return c;` — 0 count ⇒ `*insn == NULL`.
> cs.c:1029-1041, 1063-1077: mid-stream realloc failures set `*insn = NULL; handle->errnum = CS_ERR_MEM; return 0;`.
> suite/fuzz/fuzz_harness.c:172-214: `if (count) { ... cs_free(all_insn, count); } else { printf("ERROR: Failed to disasm given code!\n"); }` — real caller checks return value before touching `all_insn`/calling `cs_free`.
> include/capstone/capstone.h (cs_errno decl, cs.c:415-428 `cs_errno`): caller can query `cs_errno(handle)` after a 0 return for diagnostics (not required, but documented pattern).

### P2.5 Cleanup Sequence
**Claim:** On success (`count>0`): free the result array with `cs_free(insn, count)` (this frees each `insn[i].detail` if detail was allocated, then the array itself), performed before or independent of `cs_close`. On failure (`count==0`): `*insn` is already `NULL`/untouched-safe — nothing to free from `cs_disasm` itself. Finally call `cs_close(&handle)` to free the handle (frees `printer_info`, `mnem_list` linked list, `insn_cache`, x86 LUTs, then the handle struct itself). Order: `cs_free` (if any instructions were produced) happens any time after use and before/around `cs_close`; `cs_close` must be last since `cs_free` does not touch the handle.
**Evidence:**
> cs.c:1088-1098 `cs_free`: `for (i = 0; i < count; i++) cs_mem_free(insn[i].detail); cs_mem_free(insn);` — frees per-insn detail then array.
> cs.c:518-547 `cs_close`: frees `ud->printer_info`, walks and frees `ud->mnem_list`, frees `ud->insn_cache`, `ud->x86_insn_lut`, `ud->x86_insn_reg_lut`, memsets and frees `ud` itself, sets `*handle = 0`.
> suite/fuzz/fuzz_harness.c:211,219: `cs_free(all_insn, count);` ... later `cs_close(&handle);` — free-then-close order used by real caller.
> tests/test_iter.c:339,341: `cs_free(insn, 1); cs_close(&handle);` same order for the iter variant.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase (`capstonev5`): `cs_disasm`, `cs_open`, `cs_close`, `cs_option`, `cs_disasm_iter`, `cs_malloc`, `cs_free`, `cs_regs_access`, `cs_errno`, `cs_insn_group`, `cs_reg_read`, `cs_reg_write`, `cs_op_count`, `cs_op_index`, `cs_insn_name`, `cs_reg_name`, `cs_group_name`.
**Evidence:**
> cs.c:470 `cs_open`, cs.c:518 `cs_close`, cs.c:711 `cs_option`, cs.c:870 `cs_disasm`, cs.c:1101 `cs_malloc`, cs.c:1129 `cs_disasm_iter`, cs.c:1088 `cs_free`, cs.c:1647 `cs_regs_access`, cs.c:415 `cs_errno`, cs.c:1265 `cs_insn_group`, cs.c:1292 `cs_reg_read`, cs.c:1319 `cs_reg_write`.
> include/capstone/capstone.h:656 `cs_disasm` decl, :882 `cs_regs_access` decl, :839 `cs_op_count`, :858 `cs_op_index`.

### P2.7 Co-call Constraints
**Claim:** Paired APIs: `cs_open` / `cs_close` (handle create/destroy, 1:1, `cs_close` takes `csh*` and zeroes it); `cs_disasm` (array) / `cs_free` (array) must be paired for every successful `cs_disasm` call that returned >0; `cs_malloc` / `cs_free(insn,1)` paired around `cs_disasm_iter` loops. `CS_OPT_SKIPDATA` and `CS_OPT_SKIPDATA_SETUP` are not strictly paired but SKIPDATA_SETUP only has effect once `CS_OPT_SKIPDATA` is turned ON (both configured on the handle before `cs_disasm`).
**Evidence:**
> cs.c:518 `cs_err CAPSTONE_API cs_close(csh *handle)` takes pointer, cs.c:544-547 zeroes/frees it — must match a prior `cs_open`.
> cs.c:1088 `cs_free(cs_insn *insn, size_t count)` — `count` here must equal the number cs_disasm returned (used to free exactly that many `.detail` pointers); mismatched count under/over-frees.
> cs.c:746-754 `CS_OPT_SKIPDATA`: sets `handle->skipdata` and default `skipdata_size`; cs.c:756-763 `CS_OPT_SKIPDATA_SETUP` only stores `skipdata_setup.callback/mnemonic`, which cs_disasm's skipdata branch (cs.c:978-993) uses only `if (!handle->skipdata || ...)` — i.e. SETUP without SKIPDATA=ON is inert.
> tests/test_skipdata.c:125-126: `cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON); cs_option(handle, platforms[i].opt_skipdata /*CS_OPT_SKIPDATA_SETUP*/, ...);` shows the pairing/order used by real callers.

### P2.8 Prerequisite State
**Claim:** `cs_disasm` requires a `cs_struct` handle produced by a successful `cs_open` call for a supported `arch`/`mode` combination (architecture support compiled in, checked via `arch_configs[arch].arch_init` being non-NULL and `mode` not in `arch_disallowed_mode_mask`). No open "transaction"/connection beyond the handle is required. `CS_OPT_DETAIL` must be ON before `cs_disasm` for `cs_detail`/groups/regs to be populated (default is OFF, see cs_open initializing `ud->detail = CS_OPT_OFF`).
**Evidence:**
> cs.c:479-490: `if (arch < CS_ARCH_MAX && arch_configs[arch].arch_init) { if (mode & arch_configs[arch].arch_disallowed_mode_mask) {*handle=0; return CS_ERR_MODE;} ud = cs_mem_calloc(...); ... }` else `return CS_ERR_ARCH;`
> cs.c:496: `ud->detail = CS_OPT_OFF;` (default) inside `cs_open`.
> cs.c:929-939: `if (handle->detail) { insn_cache->detail = cs_mem_malloc(sizeof(cs_detail)); ... } else { insn_cache->detail = NULL; }` — detail only allocated when the option was turned on beforehand.
> suite/fuzz/fuzz_harness.c:167,184-208: `cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);` then later code dereferences `detail = i->detail;` and reads `detail->regs_read_count` etc — would crash/NULL-deref without DETAIL being ON first.

### P2.9 Result Usage
**Claim:** The array `*insn` filled by `cs_disasm` is consumed by: (a) direct field reads on each `cs_insn` (`address`, `size`, `bytes`, `mnemonic`, `op_str`, `id`); (b) `cs_insn_name(handle, insn->id)` to get the mnemonic string for an id; (c) when `CS_OPT_DETAIL` was ON, `insn[i].detail` (a `cs_detail*`) is walked directly (`regs_read`, `regs_write`, `groups` arrays) or passed into `cs_regs_access(handle, &insn[i], regs_read, &n, regs_write, &n)`, `cs_insn_group(handle, &insn[i], group_id)`, `cs_reg_read`/`cs_reg_write`, `cs_op_count`/`cs_op_index`; (d) the whole array is eventually released via `cs_free(insn, count)`.
**Evidence:**
> suite/fuzz/fuzz_harness.c:178-209: loops `for (j=0;j<count;j++) { cs_insn *i=&all_insn[j]; printf(...,i->address,i->mnemonic,i->op_str,i->id,cs_insn_name(handle,i->id)); detail=i->detail; if(detail->regs_read_count>0) ... cs_reg_name(handle, detail->regs_read[n]) ... if(detail->groups_count>0) ... cs_group_name(handle, detail->groups[n]); }`
> cstool/cstool_x86.c:304-306 and tests/test_x86.c:326: `cs_regs_access(ud/handle, ins, regs_read, &regs_read_count, regs_write, &regs_write_count)` — consumes a `cs_insn*` (`ins`) that came from `cs_disasm`/`cs_disasm_iter`.
> include/capstone/capstone.h:831-832,848-849,871-872: doxygen on `cs_op_count`, `cs_op_index`, `cs_regs_access` all state `@insn: disassembled instruction structure received from cs_disasm() or cs_disasm_iter()`.
> cs.c:1265 `cs_insn_group`, cs.c:1292 `cs_reg_read`, cs.c:1319 `cs_reg_write` all take `const cs_insn *insn` — same result-consuming pattern, used similarly in tests/test_arm.c:162, tests/test_bpf.c:92.

### P2.10 Behaviour Controls
**Claim:** Behavior-selecting parameters/flags:
1. `cs_open(arch, mode, ...)` — `arch` (CS_ARCH_X86/ARM/ARM64/MIPS/PPC/SPARC/SYSZ/XCORE/M68K/TMS320C64X/M680X/EVM/RISCV/MOS65XX/WASM/BPF/SH/TRICORE) selects the entire decoder/printer/insn_id function-pointer set via `arch_configs[arch].arch_init`; `mode` bits (CS_MODE_32/64/THUMB/ARM/MCLASS/V8/BIG_ENDIAN/LITTLE_ENDIAN/MICRO/MIPS32R6/RISCVC/etc.) select sub-variants and are validated against `arch_disallowed_mode_mask`.
2. `cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON/OFF)` — toggles `cs_detail` allocation/fill path in `cs_disasm`.
3. `cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON/OFF)` + `CS_OPT_SKIPDATA_SETUP` (`cs_opt_skipdata{mnemonic, callback, user_data}`) — selects per-arch default skip size (`skipdata_size()`, cs.c:646-708) vs user callback (cs.c:982-992) on decode failure.
4. `cs_option(handle, CS_OPT_UNSIGNED, ...)` sets `handle->imm_unsigned`, affecting how immediates are printed (consumed inside `fill_insn`/printer paths, not shown in cs_disasm itself but stored on handle before it is used).
5. `cs_option(handle, CS_OPT_MNEMONIC, &cs_opt_mnem{id,mnemonic})` — builds `handle->mnem_list`, consumed inside `fill_insn` (cs.c:613-629) to rewrite `insn->mnemonic` for a matching `insn->id`.
6. `count` parameter of `cs_disasm` (0 = unlimited vs >0 = stop early / affects `cache_size`).
**Evidence:**
> cs.c:479-484 (`arch`/`mode` validated and dispatch to `arch_configs[].arch_init`).
> cs.c:742-744 (`CS_OPT_DETAIL` sets `handle->detail`); cs.c:929-939 (`cs_disasm` branches on `handle->detail`).
> cs.c:646-708 `skipdata_size()` per-arch table; cs.c:746-763 `CS_OPT_SKIPDATA`/`CS_OPT_SKIPDATA_SETUP`; cs.c:978-1013 skipdata branch inside `cs_disasm`'s loop using `handle->skipdata_setup.callback` vs `handle->skipdata_size`.
> cs.c:738-740 `CS_OPT_UNSIGNED` sets `handle->imm_unsigned`.
> cs.c:765-818 `CS_OPT_MNEMONIC` builds/edits/deletes `handle->mnem_list`; cs.c:613-629 `fill_insn` consumes `handle->mnem_list`.
> cs.c:902-905, 1021-1023 (`count` controls `cache_size` and early loop break).

**Additional behaviour controls (from project source; previously missing from this section):**
7. `cs_option(handle, CS_OPT_SYNTAX, ...)` — X86: INTEL/MASM (Intel printer) vs ATT (separate AT&T printer, X86Module.c:61-90); ARM/PPC: NOREGNAME; MOS65XX: MOTOROLA. Drive it from input bits on every input.
8. `cs_option(handle, CS_OPT_MODE, mode)` — run-time mode switch on the same handle between two `cs_disasm` calls (cs.c:824-829, X86Module.c:53-59, ARMModule.c:46-53).
9. `CS_OPT_DETAIL` should be ON for every input (superset); `CS_OPT_NO_BRANCH_OFFSET` exists in capstone.h:287 but has no handler in v5 (harmless).
10. `CS_OPT_MNEMONIC` (cs.c:765-818 → fill_insn → `str_replace`, cs.c:556-567) crashes quickly (size_t underflow in the length check when the printed mnemonic is shorter than the insn name); keep it behind a rare selector so restarts do not eat the budget.

### P2.11 Input Expectations
**Claim:** `cs_disasm`'s `buffer`/`size` are treated as an opaque raw byte stream with no required magic/header/termination — the function simply walks `size` bytes, calling `handle->disasm()` repeatedly starting at `buffer`/`offset`, advancing by the architecture-reported instruction length (`insn_size`) each iteration, until `size` reaches 0, `count` instructions are produced, or a decode failure without skipdata support/space occurs. There is no minimum size requirement documented; `size==0` simply returns 0 instructions. Max instruction byte length actually copied into `cs_insn.bytes` is bounded by `sizeof(insn->bytes)` (`MIN(skipdata_bytes, sizeof(insn->bytes))`, cs.c:999-1002) for the skipdata path; the normal decode path relies on the architecture decoder to respect `size`.
**Evidence:**
> cs.c:922 `while (size > 0) { ... }` — loop termination purely on remaining byte count (or `count`/failure breaks).
> cs.c:950: `r = handle->disasm(ud, buffer, size, &mci, &insn_size, offset, handle->getinsn_info);` — decoder receives remaining `buffer`/`size` and must not read past it (bounds enforcement happens in the arch-specific decoder, e.g. X86_getInstruction, not in cs_disasm itself).
> cs.c:977-980: `if (!handle->skipdata || handle->skipdata_size > size) break;` — when decode fails and remaining `size` is smaller than the required skip size, the loop stops instead of reading out of bounds.
> cs.c:999-1002: `insn_cache->size = (uint16_t)MIN(skipdata_bytes, sizeof(insn_cache->bytes)); memcpy(insn_cache->bytes, buffer, MIN(skipdata_bytes, sizeof(insn_cache->bytes)));` — bytes copy is clamped to the fixed `cs_insn.bytes` array size (defined in capstone.h, typically 24 bytes), not to `size` alone; `skipdata_bytes` itself is already checked `<= size` beforehand.
> suite/fuzz/fuzz_harness.c:151,156-157: real fuzz driver imposes only its own minimum (`if (bufsize < 3) return 0;`) to reserve 1 byte for platform selection, not a cs_disasm requirement itself.

## Other entries of the Logic Group (call protocol & combination with cs_disasm)

### cs_open
**Claim:** `cs_open(cs_arch arch, cs_mode mode, csh *handle)` must be the first capstone call for a given handle (aside from the optional global `cs_option(0, CS_OPT_MEM, ...)`). `arch` selects the architecture (must be `< CS_ARCH_MAX` and supported/compiled in); `mode` is a bitmask of `CS_MODE_*` validated against `arch_configs[arch].arch_disallowed_mode_mask`. Returns `cs_err`: `CS_ERR_OK` on success (`*handle` set to non-zero `csh`), `CS_ERR_ARCH` if arch unsupported, `CS_ERR_MODE` if mode invalid for that arch, `CS_ERR_MEM` on allocation failure, `CS_ERR_MEMSETUP` if the memory-management hooks are uninitialized. The harness must check the return value is `CS_ERR_OK` before using `*handle` with `cs_disasm`/`cs_option`/etc. Must be matched 1:1 with a later `cs_close(&handle)`.
**Evidence:**
> cs.c:470-515 definition; cs.c:479-514 arch/mode validation and allocation; suite/fuzz/fuzz_harness.c:161-165 `err = cs_open(...); if (err) { printf(...); return 1; }`.

### cs_option
**Claim:** `cs_option(csh ud, cs_opt_type type, size_t value)` can be called any time after `cs_open` (except `CS_OPT_MEM` which may precede `cs_open` with `ud==0`). `type` selects which handle field to mutate (`CS_OPT_DETAIL`, `CS_OPT_SKIPDATA`, `CS_OPT_SKIPDATA_SETUP`, `CS_OPT_UNSIGNED`, `CS_OPT_MNEMONIC`, plus arch-specific `CS_OPT_SYNTAX`/`CS_OPT_MODE` handled elsewhere in cs.c); `value` is either a `cs_opt_value` enum (`CS_OPT_ON`/`CS_OPT_OFF`) or a pointer cast to `size_t` (for `CS_OPT_SKIPDATA_SETUP` and `CS_OPT_MNEMONIC`, which take `cs_opt_skipdata*`/`cs_opt_mnem*`). Returns `cs_err` (`CS_ERR_OK`, `CS_ERR_CSH` if handle NULL, `CS_ERR_MEM` for `CS_OPT_MNEMONIC` allocation failure). It can be called repeatedly/interleaved with multiple `cs_disasm` calls to change behavior between runs on the same handle. Nothing to free from `cs_option` itself except indirectly via `cs_close` (frees `mnem_list`).
**Evidence:**
> cs.c:711 definition; cs.c:716-728 `CS_OPT_MEM` special-case with `ud` possibly NULL; cs.c:730-732 `if (!handle) return CS_ERR_CSH;` for all other types; cs.c:742-818 per-type handling; tests/test_skipdata.c:121-126 shows repeated calls on same handle before `cs_disasm`.

### cs_disasm_iter
**Claim:** Alternative to `cs_disasm` — decodes exactly one instruction per call into a caller-supplied `cs_insn*` (obtained via `cs_malloc`), advancing caller-owned `code`/`size`/`address` pointers in place. Requires `cs_open` first (same handle rules), and an `insn` buffer allocated by `cs_malloc(handle)` (allocation must happen *after* `CS_OPT_DETAIL` is set if detail is wanted, since `cs_malloc` checks `handle->detail` once at allocation time). Returns `bool`: `true` if an instruction (or skipdata chunk) was produced and `*code`/`*size`/`*address` advanced; `false` on `*size==0`, NULL handle, or an unrecoverable decode failure (no skipdata configured / not enough remaining bytes / user callback declines). Caller loops `while (cs_disasm_iter(handle,&code,&size,&address,insn))`. The harness can call this instead of, or in addition to, `cs_disasm` on the same buffer (same decode core, same skipdata/detail options) to exercise the single-step path.
**Evidence:**
> cs.c:1129 definition; cs.c:1132-1133 `if (*size==0) return false;`; cs.c:1140-1143 handle NULL check; cs.c:1162-1182 success path (`*code += insn_size; *size -= insn_size; *address += insn_size;`); cs.c:1183-1200 skipdata/failure path returning `false`; tests/test_iter.c:290-299 `insn = cs_malloc(handle); ... while(cs_disasm_iter(handle, &code, &size, &address, insn)) { ... }`.

### cs_malloc / cs_free
**Claim:** `cs_malloc(csh ud)` allocates a single `cs_insn` (plus a `cs_detail` if `handle->detail` is ON at call time) for use with `cs_disasm_iter`; returns `NULL` on OOM (and sets `handle->errnum = CS_ERR_MEM`). Must be freed with `cs_free(insn, 1)` (same function used for `cs_disasm`'s array, with `count=1`). `cs_free(cs_insn *insn, size_t count)` frees `insn[i].detail` for `i` in `[0,count)` then frees `insn` itself; `count` must equal the number of instructions actually present in that array (for `cs_disasm` results, the return value of `cs_disasm`; for `cs_malloc`/`cs_disasm_iter`, always 1).
**Evidence:**
> cs.c:1101 `cs_malloc` definition; cs.c:1106-1121 detail allocation conditioned on `handle->detail`; cs.c:1088-1098 `cs_free` definition; tests/test_iter.c:291 `insn = cs_malloc(handle);` and :339 `cs_free(insn, 1);`.

### cs_regs_access
**Claim:** Consumer, not a producer — takes a `const cs_insn *insn` that must have come from `cs_disasm` or `cs_disasm_iter` **with `CS_OPT_DETAIL` ON** (so `insn->detail` is non-NULL; the function dereferences `insn->detail` internally per-architecture). Fills caller-provided fixed arrays `cs_regs regs_read`/`regs_write` (`typedef uint16_t cs_regs[64]`) and counts `uint8_t *regs_read_count`/`regs_write_count`. Returns `cs_err` (`CS_ERR_OK` on success, other value on failure, e.g. when detail is unavailable/diet mode). Must be called after `cs_disasm`/`cs_disasm_iter` and before the instruction array is freed with `cs_free`.
**Evidence:**
> include/capstone/capstone.h:882-884 declaration and :871-879 doxygen ("@insn: disassembled instruction structure returned from cs_disasm() or cs_disasm_iter()", "WARN: when in 'diet' mode, this API is irrelevant because engine does not store registers").
> cs.c:1647 definition location (per-arch dispatch for `cs_regs_access`).
> tests/test_x86.c:326, tests/test_arm.c:162, tests/test_arm64.c:141, tests/test_bpf.c:92: all call `cs_regs_access(handle, ins, regs_read, &regs_read_count, regs_write, &regs_write_count)` with `ins` taken from the detail-enabled `cs_disasm`/`cs_disasm_iter` loop, before the array/iter buffer is freed.

## Build notes
**Claim:** Fuzz targets are compiled in C (the real library and `fuzz_disasm.c`/`fuzz_llvm.cpp` style harness), linked against the static `libcapstone.a`, built via CMake with `CAPSTONE_BUILD_SHARED=0`; the target header is reached through the public `include/` directory added as a public include dir on the `capstone`/`capstone_static` targets, so a harness includes `<capstone/capstone.h>` (or `<capstone.h>` for the C++ llvm harness, since `-I` picks the top-level `include` as shown by `#include <capstone.h>` resolving via `include/capstone.h` wrapper) without extra flags. OSS-Fuzz's `build.sh` builds the whole project tree first, then compiles/links the fuzzer object(s) against `libcapstone.a` with `$CXX $CXXFLAGS` and `$LIB_FUZZING_ENGINE`.
**Evidence:**
> ${QF_WORK}/ossfuzz_view/capstone-c2/build.sh:19-54: `cmake -DCAPSTONE_BUILD_SHARED=0 .. && make` then `FUZZO=CMakeFiles/fuzz_disasm.dir/suite/fuzz/fuzz_disasm.c.o ... $CXX $CXXFLAGS $FUZZO -o $OUT/fuzz_disasm$branch libcapstone.a $LIB_FUZZING_ENGINE`.
> ${QF_WORK}/ossfuzz_view/capstone-c2/Dockerfile:17-21: base image `base-builder-python`, installs `make cmake`, clones `capstone` repo branches `v5`/`next` into `$SRC/capstonev5`/`capstonenext`.
> CMakeLists.txt:653-657: `add_library(capstone OBJECT ${ALL_SOURCES} ${ALL_HEADERS}); target_include_directories(capstone PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)` — public header include path is `include/`.
> CMakeLists.txt:699-700: `add_executable(fuzz_disasm suite/fuzz/onefile.c suite/fuzz/fuzz_disasm.c suite/fuzz/platform.c); target_link_libraries(fuzz_disasm PRIVATE capstone)` — the in-repo fuzz target links against the `capstone` library target (C build; `onefile.c`/`platform.c`/`fuzz_disasm.c` are plain `.c` files).
> suite/fuzz/fuzz_harness.c:4: `#include <capstone.h>` — a harness-style file includes the top-level umbrella header; suite/fuzz/fuzz_diff.c:7 and tests/test_skipdata.c:8 use `#include <capstone/capstone.h>` — both include forms resolve given `include/` on the include path (both `include/capstone.h` wrapper and `include/capstone/capstone.h` exist under the installed/public include dir).