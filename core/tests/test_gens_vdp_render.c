#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gens_vdp_render.h"

#ifdef GENS_VDP_RENDER_REFERENCE
#define STORAGE extern
void Render_Line(void);
#else
#define STORAGE static
#endif

#define SCREEN_PIXELS 80640u

STORAGE uint8_t VRam[65536], CRam[256], VSRam_Over[32], VSRam[256], H_Counter_Table[1024];
STORAGE unsigned int VDP_Reg[26];
STORAGE uint32_t Sprite_Struct[2048], Sprite_Visible[256], Data_Misc[13], Data_Spr[2];
STORAGE uint16_t Screen_16X[SCREEN_PIXELS], Palette[32768], MD_Palette[256];
STORAGE uint32_t Palette32[32768], MD_Palette32[256];
STORAGE uint32_t H_Cell, H_Pix, H_Win_Mul, H_Scroll_Mask, H_Scroll_CMul, H_Scroll_CMask;
STORAGE uint32_t V_Scroll_CMask, V_Scroll_MMask, Win_X_Pos, Win_Y_Pos;
STORAGE int H_Pix_Begin, VDP_Current_Line, VDP_Status, VRam_Flag, CRam_Flag, Sprite_Over, Mode_555;
STORAGE uint8_t *ScrA_Addr, *ScrB_Addr, *Win_Addr, *Spr_Addr, *H_Scroll_Addr;
STORAGE uint8_t ScrollAOn, ScrollBOn, VScrollAl, VScrollAh, VScrollBl, VScrollBh;
STORAGE uint8_t VSpritel, VSpriteh, Sprite_Always_Top, Swap_Scroll_PriorityA;
STORAGE uint8_t Swap_Scroll_PriorityB, Swap_Sprite_Priority, SpriteOn, PalLock, PinkBG;

#ifndef GENS_VDP_RENDER_REFERENCE
static uint32_t rows[240];
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
    &sprites, &line, Screen_16X, SCREEN_PIXELS, 0, &SpriteOn, &VSpritel, &VSpriteh,
    &Sprite_Always_Top, &Swap_Sprite_Priority, &Data_Misc[2], &Data_Misc[11],
    &Data_Misc[10], &Data_Spr[0], &Data_Spr[1], 0, 0
};
static GensVdpScrollB scroll_b = {
    &sprites, Screen_16X, SCREEN_PIXELS, 0, &ScrB_Addr, &H_Scroll_Addr, &VDP_Reg[11],
    &H_Cell, &H_Scroll_Mask, &H_Scroll_CMul, &H_Scroll_CMask, &V_Scroll_CMask,
    &VDP_Current_Line, &ScrollBOn, &VScrollBl, &VScrollBh, &Swap_Scroll_PriorityB,
    &Data_Misc[2], &Data_Misc[3], &Data_Misc[1], &VDP_Status, 0, 0, 0
};
static GensVdpScrollA scroll_a = {
    &sprites, Screen_16X, SCREEN_PIXELS, 0, &ScrA_Addr, &H_Scroll_Addr, &Win_Addr,
    &VDP_Reg[11], &VDP_Reg[17], &VDP_Reg[18],
    &H_Cell, &H_Scroll_Mask, &H_Scroll_CMul, &H_Scroll_CMask, &V_Scroll_CMask,
    &V_Scroll_MMask, &Win_X_Pos, &Win_Y_Pos, &H_Win_Mul,
    &VDP_Current_Line, &ScrollAOn, &VScrollAl, &VScrollAh, &Swap_Scroll_PriorityA,
    Data_Misc, &VDP_Status, 0, 0, 0
};
static GensVdpPalette palette = {
    CRam, 256, Palette, Palette32, 32768, 32768, MD_Palette, MD_Palette32, 256, 256,
    &VDP_Reg[7], &Mode_555, &PalLock, &PinkBG, &CRam_Flag
};
static GensVdpPixelResolve resolve = {
    Screen_16X, SCREEN_PIXELS, 0, CRam, 256, &VDP_Reg[7], &H_Pix_Begin
};
static GensVdpRender render_state = {
    &sprites, &scroll_b, &scroll_a, &draw, &palette, &resolve, Screen_16X, SCREEN_PIXELS,
    rows, 240, &VDP_Current_Line
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

static void render(void)
{
#ifdef GENS_VDP_RENDER_REFERENCE
    Render_Line();
#else
    assert(gens_vdp_render_line(&render_state));
#endif
}

static void initialize(void)
{
    unsigned int i;
    for (i = 0; i < 2048; ++i) Sprite_Struct[i] = 0;
    for (i = 0; i < 256; ++i) Sprite_Visible[i] = 0xdeadbeef;
    for (i = 0; i < SCREEN_PIXELS; ++i) Screen_16X[i] = 0;
    for (i = 0; i < 13; ++i) Data_Misc[i] = 0x12345678;
    Data_Spr[0] = 0x12345678;
    Data_Spr[1] = 0x23456789;
    Data_Misc[9] = 0;
    for (i = 0; i < 26; ++i) VDP_Reg[i] = 0;
    for (i = 0; i < 256; ++i) MD_Palette[i] = (uint16_t)(0x1111u * (i & 15u));
    for (i = 0; i < 256; ++i) MD_Palette32[i] = 0x01010101u * i;
    VDP_Reg[1] = 0x40;
    H_Cell = 32;
    H_Pix = 256;
    H_Pix_Begin = 32;
    H_Win_Mul = 5;
    H_Scroll_Mask = 0;
    H_Scroll_CMul = 5;
    H_Scroll_CMask = V_Scroll_CMask = 31;
    V_Scroll_MMask = Win_X_Pos = Win_Y_Pos = 0;
    VDP_Current_Line = 0;
    VDP_Status = 0x12340000;
    VRam_Flag = CRam_Flag = Sprite_Over = Mode_555 = 0;
    H_Scroll_Addr = VRam + 0x800;
    ScrA_Addr = VRam + 0x2000;
    ScrB_Addr = VRam + 0x4000;
    Win_Addr = VRam + 0x6000;
    Spr_Addr = VRam + 0xd800;
    ScrollAOn = ScrollBOn = VScrollAl = VScrollAh = VScrollBl = VScrollBh = 1;
    VSpritel = VSpriteh = SpriteOn = 1;
    Sprite_Always_Top = Swap_Scroll_PriorityA = Swap_Scroll_PriorityB = 0;
    Swap_Sprite_Priority = PalLock = PinkBG = 0;
#ifndef GENS_VDP_RENDER_REFERENCE
    for (i = 0; i < 240; ++i) rows[i] = i * 336u;
#endif
}

#ifndef GENS_VDP_RENDER_REFERENCE
static void render_manual(void)
{
    size_t row = rows[VDP_Current_Line];
    unsigned int i;
    if ((VDP_Reg[1] & 0x40u) == 0u)
        for (i = 0; i < 320u; ++i) Screen_16X[row + 8u + i] = VDP_Reg[12] & 8u ? 0x4040 : 0;
    else
    {
        assert(gens_vdp_sprites_prepare(&sprites));
        scroll_b.interlace = scroll_a.interlace = draw.interlace = VDP_Reg[12] >> 2 & 1u;
        scroll_b.shadow = scroll_a.shadow = draw.shadow = VDP_Reg[12] >> 3 & 1u;
        scroll_b.cell_scroll = scroll_a.cell_scroll = VDP_Reg[11] >> 2 & 1u;
        scroll_b.row_offset = scroll_a.row_offset = draw.row_offset = row;
        assert(gens_vdp_scroll_b_draw(&scroll_b));
        assert(gens_vdp_scroll_a_draw(&scroll_a));
        assert(gens_vdp_sprites_draw(&draw));
    }
    assert(gens_vdp_palette_update(&palette));
    resolve.offset = row + 8u;
    assert(gens_vdp_pixel_resolve(&resolve));
}

static void unit_cases(void)
{
    unsigned int i;
    initialize();
    assert(gens_vdp_render_valid(&render_state));
    VDP_Reg[1] = 0;
    VDP_Reg[12] = 8;
    VDP_Current_Line = 2;
    for (i = 0; i < SCREEN_PIXELS; ++i) Screen_16X[i] = 0xffff;
    CRam[0] = 0xee; CRam[1] = 0x0e;
    render();
    assert(Screen_16X[2 * 336 + 7] == 0xffff && Screen_16X[2 * 336 + 8] == 0x1eee);
    assert(Screen_16X[2 * 336 + 8 + 255] == 0x1eee && Screen_16X[2 * 336 + 8 + 256] == 0x4040);
    assert(Screen_16X[2 * 336 + 8 + 320] == 0xffff && Data_Misc[2] == 0x12345678);
    for (i = 0; i < SCREEN_PIXELS; ++i) Screen_16X[i] = 0xffff;
    render_manual();
    assert(Screen_16X[2 * 336 + 8] == 0x1eee && Screen_16X[2 * 336 + 8 + 256] == 0x4040);

    initialize();
    VDP_Current_Line = 240;
    assert(!gens_vdp_render_valid(&render_state));
    VDP_Current_Line = 0;
    render_state.pixel_count = 335;
    assert(!gens_vdp_render_valid(&render_state));
    render_state.pixel_count = SCREEN_PIXELS;
}
#endif

static uint64_t hash;

#ifdef GENS_VDP_RENDER_VERBOSE
static uint64_t part_hash;
#endif

static void record(uint32_t value)
{
    unsigned int shift;
    for (shift = 0; shift < 32; shift += 8)
    {
        hash ^= (value >> shift) & 255u;
        hash *= UINT64_C(1099511628211);
#ifdef GENS_VDP_RENDER_VERBOSE
        part_hash ^= (value >> shift) & 255u;
        part_hash *= UINT64_C(1099511628211);
#endif
    }
}

#ifdef GENS_VDP_RENDER_VERBOSE
#define PART(name, statement) do { part_hash = 0; statement; \
    printf("%u %s %016" PRIx64 "\n", scenario, name, part_hash); } while (0)
#else
#define PART(name, statement) do { statement; } while (0)
#endif

static unsigned int bit(uint32_t value, unsigned int index)
{
    return value >> index & 1u;
}

static void scenario_setup(uint32_t r)
{
    static const uint32_t hmasks[] = {0, 7, 0x1f8, 0x1ff};
    static const uint32_t cmuls[] = {5, 6, 7};
    static const uint32_t cmasks[] = {31, 63, 127};
    static const uint32_t plane_offsets[] = {0, 0x2000, 0x6000};
    static const uint32_t window_offsets[] = {0x1000, 0x4000, 0xb000};
    static const uint32_t sprite_offsets[] = {0, 0x5000, 0xd800};
    static const int lines[] = {0, 5, 8, 100, 239, 17, 200};
    unsigned int i, size, interlace, shadow, cell_scroll, wide, variant;
    uint32_t x, y;

    for (i = 0; i < 65536; ++i) VRam[i] = (uint8_t)next();
    for (i = 0; i < 256; ++i) CRam[i] = (uint8_t)next();
    for (i = 0; i < 32; ++i) VSRam_Over[i] = (uint8_t)next();
    for (i = 0; i < 256; ++i) VSRam[i] = (uint8_t)next();
    for (i = 0; i < 1024; ++i) H_Counter_Table[i] = (uint8_t)next();
    for (i = 0; i < 32768; ++i) Palette[i] = (uint16_t)next();
    for (i = 0; i < 32768; ++i)
    {
        Palette32[i] = next();
        Palette32[i] ^= next() << 16;
    }
    for (i = 0; i < 256; ++i) MD_Palette[i] = (uint16_t)next();
    for (i = 0; i < 256; ++i) MD_Palette32[i] = next();
    interlace = bit(r, 0);
    shadow = bit(r, 1);
    cell_scroll = bit(r, 2);
    wide = bit(r, 3);
    if (interlace)
        for (i = 1; i < 65536; i += 2) VRam[i] &= 0xfb;
    VDP_Reg[1] = (r >> 4 & 7u) == 0u ? 0u : 0x40u;
    VDP_Reg[7] = next() & 0xffu;
    VDP_Reg[11] = cell_scroll << 2;
    VDP_Reg[12] = interlace << 2 | shadow << 3;
    VDP_Reg[17] = bit(r, 7) ? 0x80u : 0u;
    VDP_Reg[18] = bit(r, 8) ? 0x80u : 0u;
    size = r >> 9 & 3u;
    if (size == 3u) size = 0u;
    H_Cell = wide ? 40u : 32u;
    H_Pix = wide ? 320u : 256u;
    H_Pix_Begin = wide ? 0 : 32;
    H_Win_Mul = wide ? 6u : 5u;
    H_Scroll_Mask = hmasks[r >> 11 & 3u];
    H_Scroll_CMul = cmuls[size];
    H_Scroll_CMask = cmasks[size];
    V_Scroll_CMask = bit(r, 13) ? 63u : 31u;
    V_Scroll_MMask = cell_scroll ? 0x7eu : 0u;
    ScrA_Addr = VRam + plane_offsets[r >> 14 & 1u];
    ScrB_Addr = VRam + plane_offsets[1u + (r >> 15 & 1u)];
    Win_Addr = VRam + window_offsets[(r >> 16 & 3u) % 3u];
    Spr_Addr = VRam + sprite_offsets[(r >> 18 & 3u) % 3u];
    H_Scroll_Addr = VRam + (bit(r, 20) ? 0xfc00u : 0x800u);
    VDP_Current_Line = lines[(r >> 21 & 7u) % 7u];
    variant = r >> 24 & 3u;
    x = variant == 0u ? 0u : variant == 1u ? 1u : variant == 2u ? H_Cell / 2u : H_Cell;
    Win_X_Pos = x;
    variant = r >> 26 & 3u;
    y = (uint32_t)VDP_Current_Line >> 3;
    Win_Y_Pos = variant == 0u ? 0u : variant == 1u ? y : variant == 2u ? y + 1u : 31u;
    VRam_Flag = (int)next();
    CRam_Flag = (int)next();
    Sprite_Over = (int)(next() & 1u);
    Mode_555 = (int)(next() & 1u);
    ScrollAOn = (uint8_t)(next() % 8u != 0u);
    ScrollBOn = (uint8_t)(next() % 8u != 0u);
    VScrollAl = (uint8_t)(next() % 8u != 0u);
    VScrollAh = (uint8_t)(next() % 8u != 0u);
    VScrollBl = (uint8_t)(next() % 8u != 0u);
    VScrollBh = (uint8_t)(next() % 8u != 0u);
    VSpritel = (uint8_t)(next() % 8u != 0u);
    VSpriteh = (uint8_t)(next() % 8u != 0u);
    SpriteOn = (uint8_t)(next() % 8u != 0u);
    Sprite_Always_Top = (uint8_t)(next() & 1u);
    Swap_Scroll_PriorityA = (uint8_t)(next() & 1u);
    Swap_Scroll_PriorityB = (uint8_t)(next() & 1u);
    Swap_Sprite_Priority = (uint8_t)(next() & 1u);
    PalLock = (uint8_t)(next() % 8u == 0u);
    PinkBG = (uint8_t)(next() % 4u == 0u);
}

int main(void)
{
    unsigned int i, scenario, first;
    uint32_t r;
#ifndef GENS_VDP_RENDER_REFERENCE
    unit_cases();
#endif
    hash = UINT64_C(14695981039346656037);
    seed = UINT64_C(0x341eed5e);
    for (scenario = 0; scenario < 1536; ++scenario)
    {
        initialize();
        r = next();
        r ^= next() << 16;
        scenario_setup(r);
        first = (unsigned int)VDP_Current_Line * 336u;
        for (i = 0; i < 336; ++i) Screen_16X[first + i] = (uint16_t)(i & 1u ? 0x0201 : 0x4040);
        render();
#if defined(GENS_VDP_RENDER_NEGATIVE_CONTROL) && !defined(GENS_VDP_RENDER_REFERENCE)
        Screen_16X[first + 8] ^= 1u;
#endif
        PART("status", record((uint32_t)VDP_Status));
        PART("vram_flag", record((uint32_t)VRam_Flag));
        PART("cram_flag", record((uint32_t)CRam_Flag));
        PART("data_misc", {
#ifdef GENS_VDP_RENDER_REFERENCE
            /* The original keeps a host pointer into VRAM in Data_Misc[0]. The port keeps the offset. */
            record(Data_Misc[0] == 0x12345678u ? Data_Misc[0] :
                   Data_Misc[0] - (uint32_t)(uintptr_t)VRam);
#else
            record(Data_Misc[0]);
#endif
            for (i = 1; i < 13; ++i) record(Data_Misc[i]);
        });
#ifdef GENS_VDP_RENDER_VERBOSE
        for (i = 0; i < 13; ++i) printf("%u misc%u %08" PRIx32 "\n", scenario, i, Data_Misc[i]);
#endif
        PART("data_spr", for (i = 0; i < 2; ++i) record(Data_Spr[i]));
        PART("screen", for (i = 0; i < 336; ++i) record(Screen_16X[first + i]));
        PART("md_palette", for (i = 0; i < 256; ++i) record(MD_Palette[i]));
        PART("md_palette32", for (i = 0; i < 256; ++i) record(MD_Palette32[i]));
        PART("visible", for (i = 0; i < 256; ++i) record(Sprite_Visible[i]));
        PART("struct", for (i = 0; i < 2048; ++i) record(Sprite_Struct[i]));
    }
    printf("vdp render line %016" PRIx64 "\n", hash);
    return 0;
}
