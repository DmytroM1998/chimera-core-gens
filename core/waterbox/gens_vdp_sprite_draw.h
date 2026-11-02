#ifndef CHIMERA_GENS_VDP_SPRITE_DRAW_H
#define CHIMERA_GENS_VDP_SPRITE_DRAW_H

#include "gens_vdp_sprites.h"
#include "gens_vdp_pixels.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpSpriteDraw {
    GensVdpSprites *sprites;
    GensVdpSpriteLine *line;
    uint16_t *pixels;
    size_t pixel_count, row_offset;
    const uint8_t *enabled, *low_enabled, *high_enabled, *always_top, *swap_priority;
    uint32_t *cursor, *palette, *next_cell, *h_min, *h_max;
    unsigned int interlace, shadow;
} GensVdpSpriteDraw;

int gens_vdp_sprite_draw_valid(const GensVdpSpriteDraw *);
int gens_vdp_sprites_draw(GensVdpSpriteDraw *);

#ifdef __cplusplus
}
#endif

#endif
