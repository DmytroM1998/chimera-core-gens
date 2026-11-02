#ifndef CHIMERA_GENS_VDP_DMA_H
#define CHIMERA_GENS_VDP_DMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpDmaTiming {
    unsigned int *set2, *set4;
    int *current_line, *visible_lines, *cycles_per_line;
    int *length, *type, *status;
} GensVdpDmaTiming;

int gens_vdp_dma_timing_valid(const GensVdpDmaTiming *);
int gens_vdp_dma_update(GensVdpDmaTiming *, uint32_t *);

#ifdef __cplusplus
}
#endif

#endif
