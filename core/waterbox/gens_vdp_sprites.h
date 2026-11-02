#ifndef CHIMERA_GENS_VDP_SPRITES_H
#define CHIMERA_GENS_VDP_SPRITES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpSprites {
    const uint8_t *vram, *cram, *vsram_over, *vsram, *h_counter;
    size_t vram_size, cram_size, vsram_over_size, vsram_size, h_counter_size;
    uint8_t **sprite_address;
    unsigned int *set2, *set4;
    int *dirty;
    uint32_t *table, *end;
    size_t table_dwords;
} GensVdpSprites;

typedef struct GensVdpSpriteLine {
    const uint32_t *table, *end, *h_cells, *h_pixels;
    size_t table_dwords;
    uint32_t *visible, *bound;
    size_t visible_dwords;
    const int *current_line, *limits;
    int *status;
} GensVdpSpriteLine;

int gens_vdp_sprites_valid(const GensVdpSprites *);
int gens_vdp_sprites_prepare(GensVdpSprites *);
int gens_vdp_sprites_read_word(const GensVdpSprites *, uint32_t, uint16_t *);
int gens_vdp_sprites_read_pattern(const GensVdpSprites *, uint32_t, uint32_t *);
int gens_vdp_sprite_line_valid(const GensVdpSpriteLine *);
int gens_vdp_sprites_select(GensVdpSpriteLine *);
int gens_vdp_sprites_prepare_line(GensVdpSprites *, GensVdpSpriteLine *);

#ifdef __cplusplus
}
#endif

#endif
