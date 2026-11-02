# VDP reset candidate

`waterbox\gens_vdp_reset.c` ports the pinned `Reset_VDP` using the shared
`GensVdp` configuration and IRQ adapter. It resets VDP/display state, not the
68000, Z80, sound chips, cartridge mapper, controller ports, or whole machine.
It has not been compiled or executed natively here.

`GensVdpReset` supplies guest-owned screen and palette buffers, the original
sprite backings, VSRAM overflow backing, palette-lock byte, and DMA temporary
global. Sizes count elements of the declared pointer type. Supply the actual
256-entry sprite and visible backings, even though reset touches fewer entries.
The IRQ object must be the one used by the shared register setter and must
have a CPU request backend. Configuration and backing addresses must remain
stable under the snapshot rules.

Validation checks all required backings before modifying them. A register/IRQ
failure after reset starts stops the operation without rolling back earlier
clears or register writes. The caller must report failure and stop.

## Preserved upstream effects

The order follows the original assembly:

- Clear both 336-by-240 screen buffers and all 64 KiB VRAM.
- Clear only the first 160 CRAM bytes and first 80 VSRAM bytes.
- Clear the 256-entry 16-bit and 32-bit palettes only when palette-lock bit
  zero is clear. Other lock bits do not prevent clearing.
- Clear the first 800 sprite dwords and first 100 visible-sprite dwords.
- Set registers 0 through 22 to zero in ascending order, including their
  derived-state and IRQ effects. Register 23 is not written.
- Clear visible-sprite dwords 124, 125, and 126.
- Replace VDP status with `0x0200`, clear pending interrupts, DMA scheduling,
  the DMA temporary, and all seven control dwords, and set both dirty flags.
- Rebuild the horizontal-counter table and clear only the final dword of the
  eight-dword VSRAM overflow backing.

The extra visible-sprite writes are not corrected into DMA register writes.
The original retains `EBX = Sprite_Visible + 400` before stores at offsets
`24 * 4`, `25 * 4`, and `26 * 4`. All three destinations remain inside the
original 256-dword visible backing. Consequently, the DMA-length and
DMA-source dwords retain upper bytes not modified by register setters.
Register 23 also retains its old value while `Ctrl.DMA_Mode` is cleared.
These effects are intentional fidelity choices, not suggested hardware
behavior.

VDP line counts, the current line, and CPU cycle targets are not reset here.
Region/status initialization and the rest of machine reset belong to the
eventual machine adapter.

## External comparison

`compile vdp-reset-check` compares extracted, unchanged i386 reset, register
setters, and IRQ routines against the candidate. It hashes complete screen,
palette, VRAM, CRAM/VSRAM, sprite, counter, register, control, and derived
state with multiple nonzero seeds and palette-lock values. Pointer fields
are normalized to VRAM offsets. The CPU request probe preserves the scratch
registers preserved by the pinned Starscream interrupt entry point.
It is a probe, not a running CPU.

Native-only cases cover invalid backing sizes, retained tails, upper register
bytes, palette locking, and failure during a register-triggered IRQ. A changed
visible-sprite slot is the negative control. The comparison has not been
built or run and does not establish gameplay or movie synchronization.
