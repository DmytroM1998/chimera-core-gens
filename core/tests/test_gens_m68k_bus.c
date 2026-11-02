#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "gens_m68k_bus.h"

#ifndef MAP_FIXED_NOREPLACE
#error MAP_FIXED_NOREPLACE is required for the fixed-address test
#endif

enum { EVENT_READ_BYTE = 1, EVENT_READ_WORD, EVENT_WRITE_BYTE, EVENT_WRITE_WORD, EVENT_HOOK };

struct Event {
    int kind, hook_kind;
    uint32_t address, value, pc;
};

struct Probe {
    struct Event events[8];
    int event_count, call_count, fail_call, mutate_io, mutate_trace, mutate_live;
    uint16_t words[2];
    GensM68kLive *live;
};

static void record(struct Probe *probe, int kind, uint32_t address, uint32_t value, uint32_t pc)
{
    assert(probe->event_count < 8);
    probe->events[probe->event_count].kind = kind;
    probe->events[probe->event_count].address = address;
    probe->events[probe->event_count].value = value;
    probe->events[probe->event_count].pc = pc;
    ++probe->event_count;
}

static int after_bus(struct Probe *probe, GensM68kBusState *state)
{
    ++probe->call_count;
    if (probe->mutate_io)
    {
        state->io_fetch_pc += (uintptr_t)UINT64_C(0x100000000);
        state->io_fetch_base += 0x20u;
        state->io_cycle_counter += 9u;
        state->access_address = 0xfe123456u;
    }
    if (probe->mutate_trace)
    {
        state->hook_pc = 0xde345678u;
        state->hook_address = 0x55u;
        state->hook_value = 0xaabbccddu;
    }
    return probe->call_count != probe->fail_call;
}

static int read_byte(void *opaque, GensM68kBusState *state, uint32_t address, uint8_t *value)
{
    struct Probe *probe = (struct Probe *)opaque;
    record(probe, EVENT_READ_BYTE, address, 0xa5u, 0);
    *value = 0xa5u;
    return after_bus(probe, state);
}

static int read_word(void *opaque, GensM68kBusState *state, uint32_t address, uint16_t *value)
{
    struct Probe *probe = (struct Probe *)opaque;
    unsigned int index = (unsigned int)probe->call_count;
    assert(index < 2);
    *value = probe->words[index];
    record(probe, EVENT_READ_WORD, address, *value, 0);
    return after_bus(probe, state);
}

static int write_byte(void *opaque, GensM68kBusState *state, uint32_t address, uint8_t value)
{
    struct Probe *probe = (struct Probe *)opaque;
    record(probe, EVENT_WRITE_BYTE, address, value, 0);
    return after_bus(probe, state);
}

static int write_word(void *opaque, GensM68kBusState *state, uint32_t address, uint16_t value)
{
    struct Probe *probe = (struct Probe *)opaque;
    record(probe, EVENT_WRITE_WORD, address, value, 0);
    return after_bus(probe, state);
}

static void hook(void *opaque, GensM68kBusState *state, enum GensM68kBusHook kind)
{
    struct Probe *probe = (struct Probe *)opaque;
    record(probe, EVENT_HOOK, state->hook_address, state->hook_value, state->hook_pc);
    probe->events[probe->event_count - 1].hook_kind = kind;
    assert(kind >= GENS_M68K_READ_BYTE && kind <= GENS_M68K_WRITE_LONG);
    if (probe->mutate_live)
    {
        probe->live->fetch_pc = 0;
        probe->live->fetch_base = 0;
        probe->live->cycle_counter = 0;
        probe->live->address = 0;
    }
}

static void reset_probe(struct Probe *probe, GensM68kBusState *state, GensM68kLive *live)
{
    memset(probe, 0, sizeof(*probe));
    memset(state, 0, sizeof(*state));
    live->fetch_base = (uintptr_t)UINT64_C(0x36f00002000);
    live->fetch_pc = live->fetch_base + (uintptr_t)UINT32_C(0xab001236);
    live->cycle_counter = 5000;
    live->address = 0;
    probe->live = live;
}

int main(void)
{
    const uintptr_t guest_base = (uintptr_t)UINT64_C(0x36f00000000);
    uint8_t *ram;
    GensM68kBus bus;
    GensM68kBusState state;
    GensM68kLive live;
    struct Probe probe;
    uint32_t value;
    unsigned int i;

    ram = mmap((void *)guest_base, 65536, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (ram == MAP_FAILED)
    {
        perror("fixed-address bus test mmap");
        return 1;
    }
    if ((uintptr_t)ram != guest_base)
    {
        fprintf(stderr, "fixed-address bus test mapped an unexpected address\n");
        if (munmap(ram, 65536) != 0)
            perror("fixed-address bus test munmap");
        return 1;
    }
    memset(&bus, 0, sizeof(bus));
    bus.ram = ram;
    bus.ram_size = 65536;
    bus.opaque = &probe;
    bus.state = &state;
    bus.read_byte = read_byte;
    bus.read_word = read_word;
    bus.write_byte = write_byte;
    bus.write_word = write_word;
    bus.hook = hook;

    reset_probe(&probe, &state, &live);
    live.address = 0xafe00010u;
    probe.mutate_live = 1;
    state.io_fetch_base = (uintptr_t)UINT64_C(0x36f00004000);
    state.io_fetch_pc = (uintptr_t)UINT64_C(0x36f00005000);
    state.io_cycle_counter = 77;
    assert(gens_m68k_write(&bus, &live, GENS_M68K_BYTE, GENS_M68K_NORMAL, 0x123456abu)
           == GENS_M68K_BUS_OK);
    assert(ram[0x11] == 0xab && probe.event_count == 1);
    assert(probe.events[0].kind == EVENT_HOOK && probe.events[0].address == 0xe00010u);
    assert(probe.events[0].hook_kind == GENS_M68K_WRITE_BYTE);
    assert(probe.events[0].value == 0x123456abu && probe.events[0].pc == 0x001234u);
    assert(live.address == 0xafe00010u && live.cycle_counter == 5000);
    assert(live.fetch_base == (uintptr_t)UINT64_C(0x36f00002000));
    assert(state.io_fetch_base == (uintptr_t)UINT64_C(0x36f00004000));
    assert(state.io_fetch_pc == (uintptr_t)UINT64_C(0x36f00005000));
    assert(state.io_cycle_counter == 77 && state.access_address == 0xafe00010u);
    value = 0;
    assert(gens_m68k_read(&bus, &live, GENS_M68K_BYTE, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_OK);
    assert(value == 0xabu && probe.events[1].value == 0xabu &&
           probe.events[1].hook_kind == GENS_M68K_READ_BYTE);

    reset_probe(&probe, &state, &live);
    live.address = 0x00ff0020u;
    assert(gens_m68k_write(&bus, &live, GENS_M68K_WORD, GENS_M68K_PREDECREMENT, 0x89ab1234u)
           == GENS_M68K_BUS_OK);
    assert(ram[0x20] == 0x34 && ram[0x21] == 0x12);
    assert(probe.events[0].value == 0x89ab1234u && probe.events[0].hook_kind == GENS_M68K_WRITE_WORD);
    assert(gens_m68k_read(&bus, &live, GENS_M68K_WORD, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_OK && value == 0x1234u);
    assert(probe.events[1].hook_kind == GENS_M68K_READ_WORD);
    live.address = 0x00ff0030u;
    assert(gens_m68k_write(&bus, &live, GENS_M68K_LONG, GENS_M68K_NORMAL, 0x11223344u)
           == GENS_M68K_BUS_OK);
    assert(ram[0x30] == 0x22 && ram[0x31] == 0x11 && ram[0x32] == 0x44 && ram[0x33] == 0x33);
    assert(gens_m68k_read(&bus, &live, GENS_M68K_LONG, GENS_M68K_PREDECREMENT, &value)
           == GENS_M68K_BUS_OK && value == 0x11223344u);
    assert(probe.events[2].hook_kind == GENS_M68K_WRITE_LONG &&
           probe.events[3].hook_kind == GENS_M68K_READ_LONG);

    for (i = 0; i < 256; ++i)
    {
        reset_probe(&probe, &state, &live);
        live.address = 0x00e00020u;
        assert(gens_m68k_write(&bus, &live, GENS_M68K_BYTE, GENS_M68K_NORMAL,
                                0x11220000u | i) == GENS_M68K_BUS_OK);
        assert((unsigned int)ram[0x21] == i && probe.events[0].value == (0x11220000u | i));
        live.address = 0x00ff0020u;
        assert(gens_m68k_read(&bus, &live, GENS_M68K_BYTE, GENS_M68K_NORMAL, &value)
               == GENS_M68K_BUS_OK);
        assert(value == i && probe.events[1].address == 0xff0020u);
    }

    reset_probe(&probe, &state, &live);
    live.address = 0xabc12345u;
    assert(gens_m68k_read(&bus, &live, GENS_M68K_BYTE, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_OK && value == 0xa5u);
    assert(probe.events[0].kind == EVENT_READ_BYTE && probe.events[0].address == 0xc12345u);
    assert(probe.events[1].kind == EVENT_HOOK && probe.events[1].value == 0xa5u);
    reset_probe(&probe, &state, &live);
    live.address = 0xabc12345u;
    assert(gens_m68k_write(&bus, &live, GENS_M68K_BYTE, GENS_M68K_NORMAL, 0x123456bcu)
           == GENS_M68K_BUS_OK);
    assert(probe.events[0].kind == EVENT_WRITE_BYTE && probe.events[0].value == 0xbcu);
    assert(probe.events[1].kind == EVENT_HOOK && probe.events[1].value == 0x123456bcu);
    reset_probe(&probe, &state, &live);
    live.address = 0x00c00000u;
    probe.words[0] = 0x1122;
    assert(gens_m68k_read(&bus, &live, GENS_M68K_WORD, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_OK && value == 0x1122u);
    assert(probe.events[0].address == 0xc00000u && probe.events[1].kind == EVENT_HOOK);

    reset_probe(&probe, &state, &live);
    live.address = 0xabc00000u;
    probe.words[0] = 0x1122; probe.words[1] = 0x3344;
    probe.mutate_io = 1; probe.mutate_live = 1;
    assert(gens_m68k_read(&bus, &live, GENS_M68K_LONG, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_OK && value == 0x11223344u);
    assert(probe.event_count == 3 && probe.events[0].address == 0xc00000u);
    assert(probe.events[1].address == 0xc00002u && probe.events[2].kind == EVENT_HOOK);
    assert(probe.events[2].address == 0x123456u && probe.events[2].pc == 0x0011f4u);
    assert(live.address == 0xfe123456u && live.cycle_counter == 5018);
    assert(live.fetch_pc == (uintptr_t)UINT64_C(0x36f00002000) + 0xab001236u +
                            (uintptr_t)UINT64_C(0x200000000));
    assert(live.fetch_base == (uintptr_t)UINT64_C(0x36f00002040));

    reset_probe(&probe, &state, &live);
    live.address = 0x00c00000u;
    probe.words[0] = 0x3344; probe.words[1] = 0x1122;
    assert(gens_m68k_read(&bus, &live, GENS_M68K_LONG, GENS_M68K_PREDECREMENT, &value)
           == GENS_M68K_BUS_OK && value == 0x11223344u);
    assert(probe.events[0].address == 0xc00002u && probe.events[1].address == 0xc00000u);
    reset_probe(&probe, &state, &live);
    live.address = 0x00dfffffu;
    probe.words[0] = 0x1122; probe.words[1] = 0x3344;
    assert(gens_m68k_read(&bus, &live, GENS_M68K_LONG, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_OK);
    assert(probe.events[0].address == 0xdfffffu && probe.events[1].address == 0xe00001u);

    reset_probe(&probe, &state, &live);
    live.address = 0x00c00000u;
    assert(gens_m68k_write(&bus, &live, GENS_M68K_LONG, GENS_M68K_NORMAL, 0x11223344u)
           == GENS_M68K_BUS_OK);
    assert(probe.events[0].address == 0xc00000u && probe.events[0].value == 0x1122u);
    assert(probe.events[1].address == 0xc00002u && probe.events[1].value == 0x3344u);
    assert(probe.events[2].kind == EVENT_HOOK && probe.events[2].value == 0x11223344u);
    reset_probe(&probe, &state, &live);
    live.address = 0x00c00000u;
    probe.mutate_trace = 1;
    assert(gens_m68k_write(&bus, &live, GENS_M68K_LONG, GENS_M68K_PREDECREMENT, 0x11223344u)
           == GENS_M68K_BUS_OK);
    assert(probe.events[0].address == 0xc00002u && probe.events[0].value == 0x3344u);
    assert(probe.events[1].address == 0xc00000u && probe.events[1].value == 0x1122u);
    assert(probe.events[2].pc == 0x345678u && probe.events[2].address == 0x55u);
    assert(probe.events[2].value == 0xaabbccddu);

    reset_probe(&probe, &state, &live);
    live.address = 0x00fffffcu;
    assert(gens_m68k_write(&bus, &live, GENS_M68K_LONG, GENS_M68K_NORMAL, 0xa1b2c3d4u)
           == GENS_M68K_BUS_OK);
    assert(gens_m68k_read(&bus, &live, GENS_M68K_LONG, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_OK && value == 0xa1b2c3d4u);
    live.address = 0x00fffffeu;
    assert(gens_m68k_read(&bus, &live, GENS_M68K_WORD, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_OK && value == 0xc3d4u);
    live.address = 0x00fffffdu;
    assert(gens_m68k_write(&bus, &live, GENS_M68K_LONG, GENS_M68K_NORMAL, 1)
           == GENS_M68K_BUS_RAM_BOUNDARY && probe.event_count == 3);

    reset_probe(&probe, &state, &live);
    live.address = 0x00c00000u;
    probe.words[0] = 0x1122; probe.words[1] = 0x3344; probe.fail_call = 2;
    value = 0xdeadbeefu;
    assert(gens_m68k_read(&bus, &live, GENS_M68K_LONG, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_BACKEND_ERROR);
    assert(value == 0xdeadbeefu && probe.event_count == 2);
    live.address = 0x00ffffffu;
    assert(gens_m68k_read(&bus, &live, GENS_M68K_WORD, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_RAM_BOUNDARY);
    assert(value == 0xdeadbeefu && probe.event_count == 2);
    live.address = 0x00c00000u;
    bus.read_word = 0;
    assert(gens_m68k_read(&bus, &live, GENS_M68K_WORD, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_MISSING_CALLBACK && probe.event_count == 2);
    bus.read_word = read_word;
    assert(gens_m68k_read(&bus, &live, (enum GensM68kBusSize)3, GENS_M68K_NORMAL, &value)
           == GENS_M68K_BUS_INVALID_ARGUMENT);
    assert(gens_m68k_read(&bus, &live, GENS_M68K_BYTE, (enum GensM68kBusOrder)2, &value)
           == GENS_M68K_BUS_INVALID_ARGUMENT);
    assert(gens_m68k_read(&bus, &live, GENS_M68K_BYTE, GENS_M68K_NORMAL, 0)
           == GENS_M68K_BUS_INVALID_ARGUMENT);
    bus.hook = 0;
    assert(gens_m68k_write(&bus, &live, GENS_M68K_BYTE, GENS_M68K_NORMAL, 1)
           == GENS_M68K_BUS_MISSING_CALLBACK);
    assert(munmap(ram, 65536) == 0);
    return 0;
}
