#ifndef CHIMERA_GENS_VDP_MACHINE_H
#define CHIMERA_GENS_VDP_MACHINE_H

#include "gens_color_lookup.h"
#include "gens_vdp.h"
#include "gens_vdp_irq.h"
#include "gens_vdp_render.h"
#include "gens_vdp_video.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GENS_VDP_MACHINE_SCREEN_PIXELS 80640u

/* Objects owned by the surrounding machine and shared with the VDP. */
typedef struct GensVdpMachineHost {
    int *cycles_m68k, *cycles_per_line, *visible_lines;
    const uint8_t *rom, *ram;
    size_t rom_backing_size, ram_size;
    int *rom_size, *sega_cd_started;
    void *odometer_opaque;
    int (*read_odometer)(void *, GensM68kBusState *, uint32_t *);
    uint8_t *cpu_interrupts;
    void *interrupt_opaque;
    int (*interrupt)(void *, GensM68kBusState *, unsigned int, int);
} GensVdpMachineHost;

/* All VDP state and the wired component structures. Do not copy or move after init. */
typedef struct GensVdpMachine {
    uint8_t vram[65536], cram[256], vsram_over[32], vsram[256], h_counter[1024];
    uint32_t regs[26];
    int control[7];
    int status, current_line, dma_length, dma_type, vram_flag, cram_flag;
    int vdp_int, sprite_over, mode555;
    uint8_t *scr_a, *scr_b, *window, *sprite, *hscroll;
    uint32_t hcell, hwin_mul, hpix, hpix_begin, hscroll_mask, hscroll_cmul, hscroll_cmask;
    uint32_t vscroll_cmask, vscroll_mmask, window_x, window_y;
    uint32_t sprite_struct[2048], sprite_visible[256], data_misc[13], data_spr[2];
    uint8_t scroll_a_on, scroll_b_on, vscroll_al, vscroll_ah, vscroll_bl, vscroll_bh;
    uint8_t vsprite_l, vsprite_h, sprite_on, sprite_always_top;
    uint8_t swap_priority_a, swap_priority_b, swap_priority_sprite, pal_lock, pink;
    uint16_t screen16x[GENS_VDP_MACHINE_SCREEN_PIXELS], md_screen16[GENS_VDP_MACHINE_SCREEN_PIXELS];
    uint32_t md_screen32[GENS_VDP_MACHINE_SCREEN_PIXELS];
    uint16_t palette16[32768], md_palette[256];
    uint32_t palette32[32768], md_palette32[256], rows[240];
    GensVdpIrq irq;
    GensVdp vdp;
    GensVdpSprites sprites;
    GensVdpSpriteLine sprite_line;
    GensVdpSpriteDraw sprite_draw;
    GensVdpScrollB scroll_b;
    GensVdpScrollA scroll_a;
    GensVdpPalette palette;
    GensVdpPixelResolve resolve;
    GensVdpRender render;
    GensVdpVideo video;
} GensVdpMachine;

int gens_vdp_machine_init(GensVdpMachine *, const GensVdpMachineHost *);
int gens_vdp_machine_valid(const GensVdpMachine *);
int gens_vdp_machine_render_line(GensVdpMachine *);
int gens_vdp_machine_render_screen(GensVdpMachine *, uint8_t *fake_screen);
int gens_vdp_machine_set_colors(GensVdpMachine *, const GensColorProfile *);

#ifdef __cplusplus
}
#endif

#endif
