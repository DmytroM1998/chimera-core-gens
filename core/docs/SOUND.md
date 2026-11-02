# Pinned sound-chip adapters

`waterbox\gens_sound.c` forwards sound operations to Gens' original
`ym2612.c` and `psg.c`. It does not replace them with another emulator's
chips. The original source remains unchanged in `extern`. The existing
audio-cursor patch is applied only to disposable build copies.
Neither these chips nor the adapter have been compiled or run natively here.

`gens_sound_init` uses the pinned Genesis master clocks, divided by 7 for
YM2612 and by 15 for PSG. Supported rates are 11025, 22050, and 44100.
The YM interpolation and PSG improvement options are explicit booleans.
Initialization updates those option globals and invokes the original
initializers. It does not choose chip-enable flags, volumes, output buffers,
or machine timing. The caller must establish the same region, rate, audio
tables, and pending-buffer state used by the frame scheduler before calling
it. An initializer failure can leave earlier changes applied.

The typed callbacks can be used by the existing bus configuration:

- Route YM2612 reads and writes in `GensZ80Memory` through
  `gens_sound_z80_read` and `gens_sound_z80_write`. These also handle the
  PSG write at the Z80-facing `0x7f11`.
- Route the Genesis bus's PSG write at `0xc00011` through
  `gens_sound_genesis_write`.
- Use `gens_sound_reset_ym2612` for `GensZ80Control.reset_ym2612`.
- The adapter supplies the real `gens_frame_backend_psg_finish` and
  `gens_frame_backend_ym_finish` functions through the original special
  updates, in the scheduler's existing order.

Machine routing must still send VDP, CPU, and other device operations to
their own backends. These sound callbacks explicitly fail unsupported
operations or invalid inputs. They do not pretend to handle VDP accesses.
`YM2612_Write` returns 1 for some ignored writes and 2 for repeated writes.
Those are original chip outcomes, not adapter errors. They must not stop
emulation.

The original chips still require the machine's audio globals, including
`Seg_L`, `Seg_R`, `Seg_Junk`, `Sound_Extrapol`, pending sample lengths,
scanline timing, `disableSound2`, and chip-enable flags. Their optional
`GYM_Dumping` hook must be backed by real export tooling if enabled.
The adapter supplies no silent export stub. It also supplies no raw-register
backend for `gens_z80_native.c`. Calling C chip functions is not enough to
reproduce the original compiler's `ECX` and `EDX` scratch-register effects.
See `Z80-NATIVE.md`.

The original YM full-save API embeds architecture-dependent layouts and
pointer-relative values. It is not a portable Chimera savestate format.
Guest snapshot integration and stable guest-owned chip state remain required.
`gens_sound_output` forwards completed mixer planes to the PCM converter
described in `PCM.md`. Guest audio exports, delivery to Chimera, and a
complete machine initialization path are still unfinished.

## External comparison

`compile sound-chip-check` builds an original i386 chip fixture and a
candidate x86-64 fixture with the adapter and patched chip sources.
Unlike the isolated audio-cursor check, this runs the real PSG, FM,
DAC, and timer code. The fixture compares 24 deterministic digests covering
PAL and NTSC, all three sample rates, and both improvement options.
It exercises a mid-prefix YM reset and hashes GYM-hook events, mixed samples,
chip registers, and selected
canonical chip state without comparing host pointer values or struct
padding. It also checks rate-pointer targets, not pointer addresses.
The export hook records test events rather than writing a GYM file.
A changed output sample serves as a negative control.

The fixture intentionally stops before the last scanline and only uses
in-bounds original audio cursors. It does not establish equivalence for
the candidate patch's unresolved PAL frame-end policy. Both builds use
explicit SSE floating-point settings, disabled contraction, wrapping signed
arithmetic, GNU inline semantics, and disabled strict aliasing. This is a
controlled Linux cross-architecture comparison, not proof of matching the
original Windows compiler, floating-point environment, or release binary.
The unchanged upstream arithmetic has additional portability questions.

The command has not been run. Real-chip sample agreement, a complete
frame, game loading, rewind, and imported movie synchronization remain
unverified.
