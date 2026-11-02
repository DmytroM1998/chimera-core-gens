#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_input.h"
#include "gens_io.h"
#include "io.h"

#ifdef GENS_IO_REFERENCE
uint16_t reference_read_controller_1(void);
uint16_t reference_read_controller_2(void);
#define READ_1 reference_read_controller_1
#define READ_2 reference_read_controller_2
#else
#define READ_1 gens_io_read_word_1
#define READ_2 gens_io_read_word_2
#endif

static uint64_t hash;

static void record(uint32_t value)
{
    unsigned int shift;
    for (shift = 0; shift < 32; shift += 8)
    {
        hash ^= (value >> shift) & 0xffu;
        hash *= UINT64_C(1099511628211);
    }
}

static void record_state(void)
{
    record(Controller_1_State);
    record(Controller_1_Counter);
    record(Controller_1_Delay);
    record(Controller_2_State);
    record(Controller_2_Counter);
    record(Controller_2_Delay);
    record(Lag_Frame);
}

static uint64_t input_for(unsigned int sample)
{
    uint64_t x;
    if (sample < 36u)
        return UINT64_C(1) << (GENS_INPUT_PAD_BASE + sample);
    x = UINT64_C(0x9e3779b97f4a7c15) * (sample + 1u);
    x ^= x >> 17;
    x ^= x << 13;
    return (x & ((UINT64_C(1) << 36) - 1u)) << GENS_INPUT_PAD_BASE;
}

static void read_scenarios(void)
{
    static const unsigned int other_states[] = {
        0, 0x20, 0x40, 0x60, 0x0c, 0x1c,
        0x2c, 0x3c, 0x4c, 0x5c, 0x6c, 0x7c
    };
    unsigned int topology, options, sample, phase, low, lines, other;

    for (topology = 0; topology < 2; ++topology)
    {
        for (options = 0; options < 64; ++options)
        {
            GensInputConfig config = {
                topology ? GENS_INPUT_TEAM_PLAYER : GENS_INPUT_TWO_PADS,
                (options & 1u) ? 6u : 3u,
                (options & 2u) ? 6u : 3u,
                (options & 4u) ? 6u : 3u,
                (options & 8u) ? 6u : 3u,
                (options & 16u) ? 6u : 3u
            };
            hash = UINT64_C(14695981039346656037);
            assert(gens_input_configure(&config));
            if ((options & 32u) != 0u)
                Controller_2_Type |= 0x10u;
            for (sample = 0; sample < 48; ++sample)
            {
                gens_input_apply(input_for(sample));
                for (phase = 0; phase < 16; ++phase)
                {
                    for (low = 0; low < 4; ++low)
                    {
                        for (lines = 0; lines < 4; ++lines)
                        {
                            for (other = 0; other < sizeof(other_states) / sizeof(other_states[0]); ++other)
                            {
                                Controller_1_State = 0x12340000u | (lines << 5);
                                Controller_2_State = 0xabcd0000u | other_states[other];
                                Controller_1_Counter = 0x95000000u | (phase << 16) | low;
                                Controller_2_Counter = 0x3b000000u | (phase << 16) | ((low + 1u) & 3u);
                                Lag_Frame = 1;
                                record(READ_1());
                                assert(Lag_Frame == 0);
                                record(Lag_Frame);
                                Lag_Frame = 1;
                                record(READ_2());
                                assert(Lag_Frame == 0);
                                record(Lag_Frame);
                            }
                        }
                    }
                }
            }
            printf("read %u %u %016" PRIx64 "\n", topology, options, hash);
        }
    }
}

static void write_scenarios(void)
{
    unsigned int types, previous, data;
    for (types = 0; types < 4; ++types)
    {
        hash = UINT64_C(14695981039346656037);
        Controller_1_Type = (types & 1u) ? 0x10u : 0u;
        Controller_2_Type = (types & 2u) ? 0x10u : 0u;
        for (previous = 0; previous < 256; ++previous)
        {
            for (data = 0; data < 256; ++data)
            {
                Controller_1_State = 0x12345600u | previous;
                Controller_2_State = 0xabcdef00u | previous;
                Controller_1_Counter = 0x44ff88ffu;
                Controller_2_Counter = 0x77ff55ffu;
                Controller_1_Delay = 0x919293ffu;
                Controller_2_Delay = 0x818283ffu;
                Lag_Frame = 7;
                record(WR_Controller_1((unsigned char)data));
                record_state();
                record(WR_Controller_2((unsigned char)data));
                record_state();
            }
        }
        printf("write %u %016" PRIx64 "\n", types, hash);
    }
}

static void timeout_scenarios(void)
{
    unsigned int delay1, delay2;
    hash = UINT64_C(14695981039346656037);
    for (delay1 = 0; delay1 < 256; ++delay1)
    {
        for (delay2 = 0; delay2 < 256; ++delay2)
        {
            Controller_1_Delay = 0x12345600u | delay1;
            Controller_2_Delay = 0xabcdef00u | delay2;
            Controller_1_Counter = 0x12345678u;
            Controller_2_Counter = 0xabcdef12u;
            Fix_Controllers();
            record_state();
        }
    }
    printf("timeout %016" PRIx64 "\n", hash);
}

static void wide_scenarios(void)
{
    GensInputConfig config = {GENS_INPUT_TEAM_PLAYER, 6, 6, 6, 6, 6};
    unsigned int phase, lines;
    hash = UINT64_C(14695981039346656037);
    assert(gens_input_configure(&config));
    Controller_1B_Type = 0x101u;
    Controller_1C_Type = 0x201u;
    Controller_1D_Type = 0x401u;
    Controller_1_Up = 0x101u;
    Controller_1B_A = 0x201u;
    Controller_2_Type = 0x10u;
    for (phase = 0; phase < 16; ++phase)
    {
        for (lines = 0; lines < 4; ++lines)
        {
            Controller_1_State = lines << 5;
            Controller_2_State = lines << 5;
            Controller_1_Counter = (phase << 16) | (phase & 3u);
            Controller_2_Counter = (phase << 16) | (phase & 3u);
            record(READ_1());
            record(READ_2());
        }
    }
    printf("wide %016" PRIx64 "\n", hash);
}

int main(void)
{
    assert((uintptr_t)&Controller_2_State - (uintptr_t)&Controller_1_State == 68u);
    assert((uintptr_t)&Controller_1B_Type - (uintptr_t)&Controller_1_State == 136u);
    assert((uintptr_t)&Controller_2D_Z + sizeof(Controller_2D_Z) -
           (uintptr_t)&Controller_1_State == 448u);
    read_scenarios();
    write_scenarios();
    timeout_scenarios();
    wide_scenarios();
    return 0;
}
