#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "gens_vdp.h"
#include "gens_vdp_irq.h"

static uint8_t vram[65536], cram[256], vsram[256], counters[1024];
static uint8_t rom[6291456], ram[65536], sram[65536];
static uint32_t registers[26], dimensions[4], scroll[4], window[2];
static uint8_t *addresses[5];
static uint8_t vscroll_mask;
static int control[7], status, vram_flag, cram_flag;
static int line, visible_lines, cycles, cpl, dma_length, dma_type, rom_size, cd_started;
static int sram_start, sram_end, sram_on, sram_write, sram_custom;
static int z80_state, game_mode, cpu_mode, version;
static unsigned int irq_calls, generic_reads, generic_writes, fixes, hooks;
static unsigned int irq_level;
static int pending_irq;
static uint8_t cpu_interrupts;
static int fail_odometer;
static GensVdp vdp;
static GensGenesisBus bus;
static GensM68kLive live;

static int odometer(void *opaque, GensM68kBusState *state, uint32_t *value)
{
    (void)opaque;
    assert(state == &bus.state);
    *value = 128;
    return !fail_odometer;
}

static int interrupt(void *opaque, GensM68kBusState *state, unsigned int level, int vector)
{
    (void)opaque;
    assert(state == &bus.state);
    assert(vector == -1);
    ++irq_calls;
    irq_level = level;
    return 1;
}

static GensVdpIrq irq = {&registers[0], &registers[1], &pending_irq,
                         &cpu_interrupts, 0, interrupt};

static int generic_read(void *opaque, GensM68kBusState *state,
                        enum GensGenesisDevice device, uint32_t address,
                        unsigned int size, uint16_t *value)
{
    (void)opaque; (void)state; (void)address; (void)size;
    assert(device != GENS_GENESIS_VDP);
    ++generic_reads;
    *value = 0x5a;
    return 1;
}

static int generic_write(void *opaque, GensM68kBusState *state,
                         enum GensGenesisDevice device, uint32_t address,
                         unsigned int size, uint16_t value)
{
    (void)opaque; (void)state; (void)address; (void)size; (void)value;
    assert(device != GENS_GENESIS_VDP);
    ++generic_writes;
    return 1;
}

static int fix_codes(void *opaque, GensM68kBusState *state, uint32_t address, unsigned int size)
{
    (void)opaque; (void)state; (void)address; (void)size;
    ++fixes;
    return 1;
}

static void hook(void *opaque, GensM68kBusState *state, enum GensM68kBusHook kind)
{
    (void)opaque; (void)state; (void)kind;
    ++hooks;
}

static void write_word(uint32_t address, uint16_t value)
{
    live.address = address;
    assert(gens_m68k_write(&bus.cpu, &live, GENS_M68K_WORD, GENS_M68K_NORMAL, value) ==
           GENS_M68K_BUS_OK);
}

static uint32_t read_word(uint32_t address)
{
    uint32_t value = 0x77777777;
    live.address = address;
    assert(gens_m68k_read(&bus.cpu, &live, GENS_M68K_WORD, GENS_M68K_NORMAL, &value) ==
           GENS_M68K_BUS_OK);
    return value;
}

int main(void)
{
    GensVdpConfig config = {
        {
            registers, 26, vram, sizeof(vram),
            (uint8_t *)&vram_flag, (uint8_t *)&cram_flag, &vscroll_mask,
            &addresses[0], &addresses[1], &addresses[2], &addresses[3], &addresses[4],
            &dimensions[0], &dimensions[1], &dimensions[2], &dimensions[3],
            &scroll[0], &scroll[1], &scroll[2], &scroll[3], &window[0], &window[1],
            &registers[24], &registers[25], (uint32_t *)&control[5], &irq, gens_vdp_irq_update
        },
        control, 7, cram, vsram, counters, sizeof(cram), sizeof(vsram), sizeof(counters),
        &status, &vram_flag, &cram_flag, &line, &visible_lines, &cycles, &cpl,
        &dma_length, &dma_type, rom, ram, sizeof(rom), sizeof(ram),
        &rom_size, &cd_started, 0, odometer
    };
    GensGenesisBusConfig bus_config = {
        ram, sizeof(ram), rom, sizeof(rom), sram, sizeof(sram),
        &sram_start, &sram_end, &sram_on, &sram_write, &sram_custom,
        &z80_state, &game_mode, &cpu_mode, &version,
        0, generic_read, generic_write, fix_codes, hook, 0, 0, &vdp
    };
    GensVdpConfig invalid = config;
    GensGenesisBusConfig invalid_bus = bus_config;
    GensVdp moved;
    GensZ80Registers zregs = {0x12345678, 0x7f04, 0xabcdef12};
    uint16_t value;
    uint32_t stolen, result = 0x77777777;

    assert(!gens_vdp_valid(&vdp));
    assert(!gens_genesis_bus_init(&bus, &bus_config));
    invalid.control_count = 6;
    assert(!gens_vdp_init(&vdp, &invalid));
    invalid = config;
    invalid.registers.dma_address = &registers[24];
    assert(!gens_vdp_init(&vdp, &invalid));
    assert(gens_vdp_init(&vdp, &config));
    assert(gens_vdp_valid(&vdp));
    assert(vdp.memory.write == &vdp.write && vdp.write.register_opaque == &vdp.registers);
    moved = vdp;
    assert(!gens_vdp_valid(&moved));
    invalid = config;
    invalid.h_counter_size = 1023;
    assert(!gens_vdp_init(&vdp, &invalid));
    assert(gens_vdp_valid(&vdp));
    assert(gens_vdp_h_counter_init(counters, sizeof(counters)));
    invalid_bus.rom = ram;
    assert(!gens_genesis_bus_init(&bus, &invalid_bus));
    assert(gens_genesis_bus_init(&bus, &bus_config));

    cpu_interrupts = 0xab;
    write_word(0xc00004, 0x8c81);
    write_word(0xc00004, 0x8f02);
    write_word(0xc00004, 0x8150);
    assert(registers[12] == 0x81 && registers[15] == 2 && irq_calls == 0);
    assert(cpu_interrupts == 0xa0);
    pending_irq = 0x1234000c;
    write_word(0xc00004, 0x8010);
    assert(irq_calls == 1 && irq_level == 4);
    write_word(0xc00004, 0x8170);
    assert(irq_calls == 2 && irq_level == 6);
    assert(dimensions[0] == 40 && dimensions[2] == 320);
    write_word(0xc00004, 0x4001);
    write_word(0xc00004, 0);
    write_word(0xc00000, 0x1234);
    assert(vram[0] == 0x12 && vram[1] == 0x34 && control[4] == 3);
    write_word(0xc00004, 0);
    write_word(0xc00004, 0);
    assert(read_word(0xc00000) == 0x3412);

    rom_size = 2;
    rom[0] = 0x56; rom[1] = 0x78;
    visible_lines = 224;
    cpl = 488;
    write_word(0xc00004, 0x9301);
    write_word(0xc00004, 0x9400);
    live.cycle_counter = 1000;
    write_word(0xc00004, 0x4000);
    write_word(0xc00004, 0x0080);
    assert(vram[0] == 0x56 && vram[1] == 0x78 && live.cycle_counter == 946);
    assert(registers[24] == 0 && registers[25] == 1 && dma_length == 0);

    dma_length = 10;
    dma_type = 0;
    assert(gens_vdp_dma_tick(&vdp, &stolen));
    assert(stolen == 488 && dma_length == 1);
    assert(gens_vdp_dma_tick(&vdp, &stolen));
    assert(stolen == 54 && dma_length == 0);
    status = 0x12340080;
    assert(read_word(0xc00004) == 0x80 && status == 0x1234ff00);
    status = 0x12340080;
    assert(gens_vdp_window_read(&vdp, &bus.state, GENS_Z80_VDP_STATUS, 0x7f04, &value));
    assert(value == 0x80 && status == 0x1234ff00);
    status = 0x12340080;
    assert(gens_vdp_native_device_read(&vdp, &bus.state, GENS_Z80_NATIVE_VDP_STATUS, &zregs));
    assert(zregs.eax == 0x12340080 && status == 0x1234ff00);
    assert(zregs.ecx == 0x7f04 && zregs.edx == 0xabcdef12);
    cycles = cpl = 0;
    assert(read_word(0xc00008) == counters[257]);
    fail_odometer = 1;
    live.address = 0xc00008;
    assert(gens_m68k_read(&bus.cpu, &live, GENS_M68K_WORD, GENS_M68K_NORMAL, &result) ==
           GENS_M68K_BUS_BACKEND_ERROR);
    assert(result == 0x77777777);
    fail_odometer = 0;

    assert(read_word(0xa11100) == 0x5a);
    live.address = 0xc00011;
    assert(gens_m68k_write(&bus.cpu, &live, GENS_M68K_BYTE, GENS_M68K_NORMAL, 0x12) ==
           GENS_M68K_BUS_OK);
    assert(generic_reads == 1 && generic_writes == 1 && fixes != 0 && hooks != 0);
    assert(!gens_vdp_device_read(&vdp, &bus.state, GENS_GENESIS_PSG, 0xc00011, 1, &value));
    return 0;
}
