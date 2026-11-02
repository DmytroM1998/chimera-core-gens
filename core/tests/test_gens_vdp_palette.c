#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_vdp_palette.h"

#ifdef GENS_VDP_PALETTE_REFERENCE
#define STORAGE extern
void reference_palette_update(void);
#else
#define STORAGE static
#endif

STORAGE uint8_t CRam[256], PalLock, PinkBG;
STORAGE uint16_t Palette[32768], MD_Palette[256];
STORAGE uint32_t Palette32[32768], MD_Palette32[256];
STORAGE unsigned int VDP_Reg[26];
STORAGE int CRam_Flag, Mode_555;

#ifndef GENS_VDP_PALETTE_REFERENCE
static GensVdpPalette palette = {
    CRam, sizeof(CRam), Palette, Palette32, 32768, 32768,
    MD_Palette, MD_Palette32, 256, 256,
    &VDP_Reg[7], &Mode_555, &PalLock, &PinkBG, &CRam_Flag
};
#endif

static void initialize(unsigned int seed)
{
    unsigned int i;
    for (i = 0; i < 256; ++i)
    {
        CRam[i] = (uint8_t)(i * 73u + seed);
        MD_Palette[i] = 0xa55a;
        MD_Palette32[i] = 0x12345678;
    }
}

static void update(void)
{
#ifdef GENS_VDP_PALETTE_REFERENCE
    reference_palette_update();
#else
    assert(gens_vdp_palette_update(&palette));
#endif
}

#ifndef GENS_VDP_PALETTE_REFERENCE
static void unit_cases(void)
{
    GensVdpPalette invalid = palette;
    initialize(0);
    invalid.colors16_count = 255;
    assert(!gens_vdp_palette_valid(&invalid));
    CRam_Flag = 0x12345601;
    assert(!gens_vdp_palette_update(&invalid) && CRam_Flag == 0x12345601);
    PalLock = 1;
    update();
    assert(CRam_Flag == 0x12345601 && MD_Palette[0] == 0xa55a);
    assert(MD_Palette32[255] == 0x12345678);
    PalLock = 0;
    CRam_Flag = 0x12345602;
    update();
    assert(CRam_Flag == 0x12345602 && MD_Palette32[0] == 0x12345678);
    CRam[2] = 0xff; CRam[3] = 0xff;
    VDP_Reg[7] = 1;
    CRam_Flag = 0x123456ff;
    Mode_555 = 0;
    PinkBG = 0;
    update();
    assert(CRam_Flag == 0x12345600);
    assert(MD_Palette32[1] == Palette32[0xeee] && MD_Palette32[193] == Palette32[0xeee]);
    assert(MD_Palette32[65] == Palette32[0x777] && MD_Palette32[129] == Palette32[0xeee]);
    assert(MD_Palette[1] == Palette[0xeee] && MD_Palette[65] == Palette[0x777]);
    assert(MD_Palette[0] == MD_Palette[1] && MD_Palette32[0] == MD_Palette32[1]);
    assert(MD_Palette[64] == MD_Palette[65] && MD_Palette32[128] == MD_Palette32[129]);
    PinkBG = 1;
    CRam_Flag = 1;
    update();
    assert(MD_Palette32[0] == 0x00ff00ff && MD_Palette32[192] == 0x00ff00ff);
    assert(MD_Palette[0] == 0xf81f && MD_Palette[64] == 0xf81f);
    Mode_555 = 1;
    CRam_Flag = 1;
    update();
    assert(MD_Palette[0] == 0x7c1f && MD_Palette[128] == 0x7c1f);
    PinkBG = 2;
    PalLock = 2;
    CRam_Flag = 1;
    update();
    assert(MD_Palette32[0] == Palette32[0xeee] && CRam_Flag == 0);
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
    static const unsigned int flags[] = {0, 1, 2, 255};
    unsigned int i, seed, background, shadow, mode, pink, lock, flag;
    for (i = 0; i < 32768; ++i)
    {
        Palette[i] = (uint16_t)(i ^ 0x5a5au);
        Palette32[i] = UINT32_C(0x80000000) | i * 0x010101u;
    }
#ifndef GENS_VDP_PALETTE_REFERENCE
    unit_cases();
#endif
    hash = UINT64_C(14695981039346656037);
    for (seed = 0; seed < 4; ++seed)
        for (background = 0; background < 64; ++background)
            for (shadow = 0; shadow < 2; ++shadow)
                for (mode = 0; mode < 2; ++mode)
                    for (pink = 0; pink < 4; ++pink)
                        for (lock = 0; lock < 4; ++lock)
                            for (flag = 0; flag < 4; ++flag)
                            {
                                initialize(seed * 85u);
                                VDP_Reg[7] = 0x12340000u | background;
                                VDP_Reg[12] = shadow ? 8 : 0;
                                Mode_555 = (int)mode;
                                PinkBG = (uint8_t)pink;
                                PalLock = (uint8_t)lock;
                                CRam_Flag = (int)(0x12345600u | flags[flag]);
                                update();
#if defined(GENS_VDP_PALETTE_NEGATIVE_CONTROL) && !defined(GENS_VDP_PALETTE_REFERENCE)
                                MD_Palette32[64] ^= 1u;
#endif
                                record((uint32_t)CRam_Flag);
                                for (i = 0; i < 256; ++i)
                                {
                                    record(MD_Palette[i]);
                                    record(MD_Palette32[i]);
                                }
                            }
    printf("vdp palette %016" PRIx64 "\n", hash);
    return 0;
}
