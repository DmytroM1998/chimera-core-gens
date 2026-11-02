#ifndef CHIMERA_GENS_VDP_WRITE_H
#define CHIMERA_GENS_VDP_WRITE_H

#include "gens_genesis_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpWrite {
    uint8_t *vram, *cram, *vsram;
    size_t vram_size, cram_size, vsram_size;
    int *control_flag, *control_data, *control_access, *control_address;
    int *control_dma_mode, *control_dma;
    int *status, *dma_length, *dma_address, *dmat_length, *dmat_type;
    unsigned int *set2, *auto_increment;
    int *vram_flag, *cram_flag;
    void *opaque;
    void *register_opaque;
    int (*set_register)(void *, GensM68kBusState *, unsigned int, uint8_t);
    int (*run_dma)(void *, GensM68kBusState *, unsigned int, uint32_t, uint32_t);
} GensVdpWrite;

int gens_vdp_write_valid(const GensVdpWrite *);
int gens_vdp_data_write(GensVdpWrite *, unsigned int, uint16_t);
int gens_vdp_control_write(GensVdpWrite *, GensM68kBusState *, uint16_t);
int gens_vdp_genesis_write(void *, GensM68kBusState *, enum GensGenesisDevice,
                           uint32_t, unsigned int, uint16_t);

#ifdef __cplusplus
}
#endif

#endif
