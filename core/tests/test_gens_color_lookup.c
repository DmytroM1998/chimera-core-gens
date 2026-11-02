#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_color_lookup.h"
#include "gens_vdp_palette.h"

#ifdef GENS_COLOR_LOOKUP_REFERENCE
#define STORAGE extern
void reference_color_lookup(void);
#else
#define STORAGE static
#endif

STORAGE uint16_t Palette[32768];
STORAGE uint32_t Palette32[32768];
STORAGE int RMax_Level, GMax_Level, BMax_Level, Brightness_Level, Contrast_Level;
STORAGE int Mode_555, Greyscale, Invert_Color;
STORAGE uint8_t PinkBG;

#ifndef GENS_COLOR_LOOKUP_REFERENCE
static GensColorLookup lookup = {Palette, Palette32, 32768, 32768};
static uint8_t cram[256], palette_lock;
static uint16_t colors16[256];
static uint32_t colors32[256];
static unsigned int background = 1;
static int palette_dirty;
static GensVdpPalette palette = {
    cram, sizeof(cram), Palette, Palette32, 32768, 32768,
    colors16, colors32, 256, 256, &background, &Mode_555, &palette_lock, &PinkBG, &palette_dirty
};

static void unit_cases(void)
{
    GensColorProfile profile = {255, 255, 255, 100, 100, 0, 0, 0, 0};
    GensColorProfile bounds = profile;
    GensColorLookup invalid = lookup;
    invalid.count32 = 32767;
    Palette32[0] = 0x12345678;
    assert(!gens_color_lookup_generate(&invalid, &profile) && Palette32[0] == 0x12345678);
    profile.brightness = INT_MAX;
    assert(!gens_color_profile_valid(&profile));
    assert(!gens_color_lookup_generate(&lookup, &profile) && Palette32[0] == 0x12345678);
    profile.brightness = 100;
    profile.contrast = INT_MIN;
    assert(!gens_color_profile_valid(&profile));
    profile.contrast = 100;
    bounds.red_max = INT_MIN;
    bounds.green_max = INT_MAX;
    bounds.brightness = INT_MIN / 32 + 100;
    bounds.contrast = INT_MIN / 63;
    assert(gens_color_profile_valid(&bounds));
    --bounds.brightness;
    assert(!gens_color_profile_valid(&bounds));
    bounds.brightness = INT_MAX / 32 + 100;
    bounds.contrast = INT_MAX / 63;
    assert(gens_color_profile_valid(&bounds));
    ++bounds.brightness;
    assert(!gens_color_profile_valid(&bounds));
    bounds.brightness = 100;
    ++bounds.contrast;
    assert(!gens_color_profile_valid(&bounds));
    assert(gens_color_lookup_generate(&lookup, &profile));
    assert(Palette32[0] == 0xff000000 && Palette32[0xeee] == 0xfffcfcfc);
    assert(Palette[0xeee] == 0xffff && Palette[0x3000 | 0xeee] == Palette[0xeee]);
    assert(Palette32[0x1eee] == Palette32[0x777]);
    assert(Palette32[0x4000 | 0xeee] == Palette32[0xeee]);
    profile.greyscale = 1;
    assert(gens_color_lookup_generate(&lookup, &profile));
    assert(Palette32[0xeee] == 0xfffafafa && Palette[0xeee] == 0xf7be);
    profile.mode555 = 1;
    assert(gens_color_lookup_generate(&lookup, &profile));
    assert(Palette[0xeee] == 0x7bde);
    profile.greyscale = 0;
    profile.invert = 1;
    assert(gens_color_lookup_generate(&lookup, &profile));
    assert(Palette[0] == 0xffff && Palette32[0] == 0xffffffff);
    assert((Palette[0xeee] & 0x8000u) != 0u);
    profile.invert = 0;
    profile.pink = 1;
    assert(gens_color_lookup_generate(&lookup, &profile));
    assert(Palette32[0x3fff] == 0x00ff00ff && Palette[0x3fff] == 0x7c1f);
    assert(Palette32[0x4eee] == 0xfffcfcfc && Palette[0x4eee] == 0x7fff);
    profile.pink = 0;
    palette_dirty = 1;
    assert(gens_color_lookup_generate(&lookup, &profile) && palette_dirty == 1);
    Mode_555 = profile.mode555;
    PinkBG = 0;
    cram[2] = 0xee; cram[3] = 0x0e;
    assert(gens_vdp_palette_update(&palette));
    assert(colors32[0] == 0xfffcfcfc && colors16[0] == 0x7fff && palette_dirty == 0);
    assert(colors32[64] == Palette32[0x777] && colors16[64] == Palette[0x777]);
}
#endif

static void update(void)
{
#ifdef GENS_COLOR_LOOKUP_REFERENCE
    reference_color_lookup();
#else
    GensColorProfile profile = {
        RMax_Level, GMax_Level, BMax_Level, Brightness_Level, Contrast_Level,
        Mode_555, Greyscale, Invert_Color, PinkBG
    };
    assert(gens_color_lookup_generate(&lookup, &profile));
#ifdef GENS_COLOR_LOOKUP_NEGATIVE_CONTROL
    Palette32[0x4000] ^= 1u;
#endif
#endif
}

int main(void)
{
    static const int channels[][3] = {
        {255, 255, 255}, {224, 248, 255}, {0, 255, 128}, {-255, 447, 256},
        {INT_MIN, INT_MAX, 224}
    };
    static const int settings[][2] = {{100, 100}, {0, 200}, {200, 0}, {99, 101}};
    unsigned int channel, setting, flags, i, shift;
    uint64_t hash;
#ifndef GENS_COLOR_LOOKUP_REFERENCE
    unit_cases();
#endif
    for (channel = 0; channel < 5; ++channel)
        for (setting = 0; setting < 4; ++setting)
            for (flags = 0; flags < 16; ++flags)
            {
                RMax_Level = channels[channel][0];
                GMax_Level = channels[channel][1];
                BMax_Level = channels[channel][2];
                Brightness_Level = settings[setting][0];
                Contrast_Level = settings[setting][1];
                Mode_555 = (int)(flags & 1u);
                Greyscale = (int)(flags >> 1 & 1u);
                Invert_Color = (int)(flags >> 2 & 1u);
                PinkBG = (uint8_t)(flags >> 3 & 1u);
                update();
                hash = UINT64_C(14695981039346656037);
                for (i = 0; i < 32768; ++i)
                {
                    for (shift = 0; shift < 16; shift += 8)
                    {
                        hash ^= ((uint32_t)Palette[i] >> shift) & 255u;
                        hash *= UINT64_C(1099511628211);
                    }
                    for (shift = 0; shift < 32; shift += 8)
                    {
                        hash ^= (Palette32[i] >> shift) & 255u;
                        hash *= UINT64_C(1099511628211);
                    }
                }
                printf("lookup %u %u %u %016" PRIx64 "\n", channel, setting, flags, hash);
            }
    return 0;
}
