# The whole machine

`waterbox\gens_machine.c` wires the ported parts into one Mega Drive /
Genesis and runs it the way Gens does. It is a single instance, like the
original, because the YM2612, PSG and controller code read Gens' global
variables by name.

## What it reproduces

- Startup as `WinMain` and `Load_Config` leave it, with Gens' defaults:
  6-button pads, sprite limit on, 44100 Hz sound, all chips enabled.
- `Load_Rom`, `Init_Genesis` with country detection and SRAM mapping, the
  word swap, `M68K_Reset`, `Z80_Reset`, `Reset_VDP` and the clock settings.
- `Reset_Genesis`.
- `Do_Genesis_Frame(false)` with sound enabled: the scanline loop, DMA
  countdown, horizontal and vertical interrupts, rendering, and the Z80
  running or held. `gens_frame.c` is an earlier extraction of the same
  function. The machine uses its own copy, which calls the ported modules
  directly.
- Memory as Gens lays it out. `Ram_68k`, `Rom_Data` and `SRAM` are one block
  in that order, because word accesses at the end of RAM read the following
  bytes. `Bank_Z80` is the dword after Z80 RAM.

Game Genie codes are not supported, so `Fix_Codes` and `Patch_Codes` do
nothing, as in Gens with no active code.

## The comparison

`reference\gens_headless_reference.c` is a headless Gens built from the
unchanged i386 modules: the generated Starscream core, `Mem_M68k.asm`,
`Mem_Z80.asm`, `vdp_io.asm`, `vdp_rend.asm`, `io.asm`, `z80.asm`,
`Misc.asm`, `Cpu_68k.c`, `Cpu_Z80.c`, `ym2612.c` and `psg.c`. It replaces
only the Windows front end. Its startup, `Init_Genesis`, `Reset_Genesis`
and `Do_Genesis_Frame` follow the original files line by line. It can play
a GMV with `movie.cpp`'s own playback code.

The Linux reference uses forwarding headers under `reference/winshim` for
upstream include names whose capitalization differs from the actual filenames.
This includes `cd_sys.h` and the `sh2.h` include reached through `Mem_SH2.h`. The CPU Z80
variant of `Mem_M68K.h` is in a separate `cpuz80` include directory so the shim
files can coexist on case-insensitive filesystems. Each shim includes the
original header by its exact relative path. The pinned submodule is unchanged.

Two small assembly files complete it:

- `gens_headless_stubs_i386.asm` gives the Sega CD, 32X and PWM symbols
  that the shared memory map refers to. The functions trap if called.
- `gens_headless_sound_abi_i386.asm` keeps ECX and EDX across the C sound
  functions that Gens' assembly calls. The second YM2612 write of a Z80 word
  write reuses the registers the first call left. That depends on the
  compiler of the original binary, and the port models them as preserved.

`tests\gens_headless_port.c` runs the ported machine, and
`waterbox\run-native.c` runs the core through the exports Chimera calls.
All three write the same trace (`tests\gens_headless_trace.h`): for every
frame, digests of the 68000 and Z80 registers, the VDP state and
registers, 68000 RAM, Z80 RAM, VRAM, CRAM, VSRAM, SRAM, the rendered
screen, the mixed sound, the 16-bit output samples and the YM2612 register
writes. `tools\compare_headless_traces.py` names the first differing frame
and block.

`docker compose run --rm compile machine-check` builds all three, runs
every program of `tests\headless-roms.txt` for 6,000 frames with scripted
input from `tools\headless_input.py`, imports and plays five Gens movies
made by `tools\make_test_gmv.py`, and checks a negative control that
flips one RAM bit. The programs are freely licensed and are downloaded by
the networked `prepare` step, which checks their SHA-256 sums.
Progress output identifies the ROM, comparison phase and runner before each
execution. Reported lag frames are frames where the emulated program did not
poll controller input, not a measure of computer performance. The reference
prints its count for each synthetic-input test. Both the reference and the
core print counts during generated movie tests.

## Results

On Windows with Zig and NASM, everything matched: the 240p Test Suite over
three 20,000-frame scripts, six homebrew games over 15,000 frames, 23 SGDK
samples over 6,000 frames, and imported movies with 3- and 6-button pads,
the Team Player and a 50 Hz header. Bad Apple was left out because it is
larger than Gens' 6 MiB ROM buffer.

Two real TAS movies, with their cartridges supplied by the user, were
imported through `ImportMovie` and played through `FrameAdvance` against
Gens' own movie playback code. Every block of every frame matched, and both
sides counted the same lag frames:

| Movie | Frames | Lag frames | Last frame |
|---|---|---|---|
| Sonic 3 & Knuckles, 100% | 179,002 | not counted | the final boss |
| Zero Tolerance | 71,467 | 15,025 | the Docking Bay level 1 map |

Both runners print their lag frame count on standard error.

The comparison found four differences in ported modules, all fixed:

- A VDP data write in a read access mode failed. The original ignores it.
- A sprite whose cached maximum X did not match its width was refused. The
  original draws it.
- Unresolved pixels past H32's 256 columns can hold values of 0x8000 and
  above. The original then reads past its palette. The port masks the index.
  Those pixels are outside the picture.
- The 68000 port's write hooks reported a PC whose low byte alone was
  decremented, as the original's byte-sized `sub` does.

The reference itself needed Gens' default chip enables, layer switches and
colour tables, which `WinMain` and `Load_Config` set.

## Limits

- The reference was built with Clang here and is meant to be built with
  GCC on Linux. It is not the original Windows binary. Code whose result
  depends on the compiler, such as the scratch registers above, is matched
  by construction, not by measurement.
- The test programs cover a broad but finite part of the hardware. A game
  that uses something none of them do can still differ.