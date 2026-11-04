#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "gens_genesis_bus.h"
#include "gens_input.h"
#include "io.h"

enum { EVENT_READ = 1, EVENT_WRITE, EVENT_FIX, EVENT_HOOK };

struct Event {
    int kind, device;
    uint32_t address, value;
    unsigned int size;
};

struct Probe {
    struct Event events[16];
    unsigned int count;
    int fail_read, fail_write, fail_fix;
};

static uint8_t ram[65536], rom[6 * 1024 * 1024], sram[65536];
static int sram_start, sram_end, sram_on, sram_write, sram_custom;
static int z80_state, game_mode, cpu_mode, gen_version;
static struct Probe probe;
static GensGenesisBus bus;
static GensM68kLive live;

static void event(struct Probe *p, int kind, int device, uint32_t address,
                  unsigned int size, uint32_t value)
{
    assert(p->count < sizeof(p->events) / sizeof(p->events[0]));
    p->events[p->count].kind = kind;
    p->events[p->count].device = device;
    p->events[p->count].address = address;
    p->events[p->count].size = size;
    p->events[p->count].value = value;
    ++p->count;
}

static int read_device(void *opaque, GensM68kBusState *state,
                       enum GensGenesisDevice device, uint32_t address,
                       unsigned int size, uint16_t *value)
{
    struct Probe *p = opaque;
    event(p, EVENT_READ, device, address, size, 0);
    state->io_fetch_pc += (uintptr_t)UINT64_C(0x100000000);
    *value = UINT16_C(0x9abc);
    return !p->fail_read;
}

static int write_device(void *opaque, GensM68kBusState *state,
                        enum GensGenesisDevice device, uint32_t address,
                        unsigned int size, uint16_t value)
{
    struct Probe *p = opaque;
    event(p, EVENT_WRITE, device, address, size, value);
    state->io_cycle_counter += 3;
    return !p->fail_write;
}

static int fix_codes(void *opaque, GensM68kBusState *state, uint32_t address, unsigned int size)
{
    struct Probe *p = opaque;
    event(p, EVENT_FIX, 0, address, size, state->hook_value);
    return !p->fail_fix;
}

static void hook(void *opaque, GensM68kBusState *state, enum GensM68kBusHook kind)
{
    struct Probe *p = opaque;
    event(p, EVENT_HOOK, kind, state->hook_address, 0, state->hook_value);
}

static void clear_events(void)
{
    memset(&probe, 0, sizeof(probe));
}

static GensM68kBusStatus read_at(uint32_t address, enum GensM68kBusSize size, uint32_t *value)
{
    live.address = address;
    return gens_m68k_read(&bus.cpu, &live, size, GENS_M68K_NORMAL, value);
}

static GensM68kBusStatus write_at(uint32_t address, enum GensM68kBusSize size,
                                  enum GensM68kBusOrder order, uint32_t value)
{
    live.address = address;
    return gens_m68k_write(&bus.cpu, &live, size, order, value);
}

int main(void)
{
    GensGenesisBusConfig config = {
        ram, sizeof(ram), rom, sizeof(rom), sram, sizeof(sram),
        &sram_start, &sram_end, &sram_on, &sram_write, &sram_custom,
        &z80_state, &game_mode, &cpu_mode, &gen_version,
        &probe, read_device, write_device, fix_codes, hook, 0, 0, 0
    };
    GensInputConfig inputs = {GENS_INPUT_TEAM_PLAYER, 6, 6, 6, 3, 3};
    GensGenesisBusConfig invalid = config;
    int last_cycles = 0, last_state = 0, fake_fetch = 0, cycles_m68k = 0, cycles_z80 = 0;
    int cycle_table[512] = {0};
    GensZ80Control control = {
        &z80_state, &last_cycles, &last_state, &fake_fetch, &cycles_m68k, &cycles_z80,
        cycle_table, 512, 0, 0, 0, 0, 0, 0
    };
    uint8_t z80_ram[8192] = {0};
    int bank = 0;
    GensZ80Memory memory = {z80_ram, sizeof(z80_ram), &bank, 0, 0, 0};
    uint32_t value;
    unsigned int i;

    invalid.rom_size = sizeof(rom) - 1u;
    assert(!gens_genesis_bus_init(&bus, &invalid));
    assert(gens_genesis_bus_init(&bus, &config));
    assert(bus.cpu.ram == ram && bus.cpu.opaque == &bus && bus.cpu.state == &bus.state);
    for (i = 0; i < 32; ++i)
        assert(bus.read_map[i] == i);
    assert(gens_input_configure(&inputs));
    gens_input_apply(0);
    live.fetch_base = (uintptr_t)UINT64_C(0x36f00002000);
    live.fetch_pc = live.fetch_base + UINT32_C(0x1236);
    live.cycle_counter = 100;

    rom[0x101] = 0x12;
    rom[0x100] = 0x34;
    clear_events();
    assert(read_at(0xab000100u, GENS_M68K_BYTE, &value) == GENS_M68K_BUS_OK && value == 0x12);
    assert(probe.count == 1 && probe.events[0].kind == EVENT_HOOK);
    clear_events();
    assert(read_at(0x100u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_OK && value == 0x1234);

    sram_start = 0x200000;
    sram_end = 0x20ffff;
    sram_on = 1;
    sram_write = 1;
    sram_custom = 0;
    sram[0x12] = 0x56;
    sram[0x13] = 0x78;
    clear_events();
    assert(read_at(0x200012u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_OK && value == 0x5678);
    assert(write_at(0x200012u, GENS_M68K_LONG, GENS_M68K_NORMAL, 0xa1b2c3d4u)
           == GENS_M68K_BUS_OK);
    assert(sram[0x12] == 0xa1 && sram[0x13] == 0xb2 &&
           sram[0x14] == 0xc3 && sram[0x15] == 0xd4);
    assert(probe.count == 4 && probe.events[1].kind == EVENT_FIX &&
           probe.events[1].address == 0x200012u && probe.events[2].kind == EVENT_FIX &&
           probe.events[2].address == 0x200014u && probe.events[3].kind == EVENT_HOOK);
    clear_events();
    assert(write_at(0x20ffffu, GENS_M68K_WORD, GENS_M68K_NORMAL, 0x1234)
           == GENS_M68K_BUS_BACKEND_ERROR);
    assert(probe.count == 0);

    sram_custom = 1;
    assert(read_at(0x200012u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_OK && value == 0);
    sram_custom = 0;
    clear_events();
    assert(write_at(0xa130f1u, GENS_M68K_BYTE, GENS_M68K_NORMAL, 3) == GENS_M68K_BUS_OK);
    assert(sram_on == 1 && sram_write == 0);
    assert(probe.count == 2 && probe.events[0].kind == EVENT_FIX && probe.events[1].kind == EVENT_HOOK);
    assert(write_at(0xa130f0u, GENS_M68K_WORD, GENS_M68K_NORMAL, 1) == GENS_M68K_BUS_OK);
    assert(sram_on == 1 && sram_write == 1);

    clear_events();
    rom[0x180121] = 0x9d;
    assert(write_at(0xa130f3u, GENS_M68K_BYTE, GENS_M68K_NORMAL, 3) == GENS_M68K_BUS_OK);
    assert(bus.read_map[1] == 3);
    assert(read_at(0x080120u, GENS_M68K_BYTE, &value) == GENS_M68K_BUS_OK && value == 0x9d);
    assert(write_at(0xa130f2u, GENS_M68K_WORD, GENS_M68K_NORMAL, 0x1c00u) == GENS_M68K_BUS_OK);
    assert(bus.read_map[1] == 28);
    ram[0x121] = 0x6a;
    assert(read_at(0x080120u, GENS_M68K_BYTE, &value) == GENS_M68K_BUS_OK && value == 0x6a);
    gens_genesis_bus_reset_mapping(&bus);
    assert(bus.read_map[1] == 1);

    clear_events();
    Controller_1_COM = 0xabcd1234u;
    assert(write_at(0xa10009u, GENS_M68K_BYTE, GENS_M68K_NORMAL, 0x56) == GENS_M68K_BUS_OK);
    assert(Controller_1_COM == 0xabcd1256u);
    assert(write_at(0xa10008u, GENS_M68K_WORD, GENS_M68K_NORMAL, 0x789a) == GENS_M68K_BUS_OK);
    assert(Controller_1_COM == 0xabcd789au);
    assert(read_at(0xa10008u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_OK && value == 0x789a);
    Lag_Frame = 1;
    assert(read_at(0xa10003u, GENS_M68K_BYTE, &value) == GENS_M68K_BUS_OK && Lag_Frame == 0);
    game_mode = 1;
    cpu_mode = 1;
    gen_version = 1;
    assert(read_at(0xa10000u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_OK && value == 0xc1);
    assert(read_at(0xa10006u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_OK && value == 0xff00);

    clear_events();
    z80_state = 2;
    assert(read_at(0xa00123u, GENS_M68K_BYTE, &value) == GENS_M68K_BUS_OK && value == 0);
    assert(probe.count == 1 && probe.events[0].kind == EVENT_HOOK);
    z80_state = 0;
    clear_events();
    assert(read_at(0xa00123u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_OK && value == 0x9abc);
    assert(probe.events[0].kind == EVENT_READ && probe.events[0].device == GENS_GENESIS_Z80_MEMORY &&
           probe.events[0].address == 0x123u && probe.events[0].size == 2);
    assert(live.fetch_pc > (uintptr_t)UINT64_C(0x36f00002000) + UINT32_C(0x1236));
    clear_events();
    assert(write_at(0xa00123u, GENS_M68K_WORD, GENS_M68K_NORMAL, 0x1234) == GENS_M68K_BUS_OK);
    assert(probe.count == 3 && probe.events[0].kind == EVENT_WRITE &&
           probe.events[0].device == GENS_GENESIS_Z80_MEMORY && probe.events[0].value == 0x34 &&
           probe.events[1].kind == EVENT_FIX && probe.events[2].kind == EVENT_HOOK);

    clear_events();
    assert(read_at(0xc00000u, GENS_M68K_BYTE, &value) == GENS_M68K_BUS_OK && value == 0);
    assert(probe.count == 1 && probe.events[0].kind == EVENT_HOOK);
    clear_events();
    assert(read_at(0xc00004u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_OK && value == 0x9abc);
    assert(probe.events[0].kind == EVENT_READ && probe.events[0].device == GENS_GENESIS_VDP);
    clear_events();
    assert(write_at(0xc00011u, GENS_M68K_BYTE, GENS_M68K_NORMAL, 0x77) == GENS_M68K_BUS_OK);
    assert(probe.events[0].kind == EVENT_WRITE && probe.events[0].device == GENS_GENESIS_PSG);

    clear_events();
    invalid = config;
    invalid.device_read = 0;
    assert(gens_genesis_bus_init(&bus, &invalid));
    assert(read_at(0xc00004u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_BACKEND_ERROR);
    assert(probe.count == 0);
    assert(gens_genesis_bus_init(&bus, &config));
    probe.fail_fix = 1;
    assert(write_at(0xa130f3u, GENS_M68K_BYTE, GENS_M68K_NORMAL, 4) == GENS_M68K_BUS_BACKEND_ERROR);
    assert(bus.read_map[1] == 4 && probe.count == 1 && probe.events[0].kind == EVENT_FIX);
    clear_events();
    assert(write_at(0xe00020u, GENS_M68K_BYTE, GENS_M68K_NORMAL, 0x55) == GENS_M68K_BUS_OK);
    assert(ram[0x21] == 0x55 && probe.count == 1 && probe.events[0].kind == EVENT_HOOK);
    clear_events();
    assert(write_at(0xdffffeu, GENS_M68K_LONG, GENS_M68K_NORMAL, 0xa1b2c3d4u)
           == GENS_M68K_BUS_OK);
    assert(ram[0] == 0xd4 && ram[1] == 0xc3);
    assert(probe.count == 3 && probe.events[0].kind == EVENT_FIX &&
           probe.events[0].address == 0xdffffeu && probe.events[1].kind == EVENT_FIX &&
           probe.events[1].address == 0xe00000u && probe.events[2].kind == EVENT_HOOK);
    clear_events();
    assert(read_at(0xdffffeu, GENS_M68K_LONG, &value) == GENS_M68K_BUS_OK && value == 0xc3d4u);
    ram[0] = ram[1] = 0;
    clear_events();
    assert(write_at(0xdffffeu, GENS_M68K_LONG, GENS_M68K_PREDECREMENT, 0xa1b2c3d4u)
           == GENS_M68K_BUS_OK);
    assert(ram[0] == 0xd4 && ram[1] == 0xc3);
    assert(probe.count == 3 && probe.events[0].kind == EVENT_FIX &&
           probe.events[0].address == 0xe00000u && probe.events[1].kind == EVENT_FIX &&
           probe.events[1].address == 0xdffffeu && probe.events[2].kind == EVENT_HOOK);
    invalid = config;
    invalid.z80_control = &control;
    control.z80_state = &last_state;
    assert(!gens_genesis_bus_init(&bus, &invalid));
    control.z80_state = &z80_state;
    assert(gens_genesis_bus_init(&bus, &invalid));
    clear_events();
    z80_state = 2;
    assert(read_at(0xa11100u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_OK && value == 0x81ff);
    assert(fake_fetch == 0xff && probe.count == 1 && probe.events[0].kind == EVENT_HOOK);
    clear_events();
    z80_state = 0;
    assert(read_at(0xa11100u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_BACKEND_ERROR);
    assert(probe.count == 0 && fake_fetch == 0xff);
    assert(write_at(0xa11200u, GENS_M68K_WORD, GENS_M68K_NORMAL, 0)
           == GENS_M68K_BUS_BACKEND_ERROR);
    assert(probe.count == 0 && z80_state == 0);
    invalid.z80_memory = &memory;
    memory.ram_size = 8191;
    assert(!gens_genesis_bus_init(&bus, &invalid));
    memory.ram_size = sizeof(z80_ram);
    assert(gens_genesis_bus_init(&bus, &invalid));
    z80_ram[0x12] = 0x55;
    clear_events();
    assert(read_at(0xa00012u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_OK && value == 0x55);
    assert(probe.count == 1 && probe.events[0].kind == EVENT_HOOK);
    clear_events();
    assert(write_at(0xa02012u, GENS_M68K_WORD, GENS_M68K_NORMAL, 0xabcd) == GENS_M68K_BUS_OK);
    assert(z80_ram[0x12] == 0xcd && z80_ram[0x13] == 0);
    assert(probe.count == 2 && probe.events[0].kind == EVENT_FIX &&
           probe.events[1].kind == EVENT_HOOK);
    clear_events();
    assert(read_at(0xa04000u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_BACKEND_ERROR);
    assert(probe.count == 0);
    z80_ram[0x1ffe] = 0x12;
    z80_ram[0] = 0x34;
    assert(read_at(0xa01ffeu, GENS_M68K_LONG, &value) == GENS_M68K_BUS_OK && value == 0x00120034);
    clear_events();
    assert(write_at(0xa01ffeu, GENS_M68K_LONG, GENS_M68K_NORMAL, 0x1234abcdu)
           == GENS_M68K_BUS_OK);
    assert(z80_ram[0x1ffe] == 0x34 && z80_ram[0x1fff] == 0 &&
           z80_ram[0] == 0xcd && z80_ram[1] == 0);
    assert(probe.count == 3 && probe.events[0].kind == EVENT_FIX &&
           probe.events[1].kind == EVENT_FIX && probe.events[2].kind == EVENT_HOOK);
    z80_state = 2;
    assert(read_at(0xa00012u, GENS_M68K_WORD, &value) == GENS_M68K_BUS_OK && value == 0);

    /* Debugger accesses bypass devices and hooks, but follow the live cartridge map. */
    clear_events();
    {
        GensM68kBusState before = bus.state;
        int lag_before = Lag_Frame, fetch_before = fake_fetch;
        rom[0x101] = 0x12;
        rom[0x100] = 0x34;
        assert(gens_genesis_bus_peek(&bus, 0x100) == 0x12);
        assert(gens_genesis_bus_peek(&bus, 0x101) == 0x34);
        gens_genesis_bus_poke(&bus, 0x100, 0xff);
        assert(rom[0x101] == 0x12);
        gens_genesis_bus_poke(&bus, 0xff0020, 0x65);
        assert(ram[0x21] == 0x65 && gens_genesis_bus_peek(&bus, 0xe00020) == 0x65);
        gens_genesis_bus_poke(&bus, 0xffffff, 0x87);
        assert(ram[0xfffe] == 0x87 && gens_genesis_bus_peek(&bus, 0xffffff) == 0x87);
        z80_ram[0x12] = 0x76;
        assert(gens_genesis_bus_peek(&bus, 0xa00012) == 0x76);
        assert(gens_genesis_bus_peek(&bus, 0xa02012) == 0x76);
        gens_genesis_bus_poke(&bus, 0xa02013, 0x98);
        assert(z80_ram[0x13] == 0x98);
        sram_on = 1;
        sram_write = 0;
        gens_genesis_bus_poke(&bus, 0x200012, 0xa9);
        assert(sram[0x12] == 0xa9 && gens_genesis_bus_peek(&bus, 0x200012) == 0xa9);
        sram_custom = 1;
        gens_genesis_bus_poke(&bus, 0x200012, 0xbb);
        assert(sram[0x12] == 0xa9 && gens_genesis_bus_peek(&bus, 0x200012) == 0);
        sram_custom = 0;
        bus.read_map[1] = 3;
        rom[0x180121] = 0xdc;
        assert(gens_genesis_bus_peek(&bus, 0x080120) == 0xdc);
        bus.read_map[1] = 28;
        assert(gens_genesis_bus_peek(&bus, 0x080020) == 0x65);
        for (i = 0; i < 0x1000000; ++i)
            (void)gens_genesis_bus_peek(&bus, i);
        assert(gens_genesis_bus_peek(&bus, 0x1000000) == 0);
        assert(gens_genesis_bus_peek(&bus, UINT32_MAX) == 0);
        gens_genesis_bus_poke(&bus, UINT32_MAX, 0xff);
        gens_genesis_bus_poke(&bus, 0xa10003, 0xff);
        gens_genesis_bus_poke(&bus, 0xa11100, 0xff);
        gens_genesis_bus_poke(&bus, 0xa130f3, 0xff);
        gens_genesis_bus_poke(&bus, 0xc00000, 0xff);
        assert(gens_genesis_bus_peek(&bus, 0xc00004) == 0);
        assert(probe.count == 0 && Lag_Frame == lag_before && fake_fetch == fetch_before);
        assert(memcmp(&before, &bus.state, sizeof(before)) == 0);
        assert(bus.read_map[1] == 28);
    }
    return 0;
}
