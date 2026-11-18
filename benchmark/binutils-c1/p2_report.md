## target: disassembler

### Overview / Signature
`opcodes/disassemble.c:111-539`:
```c
disassembler_ftype
disassembler (enum bfd_architecture a,
	      bool big ATTRIBUTE_UNUSED,
	      unsigned long mach ATTRIBUTE_UNUSED,
	      bfd *abfd ATTRIBUTE_UNUSED)
```
Declared `include/dis-asm.h:417-419`. It is a pure dispatcher: a `switch (a)` over `enum bfd_architecture` (compiled in only for `ARCH_<name>` macros enabled at `opcodes/disassemble.c:26-104`, matching `--enable-targets=...` at configure time) that sets a local `disassemble` function pointer to a `print_insn_*` core (or calls `arc_get_disassembler(abfd)` / `cris_get_disassembler(abfd)` / `csky_get_disassembler(abfd)` for a few targets that need extra bfd-based setup), and `return`s it. On no match it `return 0;` (`opcodes/disassemble.c:535-536`). It performs **no allocation and no side effects** on `abfd` itself except calling those `*_get_disassembler` helpers, which may read bfd info (`arc_get_disassembler` calls `build_ARC_extmap(abfd)`, `opcodes/arc-dis.c:1432-1446`).

### P2.1 Init Sequence
**Claim:** No library-global init is required before calling `disassembler()` itself (it is a stateless switch). However, a `struct disassemble_info` must already exist (even if not yet passed to `disassembler`, since `disassembler` doesn't take one) because callers always call `init_disassemble_info()` before/around it and use the struct's `arch`/`mach` fields to drive both the call and the following `disassemble_init_for_target()`.
**Evidence:**
> binutils/objdump.c:4183-4184: `init_disassemble_info (&disasm_info, stdout, (fprintf_ftype) fprintf, (fprintf_styled_ftype) fprintf_styled);` called before `disassembler(...)` at line 4220.
> binutils/objdump.c:4220-4222: `aux.disassemble_fn = disassembler (bfd_get_arch (abfd), bfd_big_endian (abfd), bfd_get_mach (abfd), abfd);`
> ossfuzz fuzz_disas_ext.c:74, 95-96: `init_disassemble_info(&disasm_info, ...)` then later `disassembler(disasm_info.arch, big, disasm_info.mach, NULL)`.

### P2.2 Parameter Construction
**Claim:** `arc` (enum bfd_architecture) selects the compiled-in case (only architectures with `ARCH_<name>` defined, i.e. built with `--enable-targets=...`); `big` (bool) selects endian variant only for the few targets with a big/little split (`bfd_arch_arm`, `bfd_arch_score`; m32c forces endian itself in `disassemble_init_for_target`); `mach` (unsigned long) selects sub-variant inside an architecture (e.g. `bfd_arch_h8300`, `bfd_arch_z8k` at lines 194-204, 482-488); `abfd` (bfd*) may be `NULL` — only dereferenced by `arc_get_disassembler`/`cris_get_disassembler`/`csky_get_disassembler`, which are written to tolerate `abfd == NULL` (`opcodes/arc-dis.c:1436`, `opcodes/cris-dis.c:1646`).
**Evidence:**
> opcodes/disassemble.c:112-115: parameter list and ATTRIBUTE_UNUSED markers (abfd/mach/big only used by some cases).
> opcodes/disassemble.c:138-144 (arm), 404-409 (score), 483-488 (z8k): branches on `big`/`mach`.
> include/dis-asm.h:414-416: "/* Fetch the disassembler for a given architecture ARC, endianess (big endian if BIG is true), bfd_mach value MACH, and ABFD, if that support is available. ABFD may be NULL. */"
> opcodes/arc-dis.c:1436: `if (abfd != NULL) { build_ARC_extmap (abfd); ... }` — guards NULL.

### P2.3 Object Lifecycle
**Claim:** `disassembler()` itself has no lifecycle (stateless, returns a function pointer, nothing to destroy). The real lifecycle is around the `struct disassemble_info`: create (stack-allocate) → `init_disassemble_info()` → fill `arch`/`mach`/`buffer*` → call `disassembler()` to get the decoder → `disassemble_init_for_target()` to patch target-specific fields → invoke the returned decoder (possibly in a loop advancing through the buffer) → `disassemble_free_target()`.
**Evidence:**
> binutils/objdump.c:4155, 4183, 4220, 4252, 4278(loop via bfd_map_over_sections→disassemble_bytes), 4282: `struct disassemble_info disasm_info;` → `init_disassemble_info` → `disassembler(...)` → `disassemble_init_for_target(&disasm_info)` → disassembly loop → `disassemble_free_target(&disasm_info)`.
> ossfuzz fuzz_disas_ext.c:71-112: same order: `init_disassemble_info` → fill `disasm_info.arch/mach/buffer*` → `disassembler(...)` → `disassemble_init_for_target(&disasm_info)` → loop calling `disasfunc(...)` → `disassemble_free_target(&disasm_info)`.

### P2.4 Return Value Handling
**Claim:** Return value is `disassembler_ftype` (a function pointer typedef) or `NULL`/`0` if the architecture is unsupported (not compiled in, or no matching case). Callers MUST check for NULL before invoking it.
**Evidence:**
> opcodes/disassemble.c:535-536: `default: return 0;`
> binutils/objdump.c:4223-4229: `if (!aux.disassemble_fn) { non_fatal(...); exit_status = 1; goto out; }`
> ossfuzz fuzz_disas_ext.c:96-97: `disassembler_ftype disasfunc = disassembler(...); if (disasfunc != NULL) { ... }` — guards NULL before use.

### P2.5 Cleanup Sequence
**Claim:** `disassembler()` allocates nothing and needs no cleanup of its own. Cleanup objects are: the `struct disassemble_info` (`disassemble_free_target()` frees `info->private_data` and any target-specific sub-allocations such as powerpc/riscv internal state, m32c CGEN_BITSET bits), and any buffers the caller allocated (`disasm_info.dynrelbuf`, the instruction byte buffer, output text buffer `SFILE`/`AssemblyText`) that are independent of `disassembler()`.
**Evidence:**
> opcodes/disassemble.c:729-785 (`disassemble_free_target`): frees `info->private_data` at line 784, with `mask->bits` freed for m32c at line 747, `disassemble_free_powerpc`/`disassemble_free_riscv` called for those archs.
> binutils/objdump.c:4280-4282: `free (disasm_info.dynrelbuf); disasm_info.dynrelbuf = NULL; disassemble_free_target (&disasm_info);`
> ossfuzz fuzz_disas_ext.c:110: `disassemble_free_target(&disasm_info);` called once after the decode loop, inside the `if (disasfunc != NULL)` block (i.e. only if a decoder was obtained and `disassemble_init_for_target` ran).

### P2.6 API Existence
**Claim:** All entries in the Logic Group exist in the current codebase at the stated locations.
**Evidence:**
> opcodes/disassemble.c:111-539: `disassembler` definition.
> include/dis-asm.h:417-419: `disassembler` declaration.
> opcodes/dis-init.c:26-48: `init_disassemble_info` definition; declared include/dis-asm.h:472-474.
> opcodes/disassemble.c:587-727: `disassemble_init_for_target` definition; declared include/dis-asm.h:423.
> opcodes/disassemble.c:729-785: `disassemble_free_target` definition; declared include/dis-asm.h:426.
> opcodes/dis-buf.c:29-?: `buffer_read_memory` definition; declared include/dis-asm.h:448-449.
> opcodes/arc-dis.c:1432-1446: `arc_get_disassembler` definition; declared include/dis-asm.h:383.
> opcodes/cris-dis.c:1641-...: `cris_get_disassembler` definition; declared include/dis-asm.h:384.
> opcodes/dis-buf.c:81-102: `generic_print_address`, `generic_symbol_at_address`, `generic_symbol_is_valid` definitions.

### P2.7 Co-call Constraints
**Claim:** Paired calls: `init_disassemble_info()` (begin) must precede any use of the `disassemble_info` struct including `disassembler()`/`disassemble_init_for_target()`; `disassemble_init_for_target()` must be called after `info->arch`/`info->mach` are set and after `disassembler()` returned a non-NULL function, and must be paired with a later `disassemble_free_target()` (its counterpart) — calling `disassemble_free_target` without a prior successful `disassemble_init_for_target` is harmless (it just frees NULL `private_data` for most archs) but calling the decoder without `disassemble_init_for_target` first risks using unset target-specific fields (e.g. m32c `private_data`/`endian`, arm `symbol_is_valid`).
**Evidence:**
> include/dis-asm.h:421-423: "/* Amend the disassemble_info structure as necessary for the target architecture. Should only be called after initialising the info->arch field. */ extern void disassemble_init_for_target (struct disassemble_info *);"
> include/dis-asm.h:425-426: "/* Tidy any memory allocated by targets, such as info->private_data. */ extern void disassemble_free_target (struct disassemble_info *);"
> binutils/objdump.c:4252 / 4282: `disassemble_init_for_target(&disasm_info);` ... `disassemble_free_target(&disasm_info);` — bracket the whole disassembly phase.
> ossfuzz fuzz_disas_ext.c:98,110: `disassemble_init_for_target(&disasm_info);` ... decode loop ... `disassemble_free_target(&disasm_info);` — only reached inside `if (disasfunc != NULL)`.

### P2.8 Prerequisite State
**Claim:** Before `disassembler()` is called, the caller must have already decided/known the triple `(arch, big, mach)` — typically derived from a `bfd*` via `bfd_get_arch(abfd)`, `bfd_big_endian(abfd)`, `bfd_get_mach(abfd)` (objdump path) or set directly from fuzz input validated with `bfd_lookup_arch()` (fuzz harness path). `abfd` may be NULL only if the chosen arch's branch doesn't dereference it (true for all except arc/cris/csky). The project enforces which `ARCH_*` are compiled in via `--enable-targets=...` at configure time (`binutils-gdb/configure` → `ARCH_all`/`ARCH_<name>` macros in disassemble.c lines 26-104); OSS-Fuzz build uses `--enable-targets=all`.
**Evidence:**
> opcodes/disassemble.c:26-104: `#ifdef ARCH_all ... #define ARCH_aarch64 ... #endif` block; every `case` in `disassembler()` is itself guarded by the matching `#ifdef ARCH_x`.
> binutils-gdb/build.sh (ossfuzz_view) :38-41: `./configure --disable-gdb ... --enable-targets=all` — ensures (almost) all ARCH_* macros are defined so `disassembler()` supports all architectures in the fuzz build.
> ossfuzz fuzz_disas_ext.c:95-96: `if (bfd_lookup_arch (disasm_info.arch, disasm_info.mach) != NULL) { disassembler_ftype disasfunc = disassembler(disasm_info.arch, big, disasm_info.mach, NULL); ...}` — validates arch/mach combo with `bfd_lookup_arch` before calling `disassembler`, and passes `abfd = NULL`.

### P2.9 Result Usage
**Claim:** The returned `disassembler_ftype` (signature `int (*)(bfd_vma, struct disassemble_info *)`) is invoked directly by the caller, normally in a loop that advances `info->buffer`/`info->buffer_vma`/`info->buffer_length` by the number of octets the decoder consumed (its return value), stopping when the decoder returns too few octets or the buffer is exhausted; the decoded text lands in whatever `info->stream`/`info->fprintf_func` the caller configured.
**Evidence:**
> opcodes/dis-asm.h-derived typedef: `typedef int (*disassembler_ftype) (bfd_vma, struct disassemble_info *);` (used by all call sites below).
> binutils/objdump.c:3104: `octets = (*disassemble_fn) (section->vma + addr_offset, inf);`
> binutils/objdump.c:3517-3518: `insn_size = (*disassemble_fn) (section->vma + addr_offset, inf); octets = insn_size;`
> ossfuzz fuzz_disas_ext.c:99-109:
> ```
> while (1) {
>   s.pos = 0;
>   int octets = disasfunc(disasm_info.buffer_vma, &disasm_info);
>   if (octets < (int) disasm_info.octets_per_byte) break;
>   if (disasm_info.buffer_length <= (size_t) octets) break;
>   disasm_info.buffer += octets;
>   disasm_info.buffer_vma += octets / disasm_info.octets_per_byte;
>   disasm_info.buffer_length -= octets;
> }
> ```
> this is the canonical "advance by return value" consumption pattern of the target's result.

### P2.10 Behaviour Controls
**Claim:** Within `disassembler()`: `a` (arch) is the dominant axis selecting an entirely different decoder (`print_insn_i386`, `print_insn_aarch64`, etc., opcodes/disassemble.c:123-534); `big` flips the decoder function for `bfd_arch_arm` (`print_insn_big_arm` vs `print_insn_little_arm`, lines 140-144) and `bfd_arch_score` (lines 405-409); `mach` selects within `bfd_arch_h8300` (lines 195-203) and `bfd_arch_z8k` (`bfd_mach_z8001` vs default z8002, lines 484-487); `abfd` controls `arc_get_disassembler`/`cris_get_disassembler`/`csky_get_disassembler` sub-selection (cris: `bfd_get_symbol_leading_char`, `bfd_get_mach` → v32/v10_v32/v10 register-prefix variants, opcodes/cris-dis.c:1641-1670ish). After the call, `disassemble_init_for_target()` further branches on `info->arch` to set `info->endian`, `info->private_data`, `info->symbol_is_valid`, `info->disassembler_needs_relocs`, `info->created_styled_output`, `info->skip_zeroes` — these change which code paths the *print_insn_** core takes once invoked (e.g. styled-vs-plain output, zero-skipping heuristics, relocation-aware operand printing). `info->disassembler_options` (a string, e.g. `options[100]` in the fuzzer) is parsed per-architecture by the core decoders (not shown here but referenced via `disasm_info.disassembler_options`).
**Evidence:**
> opcodes/disassemble.c:138-144, 404-409, 194-204, 482-488: arch/big/mach branching shown above.
> opcodes/disassemble.c:587-727 (`disassemble_init_for_target`): per-arch field tweaks, e.g. lines 606-611 (arm: `symbol_is_valid`, `disassembler_needs_relocs`, `created_styled_output`), 630-633 (ia64: `skip_zeroes = 16`), 666-680 (m32c: forces `endian = BFD_ENDIAN_BIG`, allocates `private_data` CGEN bitset keyed on `info->mach == bfd_mach_m16c`).
> ossfuzz fuzz_disas_ext.c:82,90-93: `disasm_info.disassembler_options = options;` and `disasm_info.flags |= USER_SPECIFIED_MACHINE_TYPE; disasm_info.arch = arch; disasm_info.mach = bfd_getl64(&Data[Size-9]); disasm_info.flavour = Data[Size-10];` — demonstrates fuzz-input-driven arch/mach/options selection feeding the behaviour controls.

### P2.11 Input Expectations
**Claim:** `disassembler()` itself takes no buffer/byte input — it only selects a decoder based on `(arch, big, mach, abfd)` metadata, all of which must be valid/consistent (`arch` a legal `enum bfd_architecture` value, `mach` a value `bfd_lookup_arch` accepts for that arch, or 0 for the architecture default). The *returned* decoder (not `disassembler` itself) is what consumes the raw instruction bytes, via `info->buffer`/`info->buffer_vma`/`info->buffer_length` and the `info->read_memory_func` (default `buffer_read_memory`), which bounds-checks `memaddr`/`length` against `buffer_vma`/`buffer_length` before any decoder reads bytes (preventing OOB at the buffer layer, though decoder-internal logic can still request multiple reads producing logic errors). No required header/magic/termination is imposed by `disassembler()` itself; termination of a decode loop is caller responsibility (checking the decoder's returned octet count vs `octets_per_byte` and remaining `buffer_length`, as in objdump.c/fuzz_disas_ext.c above).
**Evidence:**
> include/dis-asm.h:414-416: doc comment, no byte buffer parameter in `disassembler()`'s own signature (opcodes/disassemble.c:112-115).
> opcodes/dis-buf.c:29-44 (`buffer_read_memory`): bounds checks `memaddr < info->buffer_vma`, `addr_off > info->buffer_length`, `length > info->buffer_length - addr_off`, and `stop_vma` checks before copying bytes — this is the actual input-size gate for the returned decoder's byte stream.
> include/dis-asm.h:236-239: "/* These are for buffer_read_memory.  */ bfd_byte *buffer; bfd_vma buffer_vma; size_t buffer_length;" — fields the caller must set before invoking the returned decoder.
> ossfuzz fuzz_disas_ext.c:78-80: `disasm_info.buffer = (bfd_byte *) Data; disasm_info.buffer_vma = 0x1000; disasm_info.buffer_length = Size-10;` — real harness filling buffer fields from fuzz input before calling the returned decoder.

### Build notes
**Claim:** Fuzzer sources for this project are plain C, compiled with `$CC`/`$CXX` from OSS-Fuzz env, linking statically against `libopcodes.a`, `libbfd.a`, `libctf.a`, `libz.a`, `libsframe.a`, `libiberty.a`; the target's header is reached via `-I ../include` (for `dis-asm.h`) plus `-I ../bfd -I ../opcodes` (for `bfd.h`, `disassemble.h`, `sysdep.h`). A dedicated per-architecture fuzzer `fuzz_disas_ext.c` is compiled multiple times with `-DFUZZ_TARGET_ARCH=<bfd_arch_X>` to produce one binary per architecture.
**Evidence:**
> ossfuzz_view/binutils-c1/build.sh:51-55:
> ```
> LIBS="../opcodes/libopcodes.a ../libctf/.libs/libctf.a ../bfd/.libs/libbfd.a ../zlib/libz.a ../libsframe/.libs/libsframe.a ../libiberty/libiberty.a"
> for i in fuzz_disassemble fuzz_bfd fuzz_bfd_ext; do
>     $CC $CFLAGS -I ../include -I ../bfd -I ../opcodes -c $i.c -o $i.o
>     $CXX $CXXFLAGS $i.o -o $OUT/$i $LIB_FUZZING_ENGINE -Wl,--start-group ${LIBS} -Wl,--end-group
> done
> ```
> ossfuzz_view/binutils-c1/build.sh:62-67:
> ```
> for ARCH_TARGET in bfd_arch_arm bfd_arch_mips bfd_arch_i386 bfd_arch_arc bfd_arch_csky bfd_arch_mep; do
>     $CC $CFLAGS -I ../include -I ../bfd -I ../opcodes -c fuzz_disas_ext.c -DFUZZ_TARGET_ARCH=$ARCH_TARGET -o fuzz_disas_ext-$ARCH_TARGET.o
>     $CXX $CXXFLAGS fuzz_disas_ext-$ARCH_TARGET.o -o $OUT/fuzz_disas_ext-$ARCH_TARGET $LIB_FUZZING_ENGINE -Wl,--start-group ${LIBS} -Wl,--end-group
> done
> ```
> ossfuzz_view/binutils-c1/fuzz_disas_ext.c:22-25: `#include "sysdep.h" / #include "bfd.h" / #include "dis-asm.h" / #include "disassemble.h"` — header include pattern matching the `-I ../include -I ../bfd -I ../opcodes` search paths.
> ossfuzz_view/binutils-c1/build.sh:38-41: `./configure --disable-gdb --disable-gdbserver --disable-gdbsupport --disable-libdecnumber --disable-readline --disable-sim --disable-libbacktrace --disable-gas --disable-ld --disable-werror --enable-targets=all` — `--enable-targets=all` ensures the `ARCH_all` macro block in opcodes/disassemble.c is active so `disassembler()` supports (nearly) every architecture in the fuzz build.
> Note: `ossfuzz_view/binutils-c1/fuzz_disassemble.c` in the current snapshot is a stub ("Gold harness replaced by an empty stub for baseline generation") that only keeps `disassembler` linked via an `__asm__` alias and does not exercise it; `fuzz_disas_ext.c` is the real, functioning example of correct target usage and should be used as the template for the new harness.