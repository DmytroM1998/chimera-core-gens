/* Genesis lookup generation follows the pinned Recalculate_Palettes.
 * The upstream component terms apply to derived portions.
 */
#include "gens_color_lookup.h"

#include <float.h>
#include <limits.h>

_Static_assert(sizeof(int) == 4 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "Gens color arithmetic widths");

int gens_color_profile_valid(const GensColorProfile *profile)
{
    return profile != 0 &&
        profile->brightness >= INT_MIN / 32 + 100 &&
        profile->brightness <= INT_MAX / 32 + 100 &&
        profile->contrast >= INT_MIN / 63 && profile->contrast <= INT_MAX / 63;
}

static int clamp_level(int level)
{
    if (level < 0)
        return 0;
    return level > 63 ? 63 : level;
}

static int channel_level(int component, int maximum, int brightness, int contrast)
{
    int level = (int)((double)(component << 2) * ((double)maximum / 224.0));
    level = clamp_level(level + brightness);
    return clamp_level(level * contrast / 100);
}

static uint16_t pack16(int red, int green, int blue, int mode555)
{
    if ((mode555 & 1) != 0)
        return (uint16_t)((red >> 1) << 10 | (green >> 1) << 5 | (blue >> 1));
    return (uint16_t)((red >> 1) << 11 | green << 5 | (blue >> 1));
}

static int grey_level(int red, int green, int blue)
{
    return ((red * (unsigned int)(0.30 * 65536.0)) >> 16) +
        ((green * (unsigned int)(0.59 * 65536.0)) >> 16) +
        ((blue * (unsigned int)(0.11 * 65536.0)) >> 16);
}

int gens_color_lookup_generate(GensColorLookup *lookup, const GensColorProfile *profile)
{
    unsigned int red, green, blue, i;
    int r, g, b, grey, brightness;
    uint16_t color16, pink16;
    uint32_t color32;

    if (lookup == 0 || lookup->colors16 == 0 || lookup->colors32 == 0 ||
        lookup->count16 < 32768u || lookup->count32 < 32768u ||
        !gens_color_profile_valid(profile))
        return 0;
    brightness = (profile->brightness - 100) * 32 / 100;
    for (red = 0; red < 16u; ++red)
        for (green = 0; green < 16u; ++green)
            for (blue = 0; blue < 16u; ++blue)
            {
                r = channel_level((int)red, profile->red_max, brightness, profile->contrast);
                g = channel_level((int)green, profile->green_max, brightness, profile->contrast);
                b = channel_level((int)blue, profile->blue_max, brightness, profile->contrast);
                i = blue << 8 | green << 4 | red;
                lookup->colors32[i] = UINT32_C(0xff000000) |
                    (uint32_t)r << 18 | (uint32_t)g << 10 | (uint32_t)b << 2;
                lookup->colors16[i] = pack16(r, g, b, profile->mode555);
            }

    if (profile->greyscale != 0)
        for (i = 0; i < 4096u; ++i)
        {
            color32 = lookup->colors32[i];
            grey = grey_level((int)(color32 >> 16 & 255u),
                              (int)(color32 >> 8 & 255u), (int)(color32 & 255u));
            lookup->colors32[i] = UINT32_C(0xff000000) |
                (uint32_t)grey << 16 | (uint32_t)grey << 8 | (uint32_t)grey;
            color16 = lookup->colors16[i];
            if ((profile->mode555 & 1) != 0)
            {
                r = (color16 >> 10 & 31) << 1;
                g = (color16 >> 5 & 31) << 1;
            }
            else
            {
                r = (color16 >> 11 & 31) << 1;
                g = color16 >> 5 & 63;
            }
            b = (color16 & 31) << 1;
            grey = grey_level(r, g, b);
            lookup->colors16[i] = pack16(grey, grey, grey, profile->mode555);
        }

    if (profile->invert != 0)
        for (i = 0; i < 4096u; ++i)
        {
            lookup->colors16[i] ^= UINT16_C(0xffff);
            lookup->colors32[i] ^= UINT32_C(0x00ffffff);
        }
    for (i = 0; i < 4096u; ++i)
    {
        lookup->colors16[0x1000u | i] = lookup->colors16[i >> 1];
        lookup->colors32[0x1000u | i] = lookup->colors32[i >> 1];
        lookup->colors16[0x2000u | i] = lookup->colors16[(i >> 1) + 0x777u];
        lookup->colors32[0x2000u | i] = lookup->colors32[(i >> 1) + 0x777u];
        lookup->colors16[0x3000u | i] = lookup->colors16[i];
        lookup->colors32[0x3000u | i] = lookup->colors32[i];
    }
    for (i = 0; i < 16384u; ++i)
    {
        lookup->colors16[0x4000u | i] = lookup->colors16[i];
        lookup->colors32[0x4000u | i] = lookup->colors32[i];
    }
    if (profile->pink != 0)
    {
        pink16 = (profile->mode555 & 1) != 0 ? 0x7c1f : 0xf81f;
        for (i = 0; i < 16384u; ++i)
        {
            lookup->colors16[i] = pink16;
            lookup->colors32[i] = UINT32_C(0x00ff00ff);
        }
    }
    return 1;
}
