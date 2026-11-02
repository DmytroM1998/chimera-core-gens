# Genesis Z80 bus arbitration

`waterbox\gens_z80_control.c` ports the Genesis bus-request and reset paths
from the pinned `Mem_M68k.asm`. It is not a Z80 CPU or sound-chip
implementation. It has not been compiled or run natively.

Set `GensGenesisBusConfig.z80_control` to a stable, guest-owned
`GensZ80Control` to route accesses at `0xa11100` and writes at `0xa11200`
through this implementation. The control object must reference the same
`Z80_State` as the bus configuration. If it is null, those accesses still
require the generic device callbacks. Other device accesses are unchanged.
The control object and its machine globals must remain valid at stable
addresses and obey miniBox snapshot rules.

The control references Gens' existing machine globals rather than keeping
a second copy. Supply the original 512-entry `Z80_M68K_Cycle_Tab`, generated
by Gens with `(int)((double)x * 7.0 / 15.0)`, and the current frame cycle
targets. CPU odometer, Z80 execution, Z80 odometer updates, Z80 reset, and
YM2612 reset use caller-supplied callbacks. They receive the current memory
path state so CPU timing can use the live values saved at the bus boundary.
They must perform the real emulation operations before this can be used
in a running machine.
`gens_sound_reset_ym2612` in `SOUND.md` forwards the sound-reset callback
to the pinned chip implementation. The CPU callbacks remain unconnected.

The port preserves the strictly-greater-than-16-cycle bus-taking boundary,
32-bit odometer subtraction, byte-sized updates to machine globals, and
the different byte and word bus-request results. Word reads toggle only
the low byte of `Fake_Fetch`. Within the bus-taking window, the word path
adds `0x80` to the low byte of `Last_BUS_REQ_St`, whereas the byte path
ORs it with `0x80`. This matters for noncanonical restored values.

Every bus-request write clears the full controller counters and delays,
including repeated writes that do not change ownership. Word writes test
bit zero of the high byte, not the low byte. Z80 reset assertion calls the
Z80 reset callback, sets state bit 2, then calls the YM2612 reset callback.
Releasing reset clears that bit without executing either callback.

Missing callbacks fail only when required by the selected path. Invalid
arguments, a cycle table shorter than 512 entries, and a cycle index outside
the original table also fail. The bus propagates these failures as backend
errors. Already-applied controller and machine-state changes are not rolled
back. The caller must report the error and stop execution. The original
out-of-bounds table read is not reproduced.

On the external Linux builder, `compile z80-control-check` exercises
synthetic timing boundaries, byte and word behavior, reset ordering, and
failure side effects. `compile genesis-bus-reference-check` also compares
these paths against the extracted original i386 handlers. Its callback
traces record state at odometer, execution, and reset boundaries, using stub
CPU and sound-chip operations. Neither command has been run here. Neither
establishes game or movie synchronization.
