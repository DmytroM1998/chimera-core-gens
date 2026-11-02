# Genesis color-profile lookup generation

`waterbox\gens_color_lookup.c` ports the Genesis-table effects of the pinned
`Recalculate_Palettes`. It fills all 32768 entries of the 16-bit and 32-bit
lookup tables consumed by palette refresh and eventual video conversion.
It does not generate the separate 32X lookup/adjusted-CRAM tables or draw
pixels.

`GensColorProfile` makes the original settings explicit: channel maxima,
brightness, contrast, 555/565 mode, grayscale, inversion, and pink background.
There are no implicit defaults. The original configuration loader uses
channel maxima 255 and brightness/contrast 100 by default. Storage and
settings must remain guest-owned and stable under snapshot rules.

Supply the generated arrays as `GensVdpPalette.lookup16` and `lookup32`.
Generation does not refresh the VDP's 256-color palettes, modify the CRAM
dirty flag, or silently change the original update schedule. The candidate
fixture also exercises generated tables through the palette refresher.

## Preserved arithmetic and ordering

The base index is `(blue << 8) | (green << 4) | red`, with four bits per
channel. Channel scaling keeps the original double division by 224 followed
by multiplication and conversion toward zero. It is not replaced with
division by 255 or an integer approximation.

Brightness uses signed integer arithmetic and division toward zero before
addition. Each channel is clamped, multiplied by contrast, divided by 100,
and clamped again. RGB output uses the original six-bit levels. At the
default profile, a maximum Genesis color becomes `0xfffcfcfc`, not
`0xffffffff`.

Grayscale converts each output format independently. The 16-bit calculation
first unpacks its already-quantized channels, rather than deriving them from
the 32-bit grayscale result. The weighted terms are individually truncated.
Inversion applies before the shadow/highlight lookup banks are built and
preserves 32-bit alpha. It still flips bit 15 in 555 output, as in the
original word-wide XOR.

The upper `0x4000` bank is copied before pink replaces the lower half.
Consequently, pink overrides entries `0x0000` through `0x3fff`, while
entries `0x4000` through `0x7fff` retain the pre-pink profile. The pink
32-bit value is the original `0x00ff00ff`.

Brightness and contrast values that would overflow the original signed
integer expressions are rejected before either table changes. Channel
maxima may use the complete signed 32-bit range. No unsafe profile is
silently clamped to a default. Floating-point compilation must preserve
double evaluation and must not enable fast-math reassociation or contraction.

## External comparison

`compile color-lookup-check` compares complete Genesis lookup tables from
an isolated original-source function compiled for i386 and x86-64 with the
candidate x86-64 implementation. The fixture retains the full original
function and its private 32X arrays, but compares only the Genesis outputs.
The only source adaptation normalizes 12 MSVC-style `unsigned int (...)`
casts to portable C++ cast syntax. The copied upstream tree is untouched.

The builder selects SSE double arithmetic and disables contraction. These
are source-level comparison settings, not a reconstruction of an original
Windows release binary's floating-point environment. The 320 profile
combinations include channel extremes, brightness/contrast, both output
formats, grayscale, inversion, and pink. A changed upper-bank entry is the
negative control.

This command has not been built or executed here. Source-level interpretation
is not native execution or evidence of complete video output or movie sync.
The lookup generator is not yet attached to a complete machine/frontend.
