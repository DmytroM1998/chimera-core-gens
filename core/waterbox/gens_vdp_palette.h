#ifndef CHIMERA_GENS_VDP_PALETTE_H
#define CHIMERA_GENS_VDP_PALETTE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpPalette {
    const uint8_t *cram;
    size_t cram_size;
    const uint16_t *lookup16;
    const uint32_t *lookup32;
    size_t lookup16_count, lookup32_count;
    uint16_t *colors16;
    uint32_t *colors32;
    size_t colors16_count, colors32_count;
    const unsigned int *background;
    const int *mode555;
    const uint8_t *lock, *pink;
    int *dirty;
} GensVdpPalette;

int gens_vdp_palette_valid(const GensVdpPalette *);
int gens_vdp_palette_update(GensVdpPalette *);

#ifdef __cplusplus
}
#endif

#endif
