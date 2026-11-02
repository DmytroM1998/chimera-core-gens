# Chimera Gens core

A Chimera core that runs Mega Drive / Genesis cartridges on a C port of the
Gens-Rerecording emulator. It is meant for tool-assisted speedruns: the
machine behaves like the pinned standalone Gens, and existing Gens movies
(`.gmv`) can be imported.

The core is built into a `gens.chimeraCore` package by the Linux container
(`docs\BUILDING.md`). That container build has not been run yet, because no
Linux machine was available while the port was written. Everything that runs
on Windows was run there with a portable Zig and NASM toolchain. See "What
has been verified" below.

## The source

`extern/gens-rerecording` is a Git submodule of
`https://github.com/TASEmulators/gens-rerecording`. It is pinned to commit
[`b948995d15034983cbf5562610586d93e0b25a9b`](https://github.com/TASEmulators/gens-rerecording/commit/b948995d15034983cbf5562610586d93e0b25a9b).
This is the one revision that was taken for porting. It is treated as
the r341 release that GMV movies were made with. The link between the old
Google Code SVN revisions and the Git commits was lost when the project
moved, so this cannot be checked from the history.
It includes the original Sega CD, 32X, Lua, and standalone frontend sources.
The core runs Mega Drive cartridges only.

Clone with `git clone --recurse-submodules`, or run
`git submodule update --init` in an existing clone. To check the checked-out
submodule against a Git repository containing that revision, run from this
directory:

```powershell
python tools\verify_source.py --reference extern\gens-rerecording
```

Do not edit the submodule in place. The one downstream change, the audio
cursor patch in `patches`, is applied to disposable copies.

## How it is built

- `waterbox\gens_m68k_cpu.c` is a C translation of the 68000 core that
  Gens' Starscream generator emits (`docs\M68K-CPU.md`).
- `waterbox\gens_z80_cpu.c` is a C translation of Gens' `z80.asm`
  (`docs\Z80-CPU.md`).
- The VDP, memory map, controllers, DMA and interrupts are ported module by
  module from Gens' assembly. Each module has its own document in `docs`.
- The YM2612 and PSG are Gens' own C files.
- `waterbox\gens_machine.c` wires them into one machine and runs Gens'
  `Init_Genesis`, `Reset_Genesis` and `Do_Genesis_Frame` (`docs\MACHINE.md`).
- `waterbox\cinterface.c` is the Chimera adapter: settings, input, picture,
  sound, memory domains, save data and the GMV import tool
  (`docs\CHIMERA-CORE.md`).

## What has been verified

- The 68000 and Z80 ports agree with the unchanged original assembly in
  differential fuzzing, linked into one 32-bit program.
- Every ported module agrees with the original in its own comparison.
- The whole machine agrees with a headless build of the unchanged i386
  Gens modules, frame by frame: CPU registers, all memories, VDP state, the
  picture and the sound. This held for 30 freely licensed programs, among
  them the 240p Test Suite, six homebrew games and 23 SGDK samples with FM
  music, raster effects and DMA, over 6,000 to 20,000 frames each with
  scripted input.
- Gens movies imported through the core's own GMV import and played
  through `FrameAdvance` agree with Gens' own movie playback code, for 3-
  and 6-button pads and for the Team Player.
- Two real TAS movies played to their last frame identically on the core
  and on Gens: a 179,002-frame Sonic 3 & Knuckles run, which reaches the
  final boss, and a 71,467-frame Zero Tolerance run with 15,025 lag frames
  on both sides. Their cartridges are not part of this repository.

`docker compose run --rm compile machine-check` repeats the whole-machine
and movie comparisons on Linux.

Not verified: the guest build in miniBox, and savestates and rewind in
Chimera.

## Licensing

Gens has been identified by the project requester as GPLv2. The 68000 core
is translated from Starscream's output, and Starscream's documentation
allows only unmodified redistribution and forbids charging for any program
that includes it. Do not publish the package until that is resolved. See
`docs\LICENSING.md`.

## Other documents

`docs\PORTING.md` lists every ported part. `docs\BUILDING.md` describes the
container and its checks. Older building blocks that the machine no longer
uses are kept with their documents: the frame scheduler extraction
(`docs\FRAME-SCHEDULER.md`), the 68000 micro-reference
(`docs\M68K-REFERENCE.md`), the fetch and memory-path prototypes
(`docs\M68K-FETCH.md`, `docs\M68K-BUS.md`) and the register bridge
(`docs\M68K-BRIDGE.md`).