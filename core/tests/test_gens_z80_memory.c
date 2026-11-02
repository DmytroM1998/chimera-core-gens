#include <assert.h>
#include <stdint.h>

#include "gens_z80_memory.h"

static uint8_t ram[8192];
static int bank;
static unsigned int calls, fail;
static enum GensZ80Device last_device;
static uint32_t last_address;
static uint8_t last_value;

static int read_device(void *opaque, GensM68kBusState *state,
                       enum GensZ80Device device, uint32_t address, uint16_t *value)
{
    (void)opaque;
    ++calls;
    ++state->io_cycle_counter;
    last_device = device;
    last_address = address;
    *value = device == GENS_Z80_VDP_STATUS ? 0xa5bc : 0x239a;
    return !fail;
}

static int write_device(void *opaque, GensM68kBusState *state,
                        enum GensZ80Device device, uint32_t address, uint8_t value)
{
    (void)opaque;
    ++calls;
    ++state->io_cycle_counter;
    last_device = device;
    last_address = address;
    last_value = value;
    return !fail;
}

int main(void)
{
    GensM68kBusState state = {0};
    GensZ80Memory memory = {ram, sizeof(ram), &bank, 0, read_device, write_device};
    GensZ80Memory invalid = memory;
    uint16_t value = 0x5555;
    unsigned int i;
    invalid.ram_size = 8191;
    assert(!gens_z80_memory_valid(&invalid));
    assert(!gens_z80_window_read(&invalid, &state, 0xa00000, 2, &value));
    assert(value == 0x5555 && calls == 0);
    assert(!gens_z80_window_write(&memory, &state, 0xa00000, 4, 0));
    assert(!gens_z80_window_read(&memory, &state, 0x1000000, 1, &value));
    assert(gens_z80_window_write(&memory, &state, 0xa02012, 2, 0xabcd));
    assert(ram[0x12] == 0xcd && ram[0x13] == 0);
    assert(gens_z80_window_read(&memory, &state, 0xa00012, 1, &value) && value == 0xcd);
    assert(gens_z80_window_read(&memory, &state, 0xa08012, 2, &value) && value == 0xcd);
    assert(gens_z80_window_read(&memory, &state, 0x080012, 2, &value) && value == 0xcd);
    assert(gens_z80_window_read(&memory, &state, 0xa06100, 2, &value) && value == 0);
    assert(gens_z80_window_read(&memory, &state, 0xa07fff, 2, &value) && value == 0);
    bank = 0xff8000;
    for (i = 0; i < 9; ++i)
        assert(gens_z80_window_write(&memory, &state, 0xa060ff, 1, 0));
    assert(bank == 0);
    for (i = 0; i < 9; ++i)
        assert(gens_z80_window_write(&memory, &state, 0xa06000, 2, 1));
    assert(bank == 0xff8000);
    assert(gens_z80_window_write(&memory, &state, 0xa06100, 1, 0));
    assert(bank == 0xff8000 && calls == 0);
    assert(gens_z80_window_read(&memory, &state, 0xa04003, 2, &value) && value == 0x239a);
    assert(last_device == GENS_Z80_YM2612 && last_address == 3 && calls == 1);
    assert(gens_z80_window_write(&memory, &state, 0xa05fff, 2, 0xabcd));
    assert(last_device == GENS_Z80_YM2612 && last_address == 3 && last_value == 0xcd);
    assert(gens_z80_window_read(&memory, &state, 0xa07f04, 2, &value) && value == 0xa5a5);
    assert(gens_z80_window_read(&memory, &state, 0xa07f05, 2, &value) && value == 0xa5bc);
    assert(gens_z80_window_read(&memory, &state, 0xa07f04, 1, &value) && value == 0xa5);
    assert(gens_z80_window_read(&memory, &state, 0xa07f08, 2, &value) && value == 0x239a);
    assert(last_device == GENS_Z80_V_COUNTER);
    assert(gens_z80_window_read(&memory, &state, 0xa07f09, 1, &value) && value == 0x9a);
    assert(last_device == GENS_Z80_H_COUNTER);
    assert(gens_z80_window_write(&memory, &state, 0xa07000, 2, 0xabcd));
    assert(last_device == GENS_Z80_VDP_DATA && last_value == 0xcd);
    assert(gens_z80_window_write(&memory, &state, 0xa07f11, 1, 0x76));
    assert(last_device == GENS_Z80_PSG && last_value == 0x76);
    memory.read_device = 0;
    value = 0x5555;
    assert(!gens_z80_window_read(&memory, &state, 0xa04000, 1, &value));
    assert(value == 0x5555);
    memory.read_device = read_device;
    fail = 1;
    assert(!gens_z80_window_read(&memory, &state, 0xa07f04, 2, &value));
    assert(value == 0x5555);
    assert(!gens_z80_window_write(&memory, &state, 0xa04000, 1, 0x12));
    assert(last_value == 0x12);
    memory.write_device = 0;
    assert(!gens_z80_window_write(&memory, &state, 0xa07f11, 1, 0x12));
    assert(gens_z80_window_write(&memory, &state, 0xa07f12, 1, 0x12));
    assert(state.io_cycle_counter == calls);
    return 0;
}
