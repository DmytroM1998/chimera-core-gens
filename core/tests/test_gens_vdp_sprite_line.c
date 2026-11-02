#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_vdp_sprites.h"

#ifdef GENS_VDP_SPRITE_LINE_REFERENCE
#define STORAGE extern
void reference_sprite_line(void);
#else
#define STORAGE static
#endif

STORAGE uint32_t Sprite_Struct[2048], Sprite_Visible[256], Data_Misc[13], H_Cell, H_Pix;
STORAGE int VDP_Current_Line, VDP_Status, Sprite_Over;

#ifndef GENS_VDP_SPRITE_LINE_REFERENCE
static GensVdpSpriteLine line = {
    Sprite_Struct, &Data_Misc[9], &H_Cell, &H_Pix, 2048,
    Sprite_Visible, &Data_Misc[12], 256, &VDP_Current_Line, &Sprite_Over, &VDP_Status
};
#endif

static void row(unsigned int index, int x, int y, int ymax, uint32_t width, int xmax)
{
    Sprite_Struct[index * 8] = (uint32_t)x;
    Sprite_Struct[index * 8 + 1] = (uint32_t)y;
    Sprite_Struct[index * 8 + 2] = width;
    Sprite_Struct[index * 8 + 3] = 0x12345678;
    Sprite_Struct[index * 8 + 4] = (uint32_t)xmax;
    Sprite_Struct[index * 8 + 5] = (uint32_t)ymax;
    Sprite_Struct[index * 8 + 6] = 0x23456789;
    Sprite_Struct[index * 8 + 7] = 0x3456789a;
}

static void initialize(unsigned int entries)
{
    unsigned int i;
    for (i = 0; i < 256; ++i)
    {
        row(i, 0, 1000, 1000, 1, 7);
        Sprite_Visible[i] = 0xdeadbeef;
    }
    for (i = 0; i < 13; ++i) Data_Misc[i] = 0x12345678;
    Data_Misc[9] = (entries - 1u) * 32u;
    H_Cell = 32;
    H_Pix = 256;
    VDP_Current_Line = 16;
    Sprite_Over = 1;
    VDP_Status = 0x12340020;
}

static void select_line(void)
{
#ifdef GENS_VDP_SPRITE_LINE_REFERENCE
    reference_sprite_line();
#else
    assert(gens_vdp_sprites_select(&line));
#endif
}

#ifndef GENS_VDP_SPRITE_LINE_REFERENCE
static void unit_cases(void)
{
    GensVdpSpriteLine invalid = line;
    invalid.visible_dwords = 255;
    assert(!gens_vdp_sprite_line_valid(&invalid));
    initialize(3);
    row(0, 0, 16, 16, 1, 7);
    row(1, 256, 16, 16, 1, 263);
    row(2, -8, 16, 16, 1, -1);
    select_line();
    assert(Data_Misc[12] == 4 && Sprite_Visible[0] == 0);
    assert(Sprite_Visible[1] == 0xdeadbeef && VDP_Status == 0x12340020);
    initialize(4);
    row(0, -128, 16, 16, 1, -121);
    row(1, 0, 16, 16, 1, 7);
    row(2, -128, 16, 16, 1, -121);
    row(3, 8, 16, 16, 1, 15);
    select_line();
    assert(Data_Misc[12] == 4 && Sprite_Visible[0] == 32 && VDP_Status == 0x12340020);
    initialize(3);
    row(0, 300, 16, 16, 1, 307);
    row(1, -128, 16, 16, 1, -121);
    row(2, 0, 16, 16, 1, 7);
    select_line();
    assert(Data_Misc[12] == 0 && VDP_Status == 0x12340020);
    initialize(3);
    row(1, -128, 16, 16, 1, -121);
    row(2, 0, 16, 16, 1, 7);
    select_line();
    assert(Data_Misc[12] == 4 && Sprite_Visible[0] == 64);
    initialize(3);
    H_Cell = 1;
    row(0, 0, 16, 16, 1, 7);
    row(1, 8, 16, 16, 1, 15);
    row(2, 300, 16, 16, 1, 307);
    select_line();
    assert(Data_Misc[12] == 8 && Sprite_Visible[0] == 0 && Sprite_Visible[1] == 32);
    assert(VDP_Status == 0x12340060 && Sprite_Visible[2] == 0xdeadbeef);
    initialize(3);
    H_Cell = 1;
    row(0, 0, 16, 16, 2, 15);
    row(2, 300, 16, 16, 1, 307);
    select_line();
    assert(Data_Misc[12] == 4 && VDP_Status == 0x12340060);
    initialize(3);
    H_Cell = 1;
    row(0, 0, 16, 16, 1, 7);
    row(1, -128, 16, 16, 1, -121);
    row(2, 8, 16, 16, 1, 15);
    select_line();
    assert(Data_Misc[12] == 4 && VDP_Status == 0x12340020);
    initialize(3);
    H_Cell = 0;
    Sprite_Over = 2;
    row(0, 0, 16, 16, 4, 31);
    row(1, 8, 16, 16, 4, 39);
    row(2, 16, 16, 16, 4, 47);
    select_line();
    assert(Data_Misc[12] == 12 && VDP_Status == 0x12340020);
    initialize(1);
    Data_Misc[9] = UINT32_C(0xffffffff);
    VDP_Current_Line = -1;
    row(0, 0, -2, 0, 1, 7);
    select_line();
    assert(Data_Misc[12] == 4 && Sprite_Visible[0] == 0);
    initialize(2);
    Data_Misc[9] = 63;
    row(0, 0, 16, 16, 1, 7);
    row(1, 8, 16, 16, 1, 15);
    select_line();
    assert(Data_Misc[12] == 8);
    initialize(1);
    VDP_Status = 0x12340060;
    row(0, 0, 16, 16, 1, 7);
    select_line();
    assert(Data_Misc[12] == 4 && VDP_Status == 0x12340060);
    initialize(1);
    Data_Misc[9] = 8192;
    assert(!gens_vdp_sprites_select(&line));
    assert(Data_Misc[12] == 0x12345678 && Sprite_Visible[0] == 0xdeadbeef);
    assert(VDP_Status == 0x12340020);
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
    record((uint32_t)VDP_Status);
    for (i = 0; i < 2048; ++i) record(Sprite_Struct[i]);
    for (i = 0; i < 13; ++i) record(Data_Misc[i]);
    for (i = 0; i < 256; ++i) record(Sprite_Visible[i]);
}

int main(void)
{
    static const unsigned int entries[] = {1, 2, 3, 80, 256};
    static const uint32_t budgets[] = {0, 1, 32, 40, UINT32_C(0xffffffff)};
    static const int lines[] = {-1, 0, 16, 239};
    unsigned int pattern, count, limit, budget, current, i;
#ifndef GENS_VDP_SPRITE_LINE_REFERENCE
    unit_cases();
#endif
    hash = UINT64_C(14695981039346656037);
    for (pattern = 0; pattern < 8; ++pattern)
        for (count = 0; count < 5; ++count)
            for (limit = 0; limit < 4; ++limit)
                for (budget = 0; budget < 5; ++budget)
                    for (current = 0; current < 4; ++current)
                    {
                        initialize(entries[count]);
                        Sprite_Over = (int)limit;
                        H_Cell = budgets[budget];
                        VDP_Current_Line = lines[current];
                        for (i = 0; i < entries[count]; ++i)
                        {
                            int x = (int)(i * 8u % 400u) - 64;
                            int y = (int)(i % 3u) * 16;
                            int ymax = y + 15;
                            uint32_t width = i % 4u + 1u;
                            if (pattern == 1) y = ymax = i + 1u == entries[count] ? 16 : 1000;
                            if (pattern == 2 && i == 1) x = -128;
                            if (pattern == 3) width = 0;
                            if (pattern == 4) width = UINT32_C(0x80000000);
                            if (pattern == 5) x = 300;
                            if (pattern == 6) y = ymax = -1;
                            if (pattern == 7) { y = 16; ymax = 0; }
                            row(i, x, y, ymax, width, x + 7);
                        }
                        select_line();
#if defined(GENS_VDP_SPRITE_LINE_NEGATIVE_CONTROL) && !defined(GENS_VDP_SPRITE_LINE_REFERENCE)
                        Data_Misc[12] ^= 4u;
#endif
                        record_state();
                    }
    printf("vdp scanline sprites %016" PRIx64 "\n", hash);
    return 0;
}
