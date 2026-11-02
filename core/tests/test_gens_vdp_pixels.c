#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_vdp_pixels.h"

#ifdef GENS_VDP_PIXELS_REFERENCE
#define STORAGE extern
void reference_pixel_row(void);
void reference_pixel_resolve(void);
#else
#define STORAGE static
#endif

STORAGE uint16_t Screen_16X[384];
STORAGE uint8_t CRam[256];
STORAGE uint32_t test_pattern, test_palette, test_offset, test_variant;
STORAGE uint32_t test_enabled, test_priority_enabled;
STORAGE int VDP_Status;
STORAGE unsigned int test_background;
STORAGE int H_Pix_Begin;

static void initialize(unsigned int seed)
{
    unsigned int i;
    for (i = 0; i < 384; ++i) Screen_16X[i] = (uint16_t)(seed ^ (i * 0x103u));
    for (i = 0; i < 256; ++i) CRam[i] = (uint8_t)(i * 73u + seed);
    test_offset = 8;
    test_pattern = 0x87654321;
    test_palette = 0x10;
    test_variant = 0;
    test_enabled = test_priority_enabled = 1;
    VDP_Status = 0x12340040;
    test_background = 0;
    H_Pix_Begin = 0;
}

static void resolve(void)
{
#ifdef GENS_VDP_PIXELS_REFERENCE
    reference_pixel_resolve();
#else
    GensVdpPixelResolve resolve = {
        Screen_16X, 384, test_offset, CRam, sizeof(CRam), &test_background, &H_Pix_Begin
    };
    assert(gens_vdp_pixel_resolve(&resolve));
#endif
}

static void draw(void)
{
#ifdef GENS_VDP_PIXELS_REFERENCE
    reference_pixel_row();
#else
    GensVdpPixelRow row = {
        Screen_16X, 64, test_offset, test_pattern, test_palette,
        (enum GensVdpPixelLayer)(test_variant / 8u), test_variant >> 2 & 1u,
        test_variant >> 1 & 1u, test_variant & 1u, test_enabled, test_priority_enabled, &VDP_Status
    };
    assert(gens_vdp_pixel_row(&row));
#endif
}

#ifndef GENS_VDP_PIXELS_REFERENCE
static void unit_cases(void)
{
    GensVdpPixelRow invalid = {
        Screen_16X, 64, 57, 0, 0, GENS_VDP_PIXELS_SCROLL_B, 0, 0, 0, 1, 1, &VDP_Status
    };
    GensVdpPixelResolve invalid_resolve = {
        Screen_16X, 384, 8, CRam, sizeof(CRam), &test_background, &H_Pix_Begin
    };
    unsigned int i;
    assert(!gens_vdp_pixel_row_valid(&invalid));
    assert(!gens_vdp_pixel_row(&invalid));
    initialize(0);
    invalid.offset = 8;
    invalid.priority = 2;
    assert(!gens_vdp_pixel_row(&invalid) && Screen_16X[8] == 8u * 0x103u);
    invalid.priority = 0;
    invalid.layer = (enum GensVdpPixelLayer)3;
    assert(!gens_vdp_pixel_row(&invalid) && Screen_16X[8] == 8u * 0x103u);
    invalid.layer = (enum GensVdpPixelLayer)-1;
    assert(!gens_vdp_pixel_row(&invalid) && Screen_16X[8] == 8u * 0x103u);
    invalid.layer = GENS_VDP_PIXELS_SCROLL_B;
    invalid.status = 0;
    assert(!gens_vdp_pixel_row(&invalid) && Screen_16X[8] == 8u * 0x103u);
    initialize(0);
    draw();
    assert(Screen_16X[8] == 0x0114 && Screen_16X[11] == 0x0111);
    assert(Screen_16X[12] == 0x0118 && Screen_16X[15] == 0x0115);
    assert(Screen_16X[7] == (uint16_t)(7u * 0x103u) && Screen_16X[16] == 16u * 0x103u);
    test_variant = 1;
    draw();
    assert(Screen_16X[8] == 0x0115 && Screen_16X[15] == 0x0114);
    test_variant = 2;
    test_enabled = 0;
    draw();
    for (i = 8; i < 16; ++i) assert(Screen_16X[i] == 0x4040);
    test_variant = 14;
    test_pattern = 0;
    draw();
    assert(Screen_16X[8] == 0x4040);
    test_enabled = 1;
    draw();
    for (i = 8; i < 16; ++i) assert(Screen_16X[i] == 0);

    test_variant = 16;
    test_pattern = 0x1000;
    test_palette = 0x20;
    Screen_16X[8] = 0x020a;
    draw();
    assert(Screen_16X[8] == 0x220a && VDP_Status == 0x12340040);
    draw();
    assert(Screen_16X[8] == 0x220a && VDP_Status == 0x12340060);
    Screen_16X[8] = 0x020a;
    test_variant = 20;
    draw();
    assert(Screen_16X[8] == 0x2121);
    Screen_16X[8] = 0x0105;
    test_variant = 22;
    test_pattern = 0xe000;
    test_palette = 0x30;
    draw();
    assert(Screen_16X[8] == 0x8185);
    test_pattern = 0xf000;
    draw();
    assert(Screen_16X[8] == 0xc1c5 && (Screen_16X[8] & 0x2000u) == 0u);
    Screen_16X[8] = 0x4105;
    test_pattern = 0x1000;
    test_palette = 0x20;
    draw();
    assert(Screen_16X[8] == 0x2121);
    Screen_16X[8] = 0x4105;
    test_variant = 18;
    draw();
    assert(Screen_16X[8] == 0x2161);
    test_variant = 8;
    test_palette = 0x1ff;
    Screen_16X[8] = 0x4405;
    draw();
    assert(Screen_16X[8] == 0x4500);
    test_variant = 0;
    test_palette = 0;
    test_background = 2;
    CRam[2] = 0xee; CRam[3] = 0x0e;
    CRam[4] = 0x34; CRam[5] = 0x12;
    H_Pix_Begin = 159;
    draw();
    assert(Screen_16X[8] == 0x0101 && Screen_16X[9] == 0);
    resolve();
    assert(Screen_16X[8] == 0x4eee && Screen_16X[9] == 0x0224);
    Screen_16X[8] = 0x01c1;
    resolve();
    assert(Screen_16X[8] == 0x7eee);
    H_Pix_Begin = 160;
    assert(!gens_vdp_pixel_resolve(&invalid_resolve) && Screen_16X[8] == 0x7eee);
    H_Pix_Begin = -1;
    assert(!gens_vdp_pixel_resolve_valid(&invalid_resolve));
}
#endif

int main(void)
{
    static const uint32_t patterns[] = {
        0, 0x1000, 0x87654321, 0xeeeeeeee, 0xffffffff, 0x10101010, 0x80000001, 0x0f0f0f0f
    };
    static const uint32_t palettes[] = {0, 0x10, 0x20, 0x30, 0xff, 0x1ff, 0xfffffff0};
    static const unsigned int seeds[] = {0, 0x0201, 0x2142, 0xffff};
    static const int begins[] = {0, 32, 132, 159};
    unsigned int variant, pattern, palette, enabled, priority_enabled, seed, i, shift;
    uint64_t hash = UINT64_C(14695981039346656037);
#ifndef GENS_VDP_PIXELS_REFERENCE
    unit_cases();
#endif
    for (variant = 0; variant < 24; ++variant)
        for (pattern = 0; pattern < 8; ++pattern)
            for (palette = 0; palette < 7; ++palette)
                for (enabled = 0; enabled < 4; ++enabled)
                    for (priority_enabled = 0; priority_enabled < 4; ++priority_enabled)
                        for (seed = 0; seed < 4; ++seed)
                        {
                            initialize(seeds[seed]);
                            test_variant = variant;
                            test_pattern = patterns[pattern];
                            test_palette = palettes[palette];
                            test_enabled = enabled;
                            test_priority_enabled = priority_enabled;
                            draw();
#if defined(GENS_VDP_PIXELS_NEGATIVE_CONTROL) && !defined(GENS_VDP_PIXELS_REFERENCE)
                            Screen_16X[8] ^= 0x100u;
#endif
                            for (i = 0; i < 65; ++i)
                            {
                                uint32_t value = i == 64 ? (uint32_t)VDP_Status : Screen_16X[i];
                                for (shift = 0; shift < 32; shift += 8)
                                {
                                    hash ^= (value >> shift) & 255u;
                                    hash *= UINT64_C(1099511628211);
                                }
                            }
                        }
    printf("vdp pixel rows %016" PRIx64 "\n", hash);
    hash = UINT64_C(14695981039346656037);
    for (seed = 0; seed < 4; ++seed)
        for (palette = 0; palette < 64; ++palette)
            for (variant = 0; variant < 4; ++variant)
            {
                initialize(seeds[seed]);
                test_background = 0x12340000u | palette;
                H_Pix_Begin = begins[variant];
                resolve();
                for (i = 0; i < 384; ++i)
                    for (shift = 0; shift < 32; shift += 8)
                    {
                        hash ^= ((uint32_t)Screen_16X[i] >> shift) & 255u;
                        hash *= UINT64_C(1099511628211);
                    }
            }
    printf("vdp pixel resolution %016" PRIx64 "\n", hash);
    return 0;
}
