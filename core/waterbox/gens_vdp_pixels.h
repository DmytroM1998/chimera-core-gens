#ifndef CHIMERA_GENS_VDP_PIXELS_H
#define CHIMERA_GENS_VDP_PIXELS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum GensVdpPixelLayer {
    GENS_VDP_PIXELS_SCROLL_B,
    GENS_VDP_PIXELS_SCROLL_A,
    GENS_VDP_PIXELS_SPRITE
};

typedef struct GensVdpPixelRow {
    uint16_t *pixels;
    size_t count, offset;
    uint32_t pattern, palette;
    enum GensVdpPixelLayer layer;
    unsigned int priority, shadow, flip;
    uint32_t enabled, priority_enabled;
    int *status;
} GensVdpPixelRow;

typedef struct GensVdpPixelResolve {
    uint16_t *pixels;
    size_t count, offset;
    const uint8_t *cram;
    size_t cram_size;
    const unsigned int *background;
    const int *h_pixel_begin;
} GensVdpPixelResolve;

int gens_vdp_pixel_row_valid(const GensVdpPixelRow *);
int gens_vdp_pixel_row(const GensVdpPixelRow *);
int gens_vdp_pixel_resolve_valid(const GensVdpPixelResolve *);
int gens_vdp_pixel_resolve(const GensVdpPixelResolve *);

#ifdef __cplusplus
}
#endif

#endif
