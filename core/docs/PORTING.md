# Porting the pinned Gens revision

The source is revision `b948995d15034983cbf5562610586d93e0b25a9b`.

## Status

The port is complete for Mega Drive cartridges. `waterbox\gens_machine.c`
connects every module listed below into one machine, and
`waterbox\cinterface.c` makes it a Chimera core. The machine agrees frame
by frame with a headless build of the unchanged i386 Gens modules
(`docs\MACHINE.md`). The bullets below were written as each module was
ported. Where one says a module is not yet connected, the machine now
connects it. The guest build in miniBox has not been run.

## Architecture

- `Gens-rr\src\nasmhead.inc` selects 32-bit assembly. miniBox requires a
  fixed-address x86-64 guest, so a 32-bit guest is not an alternative.
- `Gens-rr\Starscream\Main68k\Star.c` generates the main 68000 CPU
  implementation and documents its 32-bit register conventions.
- `Gens-rr\src\Star_68k.h` mixes emulated addresses with host pointers.
  `Gens-rr\src\Cpu_68k.c` casts RAM and ROM pointers to `unsigned` when
  building the fetch map. At the miniBox guest address those values cannot
  hold a host pointer. The generator, context layouts, fetch bases, data
  tables, and callbacks must be changed together.
- The Z80, memory bus, and VDP paths also use 32-bit assembly. Porting the
  CPU alone does not make the machine executable.
- `Gens-rr\src\Gens.cpp` calculates the original PAL scheduler using 312
  lines and 50 frames per second, and the NTSC scheduler using 262 lines and
  60 frames per second. Copying timing constants from another emulator is
  not a fidelity-preserving substitute.
- `Gens-rr\src\io.asm` implements Team Player. Three-player input needs the
  original routing and handshake, not a third independent console port.
  `PlaySubMovie` in `Gens-rr\src\G_main.cpp` configures the original
  three-player movie mode using controller 1B and 1C.
- `waterbox\gens_io.c` and `gens_io_data_x64.asm` port the controller
  globals and protocol, but have not been assembled, compiled, compared
  with the original, or connected to a running emulator. See `docs\INPUT.md`.
- `waterbox\gens_genesis_bus.c` connects the candidate 68000 memory path
  to cartridge and controller behavior. Complete CPU, VDP, sound, and cheat
  integration is still missing. See `docs\GENESIS-BUS.md`.
- `waterbox\gens_z80_control.c` adds candidate Genesis bus-request and reset
  handling. CPU execution, odometer, and sound-chip reset callbacks still
  need real backends. See `docs\Z80-CONTROL.md`.
- `waterbox\gens_z80_memory.c` adds the candidate 68000-facing Z80 window.
  See `docs\Z80-MEMORY.md`.
- `waterbox\gens_z80_native.c` adds candidate native memory handlers,
  including word accesses and the banked 68000 window. Its raw callback
  bridges and actual Z80 CPU remain unconnected. See `docs\Z80-NATIVE.md`.
- `waterbox\gens_sound.c` forwards typed sound-device operations and frame
  completion to the pinned YM2612 and PSG implementations. Real-chip
  comparison fixtures are prepared but unbuilt. See `docs\SOUND.md`.
- `waterbox\gens_pcm.c` ports scalar and MMX output-sample semantics,
  including mono, softening, volume, and buffer consumption. It is not
  connected to a guest frame entry point. See `docs\PCM.md`.
- `waterbox\gens_vdp_read.c` ports VDP data, status, and counter reads.
  See `docs\VDP-READS.md`.
- `waterbox\gens_vdp_write.c` ports VRAM/CRAM/VSRAM data writes, DMA fill,
  and VRAM-to-VRAM DMA copy.
  It also decodes control-port words. `waterbox\gens_vdp_registers.c` ports
  the register-derived machine state. Rendering and whole-machine reset are still
  missing. See `docs\VDP-WRITES.md`.
- `waterbox\gens_vdp_dma.c` ports the pending-DMA scanline countdown and
  cycle-steal result. The memory-transfer backend uses it, and
  `gens_frame_run_dma` binds it to the scheduler's scanline boundaries.
  See `docs\VDP-WRITES.md`.
- `waterbox\gens_vdp_memory_dma.c` implements candidate Genesis ROM/RAM
  transfers and cycle release through the shared CPU I/O state. Sega CD DMA
  sources are unsupported. It is not wired into a running machine.
- `waterbox\gens_vdp.c` wires the candidate VDP components into one
  configuration. The Genesis bus has an optional typed VDP route, and Z80
  read wrappers share its storage. The scheduler has explicit shared-DMA
  and shared-DMA/IRQ entry points, but the renderer and complete machine
  are not implemented.
- `waterbox\gens_vdp_irq.c` ports VDP IRQ priority and acknowledge state.
  Register updates and the scheduler's `gens_frame_run_vdp` entry point can
  use it. The real CPU interrupt request/acknowledge bridge remains
  unimplemented. See `docs\VDP-WRITES.md`.
- `waterbox\gens_vdp_reset.c` ports VDP reset, including the original partial
  clearing ranges and sprite-buffer side effects. It is not whole-machine
  reset. See `docs\VDP-RESET.md`.
- `waterbox\gens_vdp_sprites.c` ports full/partial sprite-table preparation
  and Genesis dirty dispatch, plus scanline selection, masking, width budgets,
  and overflow status. See `docs\VDP-SPRITES.md`.
- `waterbox\gens_vdp_palette.c` ports VDP palette refresh and backdrop
  handling. Pixel output remains unimplemented. See `docs\VDP-PALETTE.md`.
- `waterbox\gens_color_lookup.c` ports Genesis color-profile lookup tables,
  including grayscale, inversion, and pink-bank ordering. Separate 32X
  tables and machine/frontend binding remain unimplemented.
  See `docs\COLOR-LOOKUP.md`.
- `waterbox\gens_vdp_video.c` ports final Genesis lookup-based screen
  conversion and marker writes. Complete line composition and machine/video
  export binding remain unimplemented. See `docs\VDP-VIDEO.md`.
- `waterbox\gens_vdp_pixels.c` ports eight-pixel plane/sprite composition
  and conversion of palette-slot metadata to CRAM lookup indices. Full
  scroll/window traversal is not yet connected.
  See `docs\VDP-PIXELS.md`.
- `waterbox\gens_vdp_sprite_draw.c` connects selection, sprite pattern
  traversal, and pixel kernels. It preserves clipping, interlace, priorities,
  and renderer scratch updates. Full line/frame integration remains
  unfinished. See `docs\VDP-SPRITE-DRAW.md`.
- `waterbox\gens_vdp_scroll.c` ports the Scroll B, Scroll A and window line
  renderers. The `Render_Line` dispatch remains unfinished. See
  `docs\VDP-SCROLL.md`.
- `waterbox\gens_vdp_render.c` orchestrates one `Render_Line` call from the
  ported pieces. Frame integration remains unfinished. See `docs\VDP-RENDER.md`.
- `waterbox\gens_vdp_machine.c` owns the VDP state and wires every VDP module
  together. The CPU cores and the frame backend still need to attach to it.
  See `docs\VDP-MACHINE.md`.
- `waterbox\gens_z80_cpu.c` is a complete Z80 port with generated dispatch
  tables. It matches the unchanged `z80.asm` in differential fuzzing.
  See `docs\Z80-CPU.md`.
- `waterbox\gens_m68k_cpu.c` is a complete C port of the generated
  Starscream main 68000 core. Its dispatch tables come from an instrumented
  copy of the pinned generator. It matches the generated `main68k.asm` in
  differential fuzzing. See `docs\M68K-CPU.md`.
- `waterbox\gens_machine.c` is the whole machine: startup, `Init_Genesis`,
  `Reset_Genesis` and `Do_Genesis_Frame(false)` over the ported modules.
  See `docs\MACHINE.md`.
- `waterbox\cinterface.c` is the Chimera adapter and `waterbox\gens_gmv.c`
  reads Gens movies for its importer. See `docs\CHIMERA-CORE.md`.
- The selected `Do_Genesis_Frame` skips `Render_Line` when its `fast`
  argument is true. It also gates sound-chip updates on `disableSound`.
  Neither branch can be treated as a harmless presentation switch without
  comparing its machine-visible behavior with the original reference.
- `waterbox\gens_frame.c` contains a preliminary full-render, sound-enabled
  scheduler extraction. `docs\FRAME-SCHEDULER.md` describes its unresolved
  backends and the original audio table's out-of-bounds access.

## Required evidence

The original 32-bit reference and the x86-64 port must derive from the same
revision. Compare canonical CPU, scheduler, memory, controller, VDP, and
audio state rather than host-pointer-containing context blobs. Repeat the
comparison between the native x86-64 port and its miniBox guest.

`reference` now contains an unbuilt i386 68000 micro-reference for a
synthetic arithmetic, bus, and interrupt fixture. See `docs\M68K-REFERENCE.md`.
It is not the full original Gens reference.

For imported TAS input, first establish that the ROM, settings, initial
state, controller topology, frame indices, resets, and input levels match
the original movie playback. Then compare the complete supported playback
against trusted original checkpoints. A missing original fixture or a
different starting state is not evidence of synchronization.

`tools\compare_traces.py` implements a strict frame-by-frame comparison
gate. Its provisional input contract and limitations are in
`docs\TRACE-FORMAT.md`. Neither the original nor the ported trace producer
has been implemented.

The retained Sega CD and 32X source is not an implementation of those systems
in the future core. The first core is limited to Genesis cartridges.
