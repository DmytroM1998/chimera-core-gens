# Renderer palette updates

`waterbox\gens_vdp_palette.c` ports the renderer's `UPDATE_PALETTE32` and
nested `UPDATE_PALETTE` behavior, including the dirty-bit gate at their call
sites. It updates the 256-entry 32-bit and 16-bit VDP palettes. It does not
generate color lookup tables, compose pixels, or provide a video export.

`GensVdpPalette` references the actual CRAM backing, 32768-entry `Palette`
and `Palette32` lookup tables, output palettes, background register, pixel
format setting, palette-lock/pink flags, and CRAM dirty flag. When using
`GensVdp`, use its CRAM/dirty pointers and register slot 7, and use the same
output palettes supplied to VDP reset. Storage must be distinct as in the
original machine, guest-owned, and stable under snapshot rules.

The lookup tables must already contain the original color-profile results.
The pinned `Recalculate_Palettes` applies frontend color/brightness settings
and builds the tables separately. That generator is not ported by this
module. Its Genesis-table candidate is available in `gens_color_lookup.c`,
documented in `COLOR-LOOKUP.md`. No substitute profile or silent default
table is supplied.

## Preserved behavior

Only dirty bit zero requests an update. Palette-lock bit zero prevents it
without clearing the dirty byte or changing any output colors. On an
unlocked update, only the dirty flag's low byte is cleared. Upper bytes are
preserved. The renderer can update palettes with the VDP display disabled,
so this helper has no display-enable gate.

CRAM words are read little-endian and masked with `0x0eee`. Each of the 64
colors populates four banks: normal, shadow, highlight, and a duplicate
normal bank. Shadow indexes the lookup table with the masked color shifted
right once. Highlight adds `0x777` to that shifted index.

Both shadow/highlight macro variants update all four banks in the pinned
source. Their conditional assembly guards are commented out. Do not retain
old shadow/highlight entries merely because register 12's mode bit is clear.

The low six bits of the background register choose the color copied into
entry zero of each bank. With pink-backdrop bit zero set, all four bank-zero
entries use the original constants: `0x00ff00ff` for 32-bit output, and
`0x7c1f` or `0xf81f` for 555/565 output. The 32-bit pink constant does not
have the normal lookup table's opaque alpha byte. This helper preserves it.

`mode555` selects the pink constant. It does not regenerate or reinterpret
the supplied 16-bit lookup table. The machine/frontend adapter must call the
lookup generator when the original would recalculate its color profile.

## External comparison

`compile vdp-palette-check` compares extracted, unchanged i386 macros
against the candidate with synthetic lookup tables. Cases cover every
background index, both macro variants, 555/565 pink constants, dirty/lock
bits, upper flag bytes, and arbitrary CRAM bits. The lookup values include
nonzero upper color bytes. A changed shadow color is the negative control.

The comparison has not been built or executed here. Source-level
interpretation is not native execution or proof of displayed video or
movie synchronization. The updater is not yet attached to a complete
scanline renderer.
