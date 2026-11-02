/* Genesis sources follow DMA_LOOP and MEM_To_V_RAM in the pinned vdp_io.asm.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_memory_dma.h"

#include <string.h>

_Static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4, "Gens DMA global widths");

static uint32_t bits(const void *source)
{
    uint32_t value;
    memcpy(&value, source, sizeof(value));
    return value;
}

static void store(int *target, uint32_t value)
{
    memcpy(target, &value, sizeof(value));
}

int gens_vdp_memory_dma_valid(const GensVdpMemoryDma *dma)
{
    return dma != 0 && gens_vdp_write_valid(dma->write) &&
        gens_vdp_dma_timing_valid(dma->timing) && dma->rom != 0 &&
        dma->ram != 0 && dma->ram_size >= 65536u && dma->rom_backing_size >= 2u &&
        dma->rom_size != 0 && dma->sega_cd_started != 0 &&
        dma->timing->set2 == dma->write->set2 &&
        dma->timing->length == dma->write->dmat_length &&
        dma->timing->type == dma->write->dmat_type &&
        dma->timing->status == dma->write->status;
}

int gens_vdp_memory_dma(void *opaque, GensM68kBusState *state,
                        unsigned int kind, uint32_t mode, uint32_t address)
{
    GensVdpMemoryDma *dma = opaque;
    GensVdpWrite *vdp;
    uint32_t source, initial_source, original_length, length, increment, offset, cycles;
    uint16_t data;
    const uint8_t *memory;
    int from_rom;

    kind &= 3u;
    if (!gens_vdp_memory_dma_valid(dma) || state == 0 || (mode & 0x80u) != 0u ||
        kind == 0u || address > UINT32_C(0xffff))
        return 0;
    vdp = dma->write;
    original_length = bits(vdp->dma_length);
    length = original_length & UINT32_C(0xffff);
    if (length == 0u)
        return 0;
    initial_source = bits(vdp->dma_address);
    source = initial_source * 2u;
    from_rom = source < bits(dma->rom_size);
    if (!from_rom && (bits(dma->sega_cd_started) & 255u) != 0u &&
        source < UINT32_C(0x240000))
        return 0;
    source &= from_rom ? UINT32_C(0x3ffffe) : UINT32_C(0xfffe);
    if (from_rom && (source > dma->rom_backing_size ||
                    length * 2u > dma->rom_backing_size - source))
        return 0;
    memory = from_rom ? dma->rom : dma->ram;
    increment = *vdp->auto_increment;
    if (kind != 1u)
        address &= 0x7fu;

    store(vdp->status, bits(vdp->status) | 2u);
    if (kind == 1u)
        store(vdp->vram_flag, 1);
    else if (kind == 2u)
        store(vdp->cram_flag, 1);
    store(vdp->dmat_type, (bits(vdp->dmat_type) & UINT32_C(0xffffff00)) |
                         (kind == 1u ? 0u : 1u));
    store(vdp->control_dma, 0);

    do
    {
        data = (uint16_t)(memory[source] | (uint16_t)memory[source + 1u] << 8);
        source = from_rom ? source + 2u : (source + 2u) & UINT32_C(0xffff);
        if (kind == 1u)
        {
            offset = address & UINT32_C(0xfffe);
            if ((address & 1u) != 0u)
                data = (uint16_t)((data << 8) | (data >> 8));
            vdp->vram[offset] = (uint8_t)data;
            vdp->vram[offset + 1u] = (uint8_t)(data >> 8);
        }
        else
        {
            uint8_t *destination = kind == 2u ? vdp->cram : vdp->vsram;
            offset = address & 0x7eu;
            destination[offset] = (uint8_t)data;
            destination[offset + 1u] = (uint8_t)(data >> 8);
        }
        address = (address + increment) & UINT32_C(0xffff);
    } while (--length != 0u);

    store(vdp->control_address, (bits(vdp->control_address) & UINT32_C(0xffff0000)) |
                               address);
    store(vdp->dma_length, 0);
    store(vdp->dmat_length, original_length);
    store(vdp->dma_address, (initial_source + original_length) & UINT32_C(0x7fffff));
    if (!gens_vdp_dma_update(dma->timing, &cycles))
        return 0;
    state->io_cycle_counter -= cycles;
    return 1;
}
