# 68000 CPU core

`waterbox\gens_m68k_cpu.c` is a C port of the main 68000 core that Gens
generates from the pinned `Starscream\Main68k\Star.c` with
`-quiet -hog -name main68k_`. It mirrors the generated assembly routine by
routine. That includes the flag representation, the memory helpers with their
direct RAM path and hooks, the exception and interrupt paths, the fetch rebase
with unused address bits, the odometer, and the hog-mode execution loop.

The public API covers what Gens uses: init, reset, `exec`, `interrupt`, the
odometer functions, `releaseCycles`, `addCycles` and the PC read. It also has
`flush_interrupts` for completeness.

`gens_m68k_exec_observed` additionally accepts optional debugger policy for
the current call. It reports instructions and individual byte/word accesses
separately from the historical raw hooks, including access sizes and live
PC/SR values. Instructions report size 0; byte/word accesses report 1/2,
including the individual words of a longword operation.
The policy pointer exists only on the active run's stack and is not part
of the CPU context or a savestate. `gens_m68k_exec` remains the unobserved
entry point used by the original-versus-port harness.

## Dispatch tables

`tools\generate_m68k_tables.py` compiles a temporary, instrumented copy of the
pinned `Star.c`. The copy records which routine every one of the 65,536
opcodes jumps to and the generator parameters that routine was emitted with.
The pinned source is not modified. The output is
`waterbox\gens_m68k_tables.c/.h`, which hold 7,244 routines and 8,119
run-length entries. The C core implements each emitter family from those
parameters, so the decode and timing tables come from the original generator.
`m68k-cpu-check` fails when the committed tables differ from the regenerated
ones.

## Verification

`tests\test_m68k_diff.c` links the generated, unchanged `main68k.asm`,
assembled as i386, and the C port into one 32-bit program.
`tests\test_m68k_diff_glue.asm` gives the reference's `M68K_RB`, `M68K_RW`,
`M68K_WB` and `M68K_WW` callbacks the register behavior of Gens' handlers,
which preserve ECX and EDX.

Each scenario creates random code with biased instruction families, random
exception vectors, RAM, I/O memory, registers, SR, supervisor and user stacks,
and pending interrupt levels. Some scenarios raise interrupts from I/O writes,
like the VDP does. Both cores then run several rounds with the same odometer
targets and with interrupts between rounds. After every round the harness
compares the following:

- all registers, SR, both stack pointers, PC, odometer, leftover cycles, the
  interrupt byte and `execinfo`
- all RAM and all handler-visible memory
- the ordered log of bus callbacks, interrupt acknowledges and the execute,
  read and write hooks with their PC, address and value

On Windows with Zig's clang and NASM 2.16.03, seven seeds of 20,000 scenarios
matched with no mismatch. Between 25 and 39 scenarios per seed were counted
separately as described below. A negative control that flips one register bit
in the port is detected.

`docker compose run --rm compile m68k-cpu-check` runs the same comparison on
Linux with `gcc -m32`. It also regenerates the tables and checks the negative
control. It has not been run on Linux yet.

## Original quirks that are reproduced

- Starscream's flag layout: N, Z and C are in AH in `LAHF` order, V is in AL,
  and X is in `__xflag`. Undefined x86 flags, such as those after shifts by the
  full operand width or more, come from the host CPU through inline assembly.
  `DAA` and `DAS` don't exist in x86-64, so they follow Intel's pseudo-code.
- `NBCD` ignores its operand and computes `0 - X`.
- `TAS` on memory does not write back and does not update An for `(An)+` and
  `-(An)`.
- Register shift counts of 32 or more shift by 31 only. Rotates rotate by 31
  and then by the count minus 31.
- `CHK` clears the flags when it does not trap.
- A `DIVU` or `DIVS` overflow sets V and clears the other flags. `MULU` and
  `MULS` timing counts bits as the generator does.
- Word and long RAM accesses are unaligned x86 loads on the word-swapped RAM
  and can read past 64 KiB. `GENS_M68K_RAM_SPARE` reserves those bytes.
- Long reads outside RAM call `M68K_RW` for the high word first, and the
  decrement variant reads the low word first. Starscream builds the result in
  ECX across the two calls.
- `writememorydecdword` leaves ECX rotated by 16 bits on its non-RAM path.
  `MOVE.L` to `-(An)` sets N and Z from that rotated value, so its flags can
  differ from the moved value when the destination is not RAM.
- The write hooks report a PC whose low byte alone was decremented by 2,
  because NASM assembles `sub [_hook_pc],byte 2` as a byte operation. For
  example, a write by the instruction ending at 0x1CA00 reports 0x1CAFE.
- Exceptions read the vector through the hooks. They push the PC and then the
  SR, and clear the trace and stopped bits.
- The interrupt checkpoint leaves the SR mask in CL and the interrupt byte in
  CH. With other ECX leftovers, these reach the unused upper bits of byte and
  word hook values. Callers must mask hook values by access size, as Gens'
  tracer does.

## Deliberate differences

- `DIVS` of 0x80000000 by -1 raises a host divide fault in the original. The
  port treats it as an overflow.
- Execution after a failed rebase reads through a null base in the original.
  The port stops and reports the out-of-bounds result instead.
- Relative branches do not rebase in Starscream, so code can run past the end
  of a fetch region into the host memory that follows. In Gens that is whatever
  the linker put after `Ram_68k` or the ROM buffer, which the port cannot
  reproduce. The fuzzer counts these scenarios as "escaped a fetch region" and
  does not compare them. Games do not do this deliberately.
- Fetch regions hold pointer-width host bases and must outlive the CPU. They are
  not part of any saved state.
- Callbacks receive an opaque pointer instead of using the i386 register ABI.
  The entry value of ECX's upper half is zero rather than the caller's leftover.
