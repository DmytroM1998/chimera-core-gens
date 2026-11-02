#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_input.h"
#include "io.h"

#ifdef GENS_GENESIS_REFERENCE
extern uint8_t Ram_68k[65536], Rom_Data[6 * 1024 * 1024], SRAM[65536];
extern int SRAM_Start, SRAM_End, SRAM_ON, SRAM_Write, SRAM_Custom;
extern int Z80_State, Game_Mode, CPU_Mode, Gen_Version;
extern int Last_BUS_REQ_Cnt, Last_BUS_REQ_St, Fake_Fetch;
extern int Cycles_M68K, Cycles_Z80, Z80_M68K_Cycle_Tab[512];
extern uint8_t Ram_Z80[8192];
extern int Bank_Z80;
void reference_init_genesis(void);
uint8_t M68K_RB(uint32_t address);
uint16_t M68K_RW(uint32_t address);
void M68K_WB(uint32_t address, uint32_t value);
void M68K_WW(uint32_t address, uint32_t value);
#else
#include "gens_genesis_bus.h"
static uint8_t Ram_68k[65536], Rom_Data[6 * 1024 * 1024], SRAM[65536];
static int SRAM_Start, SRAM_End, SRAM_ON, SRAM_Write, SRAM_Custom;
static int Z80_State, Game_Mode, CPU_Mode, Gen_Version;
static int Last_BUS_REQ_Cnt, Last_BUS_REQ_St, Fake_Fetch;
static int Cycles_M68K, Cycles_Z80, Z80_M68K_Cycle_Tab[512];
static uint8_t Ram_Z80[8192];
static int Bank_Z80;
static GensGenesisBus bus;
static GensZ80Control control;
static GensZ80Memory memory;
#endif

static uint64_t hash;
static uint32_t odometer;

static void record(uint32_t value)
{
    unsigned int shift;
    for (shift = 0; shift < 32; shift += 8)
    {
        hash ^= (value >> shift) & 0xffu;
        hash *= UINT64_C(1099511628211);
    }
}

static void start(void)
{
    hash = UINT64_C(14695981039346656037);
}

static void finish(const char *scenario)
{
    printf("%s %016" PRIx64 "\n", scenario, hash);
}

static void record_control(void)
{
    record((uint32_t)Z80_State);
    record((uint32_t)Last_BUS_REQ_Cnt);
    record((uint32_t)Last_BUS_REQ_St);
    record((uint32_t)Fake_Fetch);
    record(Controller_1_Counter);
    record(Controller_1_Delay);
    record(Controller_2_Counter);
    record(Controller_2_Delay);
}

void reference_z80_event(unsigned int kind, uint32_t target)
{
    record(kind);
    record(target);
    record_control();
}

uint32_t reference_odometer(void)
{
    reference_z80_event(0, odometer);
    return odometer;
}

int reference_ym_read(unsigned int port)
{
    record(10);
    record(port);
    return 0x9a;
}

void reference_ym_write(unsigned int port, unsigned int value)
{
    record(11);
    record(port);
    record(value & 0xffu);
}

void reference_psg_write(unsigned int value)
{
    record(12);
    record(value & 0xffu);
}

void reference_vdp_byte_write(unsigned int value)
{
    record(13);
    record(value & 0xffu);
}

int reference_vdp_status(void) { record(14); return 0xa5bc; }
int reference_v_counter(void) { record(15); return 0x71; }
int reference_h_counter(void) { record(16); return 0x23; }

#ifdef GENS_GENESIS_REFERENCE
void _Fix_Codes(uint32_t address, unsigned int size)
{
    record(address);
    record(size);
}

static void init_bus(void)
{
    reference_init_genesis();
}

static uint32_t read_byte(uint32_t address) { return M68K_RB(address); }
static uint32_t read_word(uint32_t address) { return M68K_RW(address); }
static void write_byte(uint32_t address, uint8_t value) { M68K_WB(address, value); }
static void write_word(uint32_t address, uint16_t value) { M68K_WW(address, value); }
#else
static int read_memory_device(void *opaque, GensM68kBusState *state,
                              enum GensZ80Device device, uint32_t address, uint16_t *value)
{
    (void)opaque;
    (void)state;
    switch (device)
    {
    case GENS_Z80_YM2612: *value = (uint16_t)reference_ym_read(address); return 1;
    case GENS_Z80_VDP_STATUS: *value = (uint16_t)reference_vdp_status(); return 1;
    case GENS_Z80_V_COUNTER: *value = (uint16_t)reference_v_counter(); return 1;
    case GENS_Z80_H_COUNTER: *value = (uint16_t)reference_h_counter(); return 1;
    default: assert(0); return 0;
    }
}

static int write_memory_device(void *opaque, GensM68kBusState *state,
                               enum GensZ80Device device, uint32_t address, uint8_t value)
{
    (void)opaque;
    (void)state;
    switch (device)
    {
    case GENS_Z80_YM2612: reference_ym_write(address, value); return 1;
    case GENS_Z80_VDP_DATA: reference_vdp_byte_write(value); return 1;
    case GENS_Z80_PSG: reference_psg_write(value); return 1;
    default: assert(0); return 0;
    }
}

static int read_odometer(void *opaque, GensM68kBusState *state, uint32_t *value)
{
    (void)opaque;
    (void)state;
    *value = reference_odometer();
    return 1;
}

static int set_odometer(void *opaque, GensM68kBusState *state, uint32_t target)
{
    (void)opaque;
    (void)state;
    reference_z80_event(1, target);
    return 1;
}

static int execute_z80(void *opaque, GensM68kBusState *state, uint32_t target)
{
    (void)opaque;
    (void)state;
    reference_z80_event(2, target);
    return 1;
}

static int reset_z80(void *opaque, GensM68kBusState *state)
{
    (void)opaque;
    (void)state;
    reference_z80_event(3, 0);
    return 1;
}

static int reset_ym2612(void *opaque, GensM68kBusState *state)
{
    (void)opaque;
    (void)state;
    reference_z80_event(4, 0);
    return 1;
}

static int fix_codes(void *opaque, GensM68kBusState *state,
                     uint32_t address, unsigned int size)
{
    (void)opaque;
    (void)state;
    record(address);
    record(size);
    return 1;
}

static void hook(void *opaque, GensM68kBusState *state, enum GensM68kBusHook kind)
{
    (void)opaque;
    (void)state;
    (void)kind;
    assert(0);
}

static void init_bus(void)
{
    GensGenesisBusConfig config = {
        Ram_68k, sizeof(Ram_68k), Rom_Data, sizeof(Rom_Data), SRAM, sizeof(SRAM),
        &SRAM_Start, &SRAM_End, &SRAM_ON, &SRAM_Write, &SRAM_Custom,
        &Z80_State, &Game_Mode, &CPU_Mode, &Gen_Version,
        0, 0, 0, fix_codes, hook, &control, &memory, 0
    };
    GensZ80Control settings = {
        &Z80_State, &Last_BUS_REQ_Cnt, &Last_BUS_REQ_St, &Fake_Fetch,
        &Cycles_M68K, &Cycles_Z80, Z80_M68K_Cycle_Tab, 512,
        0, read_odometer, set_odometer, execute_z80, reset_z80, reset_ym2612
    };
    GensZ80Memory storage = {
        Ram_Z80, sizeof(Ram_Z80), &Bank_Z80, 0, read_memory_device, write_memory_device
    };
    control = settings;
    memory = storage;
    assert(gens_genesis_bus_init(&bus, &config));
}

static uint32_t read_byte(uint32_t address)
{
    uint8_t value = 0;
    assert(bus.cpu.read_byte(bus.cpu.opaque, bus.cpu.state, address, &value));
    return value;
}

static uint32_t read_word(uint32_t address)
{
    uint16_t value = 0;
    assert(bus.cpu.read_word(bus.cpu.opaque, bus.cpu.state, address, &value));
#ifdef GENS_GENESIS_NEGATIVE_CONTROL
    if (address == 0x102u)
        value ^= 1u;
#endif
    return value;
}

static void write_byte(uint32_t address, uint8_t value)
{
    assert(bus.cpu.write_byte(bus.cpu.opaque, bus.cpu.state, address, value));
}

static void write_word(uint32_t address, uint16_t value)
{
    assert(bus.cpu.write_word(bus.cpu.opaque, bus.cpu.state, address, value));
}
#endif

static void record_controller(void)
{
    record(Controller_1_State);
    record(Controller_1_COM);
    record(Controller_1_Counter);
    record(Controller_1_Delay);
    record(Controller_2_State);
    record(Controller_2_COM);
    record(Controller_2_Counter);
    record(Controller_2_Delay);
    record(Lag_Frame);
}

static void initialise_backing(void)
{
    uint32_t x = UINT32_C(0x243f6a88);
    unsigned int i;
    for (i = 0; i < sizeof(Rom_Data); ++i)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        Rom_Data[i] = (uint8_t)x;
    }
    for (i = 0; i < sizeof(Ram_68k); ++i)
    {
        Ram_68k[i] = (uint8_t)(i ^ (i >> 7) ^ 0xa5u);
        SRAM[i] = (uint8_t)(i ^ (i >> 8) ^ 0x5au);
    }
}

static void baseline(void)
{
    static const uint32_t offsets[] = {0, 1, 0x102, 0x7fffe};
    unsigned int slot, i;
    start();
    SRAM_ON = 0;
    for (slot = 0; slot < 32; ++slot)
    {
        if (slot == 20 || slot == 24)
            continue;
        for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
        {
            uint32_t address = slot * UINT32_C(0x80000) + offsets[i];
            record(read_byte(address));
            if (offsets[i] != 1)
                record(read_word(address));
        }
    }
    finish("baseline");
}

static void mapper(void)
{
    static const uint32_t offsets[] = {0, 1, 0x100, 0x7fffe};
    unsigned int slot, selected, i;
    start();
    SRAM_ON = 0;
    Z80_State = 2;
    for (slot = 1; slot < 8; ++slot)
    {
        for (selected = 0; selected < 32; ++selected)
        {
            if (selected == 24)
                continue;
            if ((selected & 1u) != 0u)
                write_byte(UINT32_C(0xa130f1) + slot * 2u, (uint8_t)selected);
            else
                write_word(UINT32_C(0xa130f0) + slot * 2u, (uint16_t)(selected << 8));
            for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
            {
                uint32_t address = slot * UINT32_C(0x80000) + offsets[i];
                record(read_byte(address));
                if (offsets[i] != 1)
                    record(read_word(address));
            }
        }
    }
    Z80_State = 0;
    init_bus();
    finish("mapper");
}

static void sram_scenarios(void)
{
    unsigned int control, i;
    start();
    SRAM_Start = 0x200000;
    SRAM_End = 0x20ffff;
    SRAM_Custom = 0;
    for (control = 0; control < 4; ++control)
    {
        write_byte(0xa130f1, (uint8_t)control);
        record((uint32_t)SRAM_ON);
        record((uint32_t)SRAM_Write);
        for (i = 0; i < 16; ++i)
        {
            uint32_t address = UINT32_C(0x200010) + i * 2u;
            write_word(address, (uint16_t)(0x9100u + i));
            record(read_word(address));
            write_byte(address + 1u, (uint8_t)(0xe0u + i));
            record(read_byte(address + 1u));
            record(SRAM[0x10 + i * 2u]);
            record(SRAM[0x11 + i * 2u]);
        }
    }
    SRAM_ON = 1;
    SRAM_Custom = 1;
    record(read_byte(0x200012));
    record(read_word(0x200012));
    SRAM_Custom = 0;
    SRAM_Start = 0x080000;
    SRAM_End = 0x08ffff;
    write_byte(0xa130f3, 4);
    record(read_byte(0x080012));
    record(read_word(0x080012));
    init_bus();
    finish("sram");
}

static void controller_scenarios(void)
{
    GensInputConfig config = {GENS_INPUT_TEAM_PLAYER, 6, 3, 6, 3, 3};
    unsigned int phase, port, address;
    start();
    assert(gens_input_configure(&config));
    gens_input_apply(UINT64_C(0x123456789) << GENS_INPUT_PAD_BASE);
    for (phase = 0; phase < 16; ++phase)
    {
        Controller_1_Counter = (phase << 16) | (phase & 3u);
        Controller_2_Counter = ((15u - phase) << 16) | (phase & 3u);
        for (port = 0; port < 2; ++port)
        {
            address = 0xa10002u + port * 2u;
            Lag_Frame = 1;
            record(read_byte(address));
            record(read_word(address));
            write_byte(address, (uint8_t)(phase * 17u));
            write_word(address, (uint16_t)(phase * 4097u));
            record_controller();
        }
    }
    Controller_1_COM = 0xabcd1234u;
    Controller_2_COM = 0xdead5678u;
    for (address = 0xa10000u; address <= 0xa1000du; ++address)
    {
        Game_Mode = 1;
        CPU_Mode = 1;
        Gen_Version = 1;
        record(read_byte(address));
        record(read_word(address));
        if ((address & 0x0eu) == 8u || (address & 0x0eu) == 10u)
        {
            write_byte(address, (uint8_t)address);
            write_word(address, (uint16_t)address);
            record_controller();
        }
    }
    finish("controller");
}

static void ram_scenarios(void)
{
    unsigned int region, i;
    start();
    for (region = 14; region < 16; ++region)
    {
        for (i = 0; i < 64; ++i)
        {
            uint32_t address = region * UINT32_C(0x100000) + i * 2u;
            write_byte(address, (uint8_t)(i + region));
            record(read_byte(address));
            write_word(address, (uint16_t)(0x8000u + i));
            record(read_word(address));
        }
    }
    finish("ram");
}

static void control_scenarios(void)
{
    static const uint16_t values[] = {0, 1, 0x100, 0x101};
    unsigned int flags, width, i;
    for (i = 0; i < 512; ++i)
        Z80_M68K_Cycle_Tab[i] = (int)(i * 7u / 15u);
    Cycles_M68K = 1100;
    Cycles_Z80 = 700;
    odometer = 1000;
    start();
    for (flags = 0; flags < 8; ++flags)
    {
        for (width = 1; width <= 2; ++width)
        {
            for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
            {
                Z80_State = (int)(0x12345600u | flags);
                Last_BUS_REQ_Cnt = 950;
                Last_BUS_REQ_St = 0x123456ab;
                Fake_Fetch = 0x345678cc;
                Controller_1_Counter = 0x12345678;
                Controller_1_Delay = 0xabcdef12;
                Controller_2_Counter = 0xdeadbeef;
                Controller_2_Delay = 0x01020304;
                if (width == 1)
                    write_byte(0xa11100, (uint8_t)values[i]);
                else
                    write_word(0xa11100, values[i]);
                record_control();
            }
        }
    }
    finish("bus-request-write");
    start();
    for (flags = 0; flags < 8; ++flags)
    {
        for (width = 1; width <= 2; ++width)
        {
            for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
            {
                Z80_State = (int)(0x12345600u | flags);
                if (width == 1)
                    write_byte(0xa11200, (uint8_t)values[i]);
                else
                    write_word(0xa11200, values[i]);
                record_control();
            }
        }
    }
    finish("z80-reset");
    start();
    for (flags = 0; flags < 8; ++flags)
    {
        for (i = 0; i < 256; ++i)
        {
            unsigned int delay;
            Z80_State = (int)(0x12345600u | flags);
            Last_BUS_REQ_St = (int)(0x23456700u | i);
            Fake_Fetch = (int)(0x34567800u | i);
            Last_BUS_REQ_Cnt = 1000;
            for (delay = 15; delay <= 17; ++delay)
            {
                odometer = 1000u + delay;
                record(read_byte(0xa11100));
                record(read_word(0xa11100));
                record_control();
                record(read_word(0xa11100));
                record_control();
            }
        }
    }
    Z80_State = 0;
    Last_BUS_REQ_Cnt = -16;
    odometer = 0;
    record(read_word(0xa11100));
    odometer = 1;
    record(read_word(0xa11100));
    finish("bus-request-read");
}

static void memory_scenarios(void)
{
    static const uint32_t offsets[] = {
        0, 1, 0x1fff, 0x2000, 0x3fff, 0x4000, 0x4003, 0x5fff,
        0x6000, 0x60ff, 0x6100, 0x6fff, 0x7000, 0x7f03, 0x7f04, 0x7f05,
        0x7f06, 0x7f07, 0x7f08, 0x7f09, 0x7f0a, 0x7f11, 0x7fff
    };
    unsigned int flags, mirror, i, bit;
    start();
    for (i = 0; i < sizeof(Ram_Z80); ++i)
        Ram_Z80[i] = (uint8_t)(i ^ (i >> 5) ^ 0x96u);
    for (flags = 0; flags < 8; ++flags)
    {
        Z80_State = (int)flags;
        Bank_Z80 = 0x12345678;
        for (mirror = 0; mirror < 2; ++mirror)
        {
            for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i)
            {
                uint32_t address = UINT32_C(0xa00000) + mirror * 0x8000u + offsets[i];
                record(read_byte(address));
                record(read_word(address));
                write_byte(address, (uint8_t)(i * 13u));
                write_word(address, (uint16_t)(0xa500u | i));
                record((uint32_t)Bank_Z80);
            }
        }
    }
    Z80_State = 0;
    Bank_Z80 = 0xff8000;
    for (bit = 0; bit < 18; ++bit)
    {
        write_word(0xa06000u + bit, (uint16_t)((0x2a5a5u >> bit) & 1u));
        record((uint32_t)Bank_Z80);
    }
    for (i = 0; i < sizeof(Ram_Z80); ++i)
        record(Ram_Z80[i]);
    write_byte(0xa130f3, 20);
    record(read_word(0x080123));
    record(read_word(0x086100));
    init_bus();
    finish("z80-window");
}

int main(void)
{
    initialise_backing();
    init_bus();
    baseline();
    mapper();
    sram_scenarios();
    controller_scenarios();
    ram_scenarios();
    control_scenarios();
    memory_scenarios();
    return 0;
}
