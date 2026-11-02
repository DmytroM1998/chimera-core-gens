#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "gens_audio.h"

unsigned int Sound_Extrapol[312][2];
int Seg_L[882], Seg_R[882], Seg_Junk[882];
int VDP_Current_Line, VDP_Num_Lines, Seg_Length, disableSound2;
int *PSG_Buf[2], *YM_Buf[2];
int PSG_Len, YM_Len, PSG_Enable, YM2612_Enable, PSG_Improv;

static int update_kind, update_length;
static int *update_left, *update_right;

static int *LeftAudioBuffer(void) { return disableSound2 ? Seg_Junk : Seg_L; }
static int *RightAudioBuffer(void) { return disableSound2 ? Seg_Junk : Seg_R; }

static void capture_update(int kind, int **buffer, int length)
{
    assert(length > 0);
    update_kind = kind;
    update_length = length;
    update_left = buffer[0];
    update_right = buffer[1];
    buffer[0][length - 1] += 1;
    buffer[1][length - 1] += 2;
}

void PSG_Update(int **buffer, int length) { capture_update(1, buffer, length); }
void PSG_Update_SIN(int **buffer, int length) { capture_update(2, buffer, length); }
void YM2612_Update(int **buffer, int length) { capture_update(3, buffer, length); }

#include "audio_special.inc"

static void prepare(int line, int lines, int samples)
{
    int i, active = line < lines ? line : lines - 1;
    VDP_Current_Line = line;
    VDP_Num_Lines = lines;
    Seg_Length = samples;
    disableSound2 = 0;
    PSG_Enable = YM2612_Enable = 1;
    PSG_Improv = 0;
    update_kind = update_length = 0;
    for (i = 0; i < lines; ++i)
    {
        Sound_Extrapol[i][0] = (unsigned int)(samples * i / lines);
        Sound_Extrapol[i][1] = (unsigned int)(samples * (i + 1) / lines) -
                               Sound_Extrapol[i][0];
    }
    PSG_Buf[0] = YM_Buf[0] = Seg_L + Sound_Extrapol[active][0];
    PSG_Buf[1] = YM_Buf[1] = Seg_R + Sound_Extrapol[active][0];
    PSG_Len = YM_Len = (int)Sound_Extrapol[active][1];
}

int main(int argc, char **argv)
{
    int *old_left, *old_right;
    int old_length;
    if (argc > 1)
    {
        prepare(0, 262, 735);
        VDP_Num_Lines = 263;
        if (argv[1][0] == 'p')
            PSG_Special_Update();
        else
            YM2612_Special_Update();
        return 2;
    }

    prepare(7, 262, 735);
    old_left = PSG_Buf[0]; old_right = PSG_Buf[1]; old_length = PSG_Len;
    PSG_Special_Update();
    assert(update_kind == 1 && update_left == old_left && update_right == old_right);
    assert(update_length == old_length && PSG_Len == 0);
    assert(PSG_Buf[0] == Seg_L + Sound_Extrapol[8][0]);
    assert(PSG_Buf[1] == Seg_R + Sound_Extrapol[8][0]);
    update_kind = 0;
    PSG_Special_Update();
    assert(update_kind == 0);

    prepare(20, 312, 882);
    PSG_Improv = 1;
    PSG_Special_Update();
    assert(update_kind == 2 && PSG_Len == 0);
    assert(PSG_Buf[0] == Seg_L + Sound_Extrapol[21][0]);

    prepare(311, 312, 882);
    old_left = YM_Buf[0]; old_right = YM_Buf[1]; old_length = YM_Len;
    YM2612_Special_Update();
    assert(update_kind == 3 && update_left == old_left && update_right == old_right);
    assert(update_length == old_length && YM_Len == 0);
    assert(YM_Buf[0] == Seg_L + 882 && YM_Buf[1] == Seg_R + 882);

    prepare(312, 312, 882);
    PSG_Special_Update();
    assert(PSG_Len == 0 && PSG_Buf[0] == Seg_L + 882 && PSG_Buf[1] == Seg_R + 882);
    prepare(262, 262, 735);
    Sound_Extrapol[263][0] = 123;
    YM2612_Special_Update();
    assert(YM_Len == 0 && YM_Buf[0] == Seg_L + 735 && YM_Buf[1] == Seg_R + 735);

    prepare(10, 262, 735);
    disableSound2 = 1;
    PSG_Buf[0] = PSG_Buf[1] = Seg_Junk + Sound_Extrapol[10][0];
    PSG_Special_Update();
    assert(update_left == Seg_Junk + Sound_Extrapol[10][0] && update_right == update_left);
    assert(PSG_Buf[0] == Seg_Junk + Sound_Extrapol[11][0] && PSG_Buf[1] == PSG_Buf[0]);

    prepare(0, 262, 735);
    PSG_Enable = YM2612_Enable = 0;
    VDP_Num_Lines = 263;
    old_left = PSG_Buf[0]; old_length = PSG_Len;
    PSG_Special_Update();
    YM2612_Special_Update();
    assert(update_kind == 0 && PSG_Buf[0] == old_left && PSG_Len == old_length);
    assert(YM_Len == old_length);
    return 0;
}
