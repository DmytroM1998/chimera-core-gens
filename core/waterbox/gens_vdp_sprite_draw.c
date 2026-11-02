/* Traversal and metadata follow the pinned RENDER_LINE_SPR macro.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_sprite_draw.h"

#include <string.h>

static int32_t signed_bits(uint32_t value)
{
    int32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

int gens_vdp_sprite_draw_valid(const GensVdpSpriteDraw *draw)
{
    return draw != 0 && gens_vdp_sprites_valid(draw->sprites) &&
        gens_vdp_sprite_line_valid(draw->line) &&
        draw->line->table == draw->sprites->table && draw->line->end == draw->sprites->end &&
        *draw->line->h_pixels <= 320u && draw->pixels != 0 &&
        draw->row_offset <= draw->pixel_count && draw->pixel_count - draw->row_offset >= 336u &&
        draw->enabled != 0 && draw->low_enabled != 0 && draw->high_enabled != 0 &&
        draw->always_top != 0 && draw->swap_priority != 0 && draw->cursor != 0 &&
        draw->palette != 0 && draw->next_cell != 0 && draw->h_min != 0 && draw->h_max != 0 &&
        draw->interlace <= 1u && draw->shadow <= 1u &&
        draw->interlace == ((*draw->sprites->set4 & 4u) != 0u) &&
        draw->shadow == ((*draw->sprites->set4 & 8u) != 0u);
}

static int draw_pattern(GensVdpSpriteDraw *draw, uint32_t address, int32_t x,
                         uint32_t palette, unsigned int priority, unsigned int flip)
{
    uint32_t pattern;
    size_t offset;
    GensVdpPixelRow row;
    if (x < -8 || x > 319 || !gens_vdp_sprites_read_pattern(draw->sprites, address, &pattern))
        return 0;
    offset = draw->row_offset + (size_t)(x + 8);
    row = (GensVdpPixelRow){
        draw->pixels, draw->pixel_count, offset, pattern, palette, GENS_VDP_PIXELS_SPRITE,
        priority, draw->shadow, flip, *draw->enabled,
        priority ? *draw->high_enabled : *draw->low_enabled, draw->line->status
    };
    return gens_vdp_pixel_row(&row);
}

int gens_vdp_sprites_draw(GensVdpSpriteDraw *draw)
{
    const uint32_t *sprite;
    uint32_t offset, tile, attributes, y, tile_row, stride, address, palette, block, step;
    int32_t x, xmax, bound, pixels;
    unsigned int priority;

    if (!gens_vdp_sprite_draw_valid(draw))
        return 0;
    if ((*draw->sprites->set2 & 0x40u) == 0u)
        return 1;
    if (!gens_vdp_sprites_select(draw->line))
        return 0;
    *draw->cursor = 0;
    pixels = (int32_t)*draw->line->h_pixels;
    block = draw->interlace ? 64u : 32u;
    step = draw->interlace ? 8u : 4u;

    for (offset = 0; offset < *draw->line->bound; offset += 4u)
    {
        sprite = draw->line->table + draw->line->visible[offset / 4u] / 4u;
        x = signed_bits(sprite[0]);
        xmax = signed_bits(sprite[4]);
        /* The original trusts the table. Selected entries satisfy x < H_Pix and xmax >= 0, so
         * every drawn pattern stays on the line. draw_pattern rejects anything else. */
        if (x < -128 || x > 383 || xmax < -128 || xmax > 414)
            return 0;
        tile = sprite[6];
        attributes = tile ^ ((*draw->swap_priority & 1u) != 0u ? 0x8000u : 0u);
        palette = (tile >> 9) & 0x30u;
        y = (uint32_t)*draw->line->current_line - sprite[1];
        tile_row = y & 7u;
        y = (y & 0xf8u) * step;
        stride = sprite[3] * block;
        address = (tile & 0x7ffu) * block;
        if ((attributes & 0x1000u) != 0u)
        {
            address += stride - y;
            tile_row ^= 7u;
        }
        else
            address += y;
        address += tile_row * step;
        stride += block;
        *draw->palette = palette;
        *draw->next_cell = stride;
        priority = *draw->always_top != 0u || (attributes & 0x8000u) != 0u;

        if ((attributes & 0x800u) != 0u)
        {
            bound = x > -7 ? x : -7;
            *draw->h_min = (uint32_t)bound;
            x = xmax - 7;
            while (x >= pixels)
            {
                x -= 8;
                address += stride;
            }
            do
            {
                if (!draw_pattern(draw, address, x, palette, priority, 1))
                    return 0;
                x -= 8;
                address += stride;
            } while (x >= bound);
        }
        else
        {
            bound = xmax < pixels ? xmax : pixels;
            *draw->h_max = (uint32_t)bound;
            while (x < -7)
            {
                x += 8;
                address += stride;
            }
            do
            {
                if (!draw_pattern(draw, address, x, palette, priority, 0))
                    return 0;
                x += 8;
                address += stride;
            } while (x < bound);
        }
        *draw->cursor = offset + 4u;
    }
    return 1;
}
