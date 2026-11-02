/* Timing and partial-width state updates follow the pinned Update_DMA.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_dma.h"

#include <string.h>

_Static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4, "Gens DMA global widths");

static const uint32_t timing[16] = {
    83, 167, 166, 83,
    102, 205, 204, 102,
    8, 16, 15, 8,
    9, 18, 17, 9
};

static uint32_t bits(const void *source)
{
    uint32_t value;
    memcpy(&value, source, sizeof(value));
    return value;
}

int gens_vdp_dma_timing_valid(const GensVdpDmaTiming *dma)
{
    return dma != 0 && dma->set2 != 0 && dma->set4 != 0 &&
        dma->current_line != 0 && dma->visible_lines != 0 &&
        dma->cycles_per_line != 0 && dma->length != 0 &&
        dma->type != 0 && dma->status != 0;
}

int gens_vdp_dma_update(GensVdpDmaTiming *dma, uint32_t *cycles)
{
    uint32_t type, index, capacity, length, remaining, result, status;

    if (!gens_vdp_dma_timing_valid(dma) || cycles == 0)
        return 0;

    type = bits(dma->type);
    index = (*dma->set4 & 1u) * 4u + (type & 3u);
    if (bits(dma->current_line) < bits(dma->visible_lines) &&
        (*dma->set2 & 0x40u) != 0u)
        index += 8u;
    capacity = timing[index];
    length = bits(dma->length);
    result = bits(dma->cycles_per_line);
    remaining = length - capacity;
    if (length <= capacity)
    {
        remaining = 0;
        result = (uint32_t)((uint32_t)(result << 16) / capacity * length) >> 16;
        status = bits(dma->status) & UINT32_C(0xfffffffd);
        memcpy(dma->status, &status, sizeof(status));
    }
    memcpy(dma->length, &remaining, sizeof(remaining));
    *cycles = (type & 2u) != 0u ? 0u : result;
    return 1;
}
