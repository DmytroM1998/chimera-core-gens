/* Wires the ported VDP pieces around one block of machine-owned state.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_machine.h"

#include <string.h>

_Static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4, "Gens VDP machine widths");

int gens_vdp_machine_valid(const GensVdpMachine *m)
{
    return m != 0 && gens_vdp_valid(&m->vdp) && gens_vdp_irq_valid(&m->irq) &&
        gens_vdp_sprites_valid(&m->sprites) && gens_vdp_palette_valid(&m->palette) &&
        gens_vdp_video_valid(&m->video) && m->vdp.registers.registers == m->regs &&
        m->render.sprites == &m->sprites && m->render.pixels == m->screen16x &&
        m->render.row_table == m->rows;
}

int gens_vdp_machine_init(GensVdpMachine *m, const GensVdpMachineHost *host)
{
    GensVdpConfig config;

    if (m == 0 || host == 0 || host->cycles_m68k == 0 || host->cycles_per_line == 0 ||
        host->visible_lines == 0 || host->read_odometer == 0 || host->cpu_interrupts == 0 ||
        host->interrupt == 0)
        return 0;
    memset(m, 0, sizeof(*m));
    m->scr_a = m->scr_b = m->window = m->sprite = m->hscroll = m->vram;
    m->scroll_a_on = m->scroll_b_on = m->vscroll_al = m->vscroll_ah = 1;
    m->vscroll_bl = m->vscroll_bh = m->vsprite_l = m->vsprite_h = m->sprite_on = 1;
    if (!gens_vdp_video_rows_init(m->rows, 240u))
        return 0;

    m->irq = (GensVdpIrq){
        &m->regs[0], &m->regs[1], &m->vdp_int, host->cpu_interrupts,
        host->interrupt_opaque, host->interrupt
    };
    config.registers = (GensVdpRegisters){
        m->regs, 26, m->vram, sizeof(m->vram), (uint8_t *)&m->vram_flag,
        (uint8_t *)&m->cram_flag, (uint8_t *)&m->vscroll_mmask,
        &m->scr_a, &m->scr_b, &m->window, &m->sprite, &m->hscroll,
        &m->hcell, &m->hwin_mul, &m->hpix, &m->hpix_begin,
        &m->hscroll_mask, &m->hscroll_cmul, &m->hscroll_cmask, &m->vscroll_cmask,
        &m->window_x, &m->window_y, &m->regs[24], &m->regs[25], (uint32_t *)&m->control[5],
        &m->irq, gens_vdp_irq_update
    };
    config.control = m->control;
    config.control_count = 7;
    config.cram = m->cram;
    config.vsram = m->vsram;
    config.h_counter = m->h_counter;
    config.cram_size = sizeof(m->cram);
    config.vsram_size = sizeof(m->vsram);
    config.h_counter_size = sizeof(m->h_counter);
    config.status = &m->status;
    config.vram_flag = &m->vram_flag;
    config.cram_flag = &m->cram_flag;
    config.current_line = &m->current_line;
    config.visible_lines = host->visible_lines;
    config.cycles_m68k = host->cycles_m68k;
    config.cycles_per_line = host->cycles_per_line;
    config.dma_length = &m->dma_length;
    config.dma_type = &m->dma_type;
    config.rom = host->rom;
    config.ram = host->ram;
    config.rom_backing_size = host->rom_backing_size;
    config.ram_size = host->ram_size;
    config.rom_size = host->rom_size;
    config.sega_cd_started = host->sega_cd_started;
    config.odometer_opaque = host->odometer_opaque;
    config.read_odometer = host->read_odometer;
    if (!gens_vdp_init(&m->vdp, &config))
        goto fail;

    m->sprites = (GensVdpSprites){
        m->vram, m->cram, m->vsram_over, m->vsram, m->h_counter,
        sizeof(m->vram), sizeof(m->cram), sizeof(m->vsram_over), sizeof(m->vsram),
        sizeof(m->h_counter), &m->sprite, &m->regs[1], &m->regs[12],
        &m->vram_flag, m->sprite_struct, &m->data_misc[9], 2048
    };
    m->sprite_line = (GensVdpSpriteLine){
        m->sprite_struct, &m->data_misc[9], &m->hcell, &m->hpix, 2048,
        m->sprite_visible, &m->data_misc[12], 256, &m->current_line, &m->sprite_over,
        &m->status
    };
    m->sprite_draw = (GensVdpSpriteDraw){
        &m->sprites, &m->sprite_line, m->screen16x, GENS_VDP_MACHINE_SCREEN_PIXELS, 0,
        &m->sprite_on, &m->vsprite_l, &m->vsprite_h, &m->sprite_always_top,
        &m->swap_priority_sprite, &m->data_misc[2], &m->data_misc[11], &m->data_misc[10],
        &m->data_spr[0], &m->data_spr[1], 0, 0
    };
    m->scroll_b = (GensVdpScrollB){
        &m->sprites, m->screen16x, GENS_VDP_MACHINE_SCREEN_PIXELS, 0, &m->scr_b, &m->hscroll,
        &m->regs[11], &m->hcell, &m->hscroll_mask, &m->hscroll_cmul, &m->hscroll_cmask,
        &m->vscroll_cmask, &m->current_line, &m->scroll_b_on, &m->vscroll_bl,
        &m->vscroll_bh, &m->swap_priority_b, &m->data_misc[2], &m->data_misc[3],
        &m->data_misc[1], &m->status, 0, 0, 0
    };
    m->scroll_a = (GensVdpScrollA){
        &m->sprites, m->screen16x, GENS_VDP_MACHINE_SCREEN_PIXELS, 0, &m->scr_a, &m->hscroll,
        &m->window, &m->regs[11], &m->regs[17], &m->regs[18], &m->hcell, &m->hscroll_mask,
        &m->hscroll_cmul, &m->hscroll_cmask, &m->vscroll_cmask, &m->vscroll_mmask,
        &m->window_x, &m->window_y, &m->hwin_mul, &m->current_line, &m->scroll_a_on,
        &m->vscroll_al, &m->vscroll_ah, &m->swap_priority_a, m->data_misc, &m->status,
        0, 0, 0
    };
    m->palette = (GensVdpPalette){
        m->cram, sizeof(m->cram), m->palette16, m->palette32, 32768, 32768,
        m->md_palette, m->md_palette32, 256, 256, &m->regs[7], &m->mode555,
        &m->pal_lock, &m->pink, &m->cram_flag
    };
    m->resolve = (GensVdpPixelResolve){
        m->screen16x, GENS_VDP_MACHINE_SCREEN_PIXELS, 0, m->cram, sizeof(m->cram),
        &m->regs[7], (const int *)&m->hpix_begin
    };
    m->render = (GensVdpRender){
        &m->sprites, &m->scroll_b, &m->scroll_a, &m->sprite_draw, &m->palette,
        &m->resolve, m->screen16x, GENS_VDP_MACHINE_SCREEN_PIXELS, m->rows, 240,
        &m->current_line
    };
    m->video = (GensVdpVideo){
        m->screen16x, GENS_VDP_MACHINE_SCREEN_PIXELS, m->md_screen16, m->md_screen32,
        GENS_VDP_MACHINE_SCREEN_PIXELS, GENS_VDP_MACHINE_SCREEN_PIXELS, m->palette16,
        m->palette32, 32768, 32768, m->rows, 240, host->visible_lines, &m->current_line,
        &m->regs[12]
    };
    if (gens_vdp_machine_valid(m))
        return 1;
fail:
    memset(m, 0, sizeof(*m));
    return 0;
}

int gens_vdp_machine_render_line(GensVdpMachine *m)
{
    return gens_vdp_machine_valid(m) && gens_vdp_render_line(&m->render);
}

int gens_vdp_machine_render_screen(GensVdpMachine *m, uint8_t *fake_screen)
{
    return gens_vdp_machine_valid(m) && gens_vdp_video_render_screen(&m->video, fake_screen);
}

int gens_vdp_machine_set_colors(GensVdpMachine *m, const GensColorProfile *profile)
{
    GensColorLookup lookup;
    if (!gens_vdp_machine_valid(m))
        return 0;
    lookup = (GensColorLookup){m->palette16, m->palette32, 32768, 32768};
    if (!gens_color_lookup_generate(&lookup, profile))
        return 0;
    m->cram_flag |= 1;
    return 1;
}
