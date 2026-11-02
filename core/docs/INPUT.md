# Controller mapping

`waterbox\gens_input.c` is a preliminary native/guest input mapping module.
`gens_io.c` and `gens_io_data_x64.asm` implement a candidate x86-64 port of
the original controller globals and protocol. They are not yet linked to
an emulator, package, or Chimera `FrameAdvance` export. They have not been
assembled, compiled, or executed natively on this machine.

The provisional packed wire uses 38 bits, within Chimera's 64-bit
`FrameAdvance` channel:

| Bit range | Meaning |
|---|---|
| 0 | Power level |
| 1 | Reset level |
| 2-13 | Player 1 |
| 14-25 | Player 2 |
| 26-37 | Player 3 |

Within each player, the bit order is Up, Down, Left, Right, A, B, C,
Start, X, Y, Z, and Mode. A set wire bit means pressed. The mapping sets
Gens' controller globals to active-low levels. It does not filter
opposite-direction inputs or suppress X, Y, Z, and Mode for a three-button
pad. The latter preserves the levels supplied by Gens' movie playback
branch even though a three-button controller does not expose them on its
normal protocol.

In ordinary mode players 1 and 2 go to Gens controllers 1 and 2.
Player 3 is ignored. In Team Player mode players 1, 2, and 3 go to Gens
controllers 1, 1B, and 1C. Controller 1D and the separate physical port 2
are set to neutral. The module permits 3- or 6-button types for the mapped
pads. The separate port 2's controller type is explicit in the Team Player
configuration. The fourth Team Player pad's type is also explicit, and
should default to three buttons as in the original configuration. Both pads'
levels remain neutral in this three-player mapping. A fixture that depends
on non-neutral levels for either pad is not covered.

The caller must validate project settings and treat a zero return from
`gens_input_configure` as an initialization error. Configure once, after
machine I/O initialization, and do not call it on each frame. It invokes
Gens' `Make_IO_Table` for the chosen topology. The candidate implementation
represents its mutable Team Player dispatch table with byte-sized selectors
instead of 32-bit function pointers. Reconfiguration and state restoration
must still be validated against the ported machine.

`gens_input_apply` is intended to run before each frame. Power and reset
helpers only report their levels. The future adapter must implement the
actual reset behavior, including the distinction between ROM reload and
soft reset, and decide precedence when both are held. No lag or input-poll
callbacks are implemented here.

`tests\test_gens_input.c` exercises the wire mapping against stub Gens
globals. It covers each button, player routing, neutral unused controllers,
both topologies, invalid configuration, and power/reset levels. On the
external Linux builder it is available as `compile input-check`. The test
does not validate the Gens controller handshake, timing, snapshot behavior,
or real Chimera integration.

## Controller protocol port

The port implements the original read, write, timeout, and Team Player table
operations against the original global names. Its separate x86-64 data file
preserves their original initialization and contiguous 448-byte controller
layout. This layout is relied on by the original save-state code. It does
not by itself make Gens save states or miniBox rewind work.

The original code has observable quirks that the candidate preserves.
Port 2's Team Player path dispatches through port 1's table. The special
port 1 multiplexer uses controller 1A's Start and A levels for its third
low phase, even when another pad is selected. Rebuilding the table also
leaves older entries in place when a shorter configuration replaces a
longer one. These behaviors must not be silently corrected when replay
fidelity is the goal.

On an external Linux x86-64 builder, `compile controller-check` builds the
original i386 `io.asm` and the candidate x86-64 implementation with the
same input mapper and test driver. It compares deterministic per-scenario
trace digests for pad reads, Team Player phases, port multiplexing, write
edges, byte-sized counter wrapping, and timeout boundaries. The i386
reference has wrappers for its nonstandard `ebx` clobbering convention.
Digest equality is not a full machine comparison or a proof of movie sync.
The check has not yet been run and may expose port or build errors.

The future Genesis bus must call these functions using the x86-64 ABI.
The candidate bus adapter does this for controller registers. Its separate
word-read entry points retain the original assembly's full 16-bit result
instead of assuming the declared byte return is sufficient. See
`GENESIS-BUS.md`. Neither module implements a Chimera input-poll callback,
machine reset, controller initialization policy, or integration with real
CPU timing.
