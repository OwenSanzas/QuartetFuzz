## target: cs_disasm

### P2.1 Init Sequence
**Claim:** `cs_open(cs_arch arch, cs_mode mode, csh *handle)` must be called first to obtain a valid `csh handle`; it must return `CS_ERR_OK`. Optionally `cs_option()` calls (CS_OPT_DETAIL, CS_OPT_SKIPDATA, CS_OPT_SKIPDATA_SETUP, CS_OPT_UNSIGNED, CS_OPT_SYNTAX, CS_OPT_MNEMONIC, per-arch CS_OPT_MODE) follow `cs_open` and precede `cs_disasm`. `cs_option(.., CS_OPT_MEM, ..)` — if used at all — must precede even `cs_open` (special-cased, works with handle==0).
**Evidence:**
> capstone.h:849: "@handle: handle returned by cs_open()" (cs_disasm doc)
> cs.c:1066-1083 (cs_option): "cs_option() can be called with NULL handle just for CS_OPT_MEM / This is supposed to be executed before all other APIs (even cs_open())"
> cs.c:809-815 (cs_open): if `cs_mem_malloc`/`calloc`/`realloc`/`free`/`cs_vsnprintf` are unset, cs_open fails with `CS_ERR_MEMSETUP` — meaning CS_OPT_MEM setup (if non-default allocators are desired) must run first; otherwise built-in defaults are used.
> cstool/cstool.c:1095-1145: `cs_open(...)` → `cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON)` (optional) → `cs_option(handle, CS_OPT_UNSIGNED, CS_OPT_ON)` (optional) → `cs_option(handle, CS_OPT_SYNTAX, ...)` (optional) → `cs_disasm(handle, assembly, size, address, 0, &insn)`
> suite/fuzz(v5)/fuzz_disasm.c:44-56: `cs_open(...)` → `cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON)` → optional `cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT)` → `cs_disasm(handle, Data+1, Size-1, address, 0, &all_insn)`
> tests/integration/test_poc.c:28-41: `cs_open(CS_ARCH_WASM, CS_MODE_LITTLE_ENDIAN, &handle)` → `cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON)` → `cs_option(handle, CS_OPT_SKIPDATA_SETUP, (size_t)&skip)` → `cs_disasm(...)`

### P2.2 Parameter Construction
**Claim:** `handle` (csh, i.e. uintptr_t) must be the non-zero value filled by `cs_open`; `code` is any pointer to a readable buffer of `code_size` bytes (attacker data, no required header/terminator); `address` is an arbitrary uint64_t (base address used for relative-branch computation, has no validity constraint); `count` is `size_t`, 0 meaning "decode until buffer exhausted or decode fails without SKIPDATA", or a positive cap; `insn` is `cs_insn **` — an out-parameter, must point to a `cs_insn*` variable that will be overwritten by the function (any previous value is NOT freed by cs_disasm, caller must not pass an already-populated pointer without freeing it first).
**Evidence:**
> cs.c:1268-1273: `handle = (struct cs_struct *)(uintptr_t)ud; if (!handle) { ... return 0; }` — NULL/zero handle is checked and yields 0 (no crash, but no error recorded either, per `// FIXME` comment).
> capstone.h:850-856: `@code: buffer containing raw binary code...` / `@count: number of instructions to be disassembled, or 0 to get all of them` / `@insn: array of instructions filled in by this API. NOTE: @insn will be allocated by this function...`
> cs.c:1297: `while (size > 0) { ... }` — loop runs purely based on `size` (code_size) and `count`, no assumption about code content/format other than being readable memory of that length.
> cs.c:1480: `*insn = total;` — *insn is overwritten unconditionally once at least one instruction decoded; `total` may be `NULL` if `c==0` (see P2.4).

### P2.3 Object Lifecycle
**Claim:** Handle lifecycle is create (`cs_open`) → configure (`cs_option`, 0+ calls) → use (`cs_disasm` / `cs_disasm_iter`, repeatable any number of times on the same handle) → destroy (`cs_close`). Each `cs_disasm` call produces its own independently-allocated `cs_insn[]` array that must be freed with `cs_free` before (or after, order vs cs_close is not required) closing the handle, but must be freed before the process exits to avoid leaks; it is NOT tied 1:1 with cs_open/cs_close — many cs_disasm calls can be made per handle.
**Evidence:**
> cstool/cstool.c:1095-1191: single handle; cs_disasm → `cs_free(insn, count)` on success branch; `cs_close(&handle)` called on both success and failure paths, always as final step.
> cs.c:862-896 (cs_close): frees `ud->printer_info`, the `mnem_list` linked list, `ud->insn_cache`, `ud->x86_insn_lut`, `ud->x86_insn_reg_lut`, then `memset`s and frees `ud` itself and zeroes `*handle`. This is the handle teardown, independent of any cs_insn arrays.
> tests/integration/test_poc.c:27-44: `cs_open` → `cs_option`(x2) → `cs_disasm` → `cs_free(insn,1)` → `cs_close(&handle)` (classic full lifecycle in one function).

### P2.4 Return Value Handling
**Claim:** `cs_disasm` returns `size_t c` = number of instructions actually decoded (0 on failure/empty result). Callers must check `count/return value > 0` before indexing `*insn`; on `0` the `*insn` may be left `NULL` (explicitly set at cs.c:1314, 1429, 1456, 1471) so it must NOT be freed with a nonzero count, and any error detail must be retrieved via `cs_errno(handle)` (returns `CS_ERR_MEM` when calloc/realloc for the instruction cache fails; `CS_ERR_OK` otherwise even if c==0 due to e.g. bad first instruction without SKIPDATA).
**Evidence:**
> capstone.h:858-861: "@return: the number of successfully disassembled instructions, or 0 if this function failed to disassemble the given code ... On failure, call cs_errno() for error code."
> cs.c:1453-1456: "if (!c) { // we did not disassemble any instruction; cs_mem_free(total); total = NULL; }" then `*insn = total;` (NULL) and `return c;` (0).
> cs.c:1289-1292: `if (total == NULL) { handle->errnum = CS_ERR_MEM; return 0; }` (initial cache alloc failure).
> cstool/cstool.c:1145-1189: `count = cs_disasm(...); if (count > 0) { ...use insn...; cs_free(insn, count); } else { fprintf(stderr, "ERROR: invalid assembly code\n"); cs_close(&handle); ... }` — demonstrates the canonical "check >0, else skip cs_free, still cs_close" pattern.

### P2.5 Cleanup Sequence
**Claim:** On success: free each `cs_insn.detail` (if CS_OPT_DETAIL was on) via `cs_free(insn, count)`, which internally frees per-instruction `detail` then the `insn` array itself; this must be done before the `cs_insn*` pointer is dropped. On failure (`count==0`), skip `cs_free` (nothing was allocated / *insn is NULL — calling cs_free(NULL,0) is harmless but unnecessary). The `csh` handle is destroyed separately and finally via `cs_close(&handle)`, which also frees internal handle-owned allocations (mnemonic override list, printer_info, insn_cache, x86 LUTs) — this must happen exactly once per successful `cs_open`.
**Evidence:**
> cs.c:1486-1496 (cs_free): `for (i=0;i<count;i++) cs_mem_free(insn[i].detail); cs_mem_free(insn);`
> cstool/cstool.c:1182,1186,1191: `cs_free(insn, count); free(assembly);` on success vs `cs_close(&handle); free(assembly);` on failure, and unconditional final `cs_close(&handle);`
> tests/integration/test_poc.c:42-43: `cs_free(insn, 1); cs_close(&handle);` (free-then-close order used throughout the PoC regression tests).

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase (`capstonenext`).
**Evidence:**
> cs.c:804 `cs_err CAPSTONE_API cs_open(cs_arch arch, cs_mode mode, csh *handle)`
> cs.c:862 `cs_err CAPSTONE_API cs_close(csh *handle)`
> cs.c:1066 `cs_err CAPSTONE_API cs_option(csh ud, cs_opt_type type, uintptr_t value)`
> cs.c:1247 `size_t CAPSTONE_API cs_disasm(csh ud, const uint8_t *buffer, size_t size, uint64_t offset, size_t count, cs_insn **insn)`
> cs.c:1486 `void CAPSTONE_API cs_free(cs_insn *insn, size_t count)`
> cs.c:1499 `cs_insn *CAPSTONE_API cs_malloc(csh ud)`
> cs.c:1527 `bool CAPSTONE_API cs_disasm_iter(csh ud, const uint8_t **code, size_t *size, uint64_t *address, cs_insn *insn)`
> cs.c:1666 `bool CAPSTONE_API cs_insn_group(csh ud, const cs_insn *insn, unsigned int group_id)`
> cs.c:1695 `bool CAPSTONE_API cs_reg_read(csh ud, const cs_insn *insn, unsigned int reg_id)`
> cs.c:1722 `bool CAPSTONE_API cs_reg_write(...)` (not in the Entries list but is a sibling of cs_reg_read with identical protocol)
> cs.c:2145 `cs_err CAPSTONE_API cs_regs_access(csh ud, const cs_insn *insn, cs_regs regs_read, uint8_t *regs_read_count, cs_regs regs_write, uint8_t *regs_write_count)`
> cs.c:753 `cs_err CAPSTONE_API cs_errno(csh handle)`, cs.c:765 `cs_strerror`
> cs.c:924 `static void fill_insn(...)` (internal/core, not public — confirmed static, matches Logic-Group "Core" classification)

### P2.7 Co-call Constraints
**Claim:** Paired lifecycle APIs: `cs_open` / `cs_close` (handle create/destroy, exactly once each per handle — `cs_close` zeroes `*handle` to prevent reuse-after-close, cs.c:893). `cs_disasm` (or `cs_malloc`+`cs_disasm_iter`) / `cs_free` (array alloc/free — each `cs_disasm` call that returns `count>0` needs exactly one matching `cs_free(insn, count)`). `cs_malloc` (single `cs_insn`, used with `cs_disasm_iter`) is also freed via `cs_free(insn, 1)`. `CS_OPT_DETAIL` ON implies `cs_insn.detail` is allocated per-instruction by the core and is freed as part of `cs_free`/`cs_close`'s teardown of `insn_cache`/`ud` — no separate free call needed by the user. There is no explicit "begin/commit" transaction API; `cs_option` calls are idempotent/can be called multiple times on the same handle and don't need pairing beyond being set before the next `cs_disasm`/`cs_disasm_iter` call they are meant to affect.
**Evidence:**
> cs.c:884-889 (cs_close): frees `ud->insn_cache`, `ud->x86_insn_lut`, etc., as part of handle teardown — these are allocated inside `arch_init`/decoder setup at `cs_open` time, not by `cs_disasm`.
> cs.c:1486-1496 (cs_free): frees `insn[i].detail` then `insn` — single counterpart to the allocation done at cs.c:1306-1318 (`insn_cache->detail = cs_mem_calloc(1, sizeof(cs_detail))` when `handle->detail_opt`).
> tests/integration/test_poc.c:67,73 (`cs_malloc` then after the iter loop `cs_free(insn, 1)`): malloc/free pairing for the iterator variant.

### P2.8 Prerequisite State
**Claim:** Before `cs_disasm` can run: (1) a `csh` handle from a successful `cs_open` (which itself requires `arch_configs[arch].arch_init` to be compiled in for that arch — i.e. the arch must be enabled at build time, otherwise `cs_open` returns `CS_ERR_ARCH`), (2) `mode` must not intersect `arch_configs[arch].arch_disallowed_mode_mask` or `cs_open` fails with `CS_ERR_MODE`, (3) dynamic memory callbacks (`cs_mem_malloc/calloc/realloc/free/cs_vsnprintf`) must be non-NULL — by default they are set to libc equivalents unless CS_OPT_MEM overrides them before `cs_open`; if they are NULL, `cs_open` fails `CS_ERR_MEMSETUP` and `cs_disasm` can't even get a valid handle.
**Evidence:**
> cs.c:809-820: `if (!cs_mem_malloc || ...) { err = CS_ERR_MEMSETUP; goto fail; } if (arch >= CS_ARCH_MAX || !arch_configs[arch].arch_init) { err = CS_ERR_ARCH; goto fail; }`
> cs.c:822-826: `if (mode & arch_configs[arch].arch_disallowed_mode_mask) { err = CS_ERR_MODE; goto fail; }`
> cs.c:1268-1273 (cs_disasm): rejects `handle==0` (i.e. a never-opened or already-`cs_close`d handle) by returning 0 immediately.

### P2.9 Result Usage
**Claim:** After `cs_disasm` fills `*insn` (array of `count` `cs_insn`), real callers: (a) iterate `insn[i].address/size/bytes/mnemonic/op_str/id` directly (always valid, independent of CS_OPT_DETAIL); (b) call `cs_insn_name(handle, insn[i].id)` to get the mnemonic string for the opcode id; (c) if `CS_OPT_DETAIL` was ON, dereference `insn[i].detail` (arch-specific union, e.g. `detail->arm`, `detail->x86`) to read operands, then call `cs_reg_name(handle, reg)` / `cs_group_name(handle, group)` for human-readable names, and `cs_reg_read`/`cs_reg_write`/`cs_insn_group`/`cs_regs_access` to query implicit register and instruction-group membership (all of these require `insn->id != 0` and `insn->detail != NULL`, i.e. not a SKIPDATA "data" pseudo-instruction and detail mode must be enabled, else they set `handle->errnum = CS_ERR_DETAIL`/`CS_ERR_SKIPDATA` and return false); (d) finally, the whole array is released with `cs_free(insn, count)`.
**Evidence:**
> cstool/cstool.c:1148-1179: for each `insn[i]`, prints `insn[i].address/bytes[j]/mnemonic/op_str/illegal`; if `detail_flag` calls `print_details(handle, arch, mode, &insn[i])` which (in cstool_arm.c:16-20) checks `if (ins->detail == NULL) return;` before touching `ins->detail->arm`.
> cstool/cstool_arm.c:216-217: `cs_regs_access(handle, ins, regs_read, &regs_read_count, regs_write, &regs_write_count)` called on a decoded `cs_insn *ins` coming from cs_disasm/cs_disasm_iter, then names resolved with `cs_reg_name(handle, regs_read[i])`.
> suite/fuzz(v5)/fuzz_disasm.c:62-89: loops `all_insn[j]`, calls `cs_insn_name(handle, i->id)`; reads `i->detail->regs_read_count/regs_write_count/groups_count` and calls `cs_reg_name`/`cs_group_name` on the entries — this is the canonical "disasm → iterate → query detail" usage the harness should mimic.
> cs.c:1666-1692 (cs_insn_group): `if (!handle->detail_opt) {...return false;} if (!insn->id) {...return false;} if (!insn->detail) {...return false;} return arr_exist8(insn->detail->groups, insn->detail->groups_count, group_id);` — shows the required preconditions for calling this consumer API on a cs_disasm result.

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting parameters/flags around `cs_disasm`:
- `cs_arch` (cs_open arg1): selects entirely different `handle->disasm/insn_id/printer/reg_access` vtable (set inside `arch_configs[arch].arch_init`, cs.c:844).
- `cs_mode` (cs_open arg2): per-arch submodes (CS_MODE_32/64/16/ARM/THUMB/MCLASS/V8/LITTLE_ENDIAN/BIG_ENDIAN/etc.), validated against `arch_disallowed_mode_mask` (cs.c:823); also changeable later via `cs_option(handle, CS_OPT_MODE, mode)` (cs.c:1193 region).
- `count` (cs_disasm arg5): 0 = decode until buffer exhausted / first undecodable+non-skipdata byte; >0 = stop after that many instructions (cs.c:1411-1413 `if (count > 0 && c == count) break;`). Also affects initial cache sizing when `CAPSTONE_USE_SYS_DYN_MEM` (cs.c:1278-1280).
- `address` (cs_disasm arg4): seed for `mci.address`/`insn.address`, affects relative-branch target computation inside arch decoders; no format validation.
- `CS_OPT_DETAIL` (cs_option): ON allocates `cs_detail` per instruction (cs.c:1304-1321) and is required for `cs_insn_group`/`cs_reg_read`/`cs_reg_write`/`cs_regs_access` to succeed; also `CS_OPT_DETAIL_REAL` sub-flag affects alias detail reporting (cstool.c:1140-1143).
- `CS_OPT_SKIPDATA` ON + `CS_OPT_SKIPDATA_SETUP` (custom mnemonic/callback): governs the resync-on-bad-opcode path (cs.c:1363-1404); callback return value controls how many bytes are skipped (can return 0 → bail out, or an oversized value → clamped by `skipdata_bytes > size` check but the fixed-size `insn_cache->bytes[24]` copy is `MIN(skipdata_bytes, sizeof(insn_cache->bytes))`, cs.c:1387-1390) — this is the exact mechanism test_poc.c:12-45 exercises for a documented buffer-overflow regression.
- `CS_OPT_UNSIGNED`: toggles `handle->imm_unsigned`, consumed by `SStream_opt_unum` (cs.c:1344) affecting printer formatting of immediates.
- `CS_OPT_MNEMONIC`: add/replace/remove per-instruction-id mnemonic overrides in `handle->mnem_list`, consumed inside `fill_insn` (cs.c:959-978) via `str_replace`.
- `CS_OPT_SYNTAX` (X86/ARM/etc.): selects AT&T/Intel/Masm/NoRegName/etc. printer dialect (cstool.c:1137, capstone.h:396-420).
**Evidence:** see inline citations above (cs.c:1278-1280, 1344, 1363-1413, 1304-1321, 959-978; cstool.c:1128-1143, 1137; tests/integration/test_poc.c:12-45).

### P2.11 Input Expectations
**Claim:** `cs_disasm` makes no assumption about a header/magic/terminator in `code`; it treats `code[0..code_size)` purely as a byte stream to be consumed by the arch-specific `handle->disasm` state machine, one (variable-length) instruction at a time, until `size` reaches 0, `count` instructions have been produced, or a decode failure occurs that is not resolved by SKIPDATA. There is no minimum size requirement enforced by `cs_disasm` itself beyond the `while (size > 0)` loop guard (code_size==0 → 0 instructions, no error). Each decoded instruction's raw bytes are copied into the fixed-size `cs_insn.bytes[24]` via `memcpy(insn->bytes, code + insn->size - copy_size, copy_size)` with `copy_size = MIN(sizeof(insn->bytes), insn->size)` (fill_insn, cs.c:932-936) — i.e. the function clamps/truncates rather than requiring instructions ≤24 bytes, but the underlying `insn->size` set by the arch decoder is trusted to be a sane `uint16_t`. In SKIPDATA mode the skip length comes from `handle->skipdata_size` (arch-default, via `skipdata_size()`) or a user callback and is clamped with `MIN(skipdata_bytes, sizeof(insn_cache->bytes))` before the `memcpy` (cs.c:1387-1390) — this clamp is the documented fix for a historical overflow (test_poc.c:12-20,22-24).
**Evidence:**
> cs.c:1297 `while (size > 0) { ... }` — only loop-termination condition tied to input size.
> cs.c:932-936 (fill_insn): `uint16_t copy_size = MIN(sizeof(insn->bytes), insn->size); memcpy(insn->bytes, code + insn->size - copy_size, copy_size);`
> cs.c:1387-1390: `insn_cache->size = (uint16_t)MIN(skipdata_bytes, sizeof(insn_cache->bytes)); memcpy(insn_cache->bytes, buffer, MIN(skipdata_bytes, sizeof(insn_cache->bytes)));`
> capstone.h:850-851: "@code: buffer containing raw binary code to be disassembled. @code_size: size of the above code buffer." — no format/header documented, consistent with "fully untrusted raw bytes" functionality description.

## Other Entries' Call Protocol (combined with cs_disasm in one harness)

### cs_open
**Before it:** nothing from this library required (optionally `cs_option(0, CS_OPT_MEM, &mem_funcs)` if custom allocators are desired — must precede cs_open, cs.c:1071-1083).
**Params:** `cs_arch arch` (enum, e.g. `CS_ARCH_X86`, `CS_ARCH_ARM`, ... up to `CS_ARCH_MAX`), `cs_mode mode` (bitmask, arch-specific, validated against `arch_disallowed_mode_mask`), `csh *handle` (out).
**Return:** `cs_err`; must equal `CS_ERR_OK` to proceed — any other value (`CS_ERR_ARCH`, `CS_ERR_MODE`, `CS_ERR_MEM`, `CS_ERR_MEMSETUP`) means `*handle` is set to 0 and must NOT be used (cs.c:853-858).
**Free:** handle is released later with `cs_close(&handle)`.
**Combine:** Always the first Capstone call in the harness; the chosen `arch`/`mode` select which decoder cs_disasm will exercise — a good harness derives arch/mode from fuzz-input bytes (as suite/fuzz(v5)/fuzz_disasm.c:42-44 does via `get_platform_entry`) to cover multiple architectures.
**Evidence:** cs.c:804-859; cstool/cstool.c:1095-1126.

### cs_option
**Before it:** a valid handle from `cs_open` (except the special `CS_OPT_MEM` case, which can run with handle 0 and must run before `cs_open`).
**Params:** `csh ud`, `cs_opt_type type` (CS_OPT_SYNTAX, CS_OPT_DETAIL, CS_OPT_MODE, CS_OPT_MEM, CS_OPT_SKIPDATA, CS_OPT_MNEMONIC, CS_OPT_UNSIGNED, CS_OPT_SKIPDATA_SETUP, CS_OPT_ONLY_OFFSET_BRANCH — capstone.h:376-389), `uintptr_t value` (meaning depends on `type`, e.g. `CS_OPT_ON`/`CS_OPT_OFF`, a `cs_opt_skipdata*` cast to uintptr_t for SKIPDATA_SETUP, a `cs_opt_mnem*` for MNEMONIC, a `cs_opt_mem*` for MEM).
**Return:** `cs_err` (`CS_ERR_OK`, `CS_ERR_CSH` if handle NULL, `CS_ERR_MEM` if an internal allocation for `CS_OPT_MNEMONIC` fails) — should be checked.
**Free:** no explicit free by caller; CS_OPT_MNEMONIC-allocated `insn_mnem` nodes and CS_OPT_SKIPDATA_SETUP struct are owned/freed internally (mnem list freed in `cs_close`, cs.c:877-882); the `cs_opt_skipdata` struct passed by the caller to SKIPDATA_SETUP is copied by value (`handle->skipdata_setup = *((cs_opt_skipdata *)value)`, cs.c:1113) so the caller's local struct can go out of scope after the call — but any `user_data` pointer or `callback` function referenced inside it must remain valid for the handle's lifetime (used inside `cs_disasm`/`cs_disasm_iter`).
**Combine:** Called 0+ times after `cs_open` and before each `cs_disasm`/`cs_disasm_iter` call it is meant to affect; can also be called between repeated `cs_disasm` calls on the same handle to change behaviour (e.g., toggle CS_OPT_DETAIL mid-fuzzing-run).
**Evidence:** cs.c:1066-1245 (full switch); capstone.h:376-420 (enum values).

### cs_disasm_iter
**Before it:** valid handle (cs_open+cs_option as needed) AND a caller-owned `cs_insn *insn` buffer, typically obtained via `cs_malloc(handle)` (which allocates `cs_insn` + conditionally its `detail` based on `handle->detail_opt` at the time of the call — so `cs_option(CS_OPT_DETAIL,...)` should be set before `cs_malloc`, cs.c:1510-1519).
**Params:** `csh ud`, `const uint8_t **code` (in/out, advanced by decoded size), `size_t *size` (in/out, decremented), `uint64_t *address` (in/out, incremented), `cs_insn *insn` (reused buffer, overwritten each call).
**Return:** `bool` — true if one instruction (or one SKIPDATA pseudo-instruction) was decoded and `*code/*size/*address` advanced; false when `*size==0` on entry, handle NULL, or decode failed and SKIPDATA is off/insufficient — caller should stop looping on false.
**Free:** the single `insn` buffer is freed once after the loop with `cs_free(insn, 1)`.
**Combine:** Alternative to `cs_disasm` for the same core decode path; a harness may call either `cs_disasm` once or drive `cs_disasm_iter` in a `while` loop on the same handle — both consume the same `code/size` input space and should not be mixed on the same buffer position without re-synchronizing pointers.
**Evidence:** cs.c:1526-1618 (cs_disasm_iter body, shares `handle->disasm/insn_id/printer/fill_insn`/skipdata logic with cs_disasm, cs.c:1560-1616); tests/integration/test_poc.c:50-76,78-113 (cs_malloc → loop cs_disasm_iter → cs_free(insn,1) → cs_close).

### cs_free
**Before it:** a `count` matching exactly what `cs_disasm` returned (or `1` for a `cs_malloc`-allocated buffer used with `cs_disasm_iter`), and `insn` non-NULL (calling on a `NULL`/`0`-count result from a failed cs_disasm is unnecessary but not required to be guarded, since the loop body becomes a no-op; but matching cstool's pattern, skip calling it when count==0 to avoid confusion).
**Params:** `cs_insn *insn`, `size_t count`.
**Return:** void.
**Combine:** Always the paired deallocator for a successful `cs_disasm`/`cs_malloc` result; must run before the array pointer is discarded, any time after the detail/data has been consumed by `cs_insn_group`/`cs_reg_read`/`cs_regs_access`/printing code.
**Evidence:** cs.c:1486-1496; cstool/cstool.c:1182; tests/integration/test_poc.c:42,73,110.

### cs_insn_group
**Before it:** `cs_open` with a handle that has `CS_OPT_DETAIL` set ON, and a `cs_insn` produced by `cs_disasm`/`cs_disasm_iter` with `insn->id != 0` (not a SKIPDATA pseudo-insn) and `insn->detail != NULL`.
**Params:** `csh ud`, `const cs_insn *insn`, `unsigned int group_id` (arch-specific `CS_GRP_*`/`<ARCH>_GRP_*` enum value).
**Return:** `bool`; on precondition failure sets `handle->errnum` to `CS_ERR_DETAIL` or `CS_ERR_SKIPDATA` and returns `false` — caller should check `cs_errno(handle)` if the false result is ambiguous.
**Free:** nothing to free (read-only query).
**Combine:** Called per-instruction after `cs_disasm`, before `cs_free`/`cs_close`, only meaningful with CS_OPT_DETAIL ON.
**Evidence:** cs.c:1666-1692.

### cs_reg_read / (cs_reg_write, sibling)
**Before it:** same preconditions as `cs_insn_group` (CS_OPT_DETAIL ON, `insn->id != 0`, `insn->detail != NULL`).
**Params:** `csh ud`, `const cs_insn *insn`, `unsigned int reg_id` (arch register enum value, e.g. `X86_REG_EAX`).
**Return:** `bool`, with the same `handle->errnum` side-effects as `cs_insn_group` on precondition failure.
**Free:** nothing to free.
**Combine:** Per-instruction query after `cs_disasm`, same slot in the pipeline as `cs_insn_group`/`cs_regs_access`.
**Evidence:** cs.c:1695-1720 (cs_reg_read), cs.c:1722+ (cs_reg_write, same structure by inspection of surrounding code at cs.c:1722-1750).

### cs_regs_access
**Before it:** same preconditions as above; additionally unavailable (`CS_ERR_DIET`) when the library is built with `CAPSTONE_DIET`.
**Params:** `csh ud`, `const cs_insn *insn`, `cs_regs regs_read` (caller-provided fixed-size array, out), `uint8_t *regs_read_count` (out), `cs_regs regs_write` (out array), `uint8_t *regs_write_count` (out). `cs_regs` is a fixed-size array type declared in the arch headers (caller must allocate it on the stack, no dynamic allocation).
**Return:** `cs_err` (`CS_ERR_OK` on success; `CS_ERR_CSH`/`CS_ERR_DETAIL`/`CS_ERR_SKIPDATA`/`CS_ERR_DIET`/`CS_ERR_ARCH` on various precondition/support failures) — must check for `CS_ERR_OK` before trusting `regs_read_count`/`regs_write_count`.
**Free:** nothing to free (arrays are caller-stack-owned).
**Combine:** Called per decoded instruction after `cs_disasm`, exactly like `cstool_arm.c` does in its `print_insn_detail_arm` routine, immediately followed by `cs_reg_name()` lookups on the returned register IDs.
**Evidence:** cs.c:2144-2187; cstool/cstool_arm.c:211-217 (real caller).

## Build notes
**Claim:** The OSS-Fuzz harness for this project (`fuzz_disasm`) is a plain C file compiled against the statically-built `libcapstone.a`, with the public header included via the standard `-I` path set up by CMake's install/build tree (`<capstone/capstone.h>`), and linked with `$LIB_FUZZING_ENGINE`. In the current checked-out tree, the gold/"next"-branch fuzz source (`capstonenext/suite/fuzz/fuzz_disasm.c`) has been replaced by an empty stub for this task's baseline, but the real OSS-Fuzz build process and an authentic reference implementation both exist and are inspectable: the v5-branch `capstonev5/suite/fuzz/fuzz_disasm.c` (full source read above) shows the authentic `cs_open`→`cs_option`→`cs_disasm`→iterate/detail→`cs_free`→`cs_close` pattern that downstream harness generation for `capstonenext` should mirror (adjusted for `next`-branch API, e.g. the extra `cs_regs_access`/`alias_id`/`illegal` fields).
**Evidence:**
> ossfuzz_view/capstone-c1/build.sh:19-54: `for branch in v5 next do cd $SRC/capstone$branch; mkdir build; cd build; cmake -DCAPSTONE_BUILD_SHARED=0 ..; make ... FUZZO=CMakeFiles/fuzz_disasm.dir/suite/fuzz/fuzz_disasm.c.o ... $CXX $CXXFLAGS $FUZZO -o $OUT/fuzz_disasm$branch libcapstone.a $LIB_FUZZING_ENGINE`
> ossfuzz_view/capstone-c1/Dockerfile:17-21: base image `gcr.io/oss-fuzz-base/base-builder-python`; `git clone --branch next ... capstonenext`.
> capstonenext/suite/fuzz/fuzz_disasm.c:1-16: current stub only `__asm__("cs_disasm")`-aliases a no-op symbol to keep the target linked — confirms no real call site remains in this branch's fuzz dir, so cstool.c / tests/integration/test_poc.c / capstonev5's fuzz_disasm.c are the best surviving evidence of correct call protocol.
> capstonev5/suite/fuzz/fuzz_disasm.c:10,44,49,56,93,96: `#include <capstone/capstone.h>` (header resolved via CMake's generated include path for the static lib target), `cs_open`, `cs_option(CS_OPT_DETAIL,...)`, `cs_disasm(...)`, `cs_free(...)`, `cs_close(&handle)` — i.e. plain C (no C++-only constructs), compiled with `$CXX` per build.sh:54 (links fine as C compiled under a C++ driver) against `libcapstone.a`.