#ifndef CHIMERA_GENS_VDP_RESET_H
#define CHIMERA_GENS_VDP_RESET_H

#include "gens_vdp.h"
#include "gens_vdp_irq.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpReset {
    GensVdp *vdp;
    GensVdpIrq *irq;
    uint16_t *screen16, *palette16;
    size_t screen16_count, palette16_count;
    uint32_t *screen32, *palette32, *sprites, *visible, *vsram_over;
    size_t screen32_count, palette32_count, sprite_count, visible_count, vsram_over_count;
    uint8_t *palette_lock;
    int *dma_tmp;
} GensVdpReset;

int gens_vdp_reset_valid(const GensVdpReset *);
int gens_vdp_reset(GensVdpReset *, GensM68kBusState *);

#ifdef __cplusplus
}
#endif

#endif
