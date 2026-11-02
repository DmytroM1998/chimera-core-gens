#include "gens_vdp_write.h"

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

static void low_byte(int *target, uint8_t value)
{
    uint32_t result = (bits(target) & UINT32_C(0xffffff00)) | value;
    memcpy(target, &result, sizeof(result));
}

static void high_word(int *target, uint16_t value)
{
    uint32_t result = (bits(target) & UINT32_C(0x0000ffff)) | (uint32_t)value << 16;
    memcpy(target, &result, sizeof(result));
}

int gens_vdp_write_valid(const GensVdpWrite *vdp)
{
    return vdp != 0 && vdp->vram != 0 && vdp->cram != 0 && vdp->vsram != 0 &&
        vdp->vram_size >= 65536u && vdp->cram_size >= 256u && vdp->vsram_size >= 256u &&
        vdp->control_flag != 0 && vdp->control_data != 0 && vdp->control_access != 0 &&
        vdp->control_address != 0 && vdp->control_dma_mode != 0 &&
        vdp->control_dma != 0 && vdp->status != 0 &&
        vdp->dma_length != 0 && vdp->dma_address != 0 &&
        vdp->dmat_length != 0 && vdp->dmat_type != 0 &&
        vdp->set2 != 0 && vdp->auto_increment != 0 &&
        vdp->vram_flag != 0 && vdp->cram_flag != 0;
}

static const uint16_t control_decode[64] = {
    0x0005, 0x0009, 0x0000, 0x000a, 0x0007, 0x000b, 0x0000, 0x0000,
    0x0006, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0005, 0x0009, 0x0000, 0x000a, 0x0007, 0x000b, 0x0000, 0x0000,
    0x0006, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0005, 0x0509, 0x0000, 0x020a, 0x0007, 0x030b, 0x0000, 0x0000,
    0x0006, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0800, 0x0100, 0x0000, 0x0200, 0x0000, 0x0300, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

static int copy_vram(GensVdpWrite *vdp, uint32_t length)
{
    uint32_t source = bits(vdp->dma_address) & UINT32_C(0xffff);
    uint32_t address = bits(vdp->control_address) & UINT32_C(0xffff);
    uint32_t increment = *vdp->auto_increment;
    uint32_t zero = 0, type = 3, dirty = 1;
    uint8_t data;

    low_word(vdp->status, (uint16_t)(bits(vdp->status) | 2u));
    memcpy(vdp->dma_length, &zero, sizeof(zero));
    memcpy(vdp->dmat_length, &length, sizeof(length));
    memcpy(vdp->dmat_type, &type, sizeof(type));
    memcpy(vdp->vram_flag, &dirty, sizeof(dirty));
    do
    {
        data = vdp->vram[source];
        source = (source + 1u) & UINT32_C(0xffff);
        vdp->vram[address] = data;
        address = (address + increment) & UINT32_C(0xffff);
    } while (--length != 0u);
    memcpy(vdp->dma_address, &source, sizeof(source));
    low_word(vdp->control_address, (uint16_t)address);
    return 1;
}

int gens_vdp_control_write(GensVdpWrite *vdp, GensM68kBusState *state, uint16_t data)
{
    uint32_t first, second, selector, decoded, address, dma, mode, length;

    if (!gens_vdp_write_valid(vdp) || state == 0)
        return 0;

    if ((bits(vdp->control_flag) & 1u) == 0u)
    {
        first = data;
        if ((data & UINT16_C(0xc000)) == UINT16_C(0x8000))
        {
            uint32_t zero = 0, five = 5;
            unsigned int reg = (data >> 8) & 0x1fu;
            memcpy(vdp->control_access, &five, sizeof(five));
            memcpy(vdp->control_address, &zero, sizeof(zero));
            return vdp->set_register != 0 &&
                vdp->set_register(vdp->register_opaque, state, reg, (uint8_t)data);
        }

        second = (bits(vdp->control_data) >> 16) & UINT32_C(0xffff);
        low_word(vdp->control_data, (uint16_t)first);
        low_byte(vdp->control_flag, 1);
        address = ((second << 14) | (first & UINT32_C(0x3fff))) & UINT32_C(0xffff);
        selector = ((second & UINT32_C(0xf0)) | ((first & UINT32_C(0xc000)) >> 12));
        low_word(vdp->control_address, (uint16_t)address);
        low_byte(vdp->control_access, (uint8_t)control_decode[selector >> 2]);
        return 1;
    }

    first = bits(vdp->control_data) & UINT32_C(0xffff);
    second = data;
    high_word(vdp->control_data, (uint16_t)second);
    address = ((second << 14) | (first & UINT32_C(0x3fff))) & UINT32_C(0xffff);
    selector = ((second & UINT32_C(0xf0)) | ((first & UINT32_C(0xc000)) >> 12));
    decoded = control_decode[selector >> 2];
    low_word(vdp->control_address, (uint16_t)address);
    low_byte(vdp->control_flag, 0);
    low_byte(vdp->control_access, (uint8_t)decoded);
    dma = (decoded >> 8) & 0xffu;
    if (dma == 0u)
        return 1;

    if ((*vdp->set2 & 0x10u) == 0u)
    {
        uint32_t zero = 0;
        memcpy(vdp->control_dma, &zero, sizeof(zero));
        return 1;
    }

    mode = bits(vdp->control_dma_mode);
    if ((dma & 4u) != 0u && (mode & 255u) == UINT32_C(0x80))
    {
        low_byte(vdp->control_dma, (uint8_t)dma);
        return 1;
    }

    length = bits(vdp->dma_length) & UINT32_C(0xffff);
    if (length == 0u)
    {
        uint32_t zero = 0;
        memcpy(vdp->control_dma, &zero, sizeof(zero));
        return 1;
    }

    if ((mode & 255u) == UINT32_C(0xc0))
        return copy_vram(vdp, length);

    if ((mode & UINT32_C(0x80)) != 0u)
    {
        uint32_t zero = 0;
        memcpy(vdp->control_dma, &zero, sizeof(zero));
        return 1;
    }

    if ((dma & 3u) == 0u)
    {
        uint32_t zero = 0;
        low_word(vdp->status, (uint16_t)(bits(vdp->status) | 2u));
        memcpy(vdp->control_dma, &zero, sizeof(zero));
        return 1;
    }
    return vdp->run_dma != 0 &&
        vdp->run_dma(vdp->opaque, state, dma, mode, address);
}

static int fill(GensVdpWrite *vdp, uint16_t data)
{
    uint32_t address, length, increment;
    uint32_t zero = 0, type = 2;
    uint8_t low = (uint8_t)data, high = (uint8_t)(data >> 8);

    address = bits(vdp->control_address) & UINT32_C(0xffff);
    length = bits(vdp->dma_length) & UINT32_C(0xffff);
    increment = *vdp->auto_increment;
    memcpy(vdp->dma_length, &zero, sizeof(zero));
    memcpy(vdp->control_dma, &zero, sizeof(zero));
    address ^= 1u;
    low_byte(vdp->vram_flag, 1);
    vdp->vram[address] = low;
    address ^= 1u;
    low_word(vdp->status, (uint16_t)((uint16_t)bits(vdp->status) | 2u));
    memcpy(vdp->dmat_type, &type, sizeof(type));
    if (length == 0)
        length = UINT32_C(0xffff);
    memcpy(vdp->dmat_length, &length, sizeof(length));

    for (;;)
    {
        vdp->vram[address] = high;
        address = (address + increment) & UINT32_C(0xffff);
        --length;
        if ((length & UINT32_C(0x80000000)) != 0u)
            break;
    }
    low_word(vdp->control_address, (uint16_t)address);
    return 1;
}

int gens_vdp_data_write(GensVdpWrite *vdp, unsigned int size, uint16_t value)
{
    uint32_t access, address, offset;
    uint16_t data;

    if (!gens_vdp_write_valid(vdp) || (size != 1u && size != 2u))
        return 0;

    if (size == 1u)
        value = (uint16_t)((value & 0xffu) * UINT16_C(0x101));
    low_byte(vdp->control_flag, 0);
    if ((bits(vdp->control_dma) & 4u) != 0u)
        return fill(vdp, value);

    access = bits(vdp->control_access);
    address = bits(vdp->control_address);
    if (access >= 16u)
        return 0;

    data = value;
    if (access == 9u)
    {
        if (address > UINT32_C(0xffff))
            return 0;
        offset = address & UINT32_C(0xfffe);
        if ((address & 1u) != 0u)
            data = (uint16_t)((value << 8) | (value >> 8));
        vdp->vram[offset] = (uint8_t)data;
        vdp->vram[offset + 1u] = (uint8_t)(data >> 8);
        low_byte(vdp->vram_flag, 1);
    }
    else if (access == 10u || access == 11u)
    {
        offset = address & UINT32_C(0x7e);
        if (access == 10u)
        {
            vdp->cram[offset] = (uint8_t)data;
            vdp->cram[offset + 1u] = (uint8_t)(data >> 8);
            low_byte(vdp->cram_flag, 1);
        }
        else
        {
            vdp->vsram[offset] = (uint8_t)data;
            vdp->vsram[offset + 1u] = (uint8_t)(data >> 8);
        }
    }
    else
    {
        /* Read and invalid access codes go to the original's error label, which ignores the
         * write. Ctrl.Flag is already cleared and the address does not advance. */
        return 1;
    }

    low_word(vdp->control_address,
             (uint16_t)((uint16_t)address + *vdp->auto_increment));
    return 1;
}

int gens_vdp_genesis_write(void *opaque, GensM68kBusState *state,
                           enum GensGenesisDevice device, uint32_t address,
                           unsigned int size, uint16_t value)
{
    (void)state;
    if (device != GENS_GENESIS_VDP || address > UINT32_C(0xffffff) ||
        address > UINT32_C(0xc00007))
        return 0;
    if (address > UINT32_C(0xc00003))
        return size == 2u ?
            gens_vdp_control_write((GensVdpWrite *)opaque, state, value) : 0;
    return gens_vdp_data_write((GensVdpWrite *)opaque, size, value);
}
