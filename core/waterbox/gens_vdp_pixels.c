/* Eight-pixel composition follows the pinned PUTLINE/PUTPIXEL macros.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_pixels.h"

#include <string.h>

_Static_assert(sizeof(int) == 4, "Gens pixel status width");

static const uint8_t shifts[8] = {12, 8, 4, 0, 28, 24, 20, 16};

int gens_vdp_pixel_row_valid(const GensVdpPixelRow *row)
{
    return row != 0 && row->pixels != 0 && row->offset <= row->count &&
        row->count - row->offset >= 8u &&
        (unsigned int)row->layer <= (unsigned int)GENS_VDP_PIXELS_SPRITE &&
        row->priority <= 1u && row->shadow <= 1u && row->flip <= 1u && row->status != 0;
}

int gens_vdp_pixel_row(const GensVdpPixelRow *row)
{
    uint16_t *pixels, old;
    uint32_t value, status;
    unsigned int i, pixel, high, shade, collision = 0;
    uint8_t low;
    if (!gens_vdp_pixel_row_valid(row))
        return 0;
    pixels = row->pixels + row->offset;
    if (row->layer == GENS_VDP_PIXELS_SCROLL_B)
        for (i = 0; i < 8u; ++i)
            pixels[i] = row->shadow && !row->priority ? 0x4040 : 0;
    if ((row->enabled & 1u) == 0u || (row->priority_enabled & 1u) == 0u)
        return 1;
    if (row->layer == GENS_VDP_PIXELS_SCROLL_A && row->priority && row->shadow)
        for (i = 0; i < 8u; ++i)
            pixels[i] &= UINT16_C(0xbfbf);

    for (i = 0; i < 8u; ++i)
    {
        pixel = row->pattern >> shifts[row->flip ? 7u - i : i] & 15u;
        if (pixel == 0u)
            continue;
        old = pixels[i];
        high = old >> 8;
        if (row->layer == GENS_VDP_PIXELS_SPRITE)
        {
            if ((high & (row->priority ? 0x20u : 0x22u)) != 0u)
            {
                collision |= high;
                if (!row->priority)
                    pixels[i] = old | UINT16_C(0x2000);
                continue;
            }
            value = pixel + row->palette + UINT32_C(0x2000);
            if (row->shadow)
            {
                shade = high & (row->priority ? 0x80u : 0xc0u);
                if (value >= UINT32_C(0x203e))
                {
                    pixels[i] = old | (value == UINT32_C(0x203e) ?
                                       UINT16_C(0x8080) : UINT16_C(0x4040));
                    continue;
                }
                value = (value & UINT32_C(0xffffff00)) | ((value + shade) & 255u);
            }
            pixels[i] = (uint16_t)value | UINT16_C(0x0100);
        }
        else if (row->priority)
            pixels[i] = (uint16_t)(pixel + row->palette + UINT32_C(0x0300));
        else
        {
            if (row->layer == GENS_VDP_PIXELS_SCROLL_A && (high & 2u) != 0u)
                continue;
            shade = row->shadow ? (row->layer == GENS_VDP_PIXELS_SCROLL_B ? 0x40u :
                                  high & 0x40u) : 0u;
            low = (uint8_t)(pixel + row->palette + shade);
            pixels[i] = (old & UINT16_C(0xff00)) | low | UINT16_C(0x0100);
        }
    }
    if (row->layer == GENS_VDP_PIXELS_SPRITE)
    {
        memcpy(&status, row->status, sizeof(status));
        status |= collision & 0x20u;
        memcpy(row->status, &status, sizeof(status));
    }
    return 1;
}

int gens_vdp_pixel_resolve_valid(const GensVdpPixelResolve *resolve)
{
    unsigned int length;
    if (resolve == 0 || resolve->pixels == 0 || resolve->cram == 0 ||
        resolve->cram_size < 256u || resolve->background == 0 || resolve->h_pixel_begin == 0 ||
        *resolve->h_pixel_begin < 0 || *resolve->h_pixel_begin >= 160)
        return 0;
    length = (160u - (unsigned int)*resolve->h_pixel_begin) * 2u;
    return resolve->offset <= resolve->count && resolve->count - resolve->offset >= length;
}

int gens_vdp_pixel_resolve(const GensVdpPixelResolve *resolve)
{
    unsigned int i, length, color, background;
    uint16_t pixel, flags, word;
    if (!gens_vdp_pixel_resolve_valid(resolve))
        return 0;
    length = (160u - (unsigned int)*resolve->h_pixel_begin) * 2u;
    background = *resolve->background & 63u;
    for (i = 0; i < length; ++i)
    {
        pixel = resolve->pixels[resolve->offset + i];
        color = pixel & 63u;
        if (color == 0u)
            color = background;
        flags = (uint16_t)((pixel & 0x1c0u) << 6);
        word = (uint16_t)(resolve->cram[color * 2u] |
                          (uint16_t)resolve->cram[color * 2u + 1u] << 8);
        resolve->pixels[resolve->offset + i] = (word & UINT16_C(0x0eee)) | flags;
    }
    return 1;
}
