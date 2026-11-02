/* Palette banks and backdrop updates follow the pinned renderer macros.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_palette.h"

#include <string.h>

_Static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4, "Gens palette global widths");

int gens_vdp_palette_valid(const GensVdpPalette *palette)
{
    return palette != 0 && palette->cram != 0 && palette->cram_size >= 256u &&
        palette->lookup16 != 0 && palette->lookup16_count >= 32768u &&
        palette->lookup32 != 0 && palette->lookup32_count >= 32768u &&
        palette->colors16 != 0 && palette->colors16_count >= 256u &&
        palette->colors32 != 0 && palette->colors32_count >= 256u &&
        palette->background != 0 && palette->mode555 != 0 &&
        palette->lock != 0 && palette->pink != 0 && palette->dirty != 0;
}

static unsigned int color_index(const GensVdpPalette *palette, unsigned int color)
{
    unsigned int offset = color * 2u;
    return (palette->cram[offset] | (unsigned int)palette->cram[offset + 1u] << 8) & 0x0eeeu;
}

int gens_vdp_palette_update(GensVdpPalette *palette)
{
    uint32_t dirty, normal32, dark32, light32;
    uint16_t normal16, dark16, light16;
    unsigned int i, index, background;
    if (!gens_vdp_palette_valid(palette))
        return 0;
    memcpy(&dirty, palette->dirty, sizeof(dirty));
    if ((dirty & 1u) == 0u || (*palette->lock & 1u) != 0u)
        return 1;
    dirty &= UINT32_C(0xffffff00);
    memcpy(palette->dirty, &dirty, sizeof(dirty));

    for (i = 0; i < 64u; ++i)
    {
        index = color_index(palette, i);
        palette->colors32[i] = palette->lookup32[index];
        palette->colors32[i + 192u] = palette->lookup32[index];
        palette->colors32[i + 64u] = palette->lookup32[index >> 1];
        palette->colors32[i + 128u] = palette->lookup32[(index >> 1) + 0x777u];
    }
    background = *palette->background & 63u;
    if ((*palette->pink & 1u) != 0u)
        normal32 = dark32 = light32 = UINT32_C(0x00ff00ff);
    else
    {
        normal32 = palette->colors32[background];
        dark32 = palette->colors32[background + 64u];
        light32 = palette->colors32[background + 128u];
    }
    palette->colors32[0] = palette->colors32[192] = normal32;
    palette->colors32[64] = dark32;
    palette->colors32[128] = light32;

    for (i = 0; i < 64u; ++i)
    {
        index = color_index(palette, i);
        palette->colors16[i] = palette->lookup16[index];
        palette->colors16[i + 192u] = palette->lookup16[index];
        palette->colors16[i + 64u] = palette->lookup16[index >> 1];
        palette->colors16[i + 128u] = palette->lookup16[(index >> 1) + 0x777u];
    }
    if ((*palette->pink & 1u) != 0u)
        normal16 = dark16 = light16 = (*palette->mode555 & 1) != 0 ? 0x7c1f : 0xf81f;
    else
    {
        normal16 = palette->colors16[background];
        dark16 = palette->colors16[background + 64u];
        light16 = palette->colors16[background + 128u];
    }
    palette->colors16[0] = palette->colors16[192] = normal16;
    palette->colors16[64] = dark16;
    palette->colors16[128] = light16;
    return 1;
}
