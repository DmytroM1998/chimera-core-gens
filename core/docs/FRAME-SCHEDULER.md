# Genesis frame scheduler

`waterbox\gens_frame.c` is a preliminary extraction of the sound-enabled,
fully rendered Genesis frame path from the pinned Gens revision. It is not
connected to the original CPUs or to Chimera, and it has not been compiled
or executed natively here.

The scheduler keeps the original ordering of audio timing, controller
timeouts, DMA, 68000 and Z80 execution, horizontal interrupts, scanline
rendering, vertical blanking, vertical interrupt, and end-of-frame sound
updates. It uses Gens' `Cycles_M68K`, `Cycles_Z80`, VDP registers and other
machine globals. The first 68000 slice of a normal line ends at
`Cycles_M68K - 404`. The vertical blank slice uses `-360`, and the Z80 uses
`Cycles_Z80 - 168` before its interrupt.

`gens_frame_audio_init` reproduces Gens' scanline/sample tables and clears
the active sound buffers. It supports the original 11025, 22050, and 44100
Hz settings. The caller must initialize the audio tables after choosing
`CPU_Mode` and `Sound_Rate`, and drain and clear the mixed buffers at the
appropriate frame boundary. It must not reinitialize audio on each frame.
The scheduler always advances sound-chip timing and renders every visible
line. It does not expose Gens' fast path, which skips machine-visible work.

`gens_frame_run` rejects inconsistent line counts, sample tables, CPU cycle
settings, unsupported sample rates, reentrant entry, and invalid horizontal
interrupt values before changing the machine. The future adapter must report
a failed return rather than continuing as if a frame ran. It must set the
original lag flag before each frame and wire input polling at real controller
reads. Power, reset, audio output, and frame numbering are not implemented
by this module.

`gens_frame_run_dma` selects the shared VDP DMA timing helper explicitly.
Pass `&vdp.timing` from the machine's initialized `GensVdp`. Its register,
line, cycle, pending-count/type, and status pointers must match the globals
used by this scheduler. A null or mismatched configuration fails before
frame execution. This path calls the real countdown at the original visible,
first-vblank, and remaining-blank scanline positions, then adds its returned
cycle cost through the existing 68000 backend.

The no-argument `gens_frame_run` retains its original `Update_DMA` symbol
dispatch for reference comparisons and separately supplied backends. The
shared-DMA entry point does not fall back to it on failure. A failed
countdown stops the frame and clears `Inside_Frame`. Earlier audio, CPU,
controller, and VDP side effects remain applied. The caller must report
the failure and stop rather than treating the partial frame as complete.

`gens_frame_run_vdp` additionally selects the shared VDP IRQ adapter.
Pass the shared DMA timing, a `GensVdpIrq` using this scheduler's register
and pending-interrupt globals, and the machine's shared CPU I/O state.
The CPU request backend must be present before frame execution starts.
Mismatched DMA/IRQ pointers or a missing CPU state/backend fail before
changing the machine.

Horizontal interrupt updates remain after HBlank execution, before visible
line rendering. The first blank line's horizontal update remains before its
VBlank slice, and the vertical update remains after that slice and before
the Z80 interrupt. The IRQ adapter decides the actual eligible CPU level
using the live enable and pending bits. A request failure stops the frame
without running the later rendering, CPU, sound-finish, or Z80 interrupt work.
As with DMA failures, earlier side effects are not rolled back.

`gens_frame_run` and `gens_frame_run_dma` keep legacy `Update_IRQ_Line`
dispatch. The full shared-VDP entry point never falls back to that symbol.
It supplies VDP IRQ selection, not the real Starscream interrupt backend or
CPU acknowledge bridge.

The symbols in `gens_frame_backend.h` are intentionally unresolved until a
real backend supplies them. The 68000 and Z80 wrappers must preserve each
port's actual calling convention and original cycle behavior. The screen
wrapper must set `FakeVDPScreen` false before carrying out Gens'
`Render_MD_Screen` behavior. The candidate conversion and checked wrapper
are available in `gens_vdp_video.c`, documented in `VDP-VIDEO.md`, but are
not yet attached to a complete frame backend.
`Patch_Codes`, controller timeout handling,
interrupt updates, and sound-chip functions must be real machine
operations, not no-op success stubs. The old RAM-search and capture windows
are not part of this scheduler. Shared VDP paths supply DMA countdown and,
optionally, IRQ selection, not the remaining CPU, renderer, or sound implementations.

## Audio integration blocker

The original `Sound_Extrapol` has 312 entries. At the end of a PAL frame
`VDP_Current_Line` is 312, yet the original `PSG_Special_Update` and
`YM2612_Special_Update` access entry `VDP_Current_Line + 1`. Calls during
the final PAL scanline can also reach past the table. NTSC end-of-frame
accesses are inside the array but can read entries not initialized for NTSC.
This is undefined or stale-state behavior, not a safe interface to copy into
the guest.

The backend's PSG and YM finish wrappers are deliberately separate from the
original helpers. The audio port must address all of those accesses and
compare the result with independently measured original playback before it
can claim fidelity. Merely padding the table or choosing a fallback value is
not proof that existing TAS audio and state match.

`waterbox\gens_audio.c` supplies a candidate bounded next-sample offset.
It preserves Gens' table value inside the active frame and returns the
one-past-segment offset on the final scanline or after the frame. It rejects
invalid line counts, sample counts, cursor positions, and inconsistent
table entries.

`patches\0001-audio-cursor.patch` routes both original special-update
functions through the helper after the chip update, where the original
cursor lookup occurred. An invalid timing state is reported and aborts
instead of continuing with an invalid pointer. The patch is applied only to
disposable source copies. Its boundary policy is not yet proven equivalent
to original playback and the sound chips are not connected to the core.

`tests\test_gens_frame.c` checks scheduler ordering and audio table sizes
using stub backends. It also checks the actual DMA countdown through the
shared entry point in PAL/NTSC, visible and blanked lines, fill/copy types,
configuration mismatches, and a failure after execution has begun.
The full shared-VDP cases combine DMA with enabled/disabled horizontal and
vertical requests, verify event positions in both regions, and stop on
failed horizontal or vertical CPU requests. CPU requests use a probe.
On the external Linux builder it is available as
`compile frame-check`. It does not validate real CPU instructions, VDP
behavior, chip audio, the original undefined accesses, or replay sync.
`tests\test_gens_audio.c` exercises the candidate offset across all
supported rates, both regions, and frame edges. `compile audio-check` also
prepares a disposable patched tree and checks the actual patched special
update functions with stub chip mixers and invalid-timing controls. It does
not validate the original chip output or movie synchronization.
