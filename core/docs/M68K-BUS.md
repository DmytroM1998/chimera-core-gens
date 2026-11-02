# 68000 memory-path prototype

`waterbox\gens_m68k_bus.c` is a preliminary x86-64 memory-path building
block. It does not execute instructions, implement the Genesis bus, or
replace the original 68000 generator. It is not connected to the core and
has not been compiled or executed natively here.

The implementation follows the pinned main 68000 generator's memory helpers:

- It masks the emulated address to 24 bits. Addresses at or above `0xe00000`
  use the 64 KiB mirrored RAM and Gens' word-swapped byte layout.
- Non-RAM byte and word accesses go to explicit backend callbacks. A long
  access makes two word calls. Normal accesses use the high word first.
  Predecrement long accesses use the low word at address `+2` first.
- Once a long access has selected the non-RAM path, both words stay on that
  path, even if the second address crosses `0xe00000`. The backend must
  implement the original bus's own address masking and device behavior.
- Hook metadata for a write is set before backend calls. Backend activity can
  change that metadata before the outer write hook runs, as in the original.
  Read metadata is set after the backend calls. Byte and word write hooks
  retain the full original input value even though the device receives only
  its low byte or word.
- I/O cycle and fetch state is saved before non-RAM calls and restored
  afterward. Host fetch addresses use `uintptr_t`. Hook calls restore the
  modeled live registers after the callback. The hook PC is calculated
  from the live fetch PC minus its base and two, then masked to 24 bits
  when the hook runs, as in the original hook path.

The caller supplies the real RAM storage, I/O callbacks, shared hook state,
and live CPU state. Missing required callbacks and backend failures return
explicit errors. A failed second half of a long access can follow a
successful first half. The caller must stop and report the failure rather
than treating it as an atomic operation. Hook callbacks must report or
terminate on their own errors.

Guest backing and machine-state pointers must refer to guest-owned memory.
Do not persist host application pointers in miniBox snapshots. Any future
host callback policy must follow miniBox's invisible-memory and callback
rules.

The original direct RAM helpers can access beyond their 64 KiB backing at
the end of the array for some word and long addresses. This prototype
returns a boundary error instead of reproducing an out-of-bounds access.
That is an intentional safety difference requiring comparison with the
original reference. Alignment and address-error exceptions, cached CPU
flags, actual Gens device callbacks, and reentrant Lua behavior are not
implemented here.

`M68K-BRIDGE.md` describes an experimental x86-64 assembly call boundary for
this interface. It does not integrate the path into the generated CPU.
`GENESIS-BUS.md` describes the partial Genesis cartridge and controller
backend. Its remaining device callbacks are not implemented.

On the external Linux x86-64 builder, `compile bus-check` maps synthetic RAM
at `0x36f00000000` using `MAP_FIXED_NOREPLACE` and exercises widths,
mirrors, callback and hook ordering, mutable I/O state, and failure paths.
It refuses an unavailable or different mapping. It is not a miniBox guest
run or evidence of game or movie synchronization.
