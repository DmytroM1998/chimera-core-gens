# Native Z80 memory-handler candidate

`waterbox\gens_z80_native.c` ports the selected byte and word handlers in
the pinned `Mem_Z80.asm`, including the banked 68000 window. This is separate
from the 68000-facing window in `Z80-MEMORY.md`. It is not a Z80 CPU, a CPU
register bridge, or a connected machine backend. It has not been compiled
or run natively.

`GensZ80Native` references the existing `GensZ80Memory` RAM and bank latch.
Its backend callback receives an operation and the original 32-bit `EAX`,
`ECX`, and `EDX` values. At entry, `ECX` is a 16-bit Z80 address, `EDX`
contains write data, and `EAX` can contain cached CPU values. The callback
must reproduce the original callee's register results, not just its memory
or chip result. These are emulated register values, not host pointers.
The opaque pointer, backend, and backing storage must be guest-owned and
obey snapshot rules.

Native RAM words use two consecutive little-endian bytes. In the pinned
assembly, the bank global immediately follows RAM. A word at mirrored
offset `0x1fff` reads or writes RAM's final byte and the low byte of
`Bank_Z80`, not RAM's first byte. The candidate reproduces that logical
layout explicitly without accessing beyond its RAM array.
Native RAM bytes preserve the other bits of `EAX`. An unmapped
PSG-region word read clears only `AL`, preserving `AH`. A bank-register word
read clears `AX`. Those paths must not be replaced with a common zero return.

YM2612 word reads call the original read path once and clear `AH`.
YM2612 word writes make two calls, incrementing the resulting `ECX`
between them. They do not shift `EDX` before the second call. The original
caller does not restore `ECX` or `EDX` between those writes. Read handlers,
in contrast, restore the saved YM2612 port in `ECX` after the call.
Native VDP data word writes use the word routine. VDP counter word reads
call V then H and combine the bytes through `CL`, matching the assembly.

Banked 68000 words invoke the byte backend twice. Callback register effects
can affect the address or data used by the second call. Byte reads restore
their saved `ECX`, and byte writes restore both `ECX` and `EDX`.
The 68000 backend must implement the original `M68K_RB` or `M68K_WB` path,
not the Starscream memory helper with its different hooks and direct-RAM
handling. There is deliberately no success-only backend or automatic
substitution of the clean typed Genesis bus callbacks.

Missing required callbacks, invalid entry arguments, and backend failures
return zero. Failures can leave earlier
register changes, memory writes, and callback side effects applied. The
caller must report the error and stop execution. The existing
`GensM68kBusState` parameter is a provisional shared backend context, not
an implementation of the Z80 CPU's `CycleIO` or other CPU context fields.

The actual Z80 core, its 64-bit context, fetch pointers, register bridge,
interrupts, cycles, and `GENS_OPT` direct-RAM paths remain unported.
Real chip and 68000 bridges must also resolve the original callback ABI.
In particular, C callee scratch-register behavior can depend on the
original compiler and binary. Recompiling a chip routine on Linux does not
establish that it reproduces the selected Windows release's incidental
register effects. That remains an original-reference fidelity requirement.
The separate `VDP-READS.md` adapter supplies the pinned status/counter
read operations. It does not implement the other native backend operations.

On the external Linux builder, `compile z80-native-check` compares four
trace digests from the unchanged i386 handlers against the candidate,
including complete register results, callback ordering, RAM, and bank state.
The reference wrapper supplies raw register inputs instead of using the
standalone C entry points, whose word-write wrapper masks data to one byte.
Both sides use deliberately clobbering fake callbacks. An altered result
serves as a negative control. RAM-to-bank boundary cases are included.
This command has not been run. It does not execute a Z80 instruction or
establish game or movie synchronization.
