#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_vdp_memory_dma.h"

#ifdef GENS_VDP_MEMORY_DMA_REFERENCE
#define STORAGE extern
void Write_VDP_Ctrl(uint16_t);
#else
#define STORAGE static
#endif

STORAGE uint8_t Rom_Data[6291456], Ram_68k[65536], VRam[65536], CRam[256], VSRam[256];
STORAGE unsigned int VDP_Reg[26];
STORAGE int Ctrl[7], VDP_Status, VRam_Flag, CRam_Flag, DMAT_Length, DMAT_Type;
STORAGE int VDP_Current_Line, VDP_Num_Vis_Lines, CPL_M68K, Rom_Size, SegaCD_Started;
STORAGE uint32_t reference_cycle_counter;

#ifndef GENS_VDP_MEMORY_DMA_REFERENCE
static GensM68kBusState state;
static GensVdpMemoryDma memory;
static GensVdpWrite write_port = {
    VRam, CRam, VSRam, sizeof(VRam), sizeof(CRam), sizeof(VSRam),
    &Ctrl[0], &Ctrl[1], &Ctrl[3], &Ctrl[4], &Ctrl[5], &Ctrl[6],
    &VDP_Status, (int *)&VDP_Reg[24], (int *)&VDP_Reg[25], &DMAT_Length, &DMAT_Type,
    &VDP_Reg[1], &VDP_Reg[15], &VRam_Flag, &CRam_Flag,
    &memory, 0, 0, gens_vdp_memory_dma
};
static GensVdpDmaTiming timing = {
    &VDP_Reg[1], &VDP_Reg[12], &VDP_Current_Line, &VDP_Num_Vis_Lines,
    &CPL_M68K, &DMAT_Length, &DMAT_Type, &VDP_Status
};
static GensVdpMemoryDma memory = {
    &write_port, &timing, Rom_Data, Ram_68k, sizeof(Rom_Data), sizeof(Ram_68k),
    &Rom_Size, &SegaCD_Started
};
#endif

static void reset(void)
{
    unsigned int i;
    for (i = 0; i < sizeof(VRam); ++i)
        VRam[i] = (uint8_t)(i ^ 0x96);
    for (i = 0; i < 256; ++i)
    {
        CRam[i] = (uint8_t)(i ^ 0x5a);
        VSRam[i] = (uint8_t)(i ^ 0xa5);
    }
    for (i = 0; i < 26; ++i)
        VDP_Reg[i] = 0;
    for (i = 0; i < 7; ++i)
        Ctrl[i] = 0;
    VDP_Reg[1] = 0x50;
    VDP_Reg[12] = 1;
    VDP_Reg[15] = 2;
    VDP_Reg[24] = 1;
    Ctrl[4] = 0x12340000;
    Ctrl[6] = 0x23456789;
    VDP_Status = 0x34560001;
    VRam_Flag = 0x456789ab;
    CRam_Flag = 0x56789abc;
    DMAT_Type = 0x12345602;
    DMAT_Length = 0x6789abcd;
    VDP_Current_Line = 0;
    VDP_Num_Vis_Lines = 224;
    CPL_M68K = 488;
    Rom_Size = 0x400000;
    SegaCD_Started = 0;
    reference_cycle_counter = 1000;
}

static void transfer(unsigned int kind, unsigned int address)
{
    unsigned int first = address & 0x3fffu;
    unsigned int second = 0x80u | (address >> 14);
    if (kind == 1u)
        first |= 0x4000u;
    else if (kind == 2u)
        first |= 0xc000u;
    else
    {
        first |= 0x4000u;
        second |= 0x10u;
    }
#ifdef GENS_VDP_MEMORY_DMA_REFERENCE
    Write_VDP_Ctrl((uint16_t)first);
    Write_VDP_Ctrl((uint16_t)second);
#else
    state.io_cycle_counter = reference_cycle_counter;
    assert(gens_vdp_control_write(&write_port, &state, (uint16_t)first));
    assert(gens_vdp_control_write(&write_port, &state, (uint16_t)second));
    reference_cycle_counter = state.io_cycle_counter;
#endif
}

#ifndef GENS_VDP_MEMORY_DMA_REFERENCE
static void unit_cases(void)
{
    GensVdpMemoryDma invalid = memory;
    GensVdpDmaTiming different_timing = timing;
    int different_status = 0;
    reset();
    assert(gens_vdp_memory_dma_valid(&memory));
    invalid.ram_size = 65535;
    assert(!gens_vdp_memory_dma_valid(&invalid));
    invalid = memory;
    different_timing.status = &different_status;
    invalid.timing = &different_timing;
    assert(!gens_vdp_memory_dma_valid(&invalid));
    Rom_Data[0] = 0x12; Rom_Data[1] = 0x34;
    transfer(1, 1);
    assert(VRam[0] == 0x34 && VRam[1] == 0x12 && Ctrl[4] == 0x12340003);
    assert(VDP_Reg[24] == 0 && VDP_Reg[25] == 1 && DMAT_Length == 0);
    assert(VRam_Flag == 1 && CRam_Flag == 0x56789abc && Ctrl[6] == 0);
    assert(DMAT_Type == 0x12345600 && reference_cycle_counter == 946);

    reset();
    Rom_Size = 2;
    VDP_Reg[24] = 2;
    VDP_Reg[25] = 0x7fff;
    Ram_68k[65534] = 0x12; Ram_68k[65535] = 0x34;
    Ram_68k[0] = 0x56; Ram_68k[1] = 0x78;
    transfer(2, 0xffff);
    assert(CRam[0x7e] == 0x12 && CRam[0x7f] == 0x34);
    assert(CRam[0] == 0x56 && CRam[1] == 0x78 && Ctrl[4] == 0x12340083);
    assert(VDP_Reg[25] == 0x8001 && CRam_Flag == 1 && VRam_Flag == 0x456789ab);
    assert(DMAT_Type == 0x12345601 && reference_cycle_counter == 946);

    reset();
    SegaCD_Started = 1;
    Rom_Size = 0;
    assert(!gens_vdp_memory_dma(&memory, &state, 1, 0, 0));
    assert(VDP_Reg[24] == 1 && Ctrl[6] == 0x23456789);
    reset();
    SegaCD_Started = 1;
    Rom_Size = 0;
    VDP_Reg[25] = 0x120000;
    transfer(3, 0);
    assert(VSRam[0] == Ram_68k[0] && VSRam[1] == Ram_68k[1]);
    assert(VDP_Reg[25] == 0x120001);
    reset();
    memory.rom_backing_size = 1;
    assert(!gens_vdp_memory_dma_valid(&memory));
    memory.rom_backing_size = sizeof(Rom_Data);
    VDP_Reg[25] = 1;
    memory.rom_backing_size = 2;
    assert(!gens_vdp_memory_dma(&memory, &state, 1, 0, 0));
    assert(VDP_Reg[24] == 1 && Ctrl[6] == 0x23456789);
    memory.rom_backing_size = sizeof(Rom_Data);
    reset();
    VDP_Reg[24] = 0;
    assert(!gens_vdp_memory_dma(&memory, &state, 1, 0, 0));
    assert(!gens_vdp_memory_dma(&memory, &state, 1, 0x80, 0));
    assert(!gens_vdp_memory_dma(&memory, &state, 0, 0, 0));
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
    for (i = 0; i < 26; ++i)
        record(VDP_Reg[i]);
    for (i = 0; i < 7; ++i)
        record((uint32_t)Ctrl[i]);
    record((uint32_t)VDP_Status); record((uint32_t)VRam_Flag);
    record((uint32_t)CRam_Flag); record((uint32_t)DMAT_Length);
    record((uint32_t)DMAT_Type); record(reference_cycle_counter);
    for (i = 0; i < sizeof(VRam); ++i)
        record(VRam[i]);
    for (i = 0; i < 256; ++i)
    {
        record(CRam[i]);
        record(VSRam[i]);
    }
}

int main(void)
{
    static const unsigned int sources[] = {0, 1, 0x1fffff, 0x200000,
                                           0x7fff, 0x200001, 0x7fffff, 0x80000000};
    static const unsigned int lengths[] = {1, 2, 205, 0x12340003};
    static const unsigned int addresses[] = {0, 1, 0xffff};
    static const unsigned int increments[] = {0, 2, 0x10001};
    unsigned int i, kind, source, length, address, increment, blanking;
#ifndef GENS_VDP_MEMORY_DMA_REFERENCE
    unit_cases();
#endif
    for (i = 0; i < sizeof(Rom_Data); ++i)
        Rom_Data[i] = (uint8_t)(i ^ (i >> 8) ^ 0xa5);
    for (i = 0; i < sizeof(Ram_68k); ++i)
        Ram_68k[i] = (uint8_t)(i ^ (i >> 7) ^ 0x5a);
    hash = UINT64_C(14695981039346656037);
    for (kind = 1; kind < 4; ++kind)
        for (source = 0; source < 8; ++source)
            for (length = 0; length < 4; ++length)
                for (address = 0; address < 3; ++address)
                    for (increment = 0; increment < 3; ++increment)
                        for (blanking = 0; blanking < 2; ++blanking)
                        {
                            reset();
                            Rom_Size = source == 5 ? 0x600000 : 0x400000;
                            VDP_Reg[24] = lengths[length];
                            VDP_Reg[25] = sources[source];
                            VDP_Reg[15] = increments[increment];
                            VDP_Current_Line = blanking ? 224 : 0;
                            transfer(kind, addresses[address]);
#if defined(GENS_VDP_MEMORY_DMA_NEGATIVE_CONTROL) && !defined(GENS_VDP_MEMORY_DMA_REFERENCE)
                            reference_cycle_counter ^= 1u;
#endif
                            record_state();
                        }
    printf("vdp memory dma %016" PRIx64 "\n", hash);
    return 0;
}
