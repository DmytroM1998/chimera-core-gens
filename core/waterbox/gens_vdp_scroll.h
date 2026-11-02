#ifndef CHIMERA_GENS_VDP_SCROLL_H
#define CHIMERA_GENS_VDP_SCROLL_H

#include "gens_vdp_sprites.h"
#include "gens_vdp_pixels.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpScrollB {
    const GensVdpSprites *memory;
    uint16_t *pixels;
    size_t pixel_count, row_offset;
    uint8_t *const *plane_address, *const *hscroll_address;
    const unsigned int *set3;
    const uint32_t *hcell, *hscroll_mask, *hscroll_cmul, *hscroll_cmask, *vscroll_cmask;
    const int *current_line;
    const uint8_t *enabled, *low_enabled, *high_enabled, *swap_priority;
    uint32_t *cursor, *cell, *line7;
    int *status;
    unsigned int interlace, shadow, cell_scroll;
} GensVdpScrollB;

int gens_vdp_scroll_b_valid(const GensVdpScrollB *);
int gens_vdp_scroll_b_draw(GensVdpScrollB *);

/* Indexes into the original Data_Misc scratch array used by Scroll A and window. */
enum {
    GENS_SCROLL_MISC_PATTERN_ADR = 0, GENS_SCROLL_MISC_LINE7 = 1, GENS_SCROLL_MISC_CELL = 3,
    GENS_SCROLL_MISC_START_A = 4, GENS_SCROLL_MISC_LENGTH_A = 5,
    GENS_SCROLL_MISC_START_W = 6, GENS_SCROLL_MISC_LENGTH_W = 7,
    GENS_SCROLL_MISC_MASK = 8, GENS_SCROLL_MISC_COUNT = 9
};

typedef struct GensVdpScrollA {
    const GensVdpSprites *memory;
    uint16_t *pixels;
    size_t pixel_count, row_offset;
    uint8_t *const *plane_address, *const *hscroll_address, *const *window_address;
    const unsigned int *set3, *reg17, *reg18;
    const uint32_t *hcell, *hscroll_mask, *hscroll_cmul, *hscroll_cmask, *vscroll_cmask;
    const uint32_t *vscroll_mmask, *window_x, *window_y, *window_mul;
    const int *current_line;
    const uint8_t *enabled, *low_enabled, *high_enabled, *swap_priority;
    /* Data_Misc. The Pattern_Adr slot receives a VRAM offset instead of a 32-bit address. */
    uint32_t *misc;
    int *status;
    unsigned int interlace, shadow, cell_scroll;
} GensVdpScrollA;

int gens_vdp_scroll_a_valid(const GensVdpScrollA *);
int gens_vdp_scroll_a_draw(GensVdpScrollA *);

#ifdef __cplusplus
}
#endif

#endif
