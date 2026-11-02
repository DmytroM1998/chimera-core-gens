#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_vdp_registers.h"

#ifdef GENS_VDP_REGISTER_REFERENCE
#define STORAGE extern
void Set_VDP_Reg(unsigned int, unsigned char);
#else
#define STORAGE static
#endif

STORAGE uint8_t VRam[65536];
STORAGE uint32_t VDP_Reg[26], Ctrl[7], CRam_Flag, VRam_Flag;
STORAGE uint8_t *ScrA_Addr, *ScrB_Addr, *Win_Addr, *Spr_Addr, *H_Scroll_Addr;
STORAGE uint32_t H_Cell, H_Win_Mul, H_Pix, H_Pix_Begin;
STORAGE uint32_t H_Scroll_Mask, H_Scroll_CMul, H_Scroll_CMask, V_Scroll_CMask;
STORAGE uint32_t V_Scroll_MMask, Win_X_Pos, Win_Y_Pos, reference_irq_calls;

#ifndef GENS_VDP_REGISTER_REFERENCE
static GensM68kBusState state;

static int update_irq(void *opaque, GensM68kBusState *current)
{
    (void)opaque;
    assert(current == &state);
    ++reference_irq_calls;
    return 1;
}

static GensVdpRegisters vdp = {
    VDP_Reg, 26, VRam, sizeof(VRam),
    (uint8_t *)&VRam_Flag, (uint8_t *)&CRam_Flag, (uint8_t *)&V_Scroll_MMask,
    &ScrA_Addr, &ScrB_Addr, &Win_Addr, &Spr_Addr, &H_Scroll_Addr,
    &H_Cell, &H_Win_Mul, &H_Pix, &H_Pix_Begin,
    &H_Scroll_Mask, &H_Scroll_CMul, &H_Scroll_CMask, &V_Scroll_CMask,
    &Win_X_Pos, &Win_Y_Pos, &VDP_Reg[24], &VDP_Reg[25], &Ctrl[5], 0, update_irq
};
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

static void reset(unsigned int mode, uint32_t upper)
{
    unsigned int i;
    for (i = 0; i < 26; ++i)
        VDP_Reg[i] = upper | ((0xadu + i * 19u) & 255u);
    VDP_Reg[12] = upper | mode;
    for (i = 0; i < 7; ++i)
        Ctrl[i] = upper | (0x1234u + i);
    ScrA_Addr = VRam + 1;
    ScrB_Addr = VRam + 2;
    Win_Addr = VRam + 3;
    Spr_Addr = VRam + 4;
    H_Scroll_Addr = VRam + 5;
    H_Cell = (mode & 0x81u) != 0u ? 40 : 32;
    H_Win_Mul = 0x65432101;
    H_Pix = 0x65432102;
    H_Pix_Begin = 0x65432103;
    H_Scroll_Mask = 0x65432104;
    H_Scroll_CMul = 0x65432105;
    H_Scroll_CMask = 0x65432106;
    V_Scroll_CMask = 0x65432107;
    V_Scroll_MMask = 0x65432108;
    Win_X_Pos = 0x65432109;
    Win_Y_Pos = 0x6543210a;
    CRam_Flag = 0x123456a5;
    VRam_Flag = 0x789abc80;
    reference_irq_calls = 0;
}

static void write_register(unsigned int reg, uint8_t value)
{
#ifdef GENS_VDP_REGISTER_REFERENCE
    Set_VDP_Reg(reg, value);
#else
    assert(gens_vdp_set_register(&vdp, &state, reg, value));
#ifdef GENS_VDP_REGISTER_NEGATIVE_CONTROL
    if ((reg & 31u) == 23u)
        VDP_Reg[25] ^= UINT32_C(0x800000);
#endif
#endif
}

static void record_state(void)
{
    unsigned int i;
    for (i = 0; i < 26; ++i)
        record(VDP_Reg[i]);
    for (i = 0; i < 7; ++i)
        record(Ctrl[i]);
    record((uint32_t)(ScrA_Addr - VRam));
    record((uint32_t)(ScrB_Addr - VRam));
    record((uint32_t)(Win_Addr - VRam));
    record((uint32_t)(Spr_Addr - VRam));
    record((uint32_t)(H_Scroll_Addr - VRam));
    record(H_Cell); record(H_Win_Mul); record(H_Pix); record(H_Pix_Begin);
    record(H_Scroll_Mask); record(H_Scroll_CMul); record(H_Scroll_CMask);
    record(V_Scroll_CMask); record(V_Scroll_MMask);
    record(Win_X_Pos); record(Win_Y_Pos);
    record(CRam_Flag); record(VRam_Flag); record(reference_irq_calls);
}

int main(void)
{
    static const unsigned int modes[] = {0, 1, 0x80, 0x81};
    unsigned int mode, poison, reg, value, i;
    hash = UINT64_C(14695981039346656037);
    for (mode = 0; mode < 4; ++mode)
        for (poison = 0; poison < 2; ++poison)
            for (reg = 0; reg < 64; ++reg)
                for (value = 0; value < 256; ++value)
                {
                    reset(modes[mode], poison ? UINT32_C(0x98765400) : 0);
                    write_register(reg, (uint8_t)value);
                    record_state();
                }
    printf("vdp registers isolated %016" PRIx64 "\n", hash);
    hash = UINT64_C(14695981039346656037);
    for (mode = 0; mode < 4; ++mode)
    {
        reset(modes[mode], UINT32_C(0x12345600));
        for (i = 0; i < 4096; ++i)
        {
            write_register((i * 13u) & 63u, (uint8_t)(i * 73u + (i >> 5)));
            record_state();
        }
    }
    printf("vdp registers sequence %016" PRIx64 "\n", hash);
    return 0;
}
