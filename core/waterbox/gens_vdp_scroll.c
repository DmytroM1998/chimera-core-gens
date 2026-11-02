/* Scroll B traversal follows the pinned RENDER_LINE_SCROLL_B macro.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_scroll.h"

static int map_offset(const GensVdpScrollB *scroll, uint8_t *const *pointer, uint32_t *offset)
{
    uintptr_t base = (uintptr_t)scroll->memory->vram, address = (uintptr_t)*pointer;
    if (address < base || address - base > 65535u)
        return 0;
    *offset = (uint32_t)(address - base);
    return 1;
}

int gens_vdp_scroll_b_valid(const GensVdpScrollB *scroll)
{
    uint32_t offset;
    return scroll != 0 && gens_vdp_sprites_valid(scroll->memory) && scroll->pixels != 0 &&
        scroll->plane_address != 0 && scroll->hscroll_address != 0 && scroll->set3 != 0 &&
        scroll->hcell != 0 && scroll->hscroll_mask != 0 && scroll->hscroll_cmul != 0 &&
        scroll->hscroll_cmask != 0 && scroll->vscroll_cmask != 0 &&
        scroll->current_line != 0 && scroll->enabled != 0 && scroll->low_enabled != 0 &&
        scroll->high_enabled != 0 && scroll->swap_priority != 0 && scroll->cursor != 0 &&
        scroll->cell != 0 && scroll->line7 != 0 && scroll->status != 0 &&
        scroll->interlace <= 1u && scroll->shadow <= 1u && scroll->cell_scroll <= 1u &&
        scroll->interlace == ((*scroll->memory->set4 & 4u) != 0u) &&
        scroll->shadow == ((*scroll->memory->set4 & 8u) != 0u) &&
        scroll->cell_scroll == ((*scroll->set3 & 4u) != 0u) &&
        *scroll->hcell <= 40u && *scroll->hscroll_mask <= 0x1ffu &&
        *scroll->hscroll_cmask <= 127u && *scroll->vscroll_cmask <= 127u &&
        scroll->row_offset <= scroll->pixel_count &&
        scroll->pixel_count - scroll->row_offset >= 8u * *scroll->hcell + 15u &&
        map_offset(scroll, scroll->plane_address, &offset) &&
        map_offset(scroll, scroll->hscroll_address, &offset);
}

static void vertical_sum(unsigned int interlace, int current_line, uint32_t cmask,
                         uint32_t scroll_value, uint32_t *line7, uint32_t *vcell)
{
    uint32_t sum;
    if (interlace)
        scroll_value >>= 1;
    sum = (uint32_t)current_line + scroll_value;
    *line7 = sum & 7u;
    *vcell = (sum >> 3) & cmask;
}

static void vertical_offset(const GensVdpScrollB *scroll, uint32_t scroll_value,
                            uint32_t *vcell)
{
    vertical_sum(scroll->interlace, *scroll->current_line, *scroll->vscroll_cmask,
                 scroll_value, scroll->line7, vcell);
}

static uint32_t read_vsram_dword(const GensVdpScrollB *scroll, uint32_t offset)
{
    const uint8_t *vsram = scroll->memory->vsram;
    return (uint32_t)vsram[offset] | (uint32_t)vsram[offset + 1u] << 8 |
        (uint32_t)vsram[offset + 2u] << 16 | (uint32_t)vsram[offset + 3u] << 24;
}

int gens_vdp_scroll_b_draw(GensVdpScrollB *scroll)
{
    uint32_t plane, hscroll, line, entry, fine, hcell, vcell, info, tile;
    uint32_t pattern, palette, row_line, shift, step;
    uint16_t word;
    unsigned int priority, flip;
    int first = 1;
    size_t pixel;
    GensVdpPixelRow row;

    if (!gens_vdp_scroll_b_valid(scroll))
        return 0;
    map_offset(scroll, scroll->plane_address, &plane);
    map_offset(scroll, scroll->hscroll_address, &hscroll);
    line = (uint32_t)*scroll->current_line;
    if (!gens_vdp_sprites_read_word(scroll->memory,
                                    hscroll + ((line & *scroll->hscroll_mask) << 2) + 2u, &word))
        return 0;
    entry = word;
    fine = entry & 7u;
    hcell = ((entry ^ 0x3ffu) >> 3) & *scroll->hscroll_cmask;
    *scroll->cursor = *scroll->hcell;
    *scroll->cell = (((entry ^ 0x3ffu) >> 3) & 1u) - 2u;
    vertical_offset(scroll, read_vsram_dword(scroll, 2u), &vcell);
    pixel = scroll->row_offset + fine;
    shift = *scroll->hscroll_cmul & 31u;
    step = scroll->interlace ? 8u : 4u;

    for (;;)
    {
        if (scroll->cell_scroll && (*scroll->cell & 0xff81u) == 0u && first == 0)
        {
            uint32_t value = (uint32_t)scroll->memory->vsram[*scroll->cell * 2u + 2u] |
                (uint32_t)scroll->memory->vsram[*scroll->cell * 2u + 3u] << 8;
            vertical_offset(scroll, value, &vcell);
        }
        if (!gens_vdp_sprites_read_word(scroll->memory,
                                        plane + (((vcell << shift) + hcell) << 1), &word))
            return 0;
        info = word;
        tile = (info & 0x7ffu) * (scroll->interlace ? 64u : 32u);
        palette = (info >> 9) & 0x30u;
        row_line = *scroll->line7;
        if ((info & 0x1000u) != 0u)
            row_line ^= 7u;
        if (!gens_vdp_sprites_read_pattern(scroll->memory, tile + row_line * step, &pattern))
            return 0;
        if ((*scroll->swap_priority & 1u) != 0u)
            info ^= 0x8000u;
        priority = (info & 0x8000u) != 0u;
        flip = (info & 0x800u) != 0u;
        row = (GensVdpPixelRow){
            scroll->pixels, scroll->pixel_count, pixel, pattern, palette,
            GENS_VDP_PIXELS_SCROLL_B, priority, scroll->shadow, flip, *scroll->enabled,
            priority ? *scroll->high_enabled : *scroll->low_enabled, scroll->status
        };
        if (!gens_vdp_pixel_row(&row))
            return 0;
        ++*scroll->cell;
        first = 0;
        hcell = (hcell + 1u) & *scroll->hscroll_cmask;
        pixel += 8u;
        *scroll->cursor = (*scroll->cursor & ~UINT32_C(0xff)) |
            ((*scroll->cursor - 1u) & UINT32_C(0xff));
        if ((*scroll->cursor & 0x80u) != 0u)
            break;
    }
    return 1;
}

static const uint32_t mask_normal[8] = {
    0xffffffffu, 0xfff0ffffu, 0xff00ffffu, 0xf000ffffu,
    0x0000ffffu, 0x0000fff0u, 0x0000ff00u, 0x0000f000u
};
static const uint32_t mask_flipped[8] = {
    0xffffffffu, 0xffff0fffu, 0xffff00ffu, 0xffff000fu,
    0xffff0000u, 0x0fff0000u, 0x00ff0000u, 0x000f0000u
};

static int a_vram_offset(const GensVdpScrollA *scroll, uint8_t *const *pointer, uint32_t *offset)
{
    uintptr_t base = (uintptr_t)scroll->memory->vram, address = (uintptr_t)*pointer;
    if (address < base || address - base > 65535u)
        return 0;
    *offset = (uint32_t)(address - base);
    return 1;
}

int gens_vdp_scroll_a_valid(const GensVdpScrollA *scroll)
{
    uint32_t offset;
    return scroll != 0 && gens_vdp_sprites_valid(scroll->memory) && scroll->pixels != 0 &&
        scroll->plane_address != 0 && scroll->hscroll_address != 0 &&
        scroll->window_address != 0 && scroll->set3 != 0 && scroll->reg17 != 0 &&
        scroll->reg18 != 0 && scroll->hcell != 0 && scroll->hscroll_mask != 0 &&
        scroll->hscroll_cmul != 0 && scroll->hscroll_cmask != 0 &&
        scroll->vscroll_cmask != 0 && scroll->vscroll_mmask != 0 &&
        scroll->window_x != 0 && scroll->window_y != 0 && scroll->window_mul != 0 &&
        scroll->current_line != 0 && scroll->enabled != 0 && scroll->low_enabled != 0 &&
        scroll->high_enabled != 0 && scroll->swap_priority != 0 && scroll->misc != 0 &&
        scroll->status != 0 &&
        scroll->interlace <= 1u && scroll->shadow <= 1u && scroll->cell_scroll <= 1u &&
        scroll->interlace == ((*scroll->memory->set4 & 4u) != 0u) &&
        scroll->shadow == ((*scroll->memory->set4 & 8u) != 0u) &&
        scroll->cell_scroll == ((*scroll->set3 & 4u) != 0u) &&
        *scroll->hcell >= 1u && *scroll->hcell <= 40u && *scroll->hscroll_mask <= 0x1ffu &&
        *scroll->hscroll_cmask <= 127u && *scroll->vscroll_cmask <= 127u &&
        (*scroll->vscroll_mmask == 0u || *scroll->vscroll_mmask == 0x7eu) &&
        *scroll->window_x <= *scroll->hcell &&
        (*scroll->window_mul == 5u || *scroll->window_mul == 6u) &&
        scroll->row_offset <= scroll->pixel_count &&
        scroll->pixel_count - scroll->row_offset >= 8u * *scroll->hcell + 15u &&
        a_vram_offset(scroll, scroll->plane_address, &offset) &&
        a_vram_offset(scroll, scroll->hscroll_address, &offset) &&
        a_vram_offset(scroll, scroll->window_address, &offset);
}

static uint32_t vsram_word(const GensVdpScrollA *scroll, uint32_t offset)
{
    return (uint32_t)scroll->memory->vsram[offset] |
        (uint32_t)scroll->memory->vsram[offset + 1u] << 8;
}

static int put_cell(const GensVdpScrollA *scroll, uint32_t info, size_t pixel, int mask)
{
    uint32_t tile = (info & 0x7ffu) * (scroll->interlace ? 64u : 32u);
    uint32_t row_line = scroll->misc[GENS_SCROLL_MISC_LINE7], pattern;
    unsigned int priority = (info & 0x8000u) != 0u, flip = (info & 0x800u) != 0u;
    GensVdpPixelRow row;
    if ((info & 0x1000u) != 0u)
        row_line ^= 7u;
    if (!gens_vdp_sprites_read_pattern(scroll->memory,
                                       tile + row_line * (scroll->interlace ? 8u : 4u), &pattern))
        return 0;
    if (mask >= 0)
        pattern &= flip ? mask_flipped[mask] : mask_normal[mask];
    row = (GensVdpPixelRow){
        scroll->pixels, scroll->pixel_count, pixel, pattern, (info >> 9) & 0x30u,
        GENS_VDP_PIXELS_SCROLL_A, priority, scroll->shadow, flip, *scroll->enabled,
        priority ? *scroll->high_enabled : *scroll->low_enabled, scroll->status
    };
    return gens_vdp_pixel_row(&row);
}

static void update_y(const GensVdpScrollA *scroll, uint32_t *vcell)
{
    uint32_t cell = scroll->misc[GENS_SCROLL_MISC_CELL];
    if ((cell & 0xff81u) == 0u)
        vertical_sum(scroll->interlace, *scroll->current_line, *scroll->vscroll_cmask,
                     vsram_word(scroll, cell * 2u), &scroll->misc[GENS_SCROLL_MISC_LINE7], vcell);
}

static int scroll_a_info(const GensVdpScrollA *scroll, uint32_t plane, uint32_t hcell,
                         uint32_t vcell, uint32_t *info)
{
    uint16_t word;
    if (!gens_vdp_sprites_read_word(scroll->memory,
            plane + (((vcell << (*scroll->hscroll_cmul & 31u)) + hcell) << 1), &word))
        return 0;
    *info = word;
    if ((*scroll->swap_priority & 1u) != 0u)
        *info ^= 0x8000u;
    return 1;
}

static int draw_plane_a(GensVdpScrollA *scroll)
{
    uint32_t *misc = scroll->misc;
    uint32_t plane, hscroll, line = (uint32_t)*scroll->current_line;
    uint32_t entry, cell0, start, hcell, vcell, value, info;
    uint16_t word;
    size_t pixel;
    int first = 1;

    a_vram_offset(scroll, scroll->plane_address, &plane);
    a_vram_offset(scroll, scroll->hscroll_address, &hscroll);
    if (!gens_vdp_sprites_read_word(scroll->memory,
                                    hscroll + ((line & *scroll->hscroll_mask) << 2), &word))
        return 0;
    entry = word;
    cell0 = (entry ^ 0x3ffu) >> 3;
    start = misc[GENS_SCROLL_MISC_START_A];
    misc[GENS_SCROLL_MISC_MASK] = entry & 7u;
    hcell = (cell0 + start) & *scroll->hscroll_cmask;
    pixel = scroll->row_offset + (entry & 7u) + (size_t)start * 8u;
    misc[GENS_SCROLL_MISC_CELL] = start + (cell0 & 1u) - 2u;
    value = (misc[GENS_SCROLL_MISC_CELL] & 0x80000000u) != 0u ? vsram_word(scroll, 0u) :
        vsram_word(scroll, (misc[GENS_SCROLL_MISC_CELL] & *scroll->vscroll_mmask) * 2u);
    vertical_sum(scroll->interlace, (int)line, *scroll->vscroll_cmask, value,
                 &misc[GENS_SCROLL_MISC_LINE7], &vcell);

    for (;;)
    {
        if (!first && scroll->cell_scroll)
            update_y(scroll, &vcell);
        if (!scroll_a_info(scroll, plane, hcell, vcell, &info) ||
            !put_cell(scroll, info, pixel, -1))
            return 0;
        first = 0;
        ++misc[GENS_SCROLL_MISC_CELL];
        hcell = (hcell + 1u) & *scroll->hscroll_cmask;
        pixel += 8u;
        misc[GENS_SCROLL_MISC_LENGTH_A] = (misc[GENS_SCROLL_MISC_LENGTH_A] & ~UINT32_C(0xff)) |
            ((misc[GENS_SCROLL_MISC_LENGTH_A] - 1u) & UINT32_C(0xff));
        if ((misc[GENS_SCROLL_MISC_LENGTH_A] & 0x80u) != 0u)
            break;
    }
    if (scroll->cell_scroll)
        update_y(scroll, &vcell);
    return scroll_a_info(scroll, plane, hcell, vcell, &info) &&
        put_cell(scroll, info, pixel, (int)misc[GENS_SCROLL_MISC_MASK]);
}

static int draw_window(GensVdpScrollA *scroll, uint32_t start, uint32_t length)
{
    uint32_t *misc = scroll->misc, line = (uint32_t)*scroll->current_line, window, base;
    uint16_t word;
    size_t pixel;

    if (length == 0u)
        return 0;
    a_vram_offset(scroll, scroll->window_address, &window);
    base = window + (((line >> 3) << (*scroll->window_mul & 31u)) << 1);
    misc[GENS_SCROLL_MISC_PATTERN_ADR] = base;
    misc[GENS_SCROLL_MISC_LINE7] = line & 7u;
    pixel = scroll->row_offset + (size_t)start * 8u + 8u;
    do
    {
        if (!gens_vdp_sprites_read_word(scroll->memory, base + start * 2u, &word) ||
            !put_cell(scroll, word, pixel, -1))
            return 0;
        ++start;
        pixel += 8u;
    } while (--length != 0u);
    return 1;
}

int gens_vdp_scroll_a_draw(GensVdpScrollA *scroll)
{
    uint32_t *misc;
    uint32_t line, hcells, win_x;
    int bottom, below;

    if (!gens_vdp_scroll_a_valid(scroll))
        return 0;
    misc = scroll->misc;
    line = (uint32_t)*scroll->current_line;
    hcells = *scroll->hcell;
    win_x = *scroll->window_x;
    bottom = (*scroll->reg18 & 0x80u) != 0u;
    below = (line >> 3) >= *scroll->window_y;
    if (bottom == below)
        return draw_window(scroll, 0u, hcells);
    if ((*scroll->reg17 & 0x80u) != 0u)
    {
        misc[GENS_SCROLL_MISC_START_W] = win_x;
        misc[GENS_SCROLL_MISC_LENGTH_W] = hcells - win_x;
        misc[GENS_SCROLL_MISC_START_A] = 0u;
        misc[GENS_SCROLL_MISC_LENGTH_A] = win_x - 1u;
    }
    else
    {
        misc[GENS_SCROLL_MISC_START_W] = 0u;
        misc[GENS_SCROLL_MISC_LENGTH_W] = win_x;
        misc[GENS_SCROLL_MISC_START_A] = win_x;
        misc[GENS_SCROLL_MISC_LENGTH_A] = hcells - win_x - 1u;
    }
    if ((misc[GENS_SCROLL_MISC_LENGTH_A] & 0x80000000u) == 0u)
    {
        if (!draw_plane_a(scroll))
            return 0;
        if ((misc[GENS_SCROLL_MISC_LENGTH_W] & 0xffu) == 0u)
            return 1;
    }
    return draw_window(scroll, misc[GENS_SCROLL_MISC_START_W], misc[GENS_SCROLL_MISC_LENGTH_W]);
}
