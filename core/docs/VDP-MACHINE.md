# VDP machine state and wiring

`waterbox\gens_vdp_machine.c` owns all VDP-side state in one `GensVdpMachine`
block and wires the separately ported components around it. These are the
register, read, write, DMA, IRQ, sprite, scroll, palette, render and video
modules. It adds no new emulation logic.

## Ownership

The machine owns VRAM, CRAM, VSRAM, the horizontal counter table, the register
slots, the sprite scratch arrays, the intermediate and converted screens, the
palette banks and lookup tables, and the renderer layer switches. It also
owns the shared status, current line, DMA and dirty-flag integers.

The host supplies the objects that belong to the rest of the machine:

- 68000 cycle counters and the visible line count.
- ROM, work RAM and their sizes.
- The odometer reader used by the H and V counters.
- The CPU interrupt level array and the interrupt request callback.

## Aliasing choices

The original stores some byte-written values in dword globals. The register
module writes them through byte pointers, and the renderer reads them as
dwords. The machine keeps the original widths and gives each module a
pointer to the same object. This applies to `V_Scroll_MMask`, the VRAM and
CRAM dirty flags and `H_Pix_Begin`.

## Lifetime

Every component stores pointers into the machine block. Never copy or move
it after `gens_vdp_machine_init`. A failed init clears the block and returns 0.
A successful init leaves registers at zero, so the VDP reset port must run
before rendering. Rendering a line with an unset horizontal cell count fails.

`gens_vdp_machine_set_colors` fills the two lookup tables from a color profile
and marks CRAM dirty. The host still has to set the `mode555` and `pink`
fields to match its profile.

## Checks

`docker compose run --rm compile vdp-machine-check` builds the machine with all
VDP modules natively and runs a hand-made scene. It has not been run.
On the Windows host an interpreted C model passed all assertions, except the
color profile step, which uses floating point the model cannot run.
