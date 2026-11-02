# 68000 fetch rebase prototype

`waterbox\gens_m68k_fetch.c` is a preliminary x86-64 building block, not
a port of the Starscream CPU. It has not been compiled or executed natively
here and is not linked to the emulator or miniBox.

The pinned generator's `gen_basefunction` masks the emulated PC to 24 bits
when finding the first matching fetch region. It keeps the original PC's
upper eight bits in the cached range and subtracts them from the fetch base.
That makes addresses with the same low 24 bits reach the same emulated
memory while retaining the complete 32-bit PC.

The prototype represents region bounds as 24-bit values and host addresses
as `uintptr_t`. Each region's backing pointer names the byte for its first
emulated address. A successful lookup returns the full-width host address,
the unsigned fetch base satisfying `fetch_base + pc == host_pc`, and the
inclusive cached range with the PC's upper bits restored. Regions are
searched in order, so the first overlapping region wins. Caller-supplied
backing storage must remain valid and use Gens' word-swapped layout.

The API uses an explicit count, with at most 64 regions, rather than the
original 32-bit sentinel table. It rejects malformed ranges, missing or
undersized backing, and host-address overflow. Failed calls leave the
result unchanged. An unmapped PC returns a distinct status. The eventual
CPU integration must translate that status into the original context
switch, invalid fetch cache, and out-of-range execution result. It must also
handle instruction fetch width, alignment exceptions, memory callbacks,
and snapshots. This helper does not implement any of those.

The separate memory-path prototype is described in `M68K-BUS.md`. Neither
prototype is connected to the generated CPU.

On the external Linux x86-64 builder, `compile fetch-check` maps synthetic
backing memory at `0x36f00000000` using `MAP_FIXED_NOREPLACE`. It checks
upper-PC aliases, first-match ordering, boundaries, and rejected inputs.
It refuses an unavailable or different mapping rather than silently testing
a lower address. This is a host-side prototype test, not a miniBox guest
run or an instruction/cycle comparison with the original CPU.
