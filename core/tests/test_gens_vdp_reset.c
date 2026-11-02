#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gens_vdp_reset.h"

#ifdef GENS_VDP_RESET_REFERENCE
#define STORAGE extern
void Reset_VDP(void);
#else
#define STORAGE static
#endif

STORAGE uint8_t VRam[65536], CRam[256], VSRam[256], H_Counter_Table[1024], PalLock;
STORAGE uint16_t MD_Screen[80640], MD_Palette[256];
STORAGE uint32_t MD_Screen32[80640], MD_Palette32[256];
STORAGE uint32_t Sprite_Struct[2048], Sprite_Visible[256], VSRam_Over[8];
STORAGE unsigned int VDP_Reg[26];
STORAGE int Ctrl[7], VDP_Status, VDP_Int, DMAT_Tmp, DMAT_Length, DMAT_Type;
STORAGE int VRam_Flag, CRam_Flag, VDP_Current_Line, VDP_Num_Vis_Lines;
STORAGE uint8_t *ScrA_Addr, *ScrB_Addr, *Win_Addr, *Spr_Addr, *H_Scroll_Addr;
STORAGE uint32_t H_Cell, H_Win_Mul, H_Pix, H_Pix_Begin, H_Scroll_Mask;
STORAGE uint32_t H_Scroll_CMul, H_Scroll_CMask, V_Scroll_CMask, V_Scroll_MMask;
STORAGE uint32_t Win_X_Pos, Win_Y_Pos, _main68k_context[36];
static unsigned int irq_calls, irq_level;

void reference_interrupt(unsigned int level, int vector)
{
    assert(vector == -1);
    ++irq_calls;
    irq_level = level;
    _main68k_context[35] = (_main68k_context[35] & UINT32_C(0xffffff00)) | level;
}

#ifndef GENS_VDP_RESET_REFERENCE
static int current_cycles = 345, cpl = 488;
static uint8_t rom[2], ram[65536];
static int rom_size = 2, cd_started, fail_irq;
static GensM68kBusState state;
static GensVdp vdp;

static int interrupt(void *opaque, GensM68kBusState *current, unsigned int level, int vector)
{
    (void)opaque;
    assert(current == &state);
    if (fail_irq)
        return 0;
    reference_interrupt(level, vector);
    return 1;
}

static int odometer(void *opaque, GensM68kBusState *current, uint32_t *value)
{
    (void)opaque; (void)current; (void)value;
    assert(0);
    return 0;
}

static GensVdpIrq irq = {
    &VDP_Reg[0], &VDP_Reg[1], &VDP_Int, (uint8_t *)&_main68k_context[35], 0, interrupt
};
static GensVdpReset reset = {
    &vdp, &irq, MD_Screen, MD_Palette, 80640, 256,
    MD_Screen32, MD_Palette32, Sprite_Struct, Sprite_Visible, VSRam_Over,
    80640, 256, 2048, 256, 8, &PalLock, &DMAT_Tmp
};

static void configure(void)
{
    GensVdpConfig config = {
        {
            VDP_Reg, 26, VRam, sizeof(VRam),
            (uint8_t *)&VRam_Flag, (uint8_t *)&CRam_Flag, (uint8_t *)&V_Scroll_MMask,
            &ScrA_Addr, &ScrB_Addr, &Win_Addr, &Spr_Addr, &H_Scroll_Addr,
            &H_Cell, &H_Win_Mul, &H_Pix, &H_Pix_Begin,
            &H_Scroll_Mask, &H_Scroll_CMul, &H_Scroll_CMask, &V_Scroll_CMask,
            &Win_X_Pos, &Win_Y_Pos, &VDP_Reg[24], &VDP_Reg[25], (uint32_t *)&Ctrl[5],
            &irq, gens_vdp_irq_update
        },
        Ctrl, 7, CRam, VSRam, H_Counter_Table, 256, 256, 1024,
        &VDP_Status, &VRam_Flag, &CRam_Flag, &VDP_Current_Line, &VDP_Num_Vis_Lines,
        &current_cycles, &cpl, &DMAT_Length, &DMAT_Type, rom, ram, 2, sizeof(ram),
        &rom_size, &cd_started, 0, odometer
    };
    assert(gens_vdp_init(&vdp, &config));
}
#endif

static void initialize(unsigned int byte, unsigned int lock)
{
    unsigned int i;
    memset(VRam, (int)byte, sizeof(VRam));
    memset(CRam, (int)byte, sizeof(CRam));
    memset(VSRam, (int)byte, sizeof(VSRam));
    memset(H_Counter_Table, (int)byte, sizeof(H_Counter_Table));
    memset(MD_Screen, (int)byte, sizeof(MD_Screen));
    memset(MD_Screen32, (int)byte, sizeof(MD_Screen32));
    memset(MD_Palette, (int)byte, sizeof(MD_Palette));
    memset(MD_Palette32, (int)byte, sizeof(MD_Palette32));
    memset(Sprite_Struct, (int)byte, sizeof(Sprite_Struct));
    memset(Sprite_Visible, (int)byte, sizeof(Sprite_Visible));
    memset(VSRam_Over, (int)byte, sizeof(VSRam_Over));
    for (i = 0; i < 26; ++i)
        VDP_Reg[i] = 0x12340000u | (byte << 8) | byte;
    VDP_Reg[1] = 0x12340020;
    VDP_Reg[24] = 0x12345678;
    VDP_Reg[25] = 0x87654321;
    for (i = 0; i < 7; ++i)
        Ctrl[i] = (int)(0x23450000u | (byte << 8) | byte);
    VDP_Status = 0x1234ffff;
    VDP_Int = 0x3456000c;
    DMAT_Tmp = DMAT_Length = DMAT_Type = 0x56789abc;
    VRam_Flag = CRam_Flag = 0x6789abcd;
    VDP_Current_Line = 123;
    VDP_Num_Vis_Lines = 224;
    H_Cell = H_Win_Mul = H_Pix = H_Pix_Begin = 0x12345678;
    H_Scroll_Mask = H_Scroll_CMul = H_Scroll_CMask = V_Scroll_CMask = 0x12345678;
    V_Scroll_MMask = Win_X_Pos = Win_Y_Pos = 0x12345678;
    ScrA_Addr = ScrB_Addr = Win_Addr = Spr_Addr = H_Scroll_Addr = VRam + 7;
    _main68k_context[35] = 0x876543ab;
    irq_calls = irq_level = 0;
    PalLock = (uint8_t)lock;
}

static void reset_vdp(void)
{
#ifdef GENS_VDP_RESET_REFERENCE
    Reset_VDP();
#else
    assert(gens_vdp_reset(&reset, &state));
#endif
}

#ifndef GENS_VDP_RESET_REFERENCE
static void unit_cases(void)
{
    GensVdpReset invalid = reset;
    initialize(0xa5, 1);
    invalid.screen16_count = 80639;
    assert(!gens_vdp_reset(&invalid, &state) && MD_Screen[0] == 0xa5a5);
    assert(!gens_vdp_reset(&reset, 0) && VDP_Status == 0x1234ffff);
    reset_vdp();
    assert(MD_Screen[0] == 0 && MD_Screen[80639] == 0 && MD_Screen32[80639] == 0);
    assert(CRam[159] == 0 && CRam[160] == 0xa5 && CRam[255] == 0xa5);
    assert(VSRam[79] == 0 && VSRam[80] == 0xa5 && VSRam[255] == 0xa5);
    assert(Sprite_Struct[799] == 0 && Sprite_Struct[800] == UINT32_C(0xa5a5a5a5));
    assert(Sprite_Visible[99] == 0 && Sprite_Visible[100] == UINT32_C(0xa5a5a5a5));
    assert(Sprite_Visible[123] == UINT32_C(0xa5a5a5a5) && Sprite_Visible[124] == 0);
    assert(Sprite_Visible[125] == 0 && Sprite_Visible[126] == 0);
    assert(Sprite_Visible[127] == UINT32_C(0xa5a5a5a5));
    assert(VSRam_Over[6] == UINT32_C(0xa5a5a5a5) && VSRam_Over[7] == 0);
    assert(MD_Palette[0] == 0xa5a5 && MD_Palette32[255] == UINT32_C(0xa5a5a5a5));
    assert(VDP_Reg[23] == 0x1234a5a5 && VDP_Reg[24] == 0x12340000);
    assert(VDP_Reg[25] == UINT32_C(0x87650000) && VDP_Reg[7] == 0);
    assert(VDP_Status == 0x200 && VDP_Int == 0 && Ctrl[2] == 0 && Ctrl[5] == 0);
    assert(DMAT_Tmp == 0 && DMAT_Length == 0 && DMAT_Type == 0);
    assert(VRam_Flag == 1 && CRam_Flag == 1 && irq_calls == 1 && irq_level == 6);
    assert(VDP_Current_Line == 123 && VDP_Num_Vis_Lines == 224 && current_cycles == 345);
    assert(H_Counter_Table[0] == 0xe8 && H_Counter_Table[1] == 0xe4);
    initialize(0xa5, 2);
    reset_vdp();
    assert(MD_Palette[255] == 0 && MD_Palette32[255] == 0);
    initialize(0xa5, 0);
    fail_irq = 1;
    assert(!gens_vdp_reset(&reset, &state));
    assert(MD_Screen[0] == 0 && VDP_Status == 0x1234ffff && VDP_Int == 0x3456000c);
    assert((VDP_Reg[0] & 255u) == 0 && VDP_Reg[1] == 0x12340020);
    fail_irq = 0;
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
    for (i = 0; i < 80640; ++i) { record(MD_Screen[i]); record(MD_Screen32[i]); }
    for (i = 0; i < 256; ++i)
    {
        record(CRam[i]); record(VSRam[i]); record(MD_Palette[i]); record(MD_Palette32[i]);
        record(Sprite_Visible[i]);
    }
    for (i = 0; i < 65536; ++i) record(VRam[i]);
    for (i = 0; i < 2048; ++i) record(Sprite_Struct[i]);
    for (i = 0; i < 1024; ++i) record(H_Counter_Table[i]);
    for (i = 0; i < 8; ++i) record(VSRam_Over[i]);
    for (i = 0; i < 26; ++i) record(VDP_Reg[i]);
    for (i = 0; i < 7; ++i) record((uint32_t)Ctrl[i]);
    record((uint32_t)(ScrA_Addr - VRam)); record((uint32_t)(ScrB_Addr - VRam));
    record((uint32_t)(Win_Addr - VRam)); record((uint32_t)(Spr_Addr - VRam));
    record((uint32_t)(H_Scroll_Addr - VRam));
    record(H_Cell); record(H_Win_Mul); record(H_Pix); record(H_Pix_Begin);
    record(H_Scroll_Mask); record(H_Scroll_CMul); record(H_Scroll_CMask);
    record(V_Scroll_CMask); record(V_Scroll_MMask); record(Win_X_Pos); record(Win_Y_Pos);
    record((uint32_t)VDP_Status); record((uint32_t)VDP_Int);
    record((uint32_t)DMAT_Tmp); record((uint32_t)DMAT_Length); record((uint32_t)DMAT_Type);
    record((uint32_t)VRam_Flag); record((uint32_t)CRam_Flag);
    record((uint32_t)VDP_Current_Line); record((uint32_t)VDP_Num_Vis_Lines);
    record(_main68k_context[35]); record(irq_calls); record(irq_level); record(PalLock);
}

int main(void)
{
    unsigned int seed, lock;
#ifndef GENS_VDP_RESET_REFERENCE
    configure();
    unit_cases();
#endif
    hash = UINT64_C(14695981039346656037);
    for (seed = 0; seed < 4; ++seed)
        for (lock = 0; lock < 4; ++lock)
        {
            initialize(seed * 85u, lock);
            reset_vdp();
#if defined(GENS_VDP_RESET_NEGATIVE_CONTROL) && !defined(GENS_VDP_RESET_REFERENCE)
            Sprite_Visible[124] ^= 1u;
#endif
            record_state();
        }
    printf("vdp reset %016" PRIx64 "\n", hash);
    return 0;
}
