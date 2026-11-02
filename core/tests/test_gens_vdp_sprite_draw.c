#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gens_vdp_sprite_draw.h"

#ifdef GENS_VDP_SPRITE_DRAW_REFERENCE
#define STORAGE extern
void reference_sprites_draw(void);
#else
#define STORAGE static
#endif

STORAGE uint8_t VRam[65536], CRam[256], VSRam_Over[32], VSRam[256], H_Counter_Table[1024];
STORAGE unsigned int VDP_Reg[26];
STORAGE uint32_t Sprite_Struct[2048], Sprite_Visible[256], Data_Misc[13], Data_Spr[2];
STORAGE uint16_t Screen_16X[672];
STORAGE uint32_t H_Cell, H_Pix, test_row_offset;
STORAGE int VDP_Current_Line, VDP_Status, Sprite_Over, VRam_Flag;
STORAGE uint8_t *Spr_Addr;
STORAGE uint8_t SpriteOn, VSpritel, VSpriteh, Sprite_Always_Top, Swap_Sprite_Priority;

#ifndef GENS_VDP_SPRITE_DRAW_REFERENCE
static GensVdpSprites sprites = {
    VRam, CRam, VSRam_Over, VSRam, H_Counter_Table,
    65536, 256, 32, 256, 1024, &Spr_Addr, &VDP_Reg[1], &VDP_Reg[12],
    &VRam_Flag, Sprite_Struct, &Data_Misc[9], 2048
};
static GensVdpSpriteLine line = {
    Sprite_Struct, &Data_Misc[9], &H_Cell, &H_Pix, 2048,
    Sprite_Visible, &Data_Misc[12], 256, &VDP_Current_Line, &Sprite_Over, &VDP_Status
};
static GensVdpSpriteDraw draw = {
    &sprites, &line, Screen_16X, 672, 0, &SpriteOn, &VSpritel, &VSpriteh,
    &Sprite_Always_Top, &Swap_Sprite_Priority, &Data_Misc[2], &Data_Misc[11],
    &Data_Misc[10], &Data_Spr[0], &Data_Spr[1], 0, 0
};
#endif

static void row(unsigned int index, int x, int y, unsigned int width,
                 unsigned int height, unsigned int tile)
{
    Sprite_Struct[index * 8] = (uint32_t)x;
    Sprite_Struct[index * 8 + 1] = (uint32_t)y;
    Sprite_Struct[index * 8 + 2] = width;
    Sprite_Struct[index * 8 + 3] = height;
    Sprite_Struct[index * 8 + 4] = (uint32_t)(x + (int)(width * 8u) - 1);
    Sprite_Struct[index * 8 + 5] = (uint32_t)(y + (int)(height * 8u) + 7);
    Sprite_Struct[index * 8 + 6] = 0x12340000u | tile;
    Sprite_Struct[index * 8 + 7] = 0x23456789;
}

static void initialize(void)
{
    unsigned int i;
    for (i = 0; i < 2048; ++i) Sprite_Struct[i] = 0;
    for (i = 0; i < 256; ++i) Sprite_Visible[i] = 0xdeadbeef;
    for (i = 0; i < 672; ++i) Screen_16X[i] = 0;
    for (i = 0; i < 13; ++i) Data_Misc[i] = 0x12345678;
    Data_Spr[0] = 0x12345678;
    Data_Spr[1] = 0x23456789;
    VDP_Reg[1] = 0x40;
    VDP_Reg[12] = 0;
    Data_Misc[9] = 0;
    H_Cell = 32;
    H_Pix = 256;
    VDP_Current_Line = 16;
    VDP_Status = 0x12340000;
    Sprite_Over = 1;
    VRam_Flag = 0;
    test_row_offset = 0;
    Spr_Addr = VRam;
    SpriteOn = VSpritel = VSpriteh = 1;
    Sprite_Always_Top = Swap_Sprite_Priority = 0;
}

static void render(void)
{
#ifdef GENS_VDP_SPRITE_DRAW_REFERENCE
    reference_sprites_draw();
#else
    draw.interlace = VDP_Reg[12] >> 2 & 1u;
    draw.shadow = VDP_Reg[12] >> 3 & 1u;
    draw.row_offset = test_row_offset;
    assert(gens_vdp_sprites_draw(&draw));
#endif
}

#ifndef GENS_VDP_SPRITE_DRAW_REFERENCE
static void unit_cases(void)
{
    uint32_t pattern = 0x77777777;
    initialize();
    assert(gens_vdp_sprite_draw_valid(&draw));
    assert(gens_vdp_sprites_read_pattern(&sprites, 67100, &pattern));
    pattern = 0x77777777;
    assert(!gens_vdp_sprites_read_pattern(&sprites, 67101, &pattern) && pattern == 0x77777777);
    memset(VRam, 0x11, sizeof(VRam));
    VRam[0] = 0x21; VRam[1] = 0x43; VRam[2] = 0x65; VRam[3] = 0x87;
    row(0, 0, 16, 2, 1, 0);
    render();
    assert(Screen_16X[8] == 0x2104 && Screen_16X[15] == 0x2105);
    assert(Screen_16X[16] == 0x2101 && Screen_16X[23] == 0x2101);
    assert(Data_Misc[2] == 4 && Data_Misc[10] == 64 && Data_Misc[11] == 0);
    assert(Data_Spr[0] == 0x12345678 && Data_Spr[1] == 15);
    initialize();
    row(0, 0, 16, 2, 1, 0x800);
    render();
    assert(Screen_16X[8] == 0x2101 && Screen_16X[16] == 0x2105);
    assert(Screen_16X[23] == 0x2104 && Data_Spr[0] == 0 && Data_Spr[1] == 0x23456789);
    initialize();
    row(0, -8, 16, 2, 1, 0);
    render();
    assert(Screen_16X[8] == 0x2101 && Data_Spr[1] == 7);
    initialize();
    H_Pix = 0;
    row(0, -8, 16, 2, 1, 0);
    render();
    assert(Screen_16X[8] == 0x2101 && Data_Spr[1] == 0);
    initialize();
    H_Pix = 0;
    row(0, -8, 16, 2, 1, 0x800);
    render();
    assert(Screen_16X[0] == 0x2101 && Data_Spr[0] == UINT32_C(0xfffffff9));
    initialize();
    row(0, 0, 16, 1, 0, 0);
    Screen_16X[8] = 0x0205;
    render();
    assert(Screen_16X[8] == 0x2205);
    initialize();
    row(0, 0, 16, 1, 0, 0);
    Screen_16X[8] = 0x0205;
    Sprite_Always_Top = 2;
    render();
    assert(Screen_16X[8] == 0x2104);
    initialize();
    row(0, 0, 1000, 1, 0, 0);
    render();
    assert(Data_Misc[2] == 0 && Data_Misc[12] == 0 && Data_Misc[10] == 0x12345678);
    initialize();
    row(0, 0, 16, 1, 0, 0x7ff);
    VDP_Reg[12] = 4;
    draw.interlace = 1;
    SpriteOn = 0;
    assert(!gens_vdp_sprites_draw(&draw));
    assert(Data_Misc[12] == 4 && Data_Misc[2] == 0 && Screen_16X[8] == 0);
    initialize();
    draw.interlace = 0;
    draw.row_offset = 337;
    assert(!gens_vdp_sprite_draw_valid(&draw));
    draw.row_offset = 0;
    VDP_Reg[1] = 0;
    render();
    assert(Data_Misc[2] == 0x12345678 && Data_Misc[12] == 0x12345678);
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
    static const int poses[] = {-15, -7, 0, 249, 319, 16};
    unsigned int i, mode, scenario, attributes, option, width, height, tile;
#ifndef GENS_VDP_SPRITE_DRAW_REFERENCE
    unit_cases();
#endif
    for (i = 0; i < 65536; ++i) VRam[i] = (uint8_t)(i * 73u + 0x96u);
    for (i = 0; i < 256; ++i)
    {
        CRam[i] = (uint8_t)(i * 37u + 0xa5u);
        VSRam[i] = (uint8_t)(i ^ 0x69u);
    }
    for (i = 0; i < 32; ++i) VSRam_Over[i] = (uint8_t)(i ^ 0x96u);
    for (i = 0; i < 1024; ++i) H_Counter_Table[i] = (uint8_t)(i ^ 0x5au);
    hash = UINT64_C(14695981039346656037);
    for (mode = 0; mode < 4; ++mode)
        for (scenario = 0; scenario < 48; ++scenario)
            for (attributes = 0; attributes < 8; ++attributes)
                for (option = 0; option < 8; ++option)
                {
                    initialize();
                    width = scenario / 6u % 4u + 1u;
                    height = scenario / 3u % 4u;
                    tile = scenario & 1u ? 0x3ffu : 0u;
                    tile |= (scenario >> 3 & 3u) << 13;
                    tile |= attributes & 1u ? 0x800u : 0u;
                    tile |= attributes & 2u ? 0x1000u : 0u;
                    tile |= attributes & 4u ? 0x8000u : 0u;
                    VDP_Reg[12] = (mode << 2) | (scenario >> 1 & 1u);
                    H_Pix = scenario & 2u ? 320u : 256u;
                    H_Cell = option == 7 ? 1u : (H_Pix == 320 ? 40u : 32u);
                    row(0, poses[scenario % 6u], height >= 2 ? -3 : 13, width, height, tile);
                    row(1, poses[scenario % 6u] + 4, 13, 1, 0, 0x8001);
                    row(2, 8, 13, 1, 0, 0x6002);
                    Data_Misc[9] = 64;
                    if (option == 1) Sprite_Over = 0;
                    if (option == 2) SpriteOn = 0;
                    if (option == 3) VSpritel = 0;
                    if (option == 4) VSpriteh = 0;
                    if (option == 5) Sprite_Always_Top = 2;
                    if (option == 6) Swap_Sprite_Priority = 1;
                    if (option == 7) Sprite_Always_Top = Swap_Sprite_Priority = 1;
                    test_row_offset = scenario & 4u ? 336u : 0u;
                    for (i = 0; i < 672; ++i) Screen_16X[i] = (uint16_t)(i & 1u ? 0x0201 : 0x4040);
                    render();
#if defined(GENS_VDP_SPRITE_DRAW_NEGATIVE_CONTROL) && !defined(GENS_VDP_SPRITE_DRAW_REFERENCE)
                    Data_Misc[10] ^= 32u;
#endif
                    record((uint32_t)VDP_Status);
                    for (i = 0; i < 13; ++i) record(Data_Misc[i]);
                    for (i = 0; i < 2; ++i) record(Data_Spr[i]);
                    for (i = 0; i < 672; ++i) record(Screen_16X[i]);
                    for (i = 0; i < 256; ++i) record(Sprite_Visible[i]);
                    for (i = 0; i < 2048; ++i) record(Sprite_Struct[i]);
                }
    printf("vdp sprite traversal %016" PRIx64 "\n", hash);
    return 0;
}
