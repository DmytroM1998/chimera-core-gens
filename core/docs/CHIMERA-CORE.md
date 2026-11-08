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
  PC. The original names and indices are preserved. `M68K D0` to `M68K D7`,
  `M68K A0` to `M68K A7`, `M68K PC` and `M68K SR` are aliases for scripts
  using BizHawk's names.
- Save data: `SRAM.srm` as Gens' `Save_SRAM` writes it, nothing when every
  byte is zero, otherwise the bytes up to the last non-zero one rounded up
  to a power of two. SRAM always starts empty.
- `IsButtonActive` hides player 3 unless the Team Player is on.

## Lua memory access

Select `memory.usememorydomain("M68K BUS")` for logical 24-bit 68000
addresses. The bus exports `GetBusCount/Name/Size/Writable`, `PeekBus`,
`PokeBus` and `ReadBus`. Unlike the raw domains, bytes are in console order:
`memory.read_u16_be(0xFF0010)` and `memory.read_u32_be(0xFF0010)` read
big-endian words and longs without manually undoing word swaps.

The debugger bus follows the live cartridge bank mapping, mapped SRAM and
the 68000 RAM mirrors. Z80 RAM at `0xA00000` to `0xA03FFF` is in native Z80
byte order, with its 8 KiB mirror. Debugger pokes edit RAM and mapped SRAM
directly, bypassing CPU ownership and write protection. Cartridge ROM is
read-only. MMIO and unmapped addresses read zero and ignore writes. The bus
does not execute device reads or writes, advance timers, change controller
poll/lag state or run CPU hooks. Inspect chip state through its existing
domains rather than reading a device port.

`ReadBus` copies up to 64 KiB per call into invisible scratch memory, so a
tool's reads are not part of savestates. Existing raw domains are unchanged.
Scripts should explicitly select `M68K BUS` and check the selection succeeds.

Chimera `06066f3` adds `event.onloadstate` and keeps Lua `userdata` with
newly created branches, restoring it before the branch-load event. These
are frontend features, not core exports. Greenzone seeks, rewind and raw
`memorysavestate` loads do not restore that metadata or call the event.

The BizHawk Batman and Robin script uses the supported execution hooks,
register aliases and big-endian bus reads. Select the bus with
`assert(memory.usememorydomain("M68K BUS"))`, and create branches while the
script is active if its userdata should be restored with them. Its
execution handlers do not need read/write value replacement. Equivalent
game observations still require the same ROM revision and input and a
comparison against the other emulator; the synthetic witness does not
establish game-specific equivalence. Pixel fonts and overlay layout belong
to the frontend and may differ.

The native `gens-tooling` test (also run by `machine-check`) checks these
exports and register aliases, including a full bus scan that leaves the
CPU, VDP, RAM and input-poll state unchanged.

## Lua memory callbacks

Chimera commit `52dea77` or newer can use the four exports
`GetMemoryCallbackScopes`, `SetMemoryCallback`, `AddMemoryWatch` and
`RemoveMemoryWatch`. The one scope is `M68K BUS` (index 0, also the default).
It supports all three kinds: instruction execution (0), reads (1) and writes
(2). These report actual 68000 activity during `FrameAdvance`, including
direct RAM accesses and exception handling. They do not report Z80
accesses, DMA or debugger reads/pokes.

Use `event.on_bus_exec`, `event.on_bus_exec_any`, `event.on_bus_read` and
`event.on_bus_write`. Chimera's legacy `event.onmemoryexecute`,
`onmemoryexecuteany`, `onmemoryread` and `onmemorywrite` aliases work too.
Each kind accepts 64 distinct exact addresses plus an independent wildcard
(`UINT64_MAX` at the core ABI). Invalid kinds, scopes, non-24-bit addresses
and full exact-address storage return 0 from `AddMemoryWatch`, so Chimera
raises a registration error. Duplicate additions are idempotent.

An instruction callback runs before the instruction with value and size 0. Read and
write callbacks report a byte or word in console order, with no register
residue in unused bits, and size 1 or 2 respectively. With Chimera
`06066f3` or newer, Lua receives access flags: `0x4000` for execution,
`0x11000`/`0x21000` for byte/word reads and `0x12000`/`0x22000` for
byte/word writes. Longword operations report two word accesses, high
word first normally and low word first for predecrement operations.
An exact watch matches the starting address of the reported byte or word,
not any byte that overlaps it. A write is visible in memory when its
callback runs. CPU reads of MMIO report the actual device result, while
debugger reads remain side-effect-free and return zero at MMIO addresses.

`GetMemoryCallbackReplaceable` returns 0. Callbacks are observational:
`event.can_replace_value("read")` and `"write"` return false, and a numeric
return cannot replace a CPU access. Writes remain post-commit notifications.

Inside a callback, `emu.getregister` reads live D/A registers and the current
instruction's PC and SR/CCR, including the `M68K ...` aliases. The callback
may inspect memory through `PeekBus`/`ReadBus` without recursive notification.
It must not wait for a frame or recursively run the machine. Register
mutation is not provided. The original Gens trace hooks keep their existing
raw values and quirks separately.

The callback pointer, watches and live debugger view are invisible memory.
They survive state loads, power and reset independently of emulated state.
The frame passes observer policy to the interpreter, so no observer pointer
is restored from a savestate. Only active access kinds look up watches.
Removing a wildcard does not remove exact watches, or vice versa.

Native coverage is `gens-memory-callbacks` in Meson and `machine-check`.
It verifies values, sizes, ordering, live registers, registration lifecycle and
unchanged machine traces with observational callbacks enabled.

For the sandbox witness, generate a self-authored ROM:

```sh
python3 tools/make_callback_rom.py callbacks.bin
```

Create a new project with that ROM and the rebuilt Gens package in a
Chimera `06066f3` or newer, then run `tests/memory-callbacks.lua` with no
other scripts active and enough movie frames to advance. It checks the
public Lua APIs and flags, frame-event ordering, live register and memory
reads, watch persistence across core-state and branch round trips,
branch userdata restoration and unregistration. It creates and leaves a
branch named `Gens callback witness` in this disposable project. Callback
failures are collected and checked by the main script, so caught callback
errors cannot produce its final success message. Native tests do not
substitute for this miniBox integration check.

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