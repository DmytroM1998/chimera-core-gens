# Eight-pixel composition and CRAM resolution

`waterbox\gens_vdp_pixels.c` ports the pinned `PUTLINE`/`PUTPIXEL` primitives
for scroll B, scroll A/window, and sprites, plus the final Genesis CRAM
resolution loop. It handles eight-pixel pattern chunks in normal/flipped,
low/high-priority, and normal/shadow-highlight modes. It does not yet
traverse scroll tables or render a complete line. Sprite pattern fetching
is provided separately in `VDP-SPRITE-DRAW.md`.

## Row inputs

`GensVdpPixelRow` supplies a guest-owned intermediate buffer, element count,
resolved destination offset, pattern dword, palette offset, layer, priority,
shadow/highlight, flip, enable flags, and VDP status. The destination offset
already includes the caller's row origin and sprite margin. The primitive
validates its eight-word span, not visible-screen clipping.

Priority, shadow, and flip are explicit zero/one choices. High sprite
priority maps to the original macro's `PRIO_B` argument. Enable fields
retain bit-zero semantics, including the original per-layer/per-priority
visibility options. They are not safe host-side render-skip hints because
their effects include buffer clearing and collision state.

The usual palette offsets are 0, 16, 32, and 48. Arithmetic preserves the
original byte/word/32-bit operations for other raw offsets as well. Pattern
nibbles follow the original word-swapped VRAM order, not a naïve sequential
high-to-low nibble order. An invalid descriptor fails before changing pixels.

## Preserved composition effects

Scroll B initializes the entire chunk before checking its enable flags,
using the original normal or shadow pattern. Scroll A preserves existing
pixels when disabled. Enabled high-priority A in shadow/highlight mode
clears shadow bits across the whole chunk even when the pattern is transparent.

Low-priority A respects existing plane priority. High-priority plane pixels
write the complete intermediate word. Low-priority paths preserve the high
metadata byte and use byte-sized palette additions, without carrying into it.

Low-priority sprites are blocked by plane priority or existing sprites.
High-priority sprites ignore plane priority but are blocked by existing
sprites. A blocked low-priority sprite still marks sprite occupancy.
Overlaps accumulate the original sticky status bit `0x20`, even when plane
priority prevents visible sprite output.

Shadow/highlight sprite colors preserve the original operator behavior.
They OR the corresponding flags into the destination instead of becoming
ordinary opaque pixels, and do not automatically mark sprite occupancy.
Normal sprite shading retains the source macro's different low/high-priority
flag rules and byte-sized additions.

## Two intermediate formats

Composition initially produces palette-slot/metadata words, not values that
can be passed directly to the final RGB converter.

`GensVdpPixelResolve` supplies the same buffer, CRAM, background register,
and `H_Pix_Begin`. `gens_vdp_pixel_resolve` converts
`2 * (160 - H_Pix_Begin)` words, matching the original final scanline loop.
The normal start values 0 and 32 process 320 and 256 words. Values outside
the original safe positive loop-count range fail rather than underflowing
the assembly counter or overrunning storage.

Each word selects its low-six-bit CRAM slot, substituting the background
slot for zero. The result is masked with `0x0eee`, then receives the original
shadow/highlight/foreground bits shifted into lookup-bank positions.
The resulting words are the input format consumed by `gens_vdp_video_convert`.
Resolution does not clear unrelated words or modify CRAM or VDP status.

The required pipeline is pattern composition, CRAM resolution, and then
final lookup-based screen conversion. The full renderer still needs to
place these operations at the original call sites and propagate failures.

## External comparison

`compile vdp-pixels-check` compares extracted, unchanged i386 macros for
all 24 row variants against the candidate, then compares the unchanged
Genesis CRAM-resolution loop. Inputs cover raw metadata, transparent and
operator colors, enable flags, palette carries, background indices, and
safe span lengths. Both complete synthetic buffers and collision status
are recorded. A changed backdrop metadata bit is the negative control.

Native-only cases also check invalid descriptors, priority/collision rules,
and the composition-to-resolution transition. The command has not been
built or executed here. Source-level interpretation is not native execution
or evidence of complete rendering, gameplay, or movie synchronization.
