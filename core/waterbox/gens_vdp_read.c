/* Read-side state changes follow the pinned vdp_io.asm and memory dispatch.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_read.h"

#include <string.h>

_Static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4, "Gens VDP global widths");

static uint32_t bits(const void *source)
{
    uint32_t value;
    memcpy(&value, source, sizeof(value));
    return value;
}

static void low_word(int *target, uint16_t value)
{
    uint32_t result = (bits(target) & UINT32_C(0xffff0000)) | value;
    memcpy(target, &result, sizeof(result));
}

int gens_vdp_read_valid(const GensVdpRead *vdp)
{
    return vdp != 0 && vdp->vram != 0 && vdp->cram != 0 && vdp->vsram != 0 &&
        vdp->h_counter != 0 && vdp->vram_size >= 65536u && vdp->cram_size >= 256u &&
        vdp->vsram_size >= 256u && vdp->h_counter_size >= 1024u &&
        vdp->status != 0 && vdp->control_flag != 0 && vdp->control_access != 0 &&
        vdp->control_address != 0 && vdp->set2 != 0 && vdp->set4 != 0 &&
        vdp->auto_increment != 0 && vdp->cycles_m68k != 0 &&
        vdp->cpl_m68k != 0 && vdp->current_line != 0;
}

int gens_vdp_h_counter_init(uint8_t *table, size_t count)
{
    unsigned int i;
    if (table == 0 || count < 1024u)
        return 0;
    for (i = 0; i < 512u; ++i)
    {
        table[i * 2u] = (uint8_t)(i * 170u / 488u - 0x18u);
        table[i * 2u + 1u] = (uint8_t)(i * 205u / 488u - 0x1cu);
    }
    return 1;
}

int gens_vdp_data_read(GensVdpRead *vdp, GensM68kBusState *state, uint16_t *value)
{
    uint32_t flag, access, address, offset;
    const uint8_t *memory;
    if (!gens_vdp_read_valid(vdp) || state == 0 || value == 0)
        return 0;
    flag = bits(vdp->control_flag) & UINT32_C(0xffffff00);
    memcpy(vdp->control_flag, &flag, sizeof(flag));
    access = bits(vdp->control_access);
    if (access >= 16u)
        return 0;
    if (access != 5u && access != 6u && access != 7u)
    {
        *value = 0;
        return 1;
    }
    address = bits(vdp->control_address);
    offset = address & (access == 5u ? UINT32_C(0xfffe) : UINT32_C(0x7e));
    memory = access == 5u ? vdp->vram : (access == 6u ? vdp->cram : vdp->vsram);
    low_word(vdp->control_address, (uint16_t)(address + *vdp->auto_increment));
    *value = (uint16_t)(memory[offset] | (uint16_t)memory[offset + 1u] << 8);
    return 1;
}

int gens_vdp_status_read(GensVdpRead *vdp, GensM68kBusState *state, uint16_t *value)
{
    uint16_t original, next;
    if (!gens_vdp_read_valid(vdp) || state == 0 || value == 0)
        return 0;
    original = (uint16_t)bits(vdp->status);
    next = (uint16_t)((original ^ UINT16_C(0xff00)) & UINT16_C(0xff9f));
    if ((next & 8u) == 0u)
        next &= UINT16_C(0xff1f);
    low_word(vdp->status, next);
    *value = (uint16_t)(original | ((*vdp->set2 & 0x40u) != 0u ? 0u : 8u));
    return 1;
}

static int phase(GensVdpRead *vdp, GensM68kBusState *state, uint32_t *value)
{
    uint32_t now;
    if (vdp->read_odometer == 0 || !vdp->read_odometer(vdp->opaque, state, &now))
        return 0;
    *value = (now - (bits(vdp->cycles_m68k) - bits(vdp->cpl_m68k))) & 511u;
    return 1;
}

int gens_vdp_h_counter_read(GensVdpRead *vdp, GensM68kBusState *state, uint32_t *value)
{
    uint32_t index;
    if (!gens_vdp_read_valid(vdp) || state == 0 || value == 0 || !phase(vdp, state, &index))
        return 0;
    *value = vdp->h_counter[index * 2u + ((*vdp->set4 & 0x81u) != 0u)];
    return 1;
}

int gens_vdp_v_counter_read(GensVdpRead *vdp, GensM68kBusState *state, uint32_t *value)
{
    uint32_t index, result;
    unsigned int mode40, horizontal, advance, low;
    if (!gens_vdp_read_valid(vdp) || state == 0 || value == 0 || !phase(vdp, state, &index))
        return 0;
    mode40 = (*vdp->set4 & 0x81u) != 0u;
    horizontal = vdp->h_counter[index * 2u + mode40];
    advance = horizontal >= (mode40 ? 0xa4u : 0x84u) && horizontal <= 0xe0u;
    result = bits(vdp->current_line) + advance;
    if ((bits(vdp->status) & 1u) != 0u)
    {
        if (result >= 0x103u)
            result -= 56u;
    }
    else if (result >= 0xebu)
        result -= 6u;
    if ((*vdp->set4 & 2u) != 0u)
    {
        low = result & 255u;
        result = (result & UINT32_C(0xffffff00)) | ((low << 1 | low >> 7) & 255u);
    }
    *value = result & UINT32_C(0xffff00ff);
    return 1;
}

int gens_vdp_genesis_read(void *opaque, GensM68kBusState *state,
                          enum GensGenesisDevice device, uint32_t address,
                          unsigned int size, uint16_t *value)
{
    GensVdpRead *vdp = opaque;
    uint16_t status;
    uint32_t vertical, horizontal;
    if (device != GENS_GENESIS_VDP || !gens_vdp_read_valid(vdp) || state == 0 ||
        value == 0 || address > UINT32_C(0xffffff) || (size != 1u && size != 2u))
        return 0;
    if (size == 2u && address <= UINT32_C(0xc00003))
        return gens_vdp_data_read(vdp, state, value);
    if (address >= UINT32_C(0xc00004) && address <= UINT32_C(0xc00007))
    {
        if (!gens_vdp_status_read(vdp, state, &status))
            return 0;
        *value = size == 2u ? status : (uint8_t)((address & 1u) != 0u ? status : status >> 8);
        return 1;
    }
    if (address >= UINT32_C(0xc00008) && address <= UINT32_C(0xc00009))
    {
        if (size == 2u)
        {
            if (!gens_vdp_v_counter_read(vdp, state, &vertical) ||
                !gens_vdp_h_counter_read(vdp, state, &horizontal))
                return 0;
            *value = (uint16_t)((vertical & 255u) << 8 | (horizontal & 255u));
            return 1;
        }
        if ((address & 1u) != 0u)
        {
            if (!gens_vdp_h_counter_read(vdp, state, &horizontal))
                return 0;
            *value = (uint8_t)horizontal;
        }
        else
        {
            if (!gens_vdp_v_counter_read(vdp, state, &vertical))
                return 0;
            *value = (uint8_t)vertical;
        }
        return 1;
    }
    *value = 0;
    return 1;
}

int gens_vdp_z80_read(void *opaque, GensM68kBusState *state,
                      enum GensZ80Device device, uint32_t address, uint16_t *value)
{
    GensVdpRead *vdp = opaque;
    uint32_t counter;
    if (value == 0)
        return 0;
    if (device == GENS_Z80_VDP_STATUS && address >= 0x7f04u && address <= 0x7f07u)
        return gens_vdp_status_read(vdp, state, value);
    if (device == GENS_Z80_V_COUNTER && address == 0x7f08u)
    {
        if (!gens_vdp_v_counter_read(vdp, state, &counter))
            return 0;
    }
    else if (device == GENS_Z80_H_COUNTER && address == 0x7f09u)
    {
        if (!gens_vdp_h_counter_read(vdp, state, &counter))
            return 0;
    }
    else
        return 0;
    *value = (uint16_t)counter;
    return 1;
}

int gens_vdp_native_read(void *opaque, GensM68kBusState *state,
                         enum GensZ80NativeOperation operation, GensZ80Registers *registers)
{
    GensVdpRead *vdp = opaque;
    uint32_t counter;
    uint16_t status;
    if (registers == 0)
        return 0;
    if (operation == GENS_Z80_NATIVE_VDP_STATUS)
    {
        if (!gens_vdp_status_read(vdp, state, &status))
            return 0;
        registers->eax = (registers->eax & UINT32_C(0xffff0000)) | status;
        return 1;
    }
    if (operation == GENS_Z80_NATIVE_V_COUNTER)
    {
        if (!gens_vdp_v_counter_read(vdp, state, &counter))
            return 0;
    }
    else if (operation == GENS_Z80_NATIVE_H_COUNTER)
    {
        if (!gens_vdp_h_counter_read(vdp, state, &counter))
            return 0;
    }
    else
        return 0;
    registers->eax = counter;
    return 1;
}
