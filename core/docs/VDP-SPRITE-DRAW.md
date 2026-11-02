# Genesis sprite pattern traversal

`waterbox\gens_vdp_sprite_draw.c` ports `RENDER_LINE_SPR`, connecting the
scanline-visible list to pattern fetches and the eight-pixel sprite kernels.
It produces draft palette-slot/metadata words. CRAM resolution and final
RGB conversion still occur later, as documented in `VDP-PIXELS.md` and
`VDP-VIDEO.md`.

`GensVdpSpriteDraw` references the same preparation and selection views,
intermediate pixel buffer, row origin, visibility/priority settings, and
renderer scratch fields used by the original machine. Preparation and
selection must share their table and end pointers. Interlace and
shadow/highlight arguments must match register 12's dispatch bits.

The row origin is the original `TAB336` value, without the eight-pixel
sprite margin. The pass adds that margin when calling the row kernels.
Storage and scratch fields must remain guest-owned and stable under
snapshot rules. The two clip-bound fields correspond to `Data_Spr.H_Min`
and `H_Max`. Cursor, palette, and stride correspond to `Data_Misc.X`,
`Palette`, and `Next_Cell`.

## Traversal and state effects

Display-off skips the pass without clearing its scratch state. When active,
it runs the original selection/overflow logic, then resets the cursor even
if no sprites are visible. With no visible sprites, palette, stride, and clip
bounds remain unchanged.

Pattern addresses preserve 32-bit arithmetic, normal/interlace tile sizes,
vertical flip, and the original column stride. Horizontal flip walks screen
chunks right-to-left while advancing source patterns. Normal traversal
walks left-to-right. Clipping skips complete pattern chunks at the pinned
thresholds, then draws each remaining full eight-pixel chunk. It does not
silently crop individual edge pixels.

Priority swap tests bit zero. Always-on-top tests the complete flag byte,
so value 2 still forces high priority. Per-priority visibility settings retain
their bit-zero behavior. Hidden sprite pixels do not bypass selection,
pattern fetches, scratch updates, or selection-generated overflow state.

Only the clip bound used by the current horizontal direction is replaced.
The cursor advances after each completed sprite and normally ends at the
visible list's byte length. Earlier scratch/pixel/status changes remain
applied if a later fetch or kernel call fails.

Pattern reads reuse the explicit virtual backing sequence from sprite
preparation: VRAM, CRAM, VSRAM overflow, VSRAM, and horizontal counters.
They are not wrapped at 64 KiB. Reads beyond that known sequence fail.
The adapter does not invent the contents of unrelated linker-adjacent
memory or mask high interlace tile indices into a different address.

Sprite entries are drawn as the original does, without checking that the
cached X maximum matches the sprite width. Entries selected for a line have
`x < H_Pix` and a maximum of at least 0, so every pattern lands on the line.
Each pattern position is still bounds-checked. These safety checks do not
establish fidelity for every possible corrupted or noncanonical state.

## Renderer placement and comparison

The eventual full line renderer must prepare sprite structures at the
original dispatch point, render scroll/window pixels, call this sprite pass,
update palettes, and resolve CRAM before final screen conversion. Do not
replace those stages with this pass alone.

`compile vdp-sprite-draw-check` compares extracted, unchanged i386 selection,
sprite traversal, and pixel macros against the candidate. It covers both
row origins, clipping directions, normal/interlace and shadow/highlight
variants, sprite sizes, priorities, and visibility options. Complete pixel,
selection, scratch, table, and status backings are recorded. A changed
column stride is the negative control.

Native-only cases cover empty/disabled passes, clip boundaries, any-byte
always-on-top, virtual fetch bounds, and an unsupported fetch even when
sprite display is hidden. The comparison has not been built or run here.
It is not proof of complete renderer output, gameplay, or movie sync.
