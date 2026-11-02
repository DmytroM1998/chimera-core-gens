# Genesis sprite preparation

`waterbox\gens_vdp_sprites.c` ports `MAKE_SPRITE_STRUCT`,
`MAKE_SPRITE_STRUCT_PARTIAL`, their Genesis `Render_Line` dirty-flag dispatch,
and `UPDATE_MASK_SPRITE`. It prepares sprite state and the scanline-visible
list, including masking, width budgets, and overflow status. It does not
perform priority composition or pixel output, or implement the different
32X preparation path.
The separate row-composition kernels and CRAM resolution are documented
in `VDP-PIXELS.md`. `VDP-SPRITE-DRAW.md` describes the pass that connects
selection and sprite pattern fetches to those kernels.

`GensVdpSprites` references the original 2048-dword sprite backing and
`Data_Misc.Spr_End`, plus the live VDP registers, dirty flag, sprite-table
address, and memory backings. When using `GensVdp`, take the sprite-address
pointer from `vdp.registers.sprite_addr`, register pointers from its common
register array, and memory/dirty pointers from its read/write adapters.
Supply the same 32-byte VSRAM-overflow backing used by VDP reset. All
configuration and backing addresses must remain guest-owned and stable
under snapshot rules.

## Preserved behavior

With display disabled, preparation leaves the dirty flag and table untouched.
With display enabled, it clears only the dirty flag's low byte. Low flag
two-bit values 1 or 3 request a complete rebuild, value 2 requests the partial
path, and 0 skips rebuilding. Other low-byte bits do not change dispatch.
The interlace choice uses register 12 bit `0x4`, independently of the
counter-read and horizontal-width mode tests.

Both builders start at SAT entry zero, follow seven-bit links relative to
the original sprite-table base, and visit at most 80 entries. A cycle in
the list therefore terminates at the same cap rather than looping forever.
Stored sprite-table entries are eight dwords each, not host-pointer records.

Complete rebuilding updates full X/Y coordinates and maxima, only the low
bytes of X/Y sizes, and only the low word of the tile field. The remaining
bytes and the final dirt dword stay unchanged. `Spr_End` becomes the byte
offset of the final prepared entry.

The partial path updates X position, X maximum, and the tile low word.
It deliberately retains old Y geometry, stored sizes, and `Spr_End`,
including when the SAT link chain changed.

SAT fetches are not wrapped at 64 KiB. The pinned assembly places VRAM,
CRAM, the 32-byte VSRAM-overflow backing, VSRAM, and the horizontal-counter
table consecutively. A fetch beyond VRAM reads from the corresponding
adjacent emulated backing. The candidate models that sequence explicitly
without reading beyond any C array. This also covers noncanonical cached
SAT addresses near VRAM's end.

An out-of-VRAM cached SAT base fails when rebuilding is required. The
dirty-byte clear has already happened, as it precedes the source fetch in
the original dispatch. Earlier side effects are not rolled back. The
eventual renderer wrapper must report failure and stop, not continue with
an apparently successful frame.

## Scanline selection

`GensVdpSpriteLine` supplies the prepared table and `Spr_End`, the 256-dword
visible list, `Data_Misc.Borne`, horizontal cell/pixel counts, current line,
VDP status, and the original `Sprite_Over` setting. Only bit zero of that
setting enables width-budget emulation. Use the same table/end pointers as
preparation and the same line, dimensions, and status as the VDP.

`gens_vdp_sprites_select` produces sprite-table byte offsets in the visible
list and stores the list's byte length in `Borne`. It overwrites only the
used prefix. It does not clear the retained tail or an existing overflow
flag. Vertical range checks are inclusive. Coordinate and budget comparisons
follow the original signed 32-bit tests.

The first vertically eligible sprite consumes width even if horizontally
off-screen. It is not treated as an X=`-128` mask. Subsequent vertically
eligible X=`-128` sprites stop selection before consuming width. The masking
phase starts after that first eligible sprite, not the first visible one.

The first eligible sprite also bypasses the immediate width-budget check.
Later entries check the remaining signed budget after advancing. The entry
that exhausts it can still be selected. Off-screen eligible sprites consume
width. If another vertically eligible sprite remains after budget exhaustion,
the routine sets status bit `0x40` even if that later sprite is off-screen or
would otherwise be a mask. Pixel collision status is not generated here.

`Spr_End` is an inclusive byte offset. Nonaligned in-bounds values retain the
original traversal behavior, and negative signed values still inspect entry
zero. Positive end values outside the original 256-entry backing fail before
modifying selection state instead of reading beyond the C array.

`gens_vdp_sprites_prepare_line` combines preparation and selection for
state-only callers and verifies that both views share table/end storage.
Display-off skips selection and preserves the old list metadata. A complete
renderer must still place preparation, scroll rendering, selection, and sprite
pixel work at the original call sites. Neither the helper nor standalone
selection draws a scanline.

## External comparison

`compile vdp-sprites-check` compares extracted, unchanged i386 preparation
macros and Genesis dispatch against the candidate. It covers display
enable, dirty modes, interlace, high state bytes, terminating/cyclic linked
lists, the 80-entry cap, and boundary-crossing SAT sources. It hashes the
complete sprite and renderer-misc backings. A changed tile-field upper bit
is the negative control.

Native-only cases also check skipped reads, invalid storage, partial-update
retention, and failure after the dirty-byte clear. The command has not been
built or run here. Source-level interpretation is not native execution or
evidence of game/movie synchronization.

`compile vdp-sprite-line-check` compares extracted, unchanged i386 selection
macros with the candidate using both limit modes. Cases cover masking, signed
coordinates and width budgets, off-screen sprites, list lengths up to all
256 backing entries, and sticky overflow status. Native-only cases include
negative/nonaligned end values and invalid bounds. A changed visible-list
byte length is the negative control. This comparison also remains unbuilt
and unexecuted here.
