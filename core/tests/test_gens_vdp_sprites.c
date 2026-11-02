#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gens_vdp_sprites.h"

#ifdef GENS_VDP_SPRITES_REFERENCE
#define STORAGE extern
void reference_sprites_prepare(void);
#else
#define STORAGE static
#endif

STORAGE uint8_t VRam[65536], CRam[256], VSRam_Over[32], VSRam[256], H_Counter_Table[1024];
STORAGE unsigned int VDP_Reg[26];
STORAGE int VRam_Flag;
STORAGE uint8_t *Spr_Addr;
STORAGE uint32_t Sprite_Struct[2048], Data_Misc[13];

#ifndef GENS_VDP_SPRITES_REFERENCE
static GensVdpSprites sprites = {
    VRam, CRam, VSRam_Over, VSRam, H_Counter_Table,
    sizeof(VRam), sizeof(CRam), sizeof(VSRam_Over), sizeof(VSRam), sizeof(H_Counter_Table),
    &Spr_Addr, &VDP_Reg[1], &VDP_Reg[12], &VRam_Flag, Sprite_Struct, &Data_Misc[9], 2048
};
static uint32_t visible[256], bound, h_cells = 32, h_pixels = 256;
static int current_line = 16, limits = 1, status;
static GensVdpSpriteLine sprite_line = {
    Sprite_Struct, &Data_Misc[9], &h_cells, &h_pixels, 2048,
    visible, &bound, 256, &current_line, &limits, &status
};
#endif

static void byte_at(unsigned int offset, uint8_t value)
{
    if (offset < 65536u) VRam[offset] = value;
    else if (offset < 65792u) CRam[offset - 65536u] = value;
    else if (offset < 65824u) VSRam_Over[offset - 65792u] = value;
    else if (offset < 66080u) VSRam[offset - 65824u] = value;
    else H_Counter_Table[offset - 66080u] = value;
}

static void sprite_row(unsigned int base, unsigned int link, unsigned int next,
                        unsigned int size, unsigned int x, unsigned int y, unsigned int tile)
{
    unsigned int offset = base + link * 8u;
    byte_at(offset, (uint8_t)y); byte_at(offset + 1u, (uint8_t)(y >> 8));
    byte_at(offset + 2u, (uint8_t)next); byte_at(offset + 3u, (uint8_t)size);
    byte_at(offset + 4u, (uint8_t)tile); byte_at(offset + 5u, (uint8_t)(tile >> 8));
    byte_at(offset + 6u, (uint8_t)x); byte_at(offset + 7u, (uint8_t)(x >> 8));
}

static void initialize(unsigned int base, unsigned int chain)
{
    unsigned int i;
    memset(VRam, 0xa5, sizeof(VRam));
    memset(CRam, 0x5a, sizeof(CRam));
    memset(VSRam_Over, 0x96, sizeof(VSRam_Over));
    memset(VSRam, 0x69, sizeof(VSRam));
    memset(H_Counter_Table, 0xff, sizeof(H_Counter_Table));
    for (i = 0; i < 2048; ++i) Sprite_Struct[i] = 0x5a5a5a5a;
    for (i = 0; i < 13; ++i) Data_Misc[i] = 0x12345678;
    Spr_Addr = VRam + base;
    sprite_row(base, 0, chain == 0 ? 0 : chain == 3 ? 127 : 1,
               0xb, 0x1ff, 0x102, 0xabcd);
    sprite_row(base, 1, chain == 2 ? 1 : 0, 0x6, 0x80, 0x84, 0x1234);
    sprite_row(base, 127, 0, 0xf, 0x17f, 0x3fe, 0xa55a);
}

static void prepare(void)
{
#ifdef GENS_VDP_SPRITES_REFERENCE
    reference_sprites_prepare();
#else
    assert(gens_vdp_sprites_prepare(&sprites));
#endif
}

#ifndef GENS_VDP_SPRITES_REFERENCE
static void unit_cases(void)
{
    GensVdpSprites invalid = sprites;
    GensVdpSpriteLine mismatched = sprite_line;
    invalid.table_dwords = 2047;
    assert(!gens_vdp_sprites_valid(&invalid));
    initialize(0, 0);
    VDP_Reg[1] = 0;
    VRam_Flag = 1;
    Spr_Addr = 0;
    prepare();
    assert(VRam_Flag == 1 && Sprite_Struct[0] == 0x5a5a5a5a);
    VDP_Reg[1] = 0x40;
    VRam_Flag = 0x12345604;
    prepare();
    assert(VRam_Flag == 0x12345600 && Data_Misc[9] == 0x12345678);
    initialize(0, 0);
    VDP_Reg[12] = 0;
    VRam_Flag = 0x12345601;
    prepare();
    assert(Sprite_Struct[0] == 383 && Sprite_Struct[1] == 130);
    assert(Sprite_Struct[2] == 0x5a5a5a03 && Sprite_Struct[3] == 0x5a5a5a03);
    assert(Sprite_Struct[4] == 406 && Sprite_Struct[5] == 161);
    assert(Sprite_Struct[6] == 0x5a5aabcd && Sprite_Struct[7] == 0x5a5a5a5a);
    assert(Data_Misc[9] == 0 && VRam_Flag == 0x12345600);
    VDP_Reg[12] = 4;
    VRam_Flag = 1;
    prepare();
    assert(Sprite_Struct[1] == 1 && Sprite_Struct[5] == 32);
    Data_Misc[9] = 0x12345678;
    sprite_row(0, 0, 0, 0, 0, 0xffff, 0x56ab);
    VRam_Flag = 2;
    prepare();
    assert(Sprite_Struct[0] == UINT32_C(0xffffff80) &&
           Sprite_Struct[4] == UINT32_C(0xffffff87));
    assert(Sprite_Struct[1] == 1 && Sprite_Struct[2] == 0x5a5a5a03);
    assert(Sprite_Struct[6] == 0x5a5a56ab && Data_Misc[9] == 0x12345678);
    initialize(0xfe00, 3);
    VRam_Flag = 1;
    prepare();
    assert(Data_Misc[9] == 32 && Sprite_Struct[9] == 383);
    initialize(0, 2);
    VRam_Flag = 1;
    prepare();
    assert(Data_Misc[9] == 2528 && Sprite_Struct[79 * 8] == 0);
    assert(Sprite_Struct[80 * 8] == 0x5a5a5a5a);
    Spr_Addr = VRam + 65536;
    VRam_Flag = 0x12345601;
    assert(!gens_vdp_sprites_prepare(&sprites) && VRam_Flag == 0x12345600);

    initialize(0, 0);
    VDP_Reg[12] = 0;
    VRam_Flag = 1;
    sprite_row(0, 0, 0, 0, 0x80, 0x90, 0);
    mismatched.end = &Data_Misc[8];
    assert(!gens_vdp_sprites_prepare_line(&sprites, &mismatched) && VRam_Flag == 1);
    assert(gens_vdp_sprites_prepare_line(&sprites, &sprite_line));
    assert(bound == 4 && visible[0] == 0 && Data_Misc[9] == 0 && VRam_Flag == 0);
    VDP_Reg[1] = 0;
    Spr_Addr = 0;
    VRam_Flag = 1;
    Data_Misc[9] = bound = 0x12345678;
    assert(gens_vdp_sprites_prepare_line(&sprites, &sprite_line));
    assert(bound == 0x12345678 && Data_Misc[9] == 0x12345678 && VRam_Flag == 1);
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

static void record_state(void)
{
    unsigned int i;
    record((uint32_t)VRam_Flag);
    for (i = 0; i < 2048; ++i) record(Sprite_Struct[i]);
    for (i = 0; i < 13; ++i) record(Data_Misc[i]);
}

int main(void)
{
    static const unsigned int bases[] = {0, 0x100, 0xfc00, 0xfe00, 0xffff};
    static const unsigned int modes[] = {0, 2, 4, 12};
    static const unsigned int dirty[] = {0, 1, 2, 3, 4, 255};
    unsigned int base, mode, flag, enabled, chain;
#ifndef GENS_VDP_SPRITES_REFERENCE
    unit_cases();
#endif
    hash = UINT64_C(14695981039346656037);
    for (base = 0; base < 5; ++base)
        for (mode = 0; mode < 4; ++mode)
            for (flag = 0; flag < 6; ++flag)
                for (enabled = 0; enabled < 2; ++enabled)
                    for (chain = 0; chain < 4; ++chain)
                    {
                        initialize(bases[base], chain);
                        VDP_Reg[1] = enabled ? 0x12340040 : 0x12340000;
                        VDP_Reg[12] = modes[mode];
                        VRam_Flag = (int)(0x12345600u | dirty[flag]);
                        prepare();
#if defined(GENS_VDP_SPRITES_NEGATIVE_CONTROL) && !defined(GENS_VDP_SPRITES_REFERENCE)
                        Sprite_Struct[6] ^= 0x10000u;
#endif
                        record_state();
                    }
    printf("vdp sprite preparation %016" PRIx64 "\n", hash);
    return 0;
}
