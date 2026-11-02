# Original 68000 micro-reference

The reference under `reference` is a narrow CPU experiment, not a complete
headless Gens emulator. It has not been built or executed on this machine.
Its purpose is to establish a concrete i386 baseline before adapting the
original 68000 generator for miniBox's high-address x86-64 guest.

On the external Linux x86-64 builder, after source preparation, run:

```sh
docker compose run --rm compile m68k-check
```

The driver creates a new directory under `../build`.
It verifies the complete pinned source copy, compiles the original
`Starscream/Main68k/Star.c` as i386, generates NASM with the standalone
project's `-hog -name main68k_` options, and assembles it as ELF32. The
reference executable uses the original generated CPU and context layout.
It runs twice in separate processes, checks that the traces are identical,
and validates the trace before publishing `trace.jsonl` in that directory.

The original generator uses a customized memory path. It reads mirrored RAM
directly and calls `M68K_RB`, `M68K_RW`, `M68K_WB`, and `M68K_WW` for
other addresses. The reference supplies a deliberately small synthetic ROM,
RAM, and I/O device. Unknown bus accesses fail rather than returning dummy
data. The i386 bridge preserves the registers needed across those calls.
Its synthetic `Int_Ack` records the acknowledged level and clears the line.
This is not an implementation of the Genesis VDP interrupt controller.

The generated program exercises reset vectors, arithmetic, direct RAM,
byte and word I/O callbacks, STOP, an autovectored level-4 interrupt, and
RTE. The instruction fixture and callbacks are intentionally limited.
The inactive trace hooks record their arguments without running Gens Lua.
Each trace step records registers, flags, cycle counters, interrupt state,
callback history, I/O bytes, and all 64 KiB of RAM in logical byte order.
It omits host pointers from the CPU context.

`tools/check_m68k_trace.py` checks the trace shape, monotonic execution,
fixture results, full RAM image, interrupt acknowledgement, and callback
presence. When an independently produced x86-64 trace exists, compare it
with:

```sh
python3 tools/compare_m68k_traces.py original.jsonl ported.jsonl
```

The comparator validates both traces and reports the first differing
register, RAM byte, callback, or other field. Trace files alone cannot prove
that two independent CPU implementations produced them. Record the
producer and build provenance separately.

The separate high-address fetch prototype is described in `M68K-FETCH.md`.
It does not yet execute the reference fixture.

This small fixture alone cannot establish
instruction-set coverage, correct bus timing, video or audio behavior, or
GMV synchronization. A full original-machine reference is still required.

Starscream's original documentation states its attribution and distribution
terms. These local generated files are not a release. Resolve the specific
component licensing questions in `LICENSING.md` before redistributing
affected source modifications or resulting artifacts.

Starscream 680x0 emulation library by Neill Corlett
(corlett@elwha.nrrc.ncsu.edu)
