#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "psg.h"
#include "ym2612.h"
#ifndef GENS_SOUND_REFERENCE
#include "gens_sound.h"
#endif

unsigned int Sound_Extrapol[312][2];
int Seg_L[882], Seg_R[882], Seg_Junk[882];
int VDP_Current_Line, VDP_Num_Lines, Seg_Length, GYM_Dumping, disableSound2;
static uint64_t hash;
#ifndef GENS_SOUND_REFERENCE
static GensM68kBusState state;
#endif

_Static_assert(sizeof(int) == 4 && sizeof(double) == 8, "sound reference scalar widths");

static void record(uint32_t value)
{
    unsigned int shift;
    for (shift = 0; shift < 32; shift += 8)
    {
        hash ^= (value >> shift) & 0xffu;
        hash *= UINT64_C(1099511628211);
    }
}

int Update_GYM_Dump(char type, char address, char value)
{
    record((uint8_t)type);
    record((uint8_t)address);
    record((uint8_t)value);
    return 0;
}

static void ym_write(unsigned int port, uint8_t value)
{
#ifdef GENS_SOUND_REFERENCE
    YM2612_Write((unsigned char)port, value);
#else
    assert(gens_sound_z80_write(0, &state, GENS_Z80_YM2612, port, value));
#endif
}

static void psg_write(uint8_t value)
{
#ifdef GENS_SOUND_REFERENCE
    PSG_Write(value);
#else
    assert(gens_sound_genesis_write(0, &state, GENS_GENESIS_PSG, 0xc00011, 1, value));
#endif
}

static void reg_write(unsigned int bank, uint8_t address, uint8_t value)
{
    ym_write(bank, address);
    ym_write(bank + 1u, value);
}

static void finish(void)
{
#ifdef GENS_SOUND_REFERENCE
    PSG_Special_Update();
    YM2612_Special_Update();
#else
    gens_frame_backend_psg_finish();
    gens_frame_backend_ym_finish();
#endif
}

static void setup_voices(void)
{
    unsigned int channel, slot;
    for (channel = 0; channel < 6; ++channel)
    {
        unsigned int bank = (channel / 3u) * 2u, index = channel % 3u;
        for (slot = 0; slot < 4; ++slot)
        {
            unsigned int offset = slot * 4u + index;
            reg_write(bank, (uint8_t)(0x30u + offset), 1);
            reg_write(bank, (uint8_t)(0x40u + offset), (uint8_t)(0x18u + channel));
            reg_write(bank, (uint8_t)(0x50u + offset), 0x1f);
            reg_write(bank, (uint8_t)(0x60u + offset), 7);
            reg_write(bank, (uint8_t)(0x70u + offset), 5);
            reg_write(bank, (uint8_t)(0x80u + offset), 0x0f);
        }
        reg_write(bank, (uint8_t)(0xb0u + index), 7);
        reg_write(bank, (uint8_t)(0xb4u + index), 0xc7);
        reg_write(bank, (uint8_t)(0xa4u + index), 0x22);
        reg_write(bank, (uint8_t)(0xa0u + index), (uint8_t)(0x69u + channel));
        reg_write(0, 0x28, (uint8_t)(0xf0u | index | (channel >= 3u ? 4u : 0u)));
    }
    reg_write(0, 0x22, 8);
    reg_write(0, 0x24, 0xff);
    reg_write(0, 0x25, 3);
    reg_write(0, 0x26, 0xef);
    reg_write(0, 0x27, 0x0f);
    reg_write(0, 0x2b, 0x80);
    psg_write(0x80);
    psg_write(2);
    psg_write(0x94);
    psg_write(0xa0);
    psg_write(3);
    psg_write(0xb5);
    psg_write(0xe4);
    psg_write(0xf3);
}

static void record_state(void)
{
    unsigned int channel, slot, i, bank;
    uint64_t frequency;
    memcpy(&frequency, &YM2612.Frequence, sizeof(frequency));
    record((uint32_t)frequency);
    record((uint32_t)(frequency >> 32));
#define YM(field) record((uint32_t)YM2612.field)
    YM(Clock); YM(Rate); YM(TimerBase); YM(Status); YM(OPNAadr); YM(OPNBadr);
    YM(LFOcnt); YM(LFOinc); YM(TimerA); YM(TimerAL); YM(TimerAcnt);
    YM(TimerB); YM(TimerBL); YM(TimerBcnt); YM(Mode); YM(DAC); YM(DACdata);
    YM(Inter_Cnt); YM(Inter_Step);
#undef YM
    for (channel = 0; channel < 6; ++channel)
    {
        channel_ *ch = &YM2612.CHANNEL[channel];
        for (i = 0; i < 4; ++i)
        {
            record((uint32_t)ch->S0_OUT[i]);
            record((uint32_t)ch->FNUM[i]);
            record((uint32_t)ch->FOCT[i]);
            record((uint32_t)ch->KC[i]);
        }
#define CH(field) record((uint32_t)ch->field)
        CH(Old_OUTd); CH(OUTd); CH(LEFT); CH(RIGHT); CH(ALGO); CH(FB); CH(FMS); CH(AMS); CH(FFlag);
#undef CH
        for (slot = 0; slot < 4; ++slot)
        {
            slot_ *sl = &ch->SLOT[slot];
#define SL(field) record((uint32_t)sl->field)
            SL(MUL); SL(TL); SL(TLL); SL(SLL); SL(KSR_S); SL(KSR); SL(SEG);
            SL(Fcnt); SL(Finc); SL(Ecurp); SL(Ecnt); SL(Einc); SL(Ecmp);
            SL(EincA); SL(EincD); SL(EincS); SL(EincR); SL(INd); SL(ChgEnM); SL(AMS); SL(AMSon);
#undef SL
            assert(sl->DT != 0 && sl->AR != 0 && sl->DR != 0 && sl->SR != 0 && sl->RR != 0);
            record((uint32_t)*sl->DT);
            record((uint32_t)*sl->AR);
            record((uint32_t)*sl->DR);
            record((uint32_t)*sl->SR);
            record((uint32_t)*sl->RR);
        }
    }
    for (bank = 0; bank < 2; ++bank)
        for (i = 0; i < 256; ++i)
            record((uint32_t)YM2612.REG[bank][i]);
    record((uint32_t)PSG.Current_Channel);
    record((uint32_t)PSG.Current_Register);
    for (i = 0; i < 8; ++i)
        record((uint32_t)PSG.Register[i]);
    for (i = 0; i < 4; ++i)
    {
        record(PSG.Counter[i]);
        record(PSG.CntStep[i]);
        record((uint32_t)PSG.Volume[i]);
    }
    record(PSG.Noise);
    record(PSG.Noise_Type);
}

static void fixture(int pal, int rate, int improvement)
{
    int line, samples = pal ? rate / 50 : (rate + 30) / 60;
    unsigned int i, nonzero = 0;
    hash = UINT64_C(14695981039346656037);
    VDP_Num_Lines = pal ? 312 : 262;
    Seg_Length = samples;
    VDP_Current_Line = -1;
    GYM_Dumping = disableSound2 = 0;
    YM_Len = PSG_Len = 0;
    YM2612_Enable = PSG_Enable = DAC_Enable = 1;
    memset(Seg_L, 0, sizeof(Seg_L));
    memset(Seg_R, 0, sizeof(Seg_R));
    memset(Seg_Junk, 0, sizeof(Seg_Junk));
    YM_Buf[0] = PSG_Buf[0] = Seg_L;
    YM_Buf[1] = PSG_Buf[1] = Seg_R;
    for (line = 0; line < VDP_Num_Lines; ++line)
    {
        Sound_Extrapol[line][0] = (unsigned int)(samples * line / VDP_Num_Lines);
        Sound_Extrapol[line][1] = (unsigned int)(samples * (line + 1) / VDP_Num_Lines) -
            Sound_Extrapol[line][0];
    }
#ifdef GENS_SOUND_REFERENCE
    YM2612_Improv = improvement & 1;
    PSG_Improv = (improvement >> 1) & 1;
    assert(YM2612_Init((pal ? 53203424 : 53693175) / 7, rate, YM2612_Improv) == 0);
    PSG_Init((pal ? 53203424 : 53693175) / 15, rate);
#else
    assert(gens_sound_init(pal, rate, improvement & 1, (improvement >> 1) & 1));
#endif
    GYM_Dumping = 1;
    setup_voices();
    for (line = 0; line < VDP_Num_Lines - 1; ++line)
    {
        unsigned int offset = Sound_Extrapol[line][0], count = Sound_Extrapol[line][1];
        int *buffer[2] = {Seg_L + offset, Seg_R + offset};
        VDP_Current_Line = line;
        YM2612_DacAndTimers_Update(buffer, (int)count);
        YM_Len += (int)count;
        PSG_Len += (int)count;
        reg_write(0, 0x2a, (uint8_t)(line * 17));
        if ((line & 15) == 0)
        {
            psg_write((uint8_t)(0x90u | ((unsigned int)line >> 4 & 15u)));
            reg_write(0, 0xa0, (uint8_t)line);
        }
        if ((line & 31) == 31)
            finish();
        if (line == VDP_Num_Lines / 2)
        {
#ifdef GENS_SOUND_REFERENCE
            assert(YM2612_Reset() == 0);
#else
            assert(gens_sound_reset_ym2612(0, &state));
#endif
        }
#ifdef GENS_SOUND_REFERENCE
        record((uint32_t)YM2612_Read());
#else
        {
            uint16_t status;
            assert(gens_sound_z80_read(0, &state, GENS_Z80_YM2612, 0, &status));
            record(status);
        }
#endif
    }
    finish();
    assert(YM_Len == 0 && PSG_Len == 0);
    for (i = 0; i < 882; ++i)
    {
#ifdef GENS_SOUND_NEGATIVE_CONTROL
        if (i == 0)
            Seg_L[i] ^= 1;
#endif
        record((uint32_t)Seg_L[i]);
        record((uint32_t)Seg_R[i]);
        record((uint32_t)Seg_Junk[i]);
        if (Seg_L[i] != 0 || Seg_R[i] != 0)
            ++nonzero;
    }
    assert(nonzero != 0);
    record_state();
    printf("sound %d %d %d %016" PRIx64 "\n", pal, rate, improvement, hash);
}

int main(void)
{
    static const int rates[] = {11025, 22050, 44100};
    int pal, improvement;
    unsigned int rate;
    for (pal = 0; pal < 2; ++pal)
        for (rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate)
            for (improvement = 0; improvement < 4; ++improvement)
                fixture(pal, rates[rate], improvement);
    return 0;
}
