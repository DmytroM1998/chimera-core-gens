#ifndef CHIMERA_GENS_VDP_IRQ_H
#define CHIMERA_GENS_VDP_IRQ_H

#include "gens_m68k_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpIrq {
    unsigned int *set1, *set2;
    int *pending;
    uint8_t *cpu_interrupts;
    void *opaque;
    int (*interrupt)(void *, GensM68kBusState *, unsigned int, int);
} GensVdpIrq;

int gens_vdp_irq_valid(const GensVdpIrq *);
int gens_vdp_irq_update(void *, GensM68kBusState *);
int gens_vdp_irq_ack(GensVdpIrq *, uint32_t *);

#ifdef __cplusplus
}
#endif

#endif
