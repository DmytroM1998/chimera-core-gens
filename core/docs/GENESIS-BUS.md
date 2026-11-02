# Genesis 68000 bus integration

`waterbox\gens_genesis_bus.c` connects the existing 68000 memory-path
interface to candidate Genesis cartridge and controller handling. It
implements the pinned `Mem_M68k.asm` dispatch for ROM reads, the SRAM
overlay and writes, mapper registers, controller registers, and the
hardware version register. It also handles RAM reached through a remapped
read or the second half of a non-RAM long access. It forwards selected Z80,
VDP, and PSG operations to caller-supplied device callbacks. It is not
connected to a CPU or complete emulator, and has not been compiled or run here.

Initialize the adapter with the original machine's 64 KiB 68000 RAM,
6 MiB word-swapped ROM backing, 64 KiB SRAM, and corresponding machine
globals. The input mapper and controller port must use the same globals.
All pointers and callback targets must be suitable for the guest and its
snapshot rules. The configuration, adapter, and backing storage must remain
alive at stable addresses. The configuration must not be changed after
initialization.

After initialization, `bus.cpu` is a `GensM68kBus` for the CPU bridge.
The existing memory path handles direct mirrored 68000 RAM and calls this
adapter for other regions. The adapter implements Gens' 32-entry read
dispatch with selectors rather than host function pointers. Mapper writes
change data-read dispatch only, as in the original code. Reset the mapping
with `gens_genesis_bus_reset_mapping` when the original machine would call
`Init_Memory_M68K` for Genesis. This function does not reset any other
machine state.

ROM bytes are word-swapped, but SRAM bytes are not. The SRAM read overlay
belongs to Gens' ROM-4 handler, including when a mapper selects that
handler at a different address. Gens tests the original address against
the configured SRAM range. Its custom SRAM read path returns zero.
Unsupported out-of-bounds backing accesses fail rather than accessing the
adjacent original globals. In particular, a word at the final byte of a
backing array is not reproduced by reading or writing beyond the array.

The caller supplies `fix_codes` and a hook callback. Both are required at
initialization. For each non-RAM byte or word write, `fix_codes` runs
after the selected write handler and before the outer 68000 write hook.
It must implement Gens' real `Fix_Codes` behavior, not a success-only
stub. A long write can call it twice, and an error can follow earlier
side effects. The outer memory-path error must be reported and execution
must stop.

The optional `z80_control` configuration routes bus-request reads and writes
and Z80 reset writes through the candidate implementation in `Z80-CONTROL.md`.
When it is absent, the generic device callbacks still handle those accesses.
The optional `z80_memory` configuration similarly routes the 68000-facing
Z80 window through `Z80-MEMORY.md`'s candidate RAM, bank-register, and
chip-dispatch implementation.
Typed forwarding to the pinned sound chips is provided in `SOUND.md`.
Candidate data, status, and counter read callbacks are described in
`VDP-READS.md`. The data-port write and fill candidate is described in
`VDP-WRITES.md`, which also covers control-word decoding and shared `GensVdp`
configuration. The optional `vdp` field routes VDP reads/writes through that
configuration, while other devices retain their existing dispatch.
The VDP and bus must share ROM/RAM backings. Leaving the field null preserves
generic VDP callback dispatch. This is not a complete machine.
Rendering is not implemented.
See `VDP-WRITES.md`.

Device callbacks may be omitted while their devices are not accessed.
An access needing a missing callback returns a backend error rather than
pretending the device worked. The callback receives the original masked
68000 address, except Z80 memory receives the original dispatch's
`address & 0x7fff` value. Its size records the 68000 access width. For
Z80 word writes the value is the low byte, because the original invokes
the byte handler. For Z80 word reads the callback must reproduce the
original byte-handler call and its resulting 16-bit return. Bus request,
reset, VDP, and PSG callbacks must reproduce their corresponding original
device paths and may update the shared live I/O state.

Unmapped reads and writes reproduce Gens' zero or ignored result. This is
different from omitting a required device callback. This adapter does not
implement the Z80 CPU, VDP, PSG, or cheat engine. It also
does not provide an input-poll callback, address-error exceptions, save
file persistence, or a miniBox guest. The current RAM-boundary policy and
other limits of the CPU memory path still apply.

On the external Linux builder, `compile genesis-bus-check` builds a
synthetic integration harness using the candidate CPU memory path,
controller implementation, and input mapper. It checks representative
mapping, SRAM, controller, device dispatch, ordering, and error cases.
It uses fake device and cheat callbacks and is not an original-versus-port
machine comparison or evidence of game or movie synchronization.

`compile genesis-bus-reference-check` prepares a separate synthetic
differential comparison. It extracts the Genesis dispatch tables and
handlers directly from the verified pinned `Mem_M68k.asm` without modifying
the copied source. It compares nine deterministic trace digests from the
original i386 handlers and controller assembly against the candidate x86-64
adapter. The scenarios cover ROM and RAM reads, mapper-selected handlers,
SRAM, controller registers, and ordinary byte and word writes. Unexpected
calls to unmodeled devices trap in the original reference or fail in the
candidate. A Z80-window scenario links the unchanged original `Mem_Z80.asm`
and compares RAM mirrors, serial bank writes, and chip dispatch through fake
chip routines. Bus-request and reset scenarios compare the original timing and
state transitions against the candidate control module using fake CPU and
sound-chip callbacks. A deliberately altered read serves as a negative control.
The comparison invokes the bus callbacks directly and does not
exercise the Starscream CPU, long accesses, real device behavior, cheat
modifications, or full-machine execution. It does not access intentionally
unsupported out-of-bounds backing cases. It has not been assembled or run.
