# 68000-facing Z80 memory window

`waterbox\gens_z80_memory.c` implements the byte handlers reached from
Genesis' 68000 Z80-memory window in the pinned `Mem_M68k.asm` and
`Mem_Z80.asm`. It is not the Z80 CPU's complete memory backend, and has not
been compiled or run natively.

Set `GensGenesisBusConfig.z80_memory` to a stable, guest-owned
`GensZ80Memory` to enable it. When absent, the bus still forwards accesses
to its generic Z80-memory device callbacks. The existing bus gate returns
zero or ignores writes while either bit in `Z80_State & 6` is set.
The adapter references the original 8 KiB Z80 RAM and `Bank_Z80` directly.
Pointers, callbacks, and storage must obey the guest's snapshot rules.

The window mirrors at 32 KiB. Its first 16 KiB mirror the 8 KiB RAM.
Reads at `0x4000` through `0x5fff` call YM2612 with the low two address bits.
Bank-register reads return zero. Writes at `0x6000` through `0x60ff`
shift the original bank latch using
`((bank & 0xff0000) >> 1) + ((value & 1) << 23)`. Other bank writes
are ignored.

The `0x7000` region preserves Gens' unusual address checks. Byte-handler
reads at `0x7f04` through `0x7f07` call VDP status. Even addresses replace
the low result byte with the high byte without discarding the original
high byte. `0x7f08` and `0x7f09` call the V and H counter routines.
Other reads return zero. Writes anywhere from `0x7000` through `0x7f03`
call the byte VDP data routine. A write at `0x7f11` calls PSG. Other
writes are ignored.

A 68000 word access still calls these byte handlers. A RAM word read
returns one byte, not two. A word write stores only its low byte and
never changes the following RAM byte. YM2612, PSG, and VDP writes also
use only that byte. Chip reads preserve the full `AX` returned by the
original byte handler when the 68000 consumes it as a word. Read-device
callbacks must return that value, not just assume every byte handler
zero-extends its result. The supplied V/H counter backends must reproduce
the original routines' zero high byte.

Callbacks receive the current 68000 memory-path state, the selected device,
and either the YM2612 port or the mirrored Z80 address. Real chip callbacks
must preserve original chip side effects and timing. Missing or failed
required callbacks fail the access, and the outer bus propagates a backend
error. Unmapped accesses retain the original successful zero or ignored
behavior. Failures do not roll back earlier side effects.

Cold machine initialization still needs the original `Cpu_Z80.c`
`Z80_Reset` behavior, including clearing RAM and setting `Bank_Z80` to
`0xff8000`. This is not the CPU-only reset called when asserting the
Genesis Z80 reset line. This adapter does not perform either reset.
It also does not implement the Z80 CPU, its native word handlers, its
banked 68000 window, direct CPU fetch mapping, or memory hooks.
The separate candidate for native handlers is described in `Z80-NATIVE.md`.

On the external Linux builder, `compile z80-memory-check` exercises the
window with synthetic chip callbacks. `compile genesis-bus-reference-check`
also links the unchanged pinned `Mem_Z80.asm` and compares its window
dispatch with the candidate. It uses fake chip routines, not real VDP or
sound-chip implementations. These commands have not been run here and
do not establish movie synchronization.
