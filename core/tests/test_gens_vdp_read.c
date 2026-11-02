#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gens_vdp_read.h"

#ifdef GENS_VDP_READ_REFERENCE
extern uint8_t VRam[65536], CRam[256], VSRam[256], H_Counter_Table[1024];
extern unsigned int VDP_Reg[26];
extern int Ctrl[7], VDP_Status, VDP_Current_Line, Cycles_M68K, CPL_M68K;
void reference_vdp_table_init(void);
uint16_t Read_VDP_Data(void), Read_VDP_Status(void);
uint32_t Read_VDP_H_Counter(void), Read_VDP_V_Counter(void);
#else
static uint8_t VRam[65536], CRam[256], VSRam[256], H_Counter_Table[1024];
static unsigned int VDP_Reg[26];
static int Ctrl[7], VDP_Status, VDP_Current_Line, Cycles_M68K, CPL_M68K;
static GensM68kBusState state;
static int fail_odometer;
static unsigned int fail_odometer_at;
static int read_odometer(void *, GensM68kBusState *, uint32_t *);
static GensVdpRead vdp = {
    VRam, CRam, VSRam, H_Counter_Table,
    sizeof(VRam), sizeof(CRam), sizeof(VSRam), sizeof(H_Counter_Table),
    &VDP_Status, &Ctrl[0], &Ctrl[3], &Ctrl[4], &VDP_Reg[1], &VDP_Reg[12], &VDP_Reg[15],
    &Cycles_M68K, &CPL_M68K, &VDP_Current_Line, 0, read_odometer
};
#endif

static uint32_t odometer, odometer_step;
static unsigned int odometer_calls;
static uint64_t hash;

uint32_t reference_vdp_odometer(void)
{
    return odometer + odometer_step * odometer_calls++;
}

#ifndef GENS_VDP_READ_REFERENCE
static int read_odometer(void *opaque, GensM68kBusState *current, uint32_t *value)
{
    (void)opaque;
    (void)current;
    *value = reference_vdp_odometer();
    return !fail_odometer && (fail_odometer_at == 0u || odometer_calls != fail_odometer_at);
}
#endif

static uint16_t data_read(void)
{
#ifdef GENS_VDP_READ_REFERENCE
    return Read_VDP_Data();
#else
    uint16_t value;
    assert(gens_vdp_data_read(&vdp, &state, &value));
    return value;
#endif
}

static uint16_t status_read(void)
{
#ifdef GENS_VDP_READ_REFERENCE
    return Read_VDP_Status();
#else
    uint16_t value;
    assert(gens_vdp_status_read(&vdp, &state, &value));
    return value;
#endif
}

static uint32_t h_read(void)
{
#ifdef GENS_VDP_READ_REFERENCE
    return Read_VDP_H_Counter();
#else
    uint32_t value;
    assert(gens_vdp_h_counter_read(&vdp, &state, &value));
    return value;
#endif
}

static uint32_t v_read(void)
{
#ifdef GENS_VDP_READ_REFERENCE
    return Read_VDP_V_Counter();
#else
    uint32_t value;
    assert(gens_vdp_v_counter_read(&vdp, &state, &value));
    return value;
#endif
}

static void record(uint32_t value)
{
    unsigned int shift;
    for (shift = 0; shift < 32; shift += 8)
    {
        hash ^= (value >> shift) & 0xffu;
        hash *= UINT64_C(1099511628211);
    }
}

static void start(void) { hash = UINT64_C(14695981039346656037); }
static void finish(const char *name) { printf("vdp %s %016" PRIx64 "\n", name, hash); }

#ifndef GENS_VDP_READ_REFERENCE
static void unit_cases(void)
{
    uint8_t zram[8192] = {0};
    int bank = 0;
    GensZ80Memory memory = {zram, sizeof(zram), &bank, &vdp, gens_vdp_z80_read, 0};
    GensZ80Native native = {&memory, &vdp, gens_vdp_native_read};
    GensZ80Registers registers = {0x87654321, 0x7f04, 0xabcdef12};
    GensVdpRead invalid = vdp;
    uint16_t value = 0x7777;
    uint32_t raw = UINT32_C(0x55555555);
    assert(!gens_vdp_h_counter_init(H_Counter_Table, 1023));
    invalid.cram_size = 255;
    assert(!gens_vdp_read_valid(&invalid));
    assert(!gens_vdp_data_read(&invalid, &state, &value) && value == 0x7777);
    Ctrl[0] = 0x56789abc;
    Ctrl[3] = 5;
    Ctrl[4] = 0x1234ffff;
    VDP_Reg[15] = 2;
    VRam[65534] = 0x12; VRam[65535] = 0x34;
    assert(gens_vdp_data_read(&vdp, &state, &value) && value == 0x3412);
    assert(Ctrl[0] == 0x56789a00 && Ctrl[4] == 0x12340001);
    Ctrl[0] = 0x123456ab;
    Ctrl[3] = 0;
    assert(gens_vdp_data_read(&vdp, &state, &value) && value == 0);
    assert(Ctrl[4] == 0x12340001 && Ctrl[0] == 0x12345600);
    Ctrl[0] |= 0xab;
    Ctrl[3] = 16;
    value = 0x7777;
    assert(!gens_vdp_data_read(&vdp, &state, &value) && value == 0x7777);
    assert(Ctrl[0] == 0x12345600 && Ctrl[4] == 0x12340001);
    Ctrl[3] = 5;
    Ctrl[4] = 0x100;
    VRam[0x100] = 0xaa; VRam[0x101] = 0xbb;
    assert(gens_vdp_genesis_read(&vdp, &state, GENS_GENESIS_VDP, 0x080000, 2, &value));
    assert(value == 0xbbaa && Ctrl[4] == 0x102);
    Ctrl[0] = 0x123456ab;
    assert(gens_vdp_genesis_read(&vdp, &state, GENS_GENESIS_VDP, 0xc00000, 1, &value));
    assert(value == 0 && Ctrl[0] == 0x123456ab && Ctrl[4] == 0x102);
    Ctrl[0] = 0x12345600;
    VDP_Reg[1] = 0;
    VDP_Status = 0x12340080;
    assert(gens_vdp_native_read(&vdp, &state, GENS_Z80_NATIVE_VDP_STATUS, &registers));
    assert(registers.eax == 0x87650088 && registers.ecx == 0x7f04 &&
           registers.edx == 0xabcdef12);
    assert(VDP_Status == 0x1234ff00 && Ctrl[0] == 0x12345600);
    VDP_Reg[1] = 0x40;
    VDP_Status = 0x5678a5bc;
    assert(gens_z80_window_read(&memory, &state, 0xa07f04, 2, &value));
    assert(value == 0xa5a5 && Ctrl[0] == 0x12345600);
    VDP_Status = 0;
    VDP_Reg[12] = 0x81;
    Cycles_M68K = CPL_M68K = 0;
    VDP_Current_Line = 235;
    odometer = 200;
    odometer_step = 100;
    odometer_calls = 0;
    assert(gens_vdp_genesis_read(&vdp, &state, GENS_GENESIS_VDP, 0xc00008, 2, &value));
    assert(value == 0xe562 && odometer_calls == 2);
    odometer_calls = 0;
    fail_odometer_at = 2;
    value = 0x7777;
    assert(!gens_vdp_genesis_read(&vdp, &state, GENS_GENESIS_VDP, 0xc00008, 2, &value));
    assert(value == 0x7777 && odometer_calls == 2);
    fail_odometer_at = 0;
    registers.ecx = 0x7f08;
    odometer_calls = 0;
    assert(gens_z80_native_read(&native, &state, 2, &registers));
    assert((registers.eax & 0xffffu) == 0xe562 && odometer_calls == 2);
    VDP_Status = 1;
    VDP_Reg[12] = 2;
    VDP_Current_Line = 0x12340103;
    odometer = odometer_step = 0;
    assert(gens_vdp_v_counter_read(&vdp, &state, &raw) && raw == 0x12340097);
    fail_odometer = 1;
    raw = 0x55555555;
    assert(!gens_vdp_h_counter_read(&vdp, &state, &raw) && raw == 0x55555555);
    fail_odometer = 0;
    vdp.read_odometer = 0;
    assert(!gens_vdp_v_counter_read(&vdp, &state, &raw) && raw == 0x55555555);
    vdp.read_odometer = read_odometer;
    assert(!gens_vdp_native_read(&vdp, &state, GENS_Z80_NATIVE_YM_WRITE, &registers));
}
#endif

int main(void)
{
    static const unsigned int addresses[] = {0, 1, 0x7f, 0x80, 0xfffe, 0xffff};
    static const unsigned int increments[] = {0, 1, 2, 0xffff, 0x10102};
    static const unsigned int settings[] = {0, 1, 0x80, 0x81, 2, 3, 0x82, 0x83, 0x12340081};
    static const uint32_t lines[] = {
        0, 223, 224, 234, 235, 236, 255, 258, 259, 260, 261, 262, 311, 312,
        UINT32_C(0x12340103), UINT32_C(0x80000000), UINT32_C(0xffffffff)
    };
    unsigned int i, access, a, inc, display, low, setting, pal, line, phase;
#ifdef GENS_VDP_READ_REFERENCE
    reference_vdp_table_init();
#else
    assert(gens_vdp_h_counter_init(H_Counter_Table, sizeof(H_Counter_Table)));
    unit_cases();
#ifdef GENS_VDP_READ_NEGATIVE_CONTROL
    H_Counter_Table[0] ^= 1;
#endif
#endif
    start();
    for (i = 0; i < sizeof(H_Counter_Table); ++i)
        record(H_Counter_Table[i]);
    finish("table");
    for (i = 0; i < sizeof(VRam); ++i)
        VRam[i] = (uint8_t)(i ^ (i >> 7) ^ 0x96u);
    for (i = 0; i < sizeof(CRam); ++i)
    {
        CRam[i] = (uint8_t)(i ^ 0xa5u);
        VSRam[i] = (uint8_t)(i ^ 0x5au);
    }
    start();
    for (access = 0; access < 16; ++access)
        for (a = 0; a < sizeof(addresses) / sizeof(addresses[0]); ++a)
            for (inc = 0; inc < sizeof(increments) / sizeof(increments[0]); ++inc)
            {
                Ctrl[0] = 0x56789abc;
                Ctrl[3] = (int)access;
                Ctrl[4] = (int)(0x12340000u | addresses[a]);
                VDP_Reg[15] = increments[inc];
                record(data_read());
                record((uint32_t)Ctrl[0]);
                record((uint32_t)Ctrl[4]);
            }
    finish("data");
    start();
    for (display = 0; display < 2; ++display)
        for (low = 0; low < 65536; ++low)
        {
            Ctrl[0] = 0x123456ab;
            VDP_Status = (int)(0x56780000u | low);
            VDP_Reg[1] = display ? 0x12340040 : 0x12340000;
            record(status_read());
            record((uint32_t)VDP_Status);
            record((uint32_t)Ctrl[0]);
        }
    finish("status");
    start();
    Cycles_M68K = 1000;
    CPL_M68K = 488;
    for (setting = 0; setting < sizeof(settings) / sizeof(settings[0]); ++setting)
        for (pal = 0; pal < 2; ++pal)
            for (line = 0; line < sizeof(lines) / sizeof(lines[0]); ++line)
                for (phase = 0; phase < 512; ++phase)
                {
                    VDP_Reg[12] = settings[setting];
                    VDP_Status = (int)pal;
                    memcpy(&VDP_Current_Line, &lines[line], sizeof(VDP_Current_Line));
                    odometer = 512u + phase;
                    odometer_step = 17;
                    odometer_calls = 0;
                    record(h_read());
                    record(v_read());
                    record(odometer_calls);
                }
    finish("counter");
    return 0;
}
