# Linux build environment

The `chimera-core-gens` repository root contains `Dockerfile`, `compose.yaml`
and `chimera-build/driver.sh`. The core source is under `core/`. They prepare
an Ubuntu 24.04 x86-64 environment that builds miniBox and this core's
`gens.chimeraCore` package, and runs every
comparison check. They have not been run yet: no Linux machine or
virtualization was available while the port was written. The checks were run
on Windows instead, through a session-only runner that maps the same
`driver.sh` functions to Zig and NASM.

On a Linux x86-64 machine with Docker and Compose, or macOS with Colima,
use this workspace layout:

```text
workspace/
  chimera-common-minibox/
  chimera-core-gens/
    Dockerfile
    compose.yaml
    chimera-build/driver.sh
    core/
      extern/gens-rerecording/
```

All source mounts are read-only. The miniBox repository must be clean.
The Gens submodule must be initialized, clean and at the pinned revision.
Preparation downloads miniBox's submodules at the commits recorded by miniBox.
Neither the Chimera application checkout nor a separate Gens checkout is
required. Apple Silicon uses `linux/amd64` emulation and builds more slowly.

From the `chimera-core-gens` repository root, initialize the submodule, set
the output owner and initialize the work volume:

```sh
git submodule update --init
export CHIMERA_UID="$(id -u)" CHIMERA_GID="$(id -g)"
docker compose build
docker compose run --rm init
docker compose run --rm prepare
```

Only the `prepare` service defines the image build. `init` and `compile`
reuse the same image, avoiding simultaneous exports to the same image tag
when running `docker compose build`.

Preparation snapshots the current miniBox commit, copies the
core source, verifies its Gens submodule checkout,
and downloads a GCC source archive checked against a pinned SHA-512 checksum.
It also downloads the freely licensed test programs of
`tests\headless-roms.txt` and checks their SHA-256 sums. They are kept in
the work volume, not in this repository.
Test ROM downloads report the source URL and retry transient HTTP errors and
connection failures up to four times. Every ROM and archive must still match
its pinned checksum. Persistent failures stop preparation.
The preparation service needs internet access. It refuses to overwrite an
existing or partially prepared source snapshot. Start with a separate fresh
Compose project or volume if a different snapshot is needed.
After migrating from the old workspace layout, use a fresh project name,
for example `docker compose -p gens-core-v2`, for every command in the sequence.
Rebuild the container image to pick up the new driver.

The compilation service has no network access:

The `machine-check` also runs the native tooling and memory-callback
witnesses before the movie comparisons. They require no external game for
callback coverage. With a native Meson build, run
`meson test -C build/meson-native gens-tooling gens-memory-callbacks`.
The separate miniBox/Lua witness and its required Chimera revision are
described in `CHIMERA-CORE.md`.

```sh
docker compose run --rm compile minibox
docker compose run --rm compile input-check
docker compose run --rm compile rom-check
docker compose run --rm compile frame-check
docker compose run --rm compile audio-check
docker compose run --rm compile m68k-check
docker compose run --rm compile fetch-check
docker compose run --rm compile bus-check
docker compose run --rm compile bridge-check
docker compose run --rm compile controller-check
docker compose run --rm compile genesis-bus-check
docker compose run --rm compile genesis-bus-reference-check
docker compose run --rm compile z80-control-check
docker compose run --rm compile z80-memory-check
docker compose run --rm compile z80-native-check
docker compose run --rm compile sound-chip-check
docker compose run --rm compile pcm-check
docker compose run --rm compile vdp-read-check
docker compose run --rm compile vdp-write-check
docker compose run --rm compile vdp-dma-check
docker compose run --rm compile vdp-memory-dma-check
docker compose run --rm compile vdp-integration-check
docker compose run --rm compile vdp-irq-check
docker compose run --rm compile vdp-reset-check
docker compose run --rm compile vdp-sprites-check
docker compose run --rm compile vdp-sprite-line-check
docker compose run --rm compile vdp-palette-check
docker compose run --rm compile color-lookup-check
docker compose run --rm compile vdp-video-check
docker compose run --rm compile vdp-pixels-check
docker compose run --rm compile vdp-sprite-draw-check
docker compose run --rm compile vdp-scroll-check
docker compose run --rm compile vdp-render-check
docker compose run --rm compile vdp-machine-check
docker compose run --rm compile z80-cpu-check
docker compose run --rm compile m68k-cpu-check
docker compose run --rm compile machine-check
docker compose run --rm compile vdp-register-check
docker compose run --rm compile core
docker compose run --rm compile all
```

The first command builds the miniBox C++ guest kit.
The driver limits Ninja and musl's nested make to one job to reduce resource
pressure under emulation. It adjusts only the generated `build.ninja` in the
work volume, leaving the miniBox source untouched. An unexpected musl build
command stops the build rather than silently leaving parallelism unlimited.
The Chimera application and Qt are not built or downloaded.
`input-check` builds and runs the input
mapping unit test against stub Gens symbols. It does not execute the
original controller assembly or the emulator. `rom-check` builds and runs
the cartridge normalization unit test using synthetic input. `frame-check`
tests the scheduler against stub CPU, VDP, and audio callbacks, including the
shared-DMA entry point with the real countdown helper, the shared-DMA/IRQ
entry point with the real VDP IRQ decision logic, and synthetic CPU/frame
backends. `audio-check` tests the candidate bounded audio cursor using
synthetic timing tables and the patched original special-update functions
using stub chip mixers.
`m68k-check` builds and executes a small synthetic reference using the
original i386 Starscream generator. It writes its local output under the
artifacts directory. See `M68K-REFERENCE.md`.
`fetch-check` tests the preliminary x86-64 fetch resolver at the miniBox
guest's conventional high address. See `M68K-FETCH.md`.
`bus-check` exercises the separate 68000 memory-path prototype at the same
address. See `M68K-BUS.md`.
`bridge-check` exercises the experimental x86-64 register and stack
boundary for that memory path. It requires NASM and uses a fixed high
mapping, not a miniBox guest. See `M68K-BRIDGE.md`.
`controller-check` compares trace digests from the original i386 controller
assembly and the x86-64 controller port. See `INPUT.md`. It has not been run.
`genesis-bus-check` exercises the candidate cartridge and controller bus
adapter with fake device callbacks. See `GENESIS-BUS.md`.
`genesis-bus-reference-check` compares selected original i386 Genesis bus
dispatch paths with the x86-64 adapter. It has not been run. See
`GENESIS-BUS.md` for its exclusions.
`z80-control-check` exercises the candidate Z80 arbitration and reset
paths with synthetic CPU and sound callbacks. See `Z80-CONTROL.md`.
`z80-memory-check` exercises the 68000-facing Z80 memory window, not the
Z80 CPU's native word and banked 68000 handlers. See `Z80-MEMORY.md`.
`z80-native-check` compares the candidate native memory handlers against
the original i386 handlers with synthetic raw-register callbacks. It has
not been run. See `Z80-NATIVE.md`.
`sound-chip-check` compares the real pinned PSG/YM chip code in original
i386 and candidate x86-64 prefix fixtures. It does not cover the original
out-of-bounds frame-end cursor. See `SOUND.md`.
`pcm-check` compares scalar and MMX sample conversion with the original
i386 routines, including mixer-buffer consumption. See `PCM.md`.
`vdp-read-check` compares data, status, and counter reads with extracted
original i386 VDP routines using a synthetic odometer. See `VDP-READS.md`.
`vdp-write-check` runs focused synthetic cases for the candidate VDP data
port, DMA fill, and VRAM copy paths. It also compares canonical VRAM-copy
and fill-arming digests against extracted original i386 control/write handlers.
The original fill loop is not executed by this comparison. It has not been
run. See `VDP-WRITES.md`.
`vdp-dma-check` compares the pending-DMA countdown and returned CPU-cycle
cost with extracted original i386 `Update_DMA`, using synthetic line state.
It has not been run. See `VDP-WRITES.md`.
`vdp-memory-dma-check` compares Genesis ROM/RAM transfers to VRAM, CRAM,
and VSRAM with extracted original i386 handlers, timing, and a synthetic
cycle-release stub. It has not been run. See `VDP-WRITES.md`.
`vdp-integration-check` runs the shared VDP configuration through the
candidate Genesis 68000 bus and checks Z80 read wrappers and DMA ticks.
It uses synthetic IRQ, odometer, and cheat callbacks and has not been run.
See `VDP-WRITES.md`.
`vdp-register-check` runs register/control integration cases and compares
canonical register and derived-state digests against extracted original
i386 handlers, using a synthetic IRQ callback. It has not been run.
See `VDP-WRITES.md`.
`vdp-irq-check` compares IRQ priority, pending-bit updates, CPU-byte clearing,
and raw acknowledge results with extracted original i386 handlers. CPU
requests use a probe, not Starscream execution. It has not been run.
See `VDP-WRITES.md`.
`vdp-reset-check` compares VDP reset clearing ranges, register/IRQ order,
palette locking, and canonical state against extracted original i386
routines. It uses a CPU request probe and has not been run.
See `VDP-RESET.md`.
`vdp-sprites-check` compares full/partial sprite-table preparation and dirty
dispatch with extracted original i386 renderer macros. It does not render
pixels and has not been run. See `VDP-SPRITES.md`.
`vdp-sprite-line-check` compares scanline sprite masking, width budgets, and
visible-list/status changes against the original i386 selection macro.
It does not render pixels and has not been run. See `VDP-SPRITES.md`.
`vdp-palette-check` compares palette banks, backdrop overrides, and
dirty/lock handling against extracted original i386 macros with synthetic
color lookup tables. It has not been run. See `VDP-PALETTE.md`.
`color-lookup-check` compares full Genesis tables against the original-source
generator compiled for i386 and x86-64. Twelve cast expressions are normalized
for C++ syntax without changing the arithmetic. The comparison uses explicit
SSE double settings and has not been run. See `COLOR-LOOKUP.md`.
`vdp-video-check` compares Genesis final screen conversion against the
unchanged extracted C++ function on i386 and x86-64. It covers lookup
indices, markers, retained margins, and full output buffers. It has not
been run. See `VDP-VIDEO.md`.
`vdp-pixels-check` compares eight-pixel plane/sprite composition and the
CRAM-resolution loop with unchanged i386 renderer macros/code. It covers
priority, shading, collision flags, and lookup-index preparation. It has
not been run. See `VDP-PIXELS.md`.
`vdp-sprite-draw-check` compares the complete sprite selection/traversal
pass and row kernels with unchanged original i386 macros, including
clipping, interlace, and scratch-state updates. It has not been run.
See `VDP-SPRITE-DRAW.md`.
`vdp-scroll-check` compares the Scroll B and Scroll A/window line renderers with
the unchanged i386 macros across eight mode variants each. It has not been run.
See `VDP-SCROLL.md`.
`vdp-render-check` compares the complete `Render_Line` routine, including sprite
preparation, palette update and CRAM resolution, with the wrapper. It has not
been run. See `VDP-RENDER.md`.
`vdp-machine-check` builds the shared VDP state block with every VDP module and
runs a hand-made scene through render and screen conversion. It has not been
run. See `VDP-MACHINE.md`.
`z80-cpu-check` fuzzes the complete Z80 port against the unchanged `z80.asm`
in one 32-bit program and requires zero mismatches. The same harness ran
clean on Windows over 60,000 scenarios. See
`Z80-CPU.md`.
`m68k-cpu-check` regenerates the 68000 dispatch tables from the pinned
generator and fuzzes the C port against the generated `main68k.asm` in one
32-bit program. It requires zero mismatches and checks a negative control.
The same harness ran clean on Windows over 140,000 scenarios. See
`M68K-CPU.md`.
`machine-check` compares the whole ported machine with a headless build of
the unchanged i386 Gens modules, frame by frame, on every test program, and
plays five imported Gens movies through the core's exports against Gens' own
playback code. It checks a negative control. See `MACHINE.md`.
`core` builds miniBox, then runs `core/container-build.sh`. It makes the patched Gens source, builds
`core.wbx` with miniBox's guest toolchain, checks it with miniBox's
`check-wbx.sh` and writes `gens.chimeraCore` to the artifacts directory,
`chimera-core-gens\build\artifacts`. `all` runs every check, then builds
miniBox and the core. Copy the package into an existing Chimera installation's
cores folder to use it. Building the application is not necessary.
Supported GCC source versions are pinned in the driver. If the Ubuntu
package version changes to an unlisted release, preparation stops instead
of downloading an unchecked archive.

The work volume stores disposable source copies, build trees, downloads, and
source manifests. Output goes to the repository's `build/artifacts`.
The build does not download or bundle ffmpeg. The
configuration does not use a Docker socket, privileged mode, a display, or
device access.

To prepare a patched source copy without compiling, run from
`chimera-core-gens/core`:

```sh
python3 tools/prepare_patched_source.py --reference extern/gens-rerecording
```

The command verifies all copied files against the pinned Git tree, then
applies `patches/series` to the new `build/gens-source` directory. It
refuses an existing destination and never edits the pristine source copy.
Use `--destination` for another new directory inside `build`. A failed
copy or patch can leave an incomplete destination, which must be inspected
before removal.

The generated miniBox toolchain and core package still
need compilation and runtime validation on that Linux machine. These scripts
are not evidence of a working emulator or movie synchronization.
