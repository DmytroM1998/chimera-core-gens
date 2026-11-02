#ifndef CHIMERA_GENS_VDP_RENDER_H
#define CHIMERA_GENS_VDP_RENDER_H

#include "gens_vdp_palette.h"
#include "gens_vdp_scroll.h"
#include "gens_vdp_sprite_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Orchestrates one Genesis Render_Line call from the already ported pieces. */
typedef struct GensVdpRender {
    GensVdpSprites *sprites;
    GensVdpScrollB *scroll_b;
    GensVdpScrollA *scroll_a;
    GensVdpSpriteDraw *sprite_draw;
    GensVdpPalette *palette;
    GensVdpPixelResolve *resolve;
    uint16_t *pixels;
    size_t pixel_count;
    const uint32_t *row_table;
    size_t row_table_count;
    const int *current_line;
} GensVdpRender;

int gens_vdp_render_valid(const GensVdpRender *);
int gens_vdp_render_line(GensVdpRender *);

#ifdef __cplusplus
}
#endif

#endif
