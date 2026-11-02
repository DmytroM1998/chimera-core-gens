#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_vdp_video.h"
#include "gens_color_lookup.h"

#ifdef GENS_VDP_VIDEO_REFERENCE
#define STORAGE extern
void reference_video_mode(unsigned int);
void reference_video_convert(void);
void reference_video_render_screen(void);
#else
#define STORAGE static
#endif

STORAGE uint16_t Screen_16X[80640], MD_Screen[80640], Palette[32768];
STORAGE uint32_t MD_Screen32[80640], Palette32[32768], TAB336[240];
STORAGE int VDP_Current_Line, VDP_Num_Vis_Lines;
STORAGE uint8_t FakeVDPScreen;
static unsigned int mode;

static void initialize_lookup(void)
{
    unsigned int i;
    for (i = 0; i < 32768; ++i)
    {
        Palette[i] = (uint16_t)(i ^ 0xa55au);
        Palette32[i] = 0x80000000u | i * 0x010201u;
    }
}

#ifndef GENS_VDP_VIDEO_REFERENCE
static GensVdpVideo video = {
    Screen_16X, 80640, MD_Screen, MD_Screen32, 80640, 80640,
    Palette, Palette32, 32768, 32768, TAB336, 240,
    &VDP_Num_Vis_Lines, &VDP_Current_Line, &mode
};
#endif

static void initialize(unsigned int seed)
{
    unsigned int i;
    for (i = 0; i < 80640; ++i)
    {
        Screen_16X[i] = (uint16_t)((i * 73u + seed) & 0x7fffu);
        MD_Screen[i] = 0xa55a;
        MD_Screen32[i] = 0x12345678;
    }
    for (i = 0; i < 240; ++i) TAB336[i] = i * 336u;
    VDP_Current_Line = 123;
    FakeVDPScreen = 1;
}

static void convert(unsigned int wrapped)
{
#ifdef GENS_VDP_VIDEO_REFERENCE
    reference_video_mode(mode);
    if (wrapped) reference_video_render_screen();
    else reference_video_convert();
#else
    if (wrapped) assert(gens_vdp_video_render_screen(&video, &FakeVDPScreen));
    else assert(gens_vdp_video_convert(&video));
#endif
}

#ifndef GENS_VDP_VIDEO_REFERENCE
static void unit_cases(void)
{
    GensVdpVideo invalid = video;
    GensColorLookup lookup = {Palette, Palette32, 32768, 32768};
    GensColorProfile profile = {255, 255, 255, 100, 100, 0, 0, 0, 1};
    uint32_t rows[241] = {0};
    initialize(0);
    rows[240] = 0x12345678;
    assert(!gens_vdp_video_rows_init(rows, 239));
    assert(gens_vdp_video_rows_init(rows, 241));
    assert(rows[239] == 239u * 336u && rows[240] == 0x12345678);
    invalid.row_count = 239;
    assert(!gens_vdp_video_valid(&invalid));
    VDP_Num_Vis_Lines = 241;
    assert(!gens_vdp_video_render_screen(&video, &FakeVDPScreen));
    assert(FakeVDPScreen == 1 && VDP_Current_Line == 123 && MD_Screen32[8] == 0x12345678);

    VDP_Num_Vis_Lines = 2;
    Screen_16X[0] = 0xffff;
    Screen_16X[8] = 0x4eee;
    mode = 1;
    convert(0);
    assert(VDP_Current_Line == 2 && FakeVDPScreen == 1);
    assert(MD_Screen32[0] == 0x12345678 && MD_Screen[7] == 0xa55a);
    assert(MD_Screen32[8] == Palette32[0x4eee] && MD_Screen[8] == Palette[0x4eee]);
    assert(MD_Screen32[328] == 0 && MD_Screen[328] == 0);
    assert(MD_Screen32[329] == Palette32[Screen_16X[329]]);
    assert(MD_Screen32[672 + 8] == 0x12345678);
    initialize(0);
    mode = 0x80;
    convert(1);
    assert(MD_Screen32[264] == 0 && MD_Screen32[328] == Palette32[Screen_16X[328]]);
    assert(FakeVDPScreen == 0 && VDP_Current_Line == 2);

    initialize(0);
    TAB336[1] = 80640;
    assert(!gens_vdp_video_convert(&video) && VDP_Current_Line == 123);
    assert(MD_Screen32[8] == 0x12345678);
    initialize(0);
    Screen_16X[9] = 0x8000;
    /* Unresolved raw values are masked to the 15-bit lookup range instead of failing. */
    assert(gens_vdp_video_render_screen(&video, &FakeVDPScreen));
    assert(FakeVDPScreen == 0 && VDP_Current_Line == 2);
    assert(MD_Screen32[8] == Palette32[Screen_16X[8]] && MD_Screen32[9] == Palette32[0]);
    initialize(0);
    VDP_Num_Vis_Lines = -1;
    convert(0);
    assert(VDP_Current_Line == 0 && MD_Screen32[8] == 0x12345678);
    assert(gens_color_lookup_generate(&lookup, &profile));
    initialize(0);
    VDP_Num_Vis_Lines = 1;
    mode = 0;
    Screen_16X[8] = 0x0eee;
    Screen_16X[9] = 0x4eee;
    convert(0);
    assert(MD_Screen32[8] == 0x00ff00ff && MD_Screen[8] == 0xf81f);
    assert(MD_Screen32[9] == 0xfffcfcfc && MD_Screen[9] == 0xffff);
}
#endif

static uint64_t hash;

static void record(uint32_t value)
{
    unsigned int shift;
    for (shift = 0; shift < 32; shift += 8)
    {
        hash ^= (value >> shift) & 255u;
        hash *= UINT64_C(1099511628211);
    }
}

int main(void)
{
    static const int lines[] = {-1, 0, 1, 224, 240};
    static const unsigned int modes[] = {0, 1, 0x80, 0x81};
    unsigned int i, seed, count, setting, wrapped;
    initialize_lookup();
#ifndef GENS_VDP_VIDEO_REFERENCE
    unit_cases();
    initialize_lookup();
#endif
    hash = UINT64_C(14695981039346656037);
    for (seed = 0; seed < 4; ++seed)
        for (count = 0; count < 5; ++count)
            for (setting = 0; setting < 4; ++setting)
                for (wrapped = 0; wrapped < 2; ++wrapped)
                {
                    initialize(seed * 85u);
                    VDP_Num_Vis_Lines = lines[count];
                    mode = modes[setting];
                    convert(wrapped);
#if defined(GENS_VDP_VIDEO_NEGATIVE_CONTROL) && !defined(GENS_VDP_VIDEO_REFERENCE)
                    MD_Screen32[8] ^= 1u;
#endif
                    record((uint32_t)VDP_Current_Line);
                    record(FakeVDPScreen);
                    for (i = 0; i < 80640; ++i)
                    {
                        record(MD_Screen[i]);
                        record(MD_Screen32[i]);
                    }
                }
    printf("vdp video conversion %016" PRIx64 "\n", hash);
    return 0;
}
