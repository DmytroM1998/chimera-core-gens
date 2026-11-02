# The Chimera core

`waterbox\cinterface.c` is the guest ABI over the machine. The package is
declared in `waterbox\waterbox.config`, `file_slots.json`,
`default_keybinds.json`, `package-licenses.json` and `languages\en.json`.
The package is in English only. Chimera shows a key's English text in every
language.

## Exports

- `Init` reads the cartridge from the `rom` slot and the settings, then runs
  `Init_Genesis`. A refusal is a key of the text table with the file name.
- `FrameAdvance(packed)` applies the input levels and runs one
  `Do_Genesis_Frame`. Power runs `Init_Genesis` again and Reset runs
  `Reset_Genesis` before the frame, while held, as Chimera's Genesis Plus GX
  package does.
- `GetVideoBgra`, `GetVideoWidth`, `GetVideoHeight`: Gens' 32-bit screen,
  256 or 320 wide and 224 or 240 high.
- `GetAudio`, `GetAudioSampleCount`: Gens' output step at its defaults,
  master volume 128, stereo, MMX conversion, 44100 Hz. It clears the mixing
  buffers as the original does.
- `GetVsyncNumerator`/`Denominator`: 60/1, or 50/1 for a PAL console. Gens
  runs one emulated frame per tick of that rate.
- `InputWasRead` is Gens' `Lag_Frame`. `SetInputPollCallback` is called at
  every controller read.
- `SetRenderingEnabled(0)` skips only the copy of the picture. Rendering
  sets VDP status bits, so it always runs.
- Memory domains: 68K RAM and the cartridge are word-swapped, as Gens keeps
  them. Then Z80 RAM, SRAM, VRAM, CRAM and VSRAM.
- Registers: D0 to D7, A0 to A7, PC, SR, the other stack pointer and the Z80
  PC.
- Save data: `SRAM.srm` as Gens' `Save_SRAM` writes it, nothing when every
  byte is zero, otherwise the bytes up to the last non-zero one rounded up
  to a power of two. SRAM always starts empty.
- `IsButtonActive` hides player 3 unless the Team Player is on.

## Input

38 buttons, which the 64-bit packed channel carries: Power, Reset, then 12
for each player in the order Up, Down, Left, Right, A, B, C, Start, X, Y, Z,
Mode. Players 1 and 2 are the two ports. With the Team Player, players 1, 2
and 3 are its pads 1, 1B and 1C, and port 2 stays neutral.

## Settings

All of them are part of the machine, so a movie needs the same values:
region (automatic, or one of Gens' four), the pad types of the two ports and
of Team Player pads 1B and 1C, the Team Player, YM2612 Improvement and the
sprite limit. The sound rate is fixed at Gens' default 44100 Hz, because
the YM2612 timers advance per sample.

## Importing Gens movies

Tools > Core-Specific Tools > Gens-Rerecording > Import Gens Movie (GMV)...
makes a Chimera project from a Gens movie. It is the core's own tool, declared
under `"tools"` in `waterbox.config`: Chimera calls `RunTool` instead of
`Init`, with no game. The tool reads the movie and the cartridge the person
picks (mounted as `movie` and `cartridge`: Chimera builds before
ToolAssisted-run/chimera d43bb214 mount an empty `rom` in a tool run). The
output is declared a project, so the core writes its two parts, `project.json`
and `project.input` (one entry a line), and Chimera makes them into the
project's file where the person chose.

`RunTool` reads the header as `movie.cpp` does and refuses movies older than
version 9, movies that start from a savestate, unknown pad types and empty
movies. The project holds the pad settings, the Team Player for a
three-player movie with pads 1B and 1C set to three buttons as Gens does, the
cartridge by its file name and SHA-1, the movie's rerecord count, and the
input, one entry a frame with its columns named:

    |..|UDLRABCSXYZM|UDLRABCSXYZM|

with a third player group for Team Player movies. The project names this
package by the hash and version Chimera gives the tool. When it is opened,
Chimera finds the cartridge beside it or asks for it. The movie's 50 Hz flag
is reported in the tool's answer, because in Gens the region comes from the
cartridge and the settings, not from the movie.

A movie made with Gens' Clear SRAM option off and an SRAM file present
started with that file's contents. The core always starts with empty SRAM.

## Not done

- The guest build has not been run (`docs\BUILDING.md`).
- Gens-specific Lua is not provided. Chimera's own Lua API works through the
  exports above.
- Game Genie codes, Sega CD and 32X are not supported.