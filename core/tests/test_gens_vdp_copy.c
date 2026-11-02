#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_vdp_write.h"

#ifdef GENS_VDP_COPY_REFERENCE
#define STORAGE extern
void Write_VDP_Ctrl(uint16_t);
#else
#define STORAGE static
#endif

STORAGE uint8_t VRam[65536];
STORAGE unsigned int VDP_Reg[26];
STORAGE int Ctrl[7], VDP_Status, VRam_Flag, CRam_Flag;
STORAGE int DMAT_Length, DMAT_Type;

#ifndef GENS_VDP_COPY_REFERENCE
static uint8_t cram[256], vsram[256];
static GensM68kBusState state;
static GensVdpWrite vdp = {
    VRam, cram, vsram, sizeof(VRam), sizeof(cram), sizeof(vsram),
    &Ctrl[0], &Ctrl[1], &Ctrl[3], &Ctrl[4], &Ctrl[5], &Ctrl[6],
    &VDP_Status, (int *)&VDP_Reg[24], (int *)&VDP_Reg[25], &DMAT_Length, &DMAT_Type,
    &VDP_Reg[1], &VDP_Reg[15], &VRam_Flag, &CRam_Flag, 0, 0, 0, 0
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

static void command(uint16_t value)
{
#ifdef GENS_VDP_COPY_REFERENCE
    Write_VDP_Ctrl(value);
#else
    assert(gens_vdp_control_write(&vdp, &state, value));
#endif
}

static void reset(unsigned int length, uint32_t source, uint32_t mode,
                   unsigned int increment, unsigned int enabled)
{
    unsigned int i;
    for (i = 0; i < sizeof(VRam); ++i)
        VRam[i] = (uint8_t)(i ^ (i >> 7) ^ 0x96);
    for (i = 0; i < 26; ++i)
        VDP_Reg[i] = 0;
    VDP_Reg[1] = enabled ? 0x10 : 0;
    VDP_Reg[15] = increment;
    VDP_Reg[24] = 0xabcd0000u | length;
    VDP_Reg[25] = source;
    Ctrl[0] = 0x12345600;
    Ctrl[1] = 0;
    Ctrl[2] = 0x76543210;
    Ctrl[3] = 0;
    Ctrl[4] = 0x12340000;
    Ctrl[5] = (int)mode;
    Ctrl[6] = 0x12345678;
    VDP_Status = 0x56780001;
    VRam_Flag = 0x23456780;
    CRam_Flag = 0x345678ab;
    DMAT_Length = 0x456789ab;
    DMAT_Type = 0x56789abc;
}

static void record_state(void)
{
    unsigned int i;
    for (i = 0; i < 26; ++i)
        record(VDP_Reg[i]);
    for (i = 0; i < 7; ++i)
        record((uint32_t)Ctrl[i]);
    record((uint32_t)VDP_Status);
    record((uint32_t)VRam_Flag);
    record((uint32_t)CRam_Flag);
    record((uint32_t)DMAT_Length);
    record((uint32_t)DMAT_Type);
    for (i = 0; i < sizeof(VRam); ++i)
        record(VRam[i]);
}

int main(void)
{
    static const unsigned int lengths[] = {0, 1, 2, 7, 65535};
    static const uint32_t sources[] = {0, 1, 0xfffe, 0xffff, 0xabcdffff};
    static const unsigned int destinations[] = {0, 1, 0x3fff, 0xfffe, 0xffff};
    static const unsigned int increments[] = {0, 1, 2, 0xffff, 0x10002};
    static const uint32_t modes[] = {0xc0, 0x123456c0, 0x80, 0x12345680};
    unsigned int length, source, destination, increment, mode, enabled;
    hash = UINT64_C(14695981039346656037);
    for (length = 0; length < 5; ++length)
        for (source = 0; source < 5; ++source)
            for (destination = 0; destination < 5; ++destination)
                for (increment = 0; increment < 5; ++increment)
                    for (mode = 0; mode < 4; ++mode)
                        for (enabled = 0; enabled < 2; ++enabled)
                        {
                            reset(lengths[length], sources[source], modes[mode],
                                  increments[increment], enabled);
                            command((uint16_t)(destinations[destination] & 0x3fff));
                            command((uint16_t)(0xc0u | (destinations[destination] >> 14)));
#if defined(GENS_VDP_COPY_NEGATIVE_CONTROL) && !defined(GENS_VDP_COPY_REFERENCE)
                            VRam[0] ^= 1;
#endif
                            record_state();
                        }
    printf("vdp copy %016" PRIx64 "\n", hash);
    hash = UINT64_C(14695981039346656037);
    for (mode = 0; mode < 2; ++mode)
    {
        reset(1, 0xffff, mode ? 0x12345680 : 0x80, 1, 1);
        command(0x4000);
        command(0x0080);
        record_state();
    }
    printf("vdp fill arm %016" PRIx64 "\n", hash);
    return 0;
}
