/* The 68000-facing Z80 window follows the pinned Mem_Z80.asm byte handlers.
 * The upstream component terms apply to derived portions.
 */
#include "gens_z80_memory.h"

#include <string.h>

_Static_assert(sizeof(int) == 4, "Gens machine globals require 32-bit int");

int gens_z80_memory_valid(const GensZ80Memory *memory)
{
    return memory != 0 && memory->ram != 0 && memory->ram_size >= 8192u &&
        memory->bank != 0;
}

int gens_z80_window_read(GensZ80Memory *memory, GensM68kBusState *state,
                         uint32_t address, unsigned int size, uint16_t *value)
{
    uint32_t offset;
    uint16_t result;
    enum GensZ80Device device;
    if (!gens_z80_memory_valid(memory) || state == 0 || value == 0 ||
        address > UINT32_C(0xffffff) || (size != 1u && size != 2u))
        return 0;
    offset = address & UINT32_C(0x7fff);
    result = 0;
    if (offset < UINT32_C(0x4000))
        result |= memory->ram[offset & UINT32_C(0x1fff)];
    else if (offset < UINT32_C(0x6000))
    {
        if (memory->read_device == 0 ||
            !memory->read_device(memory->opaque, state, GENS_Z80_YM2612, offset & 3u, &result))
            return 0;
    }
    else if (offset >= UINT32_C(0x7f04) && offset <= UINT32_C(0x7f09))
    {
        if (offset < UINT32_C(0x7f08))
            device = GENS_Z80_VDP_STATUS;
        else
            device = (offset & 1u) != 0u ? GENS_Z80_H_COUNTER : GENS_Z80_V_COUNTER;
        if (memory->read_device == 0 ||
            !memory->read_device(memory->opaque, state, device, offset, &result))
            return 0;
        if (device == GENS_Z80_VDP_STATUS && (offset & 1u) == 0u)
            result = (uint16_t)((result & UINT16_C(0xff00)) | (result >> 8));
    }
    *value = size == 1u ? (uint8_t)result : result;
    return 1;
}

int gens_z80_window_write(GensZ80Memory *memory, GensM68kBusState *state,
                          uint32_t address, unsigned int size, uint16_t value)
{
    uint32_t offset;
    enum GensZ80Device device;
    if (!gens_z80_memory_valid(memory) || state == 0 ||
        address > UINT32_C(0xffffff) || (size != 1u && size != 2u))
        return 0;
    offset = address & UINT32_C(0x7fff);
    if (offset < UINT32_C(0x4000))
    {
        memory->ram[offset & UINT32_C(0x1fff)] = (uint8_t)value;
        return 1;
    }
    if (offset < UINT32_C(0x6000))
    {
        device = GENS_Z80_YM2612;
        offset &= 3u;
    }
    else if (offset < UINT32_C(0x7000))
    {
        if (offset <= UINT32_C(0x60ff))
        {
            uint32_t bank;
            memcpy(&bank, memory->bank, sizeof(bank));
            bank = ((bank & UINT32_C(0xff0000)) >> 1) + ((uint32_t)(value & 1u) << 23);
            memcpy(memory->bank, &bank, sizeof(bank));
        }
        return 1;
    }
    else if (offset == UINT32_C(0x7f11))
        device = GENS_Z80_PSG;
    else if (offset <= UINT32_C(0x7f03))
        device = GENS_Z80_VDP_DATA;
    else
        return 1;
    if (memory->write_device == 0)
        return 0;
    return memory->write_device(memory->opaque, state, device, offset, (uint8_t)value);
}
