# Cartridge normalization

`waterbox\gens_rom.c` is a preliminary memory-buffer normalizer for the
selected Gens revision. It is not connected to a mounted slot, emulator,
or package, and has not been compiled or executed natively here.

The caller supplies bytes already read into a buffer, its capacity, and the
logical cartridge name. The API does no file I/O and does not look for
implicit patches or save files. It accepts a maximum input size of 6 MiB,
matching the original `Rom_Data` buffer. A successful result reports the
decoded size and whether the file was interleaved.

The raw/interleaved decision follows `Detect_Format` in the selected
`Gens-rr\src\Rom.cpp`. A `SEGA` header at offset `0x100`, compared without
ASCII case, takes precedence over interleaving markers. Otherwise the `EA`
marker at `0x280` or the `AA BB 06` marker in the copier header selects SMD.
SMD data is decoded as 16 KiB blocks after the 512-byte header. Within each
block the first half supplies odd bytes and the second half supplies even
bytes. Like the original in-place operation, the normalizer leaves bytes
after the decoded size untouched.

`gens_rom_swap_words` is separate. Original Gens reads cartridge header
metadata before `Init_Genesis` byte-swaps each complete word for the 68000.
The helper swaps each adjacent pair and leaves an odd final byte untouched,
as the original `Byte_Swap` does. It does not fill metadata or initialize
the machine. The caller must zero the complete emulated ROM buffer before
loading the input, as original `Load_Rom` does. It must not clear the
post-decode tail afterward without establishing that this preserves the
original machine's behavior.

This initial API intentionally rejects input shorter than the 512-byte
Genesis header and SMD inputs with an incomplete block. It rejects
recognized Sega CD and 32X headers, `.cue`, `.iso`, `.raw`, and `.32x`
names, and compressed `.zip`, `.rar`, and `.7z` names. This is stricter
than the standalone loader for some malformed or differently named files.
The slot integration must also refuse other unsupported content. These
checks do not prove that an arbitrary remaining file is a valid cartridge.

Failures return a status identifying the error category and leave the input
and result outputs unchanged. The future Chimera adapter must convert the status into
a translated load error. No standalone English message is an ABI promise.
`gens_rom_setup` computes the initial country modes and SRAM mapping from
the normalized, unswapped cartridge header. Country preference is an
explicit permutation of USA, Japan, and Europe. The original default order
is USA, Japan, Europe. The caller can instead force Japan NTSC, USA NTSC,
Europe PAL, or Japan PAL. SRAM defaults to `0x200000-0x20ffff` unless the
header supplies a valid `RA` declaration. Gens aligns and caps the mapped
range and enables writes at startup only for ROMs no larger than 2 MiB.
The helper returns these values without changing emulated state or loading
save data.

The original configuration loader repairs invalid country orders. This
helper rejects them instead so the caller can report bad project settings.
For automatic detection it rejects non-ASCII country bytes whose original
behavior could depend on signed characters and locale. A forced country
does not inspect those bytes. The helper must be called after any explicit
cartridge patching and before word swapping, as original `Fill_Infos` and
`Init_Genesis` do.

SRAM persistence, IPS handling, the remaining ROM metadata, and machine
initialization still need porting.

`tests\test_gens_rom.c` uses synthetic raw and SMD data, including
multi-block deinterleaving, word swapping, country selection, SRAM ranges,
size boundaries, and unsupported media. `compile rom-check` runs it on the
external Linux builder. The test does not compare execution with the
original standalone emulator.
