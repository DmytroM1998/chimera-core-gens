#ifndef CHIMERA_GENS_VDP_MEMORY_DMA_H
#define CHIMERA_GENS_VDP_MEMORY_DMA_H

#include "gens_vdp_dma.h"
#include "gens_vdp_write.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpMemoryDma {
    GensVdpWrite *write;
    GensVdpDmaTiming *timing;
    const uint8_t *rom, *ram;
    size_t rom_backing_size, ram_size;
    int *rom_size, *sega_cd_started;
} GensVdpMemoryDma;

int gens_vdp_memory_dma_valid(const GensVdpMemoryDma *);
int gens_vdp_memory_dma(void *, GensM68kBusState *, unsigned int, uint32_t, uint32_t);

#ifdef __cplusplus
}
#endif

#endif
