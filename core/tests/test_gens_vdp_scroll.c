#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gens_vdp_scroll.h"

#ifdef GENS_VDP_SCROLL_REFERENCE
#define STORAGE extern
void reference_scroll_b(void);
#else
#define STORAGE static
#endif

STORAGE uint8_t VRam[65536], CRam[256], VSRam_Over[32], VSRam[256], H_Counter_Table[1024];
STORAGE unsigned int VDP_Reg[26];
STORAGE uint32_t Data_Misc[13], Data_Spr[2];
STORAGE uint16_t Screen_16X[672];
STORAGE uint32_t H_Cell, H_Pix, test_row_offset;
STORAGE uint32_t H_Scroll_Mask, H_Scroll_CMul, H_Scroll_CMask, V_Scroll_CMask;
STORAGE int VDP_Current_Line, VDP_Status;
STORAGE uint8_t *H_Scroll_Addr, *ScrB_Addr;
STORAGE uint8_t ScrollBOn, VScrollBl, VScrollBh, Swap_Scroll_PriorityB;
STORAGE uint32_t Win_X_Pos, Win_Y_Pos, H_Win_Mul, V_Scroll_MMask;
STORAGE uint8_t *ScrA_Addr, *Win_Addr;
STORAGE uint8_t ScrollAOn, VScrollAl, VScrollAh, Swap_Scroll_PriorityA;

#ifdef GENS_VDP_SCROLL_REFERENCE
void reference_scroll_a(void);
#endif

#ifndef GENS_VDP_SCROLL_REFERENCE
static uint32_t Sprite_Struct[2048];
static uint8_t *Spr_Addr;
static int VRam_Flag;
static GensVdpSprites sprites = {
    VRam, CRam, VSRam_Over, VSRam, H_Counter_Table,
    65536, 256, 32, 256, 1024, &Spr_Addr, &VDP_Reg[1], &VDP_Reg[12],
    &VRam_Flag, Sprite_Struct, &Data_Misc[9], 2048
};
static GensVdpScrollB scroll = {
    &sprites, Screen_16X, 672, 0, &ScrB_Addr, &H_Scroll_Addr, &VDP_Reg[11],
    &H_Cell, &H_Scroll_Mask, &H_Scroll_CMul, &H_Scroll_CMask, &V_Scroll_CMask,
    &VDP_Current_Line, &ScrollBOn, &VScrollBl, &VScrollBh, &Swap_Scroll_PriorityB,
    &Data_Misc[2], &Data_Misc[3], &Data_Misc[1], &VDP_Status, 0, 0, 0
};
static GensVdpScrollA scroll_a = {
    &sprites, Screen_16X, 672, 0, &ScrA_Addr, &H_Scroll_Addr, &Win_Addr,
    &VDP_Reg[11], &VDP_Reg[17], &VDP_Reg[18],
    &H_Cell, &H_Scroll_Mask, &H_Scroll_CMul, &H_Scroll_CMask, &V_Scroll_CMask,
    &V_Scroll_MMask, &Win_X_Pos, &Win_Y_Pos, &H_Win_Mul,
    &VDP_Current_Line, &ScrollAOn, &VScrollAl, &VScrollAh, &Swap_Scroll_PriorityA,
    Data_Misc, &VDP_Status, 0, 0, 0
};
#endif

static uint64_t seed;

static uint32_t next(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return (uint32_t)(seed >> 16);
}

static void initialize(void)
{
    unsigned int i;
    for (i = 0; i < 13; ++i) Data_Misc[i] = 0x12345678;
    Data_Spr[0] = 0x12345678;
    Data_Spr[1] = 0x23456789;
    for (i = 0; i < 26; ++i) VDP_Reg[i] = 0;
    for (i = 0; i < 672; ++i) Screen_16X[i] = 0;
    H_Cell = 32;
    H_Pix = 256;
    test_row_offset = 0;
    H_Scroll_Mask = 0;
    H_Scroll_CMul = 5;
    H_Scroll_CMask = 31;
    V_Scroll_CMask = 31;
    VDP_Current_Line = 0;
    VDP_Status = 0x12340000;
    H_Scroll_Addr = VRam + 0x800;
    ScrB_Addr = VRam + 0x2000;
    ScrollBOn = VScrollBl = VScrollBh = 1;
    Swap_Scroll_PriorityB = 0;
    ScrollAOn = VScrollAl = VScrollAh = 1;
    Swap_Scroll_PriorityA = 0;
    Win_X_Pos = Win_Y_Pos = V_Scroll_MMask = 0;
    H_Win_Mul = 5;
    ScrA_Addr = VRam + 0x2000;
    Win_Addr = VRam + 0x4000;
}

static void render(void)
{
#ifdef GENS_VDP_SCROLL_REFERENCE
    reference_scroll_b();
#else
    scroll.interlace = VDP_Reg[12] >> 2 & 1u;
    scroll.shadow = VDP_Reg[12] >> 3 & 1u;
    scroll.cell_scroll = VDP_Reg[11] >> 2 & 1u;
    scroll.row_offset = test_row_offset;
    assert(gens_vdp_scroll_b_draw(&scroll));
#endif
}

static void render_a(void)
{
#ifdef GENS_VDP_SCROLL_REFERENCE
    reference_scroll_a();
#else
    scroll_a.interlace = VDP_Reg[12] >> 2 & 1u;
    scroll_a.shadow = VDP_Reg[12] >> 3 & 1u;
    scroll_a.cell_scroll = VDP_Reg[11] >> 2 & 1u;
    scroll_a.row_offset = test_row_offset;
    assert(gens_vdp_scroll_a_draw(&scroll_a));
#endif
}

#ifndef GENS_VDP_SCROLL_REFERENCE
static void unit_cases(void)
{
    unsigned int i;
    initialize();
    assert(gens_vdp_scroll_b_valid(&scroll));
    memset(VRam, 0, sizeof(VRam));
    memset(VSRam, 0, sizeof(VSRam));
    VRam[0x802] = 0xf8; VRam[0x803] = 0x03;
    VRam[0x2000] = 0x01; VRam[0x2001] = 0x20;
    VRam[32] = 0x21; VRam[33] = 0x43; VRam[34] = 0x65; VRam[35] = 0x87;
    render();
    assert(Screen_16X[0] == 0x0114 && Screen_16X[1] == 0x0113);
    assert(Screen_16X[3] == 0x0111 && Screen_16X[4] == 0x0118);
    assert(Data_Misc[2] == 0xff && Data_Misc[3] == 31 && Data_Misc[1] == 0);

    initialize();
    ScrollBOn = 0;
    for (i = 0; i < 672; ++i) Screen_16X[i] = 0xffff;
    render();
    assert(Screen_16X[0] == 0 && Screen_16X[263] == 0 && Screen_16X[264] == 0xffff);

    initialize();
    H_Cell = 41;
    assert(!gens_vdp_scroll_b_valid(&scroll));
    H_Cell = 32;
    ScrB_Addr = VRam + 0x10000;
    assert(!gens_vdp_scroll_b_valid(&scroll));
    ScrB_Addr = VRam + 0x2000;
    VRam[0x2000] = 0xff; VRam[0x2001] = 0x07;
    VDP_Reg[12] = 4;
    scroll.interlace = 1;
    assert(!gens_vdp_scroll_b_draw(&scroll));

    initialize();
    memset(VRam, 0, sizeof(VRam));
    memset(VSRam, 0, sizeof(VSRam));
    Win_Y_Pos = 1;
    VRam[0x4000] = 0x01; VRam[0x4001] = 0x20;
    VRam[32] = 0x21; VRam[33] = 0x43; VRam[34] = 0x65; VRam[35] = 0x87;
    assert(gens_vdp_scroll_a_valid(&scroll_a));
    render_a();
    assert(Screen_16X[0] == 0 && Screen_16X[7] == 0 && Screen_16X[8] != 0);
    assert(Data_Misc[0] == 0x4000 && Data_Misc[1] == 0);

    initialize();
    memset(VRam, 0, sizeof(VRam));
    memset(VSRam, 0, sizeof(VSRam));
    Win_X_Pos = 2;
    render_a();
    assert(Data_Misc[4] == 2 && Data_Misc[5] == 0xff && Data_Misc[6] == 0 && Data_Misc[7] == 2);
    assert(Data_Misc[3] == 31 && Data_Misc[8] == 0 && Data_Misc[0] == 0x4000);

    initialize();
    Win_X_Pos = 33;
    assert(!gens_vdp_scroll_a_valid(&scroll_a));
    Win_X_Pos = 0;
    Win_Addr = VRam + 0x10000;
    assert(!gens_vdp_scroll_a_valid(&scroll_a));
    H_Cell = 0;
    Win_Addr = VRam + 0x4000;
    assert(!gens_vdp_scroll_a_valid(&scroll_a));
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

static void run_scroll_a(void)
{
    static const uint32_t hmasks[] = {0, 7, 0x1f8, 0x1ff};
    static const uint32_t cmuls[] = {5, 6, 7};
    static const uint32_t cmasks[] = {31, 63, 127};
    static const uint32_t vmasks[] = {31, 63};
    static const uint32_t plane_offsets[] = {0, 0x2000, 0x6000};
    static const uint32_t window_offsets[] = {0x1000, 0x4000, 0xb000};
    static const uint32_t hscroll_offsets[] = {0x800, 0xfc00};
    static const int lines[] = {0, 5, 8, 100, 239};
    unsigned int i, mode, scenario, option, size, cells, x_variant, y_variant;
    uint32_t window_x, window_y;

    hash = UINT64_C(14695981039346656037);
    seed = UINT64_C(0xa5eed341);
    for (mode = 0; mode < 8; ++mode)
        for (scenario = 0; scenario < 256; ++scenario)
        {
            initialize();
            for (i = 0; i < 65536; ++i) VRam[i] = (uint8_t)next();
            for (i = 0; i < 256; ++i) CRam[i] = (uint8_t)next();
            for (i = 0; i < 32; ++i) VSRam_Over[i] = (uint8_t)next();
            for (i = 0; i < 256; ++i) VSRam[i] = (uint8_t)next();
            for (i = 0; i < 1024; ++i) H_Counter_Table[i] = (uint8_t)next();
            if (mode & 1u)
                for (i = 1; i < 65536; i += 2) VRam[i] &= 0xfb;
            size = scenario % 3u;
            option = scenario / 3u % 16u;
            cells = scenario / 48u % 3u;
            x_variant = scenario / 23u % 4u;
            y_variant = scenario / 19u % 4u;
            VDP_Reg[12] = (mode & 3u) << 2;
            VDP_Reg[11] = (mode >> 2) << 2;
            VDP_Reg[17] = scenario / 29u % 2u ? 0x80 : 0;
            VDP_Reg[18] = scenario / 31u % 2u ? 0x80 : 0;
            H_Scroll_Mask = hmasks[scenario / 5u % 4u];
            H_Scroll_CMul = cmuls[size];
            H_Scroll_CMask = cmasks[size];
            V_Scroll_CMask = vmasks[scenario / 7u % 2u];
            V_Scroll_MMask = (mode & 4u) ? 0x7e : 0;
            ScrA_Addr = VRam + plane_offsets[scenario / 11u % 3u];
            Win_Addr = VRam + window_offsets[scenario / 17u % 3u];
            H_Scroll_Addr = VRam + hscroll_offsets[scenario / 13u % 2u];
            VDP_Current_Line = lines[scenario % 5u];
            H_Cell = cells == 0 ? 32u : cells == 1 ? 40u : 1u;
            H_Win_Mul = H_Cell == 40u ? 6u : 5u;
            window_x = x_variant == 0 ? 0u : x_variant == 1 ? 1u : x_variant == 2 ? H_Cell / 2u : H_Cell;
            Win_X_Pos = window_x > H_Cell ? H_Cell : window_x;
            window_y = (uint32_t)VDP_Current_Line >> 3;
            Win_Y_Pos = y_variant == 0 ? 0u : y_variant == 1 ? window_y : y_variant == 2 ? window_y + 1u : 31u;
            ScrollAOn = option & 1u ? 0 : 1;
            VScrollAl = option & 2u ? 0 : 1;
            VScrollAh = option & 4u ? 0 : 1;
            Swap_Scroll_PriorityA = option & 8u ? 1 : 0;
            test_row_offset = scenario & 1u ? 336u : 0u;
            for (i = 0; i < 672; ++i) Screen_16X[i] = (uint16_t)(i & 1u ? 0x0201 : 0x4040);
            render_a();
#if defined(GENS_VDP_SCROLL_NEGATIVE_CONTROL) && !defined(GENS_VDP_SCROLL_REFERENCE)
            Data_Misc[4] ^= 1u;
#endif
            record((uint32_t)VDP_Status);
#ifdef GENS_VDP_SCROLL_REFERENCE
            record(Data_Misc[0] == 0x12345678u ? Data_Misc[0] : Data_Misc[0] - (uint32_t)(uintptr_t)VRam);
#else
            record(Data_Misc[0]);
#endif
            for (i = 1; i < 13; ++i) record(Data_Misc[i]);
            for (i = 0; i < 2; ++i) record(Data_Spr[i]);
            for (i = 0; i < 672; ++i) record(Screen_16X[i]);
        }
    printf("vdp scroll a %016" PRIx64 "\n", hash);
}

int main(void)
{
    static const uint32_t hmasks[] = {0, 7, 0x1f8, 0x1ff};
    static const uint32_t cmuls[] = {5, 6, 7};
    static const uint32_t cmasks[] = {31, 63, 127};
    static const uint32_t vmasks[] = {31, 63};
    static const uint32_t plane_offsets[] = {0, 0x2000, 0x6000};
    static const uint32_t hscroll_offsets[] = {0x800, 0xfc00};
    static const int lines[] = {0, 5, 8, 100, 239};
    unsigned int i, mode, scenario, option, size, cells;
    uint32_t tile;
#ifndef GENS_VDP_SCROLL_REFERENCE
    unit_cases();
#endif
    hash = UINT64_C(14695981039346656037);
    seed = UINT64_C(0x341d5eed);
    for (mode = 0; mode < 8; ++mode)
        for (scenario = 0; scenario < 256; ++scenario)
        {
            initialize();
            for (i = 0; i < 65536; ++i) VRam[i] = (uint8_t)next();
            for (i = 0; i < 256; ++i) CRam[i] = (uint8_t)next();
            for (i = 0; i < 32; ++i) VSRam_Over[i] = (uint8_t)next();
            for (i = 0; i < 256; ++i) VSRam[i] = (uint8_t)next();
            for (i = 0; i < 1024; ++i) H_Counter_Table[i] = (uint8_t)next();
            size = scenario % 3u;
            option = scenario / 3u % 16u;
            cells = scenario / 48u % 3u;
            VDP_Reg[12] = (mode & 3u) << 2;
            VDP_Reg[11] = (mode >> 2) << 2;
            H_Scroll_Mask = hmasks[scenario / 5u % 4u];
            H_Scroll_CMul = cmuls[size];
            H_Scroll_CMask = cmasks[size];
            V_Scroll_CMask = vmasks[scenario / 7u % 2u];
            ScrB_Addr = VRam + plane_offsets[scenario / 11u % 3u];
            H_Scroll_Addr = VRam + hscroll_offsets[scenario / 13u % 2u];
            VDP_Current_Line = lines[scenario % 5u];
            H_Cell = cells == 0 ? 32u : cells == 1 ? 40u : 1u;
            ScrollBOn = option & 1u ? 0 : 1;
            VScrollBl = option & 2u ? 0 : 1;
            VScrollBh = option & 4u ? 0 : 1;
            Swap_Scroll_PriorityB = option & 8u ? 1 : 0;
            test_row_offset = scenario & 1u ? 336u : 0u;
            for (i = 0; i < 8192; ++i)
            {
                tile = next();
                if (mode & 1u) tile &= ~UINT32_C(0x400);
                ScrB_Addr[i * 2u] = (uint8_t)tile;
                ScrB_Addr[i * 2u + 1u] = (uint8_t)(tile >> 8);
            }
            for (i = 0; i < 672; ++i) Screen_16X[i] = (uint16_t)(i & 1u ? 0x0201 : 0x4040);
            render();
#if defined(GENS_VDP_SCROLL_NEGATIVE_CONTROL) && !defined(GENS_VDP_SCROLL_REFERENCE)
            Data_Misc[3] ^= 1u;
#endif
            record((uint32_t)VDP_Status);
            for (i = 0; i < 13; ++i) record(Data_Misc[i]);
            for (i = 0; i < 2; ++i) record(Data_Spr[i]);
            for (i = 0; i < 672; ++i) record(Screen_16X[i]);
        }
    printf("vdp scroll b %016" PRIx64 "\n", hash);
    run_scroll_a();
    return 0;
}
