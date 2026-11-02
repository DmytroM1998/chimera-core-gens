#ifndef CHIMERA_GENS_VDP_REGISTERS_H
#define CHIMERA_GENS_VDP_REGISTERS_H

#include "gens_m68k_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpRegisters {
    uint32_t *registers;
    size_t register_count;
    uint8_t *vram;
    size_t vram_size;
    uint8_t *vram_flag, *cram_flag, *vscroll_mmask;
    uint8_t **scr_a_addr, **scr_b_addr, **window_addr, **sprite_addr, **hscroll_addr;
    uint32_t *hcell, *hwin_mul, *hpix, *hpix_begin;
    uint32_t *hscroll_mask, *hscroll_cmul, *hscroll_cmask, *vscroll_cmask;
    uint32_t *window_x, *window_y;
    uint32_t *dma_length, *dma_address, *control_dma_mode;
    void *opaque;
    int (*update_irq)(void *, GensM68kBusState *);
} GensVdpRegisters;

int gens_vdp_registers_valid(const GensVdpRegisters *);
int gens_vdp_set_register(GensVdpRegisters *, GensM68kBusState *,
                          unsigned int, uint8_t);
int gens_vdp_register_callback(void *, GensM68kBusState *, unsigned int, uint8_t);

#ifdef __cplusplus
}
#endif

#endif
