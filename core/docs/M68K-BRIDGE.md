# Experimental 68000 memory bridge

`waterbox\gens_m68k_bridge_x64.asm` adapts a proposed Starscream register
convention to the separate `gens_m68k_bus.c` interface. It is not a port of
the Starscream generator, is not wired into an emulator, and has not been
assembled or run here. The register convention is provisional and must be
checked against the ported generator and the original i386 reference.

The bridge is entered with a regular `call` from x86-64 assembly, not by
calling its entry points as C functions. `r12` holds a guest-owned
`GensM68kBus` pointer. `rsi` and `rbp` hold the full-width host fetch PC
and fetch base. `edi` holds the cycle counter, `edx` holds the emulated
address, and `ecx` holds the value for a write. The entry points specify
byte, word, or long access and normal or predecrement ordering.

The bridge preserves `rax`, including its cached condition-code bits, as
well as `rbx`, `r8`, `r9`, `r11`, and `r12` through `r15`. It returns
the possibly changed live fetch pointers in `rsi` and `rbp`, and returns
the cycle counter and address zero-extended from `edi` and `edx`. A
successful read returns a zero-extended value in `ecx`. A write or failed
read preserves all of `rcx`.

The 32-bit bus status is returned in `r10d`. Carry is clear on success and
set on failure. The generated CPU must check this immediately and propagate
an error instead of continuing execution. A backend failure can occur after
a first word has already produced side effects. Other host flags and
vector registers are not preserved. The helper's existing restrictions,
including the explicit RAM boundary error, also apply.

The bridge aligns the stack dynamically before the SysV C call. It uses
absolute 64-bit helper addresses so the call does not depend on a 32-bit
relative displacement. It restores the caller's original stack position and
does not use a red zone. The bus implementation and any code it calls must
be compiled with the guest's ABI and no-red-zone requirements. Compile-time
assertions check the C live-state layout and enum ABI used by the assembly.

On the external Linux x86-64 builder, `compile bridge-check` assembles a
test harness and the bridge. The test maps the bus, state, and RAM at
`0x36f00000000` using `MAP_FIXED_NOREPLACE`. It covers all entry points,
both stack alignments, preserved registers, callback mutations, high fetch
addresses, long-access ordering, and failures. Linker wrappers check the
C helper's actual entry stack alignment. The test cannot establish generated
CPU correctness, device behavior, snapshot safety, or TAS synchronization.
It has not yet been run.
