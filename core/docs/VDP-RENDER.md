# VDP line orchestration

`waterbox\gens_vdp_render.c` ports the control flow of the pinned `Render_Line`
routine. It does not contain new pixel logic. It wires the earlier pieces in
the original order.

1. Look up the row origin in the `TAB336` equivalent row table.
2. When the VDP display bit (`VDP_Reg[1] & 0x40`) is off, fill 320 pixels
   starting at `row + 8` with `0x4040` in shadow mode or zero otherwise.
3. Otherwise prepare the sprite structures through the dirty-flag dispatch,
   then draw Scroll B, Scroll A with the window, and the sprites. The interlace,
   shadow and cell-scroll modes come from registers 12 and 11.
4. Update the palette banks when CRAM is dirty.
5. Resolve the row through CRAM. It starts at `row + 8` and covers
   `(160 - H_Pix_Begin) * 2` pixels.

The extra margin pixels outside that resolved range keep their intermediate
values, as in the original.

## Validation

The wrapper requires every draw structure to share the same pixel buffer,
sprite memory and current line. It rejects lines outside the row table. Any
failure in a component fails the whole call and keeps earlier partial effects.

## Checks

`docker compose run --rm compile vdp-render-check` extracts the unchanged
`Render_Line` routine with all renderer macros. It compares 1536 random
scenarios against the C wrapper. The digest covers the row pixels, both
palette banks, sprite scratch state and `Data_Misc`.
It has not been built or run. On the Windows host only an interpreted C model
ran. It compared the wrapper with a manual call sequence of the verified
components on 24 random lines. That is not native proof.
