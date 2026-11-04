/* Genesis dispatch follows Mem_M68k.asm from the pinned Gens-Rerecording tree.
 * The upstream component terms apply to derived portions.
 */
#include "gens_genesis_bus.h"
#include "gens_vdp.h"

#include <string.h>

#include "gens_io.h"
#include "io.h"

_Static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4,
               "Gens machine globals require 32-bit int");

static uint32_t bits(const int *value)
{
    uint32_t result;
    memcpy(&result, value, sizeof(result));
    return result;
}

static void low_byte(int *target, uint8_t value)
{
    uint32_t result = (bits(target) & UINT32_C(0xffffff00)) | value;
    memcpy(target, &result, sizeof(result));
}

static void low_controller_byte(unsigned int *target, uint8_t value)
{
    *target = (*target & UINT32_C(0xffffff00)) | value;
}

static void low_controller_word(unsigned int *target, uint16_t value)
{
    *target = (*target & UINT32_C(0xffff0000)) | value;
}

static int device_read(GensGenesisBus *bus, GensM68kBusState *state,
                       enum GensGenesisDevice device, uint32_t address,
                       unsigned int size, uint16_t *value)
{
    if (device == GENS_GENESIS_VDP && bus->config.vdp != 0)
        return gens_vdp_device_read(bus->config.vdp, state, device, address, size, value);
    if (bus->config.device_read == 0)
        return 0;
    return bus->config.device_read(bus->config.opaque, state, device, address, size, value);
}

static int device_write(GensGenesisBus *bus, GensM68kBusState *state,
                        enum GensGenesisDevice device, uint32_t address,
                        unsigned int size, uint16_t value)
{
    if (device == GENS_GENESIS_VDP && bus->config.vdp != 0)
        return gens_vdp_device_write(bus->config.vdp, state, device, address, size, value);
    if (bus->config.device_write == 0)
        return 0;
    return bus->config.device_write(bus->config.opaque, state, device, address, size, value);
}

static int read_misc(GensGenesisBus *bus, GensM68kBusState *state,
                     uint32_t address, unsigned int size, uint16_t *value)
{
    if (address <= UINT32_C(0xa0ffff))
    {
        if ((bits(bus->config.z80_state) & 6u) != 0u)
        {
            *value = 0;
            return 1;
        }
        if (bus->config.z80_memory != 0)
            return gens_z80_window_read(bus->config.z80_memory, state, address, size, value);
        return device_read(bus, state, GENS_GENESIS_Z80_MEMORY,
                           address & UINT32_C(0x7fff), size, value);
    }
    if (address == UINT32_C(0xa11100))
    {
        if (bus->config.z80_control != 0)
            return gens_z80_request_read(bus->config.z80_control, state, size, value);
        return device_read(bus, state, GENS_GENESIS_Z80_BUS_REQUEST, address, size, value);
    }
    if (address > UINT32_C(0xa1000d))
    {
        *value = 0;
        return 1;
    }
    switch (address & 0x0eu)
    {
    case 0:
        *value = (uint16_t)(((bits(bus->config.game_mode) * 2u |
                              bits(bus->config.cpu_mode)) << 6) |
                            bits(bus->config.gen_version));
        break;
    case 2: *value = gens_io_read_word_1(); break;
    case 4: *value = gens_io_read_word_2(); break;
    case 6: *value = size == 1u ? UINT16_C(0xff) : UINT16_C(0xff00); break;
    case 8: *value = (uint16_t)Controller_1_COM; break;
    case 10: *value = (uint16_t)Controller_2_COM; break;
    default: *value = 0; break;
    }
    return 1;
}

static int read_memory(GensGenesisBus *bus, GensM68kBusState *state,
                       uint32_t address, unsigned int size, uint16_t *value)
{
    unsigned int handler = bus->read_map[(address >> 19) & 31u];
    uint32_t offset;

    if (handler < 12u)
    {
        if (handler == 4u && (bits(bus->config.sram_on) & 1u) != 0u &&
            address >= bits(bus->config.sram_start) && address <= bits(bus->config.sram_end))
        {
            if ((bits(bus->config.sram_custom) & 1u) != 0u)
            {
                *value = 0;
                return 1;
            }
            offset = address - bits(bus->config.sram_start);
            if (offset >= bus->config.sram_size || size > bus->config.sram_size - offset)
                return 0;
            *value = size == 1u ? bus->config.sram[offset] :
                (uint16_t)((uint16_t)bus->config.sram[offset] << 8 |
                           bus->config.sram[offset + 1u]);
            return 1;
        }
        offset = handler * UINT32_C(0x80000) + (address & UINT32_C(0x7ffff));
        if (size == 1u)
            offset ^= 1u;
        if (offset >= bus->config.rom_size || size > bus->config.rom_size - offset)
            return 0;
        *value = size == 1u ? bus->config.rom[offset] :
            (uint16_t)(bus->config.rom[offset] | (uint16_t)bus->config.rom[offset + 1u] << 8);
        return 1;
    }
    if (handler == 20u)
        return read_misc(bus, state, address, size, value);
    if (handler == 24u)
    {
        if ((size == 1u && address >= UINT32_C(0xc00004) && address <= UINT32_C(0xc00009)) ||
            (size == 2u && address <= UINT32_C(0xc00009)))
            return device_read(bus, state, GENS_GENESIS_VDP, address, size, value);
    }
    else if (handler >= 28u)
    {
        offset = address & UINT32_C(0xffff);
        if (size == 1u)
            offset ^= 1u;
        if (offset >= bus->config.ram_size || size > bus->config.ram_size - offset)
            return 0;
        *value = size == 1u ? bus->config.ram[offset] :
            (uint16_t)(bus->config.ram[offset] | (uint16_t)bus->config.ram[offset + 1u] << 8);
        return 1;
    }
    *value = 0;
    return 1;
}

static int write_misc(GensGenesisBus *bus, GensM68kBusState *state,
                      uint32_t address, unsigned int size, uint16_t value)
{
    if (address <= UINT32_C(0xa0ffff))
    {
        if ((bits(bus->config.z80_state) & 6u) != 0u)
            return 1;
        if (bus->config.z80_memory != 0)
            return gens_z80_window_write(bus->config.z80_memory, state, address, size, value);
        return device_write(bus, state, GENS_GENESIS_Z80_MEMORY,
                            address & UINT32_C(0x7fff), size, (uint8_t)value);
    }
    if (address == UINT32_C(0xa11100))
    {
        if (bus->config.z80_control != 0)
            return gens_z80_request_write(bus->config.z80_control, state, size, value);
        return device_write(bus, state, GENS_GENESIS_Z80_BUS_REQUEST, address, size, value);
    }
    if (address == UINT32_C(0xa11200))
    {
        if (bus->config.z80_control != 0)
            return gens_z80_reset_write(bus->config.z80_control, state, size, value);
        return device_write(bus, state, GENS_GENESIS_Z80_RESET, address, size, value);
    }
    if (address <= UINT32_C(0xa1000d))
    {
        switch (address & 0x0eu)
        {
        case 2: WR_Controller_1((unsigned char)value); break;
        case 4: WR_Controller_2((unsigned char)value); break;
        case 8:
            if (size == 1u) low_controller_byte(&Controller_1_COM, (uint8_t)value);
            else low_controller_word(&Controller_1_COM, value);
            break;
        case 10:
            if (size == 1u) low_controller_byte(&Controller_2_COM, (uint8_t)value);
            else low_controller_word(&Controller_2_COM, value);
            break;
        default: break;
        }
        return 1;
    }
    if (address == (size == 1u ? UINT32_C(0xa130f1) : UINT32_C(0xa130f0)))
    {
        low_byte(bus->config.sram_on, (uint8_t)((value & 1u) != 0u));
        low_byte(bus->config.sram_write, (uint8_t)((value & 2u) == 0u));
        return 1;
    }
    if (address >= (size == 1u ? UINT32_C(0xa130f1) : UINT32_C(0xa130f0)) &&
        address <= UINT32_C(0xa130ff))
    {
        unsigned int selected = size == 1u ? value : value >> 8;
        bus->read_map[(address & 15u) >> 1] = (uint8_t)(selected & 31u);
    }
    return 1;
}

static int write_memory(GensGenesisBus *bus, GensM68kBusState *state,
                        uint32_t address, unsigned int size, uint16_t value)
{
    uint32_t region = address >> 20;
    uint32_t offset;
    if (region < 10u)
    {
        if ((bits(bus->config.sram_on) & 1u) == 0u ||
            (bits(bus->config.sram_write) & 1u) == 0u ||
            address < bits(bus->config.sram_start) || address > bits(bus->config.sram_end))
            return 1;
        offset = address - bits(bus->config.sram_start);
        if (offset >= bus->config.sram_size || size > bus->config.sram_size - offset)
            return 0;
        if (size == 1u)
            bus->config.sram[offset] = (uint8_t)value;
        else
        {
            bus->config.sram[offset] = (uint8_t)(value >> 8);
            bus->config.sram[offset + 1u] = (uint8_t)value;
        }
    }
    else if (region == 10u)
        return write_misc(bus, state, address, size, value);
    else if (region == 12u)
    {
        if (address <= UINT32_C(0xc00003) ||
            (size == 2u && address <= UINT32_C(0xc00007)))
            return device_write(bus, state, GENS_GENESIS_VDP, address, size, value);
        if (size == 1u && address == UINT32_C(0xc00011))
            return device_write(bus, state, GENS_GENESIS_PSG, address, size, value);
    }
    else if (region >= 14u && region <= 15u)
    {
        offset = address & UINT32_C(0xffff);
        if (size == 1u)
            offset ^= 1u;
        if (offset >= bus->config.ram_size || size > bus->config.ram_size - offset)
            return 0;
        bus->config.ram[offset] = (uint8_t)value;
        if (size == 2u)
            bus->config.ram[offset + 1u] = (uint8_t)(value >> 8);
    }
    return 1;
}

static int read_byte(void *opaque, GensM68kBusState *state, uint32_t address, uint8_t *value)
{
    uint16_t result;
    if (!read_memory((GensGenesisBus *)opaque, state, address, 1u, &result))
        return 0;
    *value = (uint8_t)result;
    return 1;
}

static uint8_t *debug_byte(const GensGenesisBus *bus, uint32_t address, int writing)
{
    unsigned int handler;
    uint32_t offset;
    if (address >= UINT32_C(0x1000000))
        return 0;
    handler = bus->read_map[address >> 19];
    if (handler < 12u)
    {
        if (handler == 4u && (bits(bus->config.sram_on) & 1u) != 0u &&
            address >= bits(bus->config.sram_start) && address <= bits(bus->config.sram_end))
        {
            offset = address - bits(bus->config.sram_start);
            return (bits(bus->config.sram_custom) & 1u) == 0u && offset < bus->config.sram_size ?
                bus->config.sram + offset : 0;
        }
        offset = (handler * UINT32_C(0x80000) + (address & UINT32_C(0x7ffff))) ^ 1u;
        return !writing && offset < bus->config.rom_size ? bus->config.rom + offset : 0;
    }
    if (handler >= 28u)
    {
        offset = (address & UINT32_C(0xffff)) ^ 1u;
        return offset < bus->config.ram_size ? bus->config.ram + offset : 0;
    }
    if (handler == 20u && address >= UINT32_C(0xa00000) &&
        address < UINT32_C(0xa04000) && bus->config.z80_memory != 0)
    {
        offset = address & UINT32_C(0x1fff);
        return offset < bus->config.z80_memory->ram_size ?
            bus->config.z80_memory->ram + offset : 0;
    }
    return 0;
}

uint8_t gens_genesis_bus_peek(const GensGenesisBus *bus, uint32_t address)
{
    const uint8_t *byte = debug_byte(bus, address, 0);
    return byte != 0 ? *byte : 0;
}

void gens_genesis_bus_poke(GensGenesisBus *bus, uint32_t address, uint8_t value)
{
    uint8_t *byte = debug_byte(bus, address, 1);
    if (byte != 0)
        *byte = value;
}

static int read_word(void *opaque, GensM68kBusState *state, uint32_t address, uint16_t *value)
{
    return read_memory((GensGenesisBus *)opaque, state, address, 2u, value);
}

static int write_byte(void *opaque, GensM68kBusState *state, uint32_t address, uint8_t value)
{
    GensGenesisBus *bus = (GensGenesisBus *)opaque;
    if (bus->config.fix_codes == 0 || !write_memory(bus, state, address, 1u, value))
        return 0;
    return bus->config.fix_codes(bus->config.opaque, state, address, 1u);
}

static int write_word(void *opaque, GensM68kBusState *state, uint32_t address, uint16_t value)
{
    GensGenesisBus *bus = (GensGenesisBus *)opaque;
    if (bus->config.fix_codes == 0 || !write_memory(bus, state, address, 2u, value))
        return 0;
    return bus->config.fix_codes(bus->config.opaque, state, address, 2u);
}

static void hook(void *opaque, GensM68kBusState *state, enum GensM68kBusHook kind)
{
    GensGenesisBus *bus = (GensGenesisBus *)opaque;
    bus->config.hook(bus->config.opaque, state, kind);
}

void gens_genesis_bus_reset_mapping(GensGenesisBus *bus)
{
    unsigned int i;
    for (i = 0; i < 32u; ++i)
        bus->read_map[i] = (uint8_t)i;
}

int gens_genesis_bus_init(GensGenesisBus *bus, const GensGenesisBusConfig *config)
{
    GensGenesisBusConfig copy;
    if (bus == 0 || config == 0 || config->ram == 0 || config->ram_size < 65536u ||
        config->rom == 0 || config->rom_size < 6u * 1024u * 1024u ||
        config->sram == 0 || config->sram_size < 65536u ||
        config->sram_start == 0 || config->sram_end == 0 ||
        config->sram_on == 0 || config->sram_write == 0 || config->sram_custom == 0 ||
        config->z80_state == 0 || config->game_mode == 0 || config->cpu_mode == 0 ||
        config->gen_version == 0 || config->fix_codes == 0 || config->hook == 0)
        return 0;
    if (config->z80_control != 0 &&
        (!gens_z80_control_valid(config->z80_control) ||
         config->z80_control->z80_state != config->z80_state))
        return 0;
    if (config->z80_memory != 0 && !gens_z80_memory_valid(config->z80_memory))
        return 0;
    if (config->vdp != 0 &&
        (!gens_vdp_valid(config->vdp) || config->vdp->memory.ram != config->ram ||
         config->vdp->memory.rom != config->rom ||
         config->vdp->memory.rom_backing_size < 6u * 1024u * 1024u))
        return 0;
    copy = *config;
    copy.ram_size = 65536u;
    copy.rom_size = 6u * 1024u * 1024u;
    copy.sram_size = 65536u;
    memset(bus, 0, sizeof(*bus));
    bus->config = copy;
    bus->cpu.ram = copy.ram;
    bus->cpu.ram_size = copy.ram_size;
    bus->cpu.opaque = bus;
    bus->cpu.state = &bus->state;
    bus->cpu.read_byte = read_byte;
    bus->cpu.read_word = read_word;
    bus->cpu.write_byte = write_byte;
    bus->cpu.write_word = write_word;
    bus->cpu.hook = hook;
    gens_genesis_bus_reset_mapping(bus);
    return 1;
}
