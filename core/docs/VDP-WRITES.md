# VDP writes

`waterbox\gens_vdp_write.c` ports the pinned `Write_Byte_VDP_Data` and
`Write_Word_VDP_Data` paths for VRAM, CRAM, VSRAM, and DMA fill. It also
implements first/second control-word sequencing and control-port register
dispatch. Its `gens_vdp_genesis_write` callback handles the Genesis VDP
write range through `0xc00007`. It is not a renderer or complete VDP.

## Shared configuration

`waterbox\gens_vdp.c` connects the read, write, register, DMA timing, and
Genesis memory-transfer adapters using a single `GensVdpConfig`. The
configuration supplies one original 26-dword register backing and one
seven-dword `Ctrl` backing. The initializer derives the individual register
and control pointers and rejects conflicting DMA/flag pointers.
The derived renderer fields still come from `GensVdpRegisters`.
Live odometer and IRQ callbacks are required.

`gens_vdp_init` configures adapters without resetting machine state or
regenerating the horizontal-counter table. Initialize that table where the
original machine resets it. Failed initialization leaves the output object
unchanged. All backings must follow the guest's snapshot rules and remain
alive at stable addresses. The `GensVdp` object also must remain at its
original address because it contains internal callback/context pointers.
Do not copy or move it after initialization. `gens_vdp_valid` rejects moved
objects whose internal links still point to the old object.

Set `GensGenesisBusConfig.vdp` to the initialized object to route Genesis
VDP reads and writes through these adapters. The bus validates the shared
ROM/RAM backing and retains the generic device callbacks for other devices.
Leaving `vdp` null preserves the earlier generic VDP callback path.

`gens_vdp_window_read` and `gens_vdp_native_device_read` expose the same state
to the Z80-facing adapters. A machine router must dispatch only VDP operations
to these callbacks and preserve its YM/PSG and banked 68000 routes.
`gens_vdp_dma_tick` exposes the same pending-DMA countdown for direct use.
The frame scheduler can select it through
`gens_frame_run_dma(&vdp.timing)` using the exact same machine globals.
The original no-argument frame entry point retains legacy `Update_DMA`
dispatch. `gens_frame_run_vdp` also binds the IRQ adapter described below.
This wiring is not machine initialization, VDP reset, rendering,
or a working emulator.
The separate reset adapter is documented in `VDP-RESET.md`.

## Read/write state

The adapter references machine-owned memory and globals. Supply the original
64 KiB VRAM and 256-byte CRAM/VSRAM backings, control state, DMA state, and
dirty flags. Keep the pointers stable and guest-owned under the snapshot
rules. The candidate uses low-word and low-byte stores where the original
assembly writes only those portions of a 32-bit global. The register setter
likewise preserves the upper bytes in dword-spaced register slots when the
assembly writes only a register byte. The write configuration's `vram_flag`
and `cram_flag` point to complete 32-bit flags so DMA can reproduce its
dword writes. Ordinary data writes and VRAM fill still modify only the
corresponding flag's low byte.

`gens_vdp_set_register` masks the register index to five bits, including
direct calls outside the control-port path. Background color replaces its
complete dword, while the other stored register bytes preserve upper bits.
The DMA source high-byte write masks the data with `0x7f`, separately from
the DMA mode bits. Register 12 tests `0x81` for display width and derived
window/sprite address masks, and updates only the low byte of `Win_X_Pos`.
Writing the window or sprite address registers directly instead tests bit
zero of register 12. These deliberately different paths follow the assembly.

Byte data writes duplicate their low byte into both halves of the word before
dispatch. VRAM writes align the memory destination to an even address and
rotate the word for odd control addresses. CRAM and VSRAM writes mask the
address to `0x7e`. Valid writes advance only the low 16 bits of
`Ctrl.Address`, preserving its high word. VRAM and CRAM writes set their
original dirty flags. VSRAM writes do not set a flag in these handlers.
A data write in a read or invalid access mode takes the original `error`
path. It only clears `Ctrl.Flag` and leaves memory and the address unchanged.

The control decoder follows the pinned 64-entry `CD_Table`. First words
preserve the existing upper half of `Ctrl.Data`, and second words replace it.
Register commands set the original access/address state before calling the
`gens_vdp_register_callback` callback. This callback ports the register
storage and derived address, window, scroll, screen-size, DMA, and IRQ-trigger
state from the pinned `Set_VDP_Reg`. The IRQ line itself is still updated by
the required `update_irq` callback in the register configuration. An absent
callback fails instead of applying only the raw register byte.

## Interrupt update and acknowledge

`waterbox\gens_vdp_irq.c` ports `Update_IRQ_Line` and `Int_Ack`.
Configure `GensVdpIrq` with the same register-0/register-1 slots used by
the VDP, the original pending-interrupt dword, and the actual CPU pending
interrupt byte. Set the register configuration's `opaque` to this object
and `update_irq` to `gens_vdp_irq_update`. The CPU interrupt request backend
remains required when an enabled interrupt is pending.

An enabled vertical interrupt takes priority over an enabled horizontal
interrupt. The backend receives level 6 or 4 and autovector `-1`.
Updating the line does not acknowledge the VDP pending bits. If neither
interrupt is eligible, the routine clears only the low nibble of the CPU
interrupt byte. A missing or failed CPU request backend fails explicitly,
without falling through from a failed vertical request to a horizontal one.

`gens_vdp_irq_ack` reproduces the original host `EAX` result through its
output argument. Vertical acknowledge clears only pending bit 3 and returns
a remaining enabled horizontal interrupt as `AL = 4`. It also reproduces
the original `AH` effect while preserving the upper word. Other acknowledges
clear the entire pending low byte and clear only `AL` in the supplied result.
Upper bytes of the pending-interrupt dword are preserved.

The acknowledge helper does not write the CPU interrupt byte. Starscream's
generated bridge calls `Int_Ack`, stores `AL` into that byte, and restores
its saved host `EAX`. The x86-64 CPU port must implement the equivalent
bridge. Do not copy the pinned i386 context offset `35 * 4` into a new CPU
layout. Supply the actual pending-byte pointer instead.

The register path and the scheduler's `gens_frame_run_vdp` entry point can
both use the same IRQ object. The latter validates register/pending pointers
and routes its original horizontal/vertical update sites through the adapter.
Older frame entry points retain legacy `Update_IRQ_Line` dispatch.
There is still no running CPU interrupt backend or acknowledge bridge.

DMA control commands honor the original enable bit and fill-mode check.
Matching fill commands arm `Ctrl.DMA` for the following data-port write.
The mode comparison uses the low byte, and arming modifies only the low byte
of `Ctrl.DMA`. A copy-mode command with a nonzero low-word length executes
the candidate VRAM copy directly. It does not need `run_dma`.

VRAM copy reads a byte, increments the wrapped 16-bit source, writes that byte,
and advances the wrapped 16-bit destination by the auto-increment. Overlapping
ranges are copied forward, not buffered or passed to `memmove`. The path
sets DMA status, clears the complete DMA length, schedules type 3 and the
low-word byte count, replaces the complete VRAM dirty flag with 1, stores the
zero-extended final source, and preserves the high word of `Ctrl.Address`.
Successful copies leave `Ctrl.DMA` unchanged. Zero-length copies are skipped
and clear `Ctrl.DMA`, unlike zero-length fill.

Other enabled DMA commands call the required `run_dma` backend with the
decoded DMA kind, mode, and destination address. The candidate
`gens_vdp_memory_dma` implements Genesis ROM/RAM sources as described below.
If no backend is supplied, the command
fails after the control-port state changes. The ineligible DMA modes clear
the pending DMA flag as in the assembly. Non-fill commands with a zero DMA
length do not transfer data. Non-copy commands with an invalid destination
code still set the DMA-status bit before clearing the pending DMA flag.
The backend receives the existing pending DMA flag unchanged.

When `Ctrl.DMA` has fill bit `0x4`, a data write bypasses the selected access
mode and starts the original VRAM fill sequence. It clears the DMA length and
fill flag, writes the low byte to the toggled initial address, sets DMA status,
and writes the high byte following the original signed decrement loop. A zero
DMA length uses the original `0xffff` counter and causes 65,536 high-byte
writes. A nonzero length `N` causes `N + 1` high-byte writes. The zero-length
fill regression case checks that a unit-stride fill wraps back to its starting
address and overwrites the initial low-byte write.
This path updates the DMA type, length, VRAM flag, and control address.

## Genesis memory transfers

`waterbox\gens_vdp_memory_dma.c` provides the `run_dma` callback for
ROM and 68000 RAM transfers to VRAM, CRAM, or VSRAM. Set the write
configuration's `opaque` to its `GensVdpMemoryDma` configuration and
`run_dma` to `gens_vdp_memory_dma`. The configuration references that same
write port, the shared DMA-timing configuration, word-swapped ROM/RAM
backings, logical ROM size, and Sega CD started flag. Validation rejects
different pending-count/type/status pointers between the two adapters.
The register callback still uses its separate `register_opaque`.

The original doubles the DMA source register with 32-bit wrapping and
selects ROM or RAM once, before applying source masks. ROM begins at
`source & 0x3ffffe` and advances continuously through its backing, including
across the initial 4 MiB mask boundary. RAM begins at `source & 0xfffe`
and wraps each source-word step at 64 KiB. Do not route these accesses
through the cartridge mapper or ordinary 68000 bus.

VRAM writes preserve the odd-address word rotation. CRAM/VSRAM mask the
initial destination to seven bits and each word's destination to `0x7e`.
The original increments the destination's low word after every transfer.
Transfers use the low DMA-length word for the loop, but scheduling and the
final source-register update use the complete original DMA-length dword.
The backend preserves that distinction even for noncanonical upper bytes.

Memory transfers set the corresponding complete dirty flag, update only
the low byte of the DMA type, clear the pending fill flag, update source
and destination registers, and call the DMA-timing helper. They then subtract
its returned cost from the shared `GensM68kBusState.io_cycle_counter`.
This ports Starscream's register-ABI `releaseCycles`, whose generated body is
`sub [__io_cycle_counter], eax`. It is not a no-argument C callback. The
existing CPU bus bridge restores the modified counter after the device call.

Sega CD program/word/cell sources fail explicitly. ROM sources and normal
RAM at or above `0x240000` retain their original selection even if the CD
started flag is set. The backend checks the entire ROM source span before
writing, rather than reading beyond the supplied backing. Failure propagates
through the control-port callback. Control-word side effects that occurred
before backend validation are not rolled back.

## DMA timing

`waterbox\gens_vdp_dma.c` ports `Update_DMA` separately from the transfer
loops. `GensVdpDmaTiming` references the live register, line, status, cycle,
and pending-DMA globals. `gens_vdp_dma_update` updates the pending count and
returns the original 68000 cycle-steal result through its output argument.
It does not itself execute a transfer or add cycles to the CPU.

The rate table uses bit zero of register 12, the low two DMA-type bits,
the unsigned line comparison, and the display-enable bit. It deliberately
does not use the `0x81` width test from register 12's setter. Types 0 and 1
steal CPU cycles. Types 2 and 3 consume DMA time without stealing cycles.

Unfinished transfers consume a table entry and return a full line's cycles
for memory DMA. Finished transfers clear the DMA-status bit and pending
count. Their cycle calculation shifts the 32-bit cycle count left by 16,
divides by the table rate, multiplies by the pre-update DMA length, and
shifts right by 16. Preserve that order and the original unsigned widths.
It is not equivalent to multiplying cycles by length before dividing. For
example, 83 units in a blanked 32-cell line at 488 cycles returns 487.

The memory-transfer backend calls this helper at transfer completion.
The frame scheduler's `gens_frame_run_dma` entry point also uses this helper
at the original scanline boundaries. A complete machine must initialize
the shared state and choose that entry point. See `FRAME-SCHEDULER.md`.

## External comparison

The adapters can be configured through `GensVdp` and the optional Genesis
bus route, but they are not connected to a complete running machine.
`vdp-write-check` runs focused synthetic unit cases and compares the extracted,
unchanged i386 control decoder and VRAM-copy path against the candidate.
The copy fixture covers overlapping ranges, source/destination wrapping,
zero and maximum lengths, auto-increments, disabled DMA, and nonzero upper
state bytes. It also checks fill arming, but does not execute the original
fill loop. Unsupported memory-transfer and register-setter paths trap in the
reference. A changed VRAM byte is the negative control.

`vdp-register-check` runs the register/control integration cases and
compares extracted, unchanged i386 register routines against the x86-64
candidate. It compares all 256 byte values at 64 direct-call indices, four
mode settings, and both zero and nonzero register upper bytes. It also
compares dependent write sequences. Pointer fields are recorded as VRAM
offsets, not host addresses. A mutated DMA-source byte is the negative control.
The reference replaces IRQ updates with a call counter, not a CPU.

`vdp-dma-check` compares extracted, unchanged i386 `Update_DMA` and its timing
table against the candidate. It covers rate boundaries, display and blanking
states, 32/40-cell selection, fill/copy types, upper state bytes, unusual
cycle counts, and a sequence of scanlines. A changed cycle result is the
negative control. The fixture has no CPU and uses synthetic line state.

`vdp-memory-dma-check` compares extracted, unchanged i386 control decoding,
Genesis transfer loops, and `Update_DMA` against the candidate. Synthetic
ROM/RAM, boundary addresses, odd VRAM destinations, palette/scroll wrapping,
upper DMA-length bytes, and active/blanked timing are covered. The reference
cycle-release stub performs the generated Starscream subtraction without
executing a CPU. Sega CD transfer labels trap. A changed cycle-counter result
is the negative control.

`vdp-integration-check` exercises shared configuration, register commands,
data read/write, ROM DMA and cycle-counter restoration through the 68000 bus,
DMA ticks, Z80 status adapters, callback failure propagation, and preservation
of non-VDP device routing. Register writes also exercise the IRQ adapter.
Its CPU interrupt, odometer, and cheat callbacks are synthetic.
It is not an original-versus-port machine comparison.

`vdp-irq-check` compares extracted, unchanged i386 IRQ update and acknowledge
routines against the candidate. It covers every pending byte, every initial
CPU interrupt byte, enabled/disabled interrupt combinations, repeated
acknowledges, and raw `EAX` effects with nonzero upper state bytes.
Its CPU request probe records levels/vectors without executing a CPU.
An altered acknowledge result is the negative control.

These commands have not been built or executed here. The register C AST has been
interpreted against a source-derived instruction model, but that is not native
execution. These fixtures do not prove gameplay or movie synchronization.
