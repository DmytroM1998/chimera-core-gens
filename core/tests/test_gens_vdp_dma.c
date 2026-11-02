#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_vdp_dma.h"

#ifdef GENS_VDP_DMA_REFERENCE
#define STORAGE extern
uint32_t Update_DMA(void);
#else
#define STORAGE static
#endif

STORAGE unsigned int VDP_Reg[26];
STORAGE int DMAT_Length, DMAT_Type, VDP_Status, VDP_Current_Line, VDP_Num_Vis_Lines, CPL_M68K;

#ifndef GENS_VDP_DMA_REFERENCE
static GensVdpDmaTiming dma = {
    &VDP_Reg[1], &VDP_Reg[12], &VDP_Current_Line, &VDP_Num_Vis_Lines,
    &CPL_M68K, &DMAT_Length, &DMAT_Type, &VDP_Status
};

static void unit_cases(void)
{
    uint32_t cycles = 0x77777777;
    GensVdpDmaTiming invalid = dma;
    invalid.length = 0;
    assert(!gens_vdp_dma_timing_valid(&invalid));
    assert(!gens_vdp_dma_update(&invalid, &cycles) && cycles == 0x77777777);
    assert(!gens_vdp_dma_update(&dma, 0));
    VDP_Reg[1] = 0x40;
    VDP_Reg[12] = 0;
    VDP_Current_Line = 0;
    VDP_Num_Vis_Lines = 224;
    CPL_M68K = 488;
    DMAT_Type = 0;
    DMAT_Length = 9;
    VDP_Status = 0x12340003;
    assert(gens_vdp_dma_update(&dma, &cycles));
    assert(cycles == 488 && DMAT_Length == 1 && VDP_Status == 0x12340003);
    assert(gens_vdp_dma_update(&dma, &cycles));
    assert(cycles == 61 && DMAT_Length == 0 && VDP_Status == 0x12340001);
    DMAT_Length = 8;
    VDP_Status = 0x12340003;
    assert(gens_vdp_dma_update(&dma, &cycles));
    assert(cycles == 488 && DMAT_Length == 0 && VDP_Status == 0x12340001);
    VDP_Current_Line = 224;
    VDP_Reg[12] = 0x80;
    DMAT_Length = 83;
    assert(gens_vdp_dma_update(&dma, &cycles));
    assert(cycles == 487 && DMAT_Length == 0);
    DMAT_Type = 0x12345602;
    DMAT_Length = 167;
    VDP_Status = 0x12340003;
    assert(gens_vdp_dma_update(&dma, &cycles));
    assert(cycles == 0 && DMAT_Length == 1 && VDP_Status == 0x12340003);
    assert(gens_vdp_dma_update(&dma, &cycles));
    assert(cycles == 0 && DMAT_Length == 0 && VDP_Status == 0x12340001);
    DMAT_Length = 0;
    VDP_Status = 0x1234ffff;
    assert(gens_vdp_dma_update(&dma, &cycles));
    assert(cycles == 0 && DMAT_Length == 0 && VDP_Status == 0x1234fffd);
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

static void update(void)
{
    uint32_t cycles;
#ifdef GENS_VDP_DMA_REFERENCE
    cycles = Update_DMA();
#else
    assert(gens_vdp_dma_update(&dma, &cycles));
#ifdef GENS_VDP_DMA_NEGATIVE_CONTROL
    cycles ^= 1u;
#endif
#endif
    record(cycles);
    record((uint32_t)DMAT_Length);
    record((uint32_t)VDP_Status);
    record((uint32_t)DMAT_Type);
}

int main(void)
{
    static const unsigned int modes[] = {0, 1, 0x80, 0x81};
    static const unsigned int types[] = {0, 1, 2, 3, 0x12345600, 0x12345601,
                                        0x12345602, 0x12345603};
    static const uint32_t lines[] = {0, 223, 224, UINT32_C(0xffffffff)};
    static const uint32_t cycle_counts[] = {0, 487, 488, 0x10000, UINT32_C(0xffffffff)};
    unsigned int mode, type, line, display, length, cpl;
#ifndef GENS_VDP_DMA_REFERENCE
    unit_cases();
#endif
    hash = UINT64_C(14695981039346656037);
    for (mode = 0; mode < 4; ++mode)
        for (type = 0; type < 8; ++type)
            for (line = 0; line < 4; ++line)
                for (display = 0; display < 2; ++display)
                    for (cpl = 0; cpl < 5; ++cpl)
                        for (length = 0; length < 258; ++length)
                        {
                            VDP_Reg[1] = display ? 0x12340040 : 0x12340000;
                            VDP_Reg[12] = modes[mode];
                            DMAT_Type = (int)types[type];
                            VDP_Current_Line = (int)lines[line];
                            VDP_Num_Vis_Lines = 224;
                            CPL_M68K = (int)cycle_counts[cpl];
                            DMAT_Length = (int)length;
                            VDP_Status = 0x5678ffff;
                            update();
                        }
    printf("vdp dma timing %016" PRIx64 "\n", hash);
    hash = UINT64_C(14695981039346656037);
    for (type = 0; type < 4; ++type)
    {
        DMAT_Length = 2000;
        DMAT_Type = (int)type;
        VDP_Status = 0x12340003;
        VDP_Reg[1] = 0x40;
        VDP_Reg[12] = 1;
        VDP_Num_Vis_Lines = 224;
        CPL_M68K = 488;
        for (line = 0; line < 262; ++line)
        {
            VDP_Current_Line = (int)line;
            update();
        }
    }
    printf("vdp dma scanlines %016" PRIx64 "\n", hash);
    return 0;
}
