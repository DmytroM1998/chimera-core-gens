#include <assert.h>
#include <stdint.h>

#include "gens_sound.h"

int YM2612_Improv, PSG_Improv;
int Seg_L[882], Seg_R[882], Seg_Length;
static unsigned int calls, last_kind, last_port, last_value;
static int fail_init, write_result, fail_reset, last_clock, last_rate, last_interpolation;

int YM2612_Init(int clock, int rate, int interpolation)
{
    ++calls;
    last_kind = 1;
    last_clock = clock;
    last_rate = rate;
    last_interpolation = interpolation;
    return fail_init;
}

void PSG_Init(int clock, int rate)
{
    ++calls;
    last_kind = 2;
    assert(rate == last_rate);
    assert(clock == (last_clock == 53693175 / 7 ? 53693175 / 15 : 53203424 / 15));
}

int YM2612_Read(void) { ++calls; last_kind = 3; return 0x1234; }
int YM2612_Write(unsigned char port, unsigned char value)
{
    ++calls;
    last_kind = 4;
    last_port = port;
    last_value = value;
    return write_result;
}
void PSG_Write(int value) { ++calls; last_kind = 5; last_value = (unsigned int)value; }
int YM2612_Reset(void) { ++calls; last_kind = 6; return fail_reset; }
void PSG_Special_Update(void) { ++calls; last_kind = 7; }
void YM2612_Special_Update(void) { ++calls; last_kind = 8; }

int main(void)
{
    GensM68kBusState state = {0};
    uint8_t ram[8192] = {0};
    int bank = 0;
    GensZ80Memory memory = {
        ram, sizeof(ram), &bank, 0, gens_sound_z80_read, gens_sound_z80_write
    };
    uint16_t value = 0x5555;
    unsigned int i;
    assert(!gens_sound_init(2, 44100, 0, 0));
    assert(!gens_sound_init(0, 48000, 0, 0));
    assert(!gens_sound_init(0, 44100, 2, 0));
    assert(!gens_sound_init(0, 44100, 0, -1));
    assert(calls == 0);
    assert(gens_sound_init(0, 11025, 0, 1));
    assert(calls == 2 && last_kind == 2 && last_clock == 53693175 / 7 &&
           last_rate == 11025 && last_interpolation == 0 && YM2612_Improv == 0 && PSG_Improv == 1);
    assert(gens_sound_init(1, 44100, 1, 0));
    assert(calls == 4 && last_clock == 53203424 / 7 && last_rate == 44100 &&
           last_interpolation == 1 && YM2612_Improv == 1 && PSG_Improv == 0);
    fail_init = 1;
    assert(!gens_sound_init(0, 22050, 0, 0));
    assert(calls == 5 && last_kind == 1);
    assert(!gens_sound_z80_read(0, &state, GENS_Z80_VDP_STATUS, 0x7f04, &value));
    assert(!gens_sound_z80_read(0, &state, GENS_Z80_YM2612, 4, &value));
    assert(!gens_sound_z80_read(0, 0, GENS_Z80_YM2612, 0, &value));
    assert(value == 0x5555 && calls == 5);
    assert(gens_sound_z80_read(0, &state, GENS_Z80_YM2612, 3, &value));
    assert(value == 0x1234 && calls == 6);
    for (i = 0; i < 3; ++i)
    {
        write_result = (int)i;
        assert(gens_sound_z80_write(0, &state, GENS_Z80_YM2612, i, 0x87));
        assert(last_port == i && last_value == 0x87 && last_kind == 4);
    }
    assert(gens_sound_z80_write(0, &state, GENS_Z80_PSG, 0x7f11, 0x91));
    assert(last_kind == 5 && last_value == 0x91);
    assert(!gens_sound_z80_write(0, &state, GENS_Z80_YM2612, 4, 0));
    assert(!gens_sound_z80_write(0, &state, GENS_Z80_VDP_DATA, 0x7000, 0));
    assert(gens_sound_genesis_write(0, &state, GENS_GENESIS_PSG, 0xc00011, 1, 0xabcd));
    assert(last_value == 0xcd);
    assert(!gens_sound_genesis_write(0, &state, GENS_GENESIS_PSG, 0xc00011, 2, 0));
    assert(!gens_sound_genesis_write(0, &state, GENS_GENESIS_VDP, 0xc00000, 1, 0));
    assert(gens_sound_reset_ym2612(0, &state));
    fail_reset = 1;
    assert(!gens_sound_reset_ym2612(0, &state));
    gens_frame_backend_psg_finish();
    assert(last_kind == 7);
    gens_frame_backend_ym_finish();
    assert(last_kind == 8);
    assert(gens_z80_window_write(&memory, &state, 0xa04003, 2, 0xabcd));
    assert(last_kind == 4 && last_port == 3 && last_value == 0xcd);
    assert(gens_z80_window_read(&memory, &state, 0xa04003, 2, &value) && value == 0x1234);
    assert(gens_z80_window_write(&memory, &state, 0xa07f11, 2, 0xabcd));
    assert(last_kind == 5 && last_value == 0xcd);
    assert(!gens_z80_window_read(&memory, &state, 0xa07f04, 2, &value));
    {
        GensPcmOutput output;
        GensPcmOptions options = {256, 1, 0, 0, 0};
        Seg_Length = 2;
        Seg_L[0] = -32769;
        Seg_R[0] = 32768;
        Seg_L[1] = 12;
        Seg_R[1] = -34;
        assert(gens_sound_output(&output, &options));
        assert(output.frames == 2 && output.samples[0] == -32768 &&
               output.samples[1] == 32767 && output.samples[2] == 12 && output.samples[3] == -34);
        assert(Seg_L[0] == 0 && Seg_R[0] == 0 && Seg_L[1] == 0 && Seg_R[1] == 0);
        Seg_Length = 883;
        assert(!gens_sound_output(&output, &options));
        Seg_Length = -1;
        assert(!gens_sound_output(&output, &options));
    }
    return 0;
}
