# VDP scroll and window line renderers

`waterbox\gens_vdp_scroll.c` ports `RENDER_LINE_SCROLL_B` and
`RENDER_LINE_SCROLL_A_WIN` from the pinned `vdp_rend.asm`. Each call draws one
scanline of its plane into the intermediate `Screen_16X` buffer using the
shared pixel kernel from `VDP-PIXELS.md`.

## Scroll B behavior preserved

- The horizontal scroll entry is read at `hscroll + ((line & mask) << 2) + 2`.
  Its low three bits give the fine offset. The remaining bits select the
  first cell.
- The initial vertical scroll reads a dword at `VSRam + 2`. Interlace halves it.
- Cell-scroll mode refreshes the vertical offset from the second cell onward.
  It is applied only while `(Cell & 0xff81) == 0`.
- Palette bits are taken before the priority swap flips the `0x8000` bit.
- Only the low byte of the cell counter is decremented, and the loop ends when
  bit 7 of that byte is set.

## Scroll A and window behavior preserved

- Window and Scroll A split a line by `VDP_Reg[18]` bit 7, `Win_Y_Pos`,
  `VDP_Reg[17]` bit 7 and `Win_X_Pos`. When the line is fully inside the window,
  only the window is drawn and `Start_W`, `Length_W`, `Start_A` and `Length_A`
  stay untouched.
- Scroll A reads its horizontal entry at offset zero of the scroll table
  entry. It draws `Length_A + 2` cells. The last cell is clipped with the
  `Mask_N` or `Mask_F` table selected by the fine offset and the flip bit.
- The first vertical scroll value comes from `VSRam[0]` when the starting cell
  is negative. Otherwise it comes from `VSRam[(Cell & V_Scroll_MMask) * 2]`.
- The Scroll A priority swap applies only to plane cells. Window cells use the
  stored priority bit unchanged.
- Window cells are drawn eight pixels to the right of the Scroll A origin and
  use `Line & 7` for the pattern row.
- The `Data_Misc` scratch slots are updated like the original. The
  `Pattern_Adr` slot stores a VRAM offset instead of a 32-bit address.

## Explicit failures

The original reads neighbouring BSS when a fetch leaves the VRAM, CRAM, VSRam
region. The port fails the call instead of normalising the address. Examples
are an interlace tile index of `0x400` or higher and a plane base near the end
of VRAM with a large plane. Partial effects already applied stay in place.
Whether real ROMs depend on those reads is not proven.

Invalid parameters are also rejected. These include `H_Cell > 40`, a window X
position beyond `H_Cell`, a zero `H_Cell` for Scroll A, a `V_Scroll_MMask`
other than 0 or 0x7e, and pointers outside VRAM. The original would
misbehave or loop for most of them.

## Not covered

The `RENDER_LINE` dispatch is described in `VDP-RENDER.md`.

## Checks

`docker compose run --rm compile vdp-scroll-check` compares both renderers with
the unchanged i386 macros through a shared fixture. It prints one digest line
per plane. It has not been built or run. Only an interpreted C AST model and
Python extraction tests were run on the Windows host, and they are not native
proof.
