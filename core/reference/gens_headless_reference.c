/* Headless reference built from the unchanged i386 Gens modules.
 *
 * The CPU cores, memory maps, VDP, renderer, controller protocol and sound chips are the
 * original assembly and C files. This file replaces only the Windows front end. Its
 * startup, Init_Genesis, Reset_Genesis and Do_Genesis_Frame bodies follow Gens.cpp,
 * G_main.cpp and G_dsound.cpp at the pinned revision, without the UI, movie, Lua, cheat
 * list and audio-device parts. Do_Genesis_Frame runs with fast = false and sound enabled,
 * as during normal recording.
 */
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "Mem_M68k.h"
#include "Mem_Z80.h"
#include "vdp_io.h"
#include "vdp_rend.h"
#include "io.h"
#include "ym2612.h"
#include "psg.h"
#include "Cpu_68k.h"
#include "Cpu_Z80.h"
#include "z80.h"
#include "Misc.h"

#include "gens_color_lookup.h"
#include "gens_input.h"
#include "gens_pcm.h"
#include "../tests/gens_headless_trace.h"

#define CLOCK_NTSC 53693175
#define CLOCK_PAL 53203424

extern struct _Z80_context M_Z80;
extern unsigned short Cell_Conv_Tab[];

/* Globals that Gens.cpp, G_dsound.cpp and ggenie.cpp define. */
int Seg_L[882], Seg_R[882], Seg_Junk[882];
int Seg_Length;
int Sound_Rate = 44100;
unsigned int Sound_Interpol[882];
unsigned int Sound_Extrapol[312][2];
int disableSound2;
int GYM_Dumping;
int Country = -1;
int Country_Order[3] = {0, 1, 2};
static int Lag_Frame_Unused;
extern unsigned char *ScrA_Addr;
static unsigned char *ScrA_Addr_Dbg(void) { return ScrA_Addr; }

void Update_GYM_Dump(unsigned char v0, unsigned char v1, unsigned char v2)
{
    (void)v0; (void)v1; (void)v2;
}

/* Called by gens_headless_sound_abi_i386.asm for every assembly-side YM2612 write. */
int YM2612_Write_Traced(unsigned char adr, unsigned char data)
{
    gens_trace_ym_write(adr, data);
    return YM2612_Write(adr, data);
}

/* No Game Genie codes are active. Gens' Fix_Codes and Patch_Codes then do nothing. */
void Fix_Codes(unsigned int address, unsigned char size) { (void)address; (void)size; }
static void Patch_Codes(void) {}

/* Starscream hooks. Lua and the tracer are not part of the reference. */
unsigned int hook_pc, hook_address, hook_value;
static int pclog;
void hook_exec(void)
{
    if (pclog > 0)
    {
        fprintf(stderr, "pc %06x d0=%08x d1=%08x a0=%08x\n", hook_pc, main68k_context.dreg[0],
                main68k_context.dreg[1], main68k_context.areg[0]);
        --pclog;
    }
}
void hook_read_byte(void) {}
void hook_read_word(void) {}
void hook_read_dword(void) {}
void hook_write_byte(void) {}
void hook_write_word(void) {}
void hook_write_dword(void) {}

static int Round_Double(double val)
{
    if ((val - (double)(int)val) > 0.5) return (int)(val + 1);
    else return (int)val;
}

/* Gens.cpp Init_Tab. */
static void Init_Tab(void)
{
    int x, y, dep;
    for (x = 0; x < 1024; x++)
        for (y = 0; y < 64; y++)
        {
            dep = (x & 3) + (((x & 0x3FC) >> 2) << 8);
            dep += ((y & 7) << 2) + (((y & 0xF8) >> 3) << 5);
            dep >>= 1;
            Cell_Conv_Tab[(x >> 1) + (y << 9)] = (unsigned short)dep;
        }
    for (x = 0; x < 512; x++)
        for (y = 0; y < 64; y++)
        {
            dep = (x & 3) + (((x & 0x1FC) >> 2) << 8);
            dep += ((y & 7) << 2) + (((y & 0xF8) >> 3) << 5);
            dep >>= 1;
            Cell_Conv_Tab[(x >> 1) + (y << 8) + 0x8000] = (unsigned short)(dep + 0x8000);
        }
    for (x = 0; x < 256; x++)
        for (y = 0; y < 64; y++)
        {
            dep = (x & 3) + (((x & 0xFC) >> 2) << 8);
            dep += ((y & 7) << 2) + (((y & 0xF8) >> 3) << 5);
            dep >>= 1;
            Cell_Conv_Tab[(x >> 1) + (y << 7) + 0xC000] = (unsigned short)(dep + 0xC000);
        }
    for (x = 0; x < 256; x++)
        for (y = 0; y < 32; y++)
        {
            dep = (x & 3) + (((x & 0xFC) >> 2) << 7);
            dep += ((y & 7) << 2) + (((y & 0xF8) >> 3) << 5);
            dep >>= 1;
            Cell_Conv_Tab[(x >> 1) + (y << 7) + 0xE000] = (unsigned short)(dep + 0xE000);
            Cell_Conv_Tab[(x >> 1) + (y << 7) + 0xF000] = (unsigned short)(dep + 0xF000);
        }
    for (x = 0; x < 512; x++) Z80_M68K_Cycle_Tab[x] = (int)((double)x * 7.0 / 15.0);
}

/* G_dsound.cpp Init_Sound, table part. */
static void Init_Sound_Tables(void)
{
    int i;
    switch (Sound_Rate)
    {
        case 11025: Seg_Length = CPU_Mode ? 220 : 184; break;
        case 22050: Seg_Length = CPU_Mode ? 441 : 368; break;
        case 44100: Seg_Length = CPU_Mode ? 882 : 735; break;
    }
    if (CPU_Mode)
    {
        for (i = 0; i < 312; i++)
        {
            Sound_Extrapol[i][0] = ((Seg_Length * i) / 312);
            Sound_Extrapol[i][1] = (((Seg_Length * (i + 1)) / 312) - Sound_Extrapol[i][0]);
        }
        for (i = 0; i < Seg_Length; i++)
            Sound_Interpol[i] = ((312 * i) / Seg_Length);
    }
    else
    {
        for (i = 0; i < 262; i++)
        {
            Sound_Extrapol[i][0] = ((Seg_Length * i) / 262);
            Sound_Extrapol[i][1] = (((Seg_Length * (i + 1)) / 262) - Sound_Extrapol[i][0]);
        }
        for (i = 0; i < Seg_Length; i++)
            Sound_Interpol[i] = ((262 * i) / Seg_Length);
    }
    memset(Seg_L, 0, Seg_Length << 2);
    memset(Seg_R, 0, Seg_Length << 2);
}

/* Gens.cpp Misc_Genesis_Init, Genesis fields. */
static void Misc_Genesis_Init(void)
{
    memset(&M_Z80.AF, 0, sizeof(M_Z80.CycleSup) + ((char *)&M_Z80.CycleSup - (char *)&M_Z80.AF));
    M_Z80.RetIC = 0;
    M_Z80.IntAckC = 0;
    Controller_1_Delay = 0;
    Controller_2_Delay = 0;
    Ctrl.DMA_Mode = 0;
    VDP_Reg.DMA_Address = 0;
    VDP_Current_Line = 0;
    Cycles_S68K = 0;
    Cycles_M68K = 0;
    Cycles_Z80 = 0;
    S68K_State = 0;
    CPL_S68K = 0;
    Bank_M68K = 0;
    Lag_Frame = 0;
    main68k_context.cycles_leftover = 0;
    main68k_context.xflag = 0;
    main68k_context.io_cycle_counter = -1;
    main68k_context.io_fetchbase = 0;
    main68k_context.io_fetchbased_pc = 0;
    main68k_context.access_address = 0;
    main68k_context.odometer = 0;
}

/* Gens.cpp Detect_Country_Genesis without the 32X and message parts. */
static void Detect_Country_Genesis(void)
{
    int c_tab[3] = {4, 1, 8};
    int gm_tab[3] = {1, 0, 1};
    int cm_tab[3] = {0, 0, 1};
    int i, coun = 0;
    char c;

    if (!strncasecmp((char *)&Rom_Data[0x1F0], "eur", 3)) coun |= 8;
    else if (!strncasecmp((char *)&Rom_Data[0x1F0], "usa", 3)) coun |= 4;
    else if (!strncasecmp((char *)&Rom_Data[0x1F0], "jap", 3)) coun |= 1;
    else for (i = 0; i < 4; i++)
    {
        c = (char)toupper(Rom_Data[0x1F0 + i]);
        if (c == 'U') coun |= 4;
        else if (c == 'J') coun |= 1;
        else if (c == 'E') coun |= 8;
        else if (c < 16) coun |= c;
        else if ((c >= '0') && (c <= '9')) coun |= c - '0';
        else if ((c >= 'A') && (c <= 'F')) coun |= c - 'A' + 10;
    }

    if (coun & c_tab[Country_Order[0]])
    {
        Game_Mode = gm_tab[Country_Order[0]];
        CPU_Mode = cm_tab[Country_Order[0]];
    }
    else if (coun & c_tab[Country_Order[1]])
    {
        Game_Mode = gm_tab[Country_Order[1]];
        CPU_Mode = cm_tab[Country_Order[1]];
    }
    else if (coun & c_tab[Country_Order[2]])
    {
        Game_Mode = gm_tab[Country_Order[2]];
        CPU_Mode = cm_tab[Country_Order[2]];
    }
    else if (coun & 2)
    {
        Game_Mode = 0;
        CPU_Mode = 1;
    }
    else
    {
        Game_Mode = 1;
        CPU_Mode = 0;
    }

    if (CPU_Mode) VDP_Status |= 0x0001;
    else VDP_Status &= 0xFFFE;
}

/* rom.cpp Fill_Infos SRAM fields and Gens.cpp Init_Genesis, Genesis parts. */
static int Init_Genesis(void)
{
    unsigned char ram_infos[12];
    unsigned int ram_start, ram_end;
    int i;

    Misc_Genesis_Init();
    for (i = 0; i < 12; i++) ram_infos[i] = Rom_Data[i + 424];
    ram_start = (unsigned)Rom_Data[436] << 24 | Rom_Data[437] << 16 | Rom_Data[438] << 8 | Rom_Data[439];
    ram_end = (unsigned)Rom_Data[440] << 24 | Rom_Data[441] << 16 | Rom_Data[442] << 8 | Rom_Data[443];

    SRAM_Start = SRAM_End = SRAM_ON = SRAM_Write = 0;
    Controller_1_COM = Controller_2_COM = 0;
    if ((ram_infos[8] == 'R') && (ram_infos[9] == 'A') && (ram_infos[10] & 0x40))
    {
        SRAM_Start = ram_start & 0x0F80000;
        SRAM_End = ram_end;
    }
    else
    {
        SRAM_Start = 0x200000;
        SRAM_End = 0x20FFFF;
    }
    if ((SRAM_Start > SRAM_End) || ((SRAM_End - SRAM_Start) >= (64 * 1024)))
        SRAM_End = SRAM_Start + (64 * 1024) - 1;
    if (Rom_Size <= (2 * 1024 * 1024))
    {
        SRAM_ON = 1;
        SRAM_Write = 1;
    }
    SRAM_Start &= 0xFFFFFFFE;
    SRAM_End |= 0x00000001;
    if ((SRAM_End - SRAM_Start) <= 2) SRAM_Custom = 1;
    else SRAM_Custom = 0;
    memset(SRAM, 0, 64 * 1024);

    switch (Country)
    {
        default:
        case -1: Detect_Country_Genesis(); break;
        case 0: Game_Mode = 0; CPU_Mode = 0; break;
        case 1: Game_Mode = 1; CPU_Mode = 0; break;
        case 2: Game_Mode = 1; CPU_Mode = 1; break;
        case 3: Game_Mode = 0; CPU_Mode = 1; break;
    }

    VDP_Num_Vis_Lines = 224;
    Gen_Version = 0x20 + 0x0;
    Byte_Swap(Rom_Data, Rom_Size);

    M68K_Init();
    M68K_Reset(0, 1);
    Z80_Reset();
    Reset_VDP();

    if (CPU_Mode)
    {
        CPL_Z80 = Round_Double((((double)CLOCK_PAL / 15.0) / 50.0) / 312.0);
        CPL_M68K = Round_Double((((double)CLOCK_PAL / 7.0) / 50.0) / 312.0);
        VDP_Num_Lines = 312;
        VDP_Status |= 0x0001;
        YM2612_Init(CLOCK_PAL / 7, Sound_Rate, YM2612_Improv);
        PSG_Init(CLOCK_PAL / 15, Sound_Rate);
    }
    else
    {
        CPL_Z80 = Round_Double((((double)CLOCK_NTSC / 15.0) / 60.0) / 262.0);
        CPL_M68K = Round_Double((((double)CLOCK_NTSC / 7.0) / 60.0) / 262.0);
        VDP_Num_Lines = 262;
        VDP_Status &= 0xFFFE;
        YM2612_Init(CLOCK_NTSC / 7, Sound_Rate, YM2612_Improv);
        PSG_Init(CLOCK_NTSC / 15, Sound_Rate);
    }
    Init_Sound_Tables();
    return 1;
}

/* Gens.cpp Reset_Genesis. */
static void Reset_Genesis(void)
{
    Controller_1_COM = Controller_2_COM = 0;
    if (Rom_Size <= (2 * 1024 * 1024))
    {
        SRAM_ON = 1;
        SRAM_Write = 1;
    }
    else
    {
        SRAM_ON = 0;
        SRAM_Write = 0;
    }
    M68K_Reset(0, 1);
    Z80_Reset();
    Reset_VDP();
    YM2612_Reset();
    if (CPU_Mode) VDP_Status |= 1;
    else VDP_Status &= ~1;
}

static int *LeftAudioBuffer(void) { return disableSound2 ? Seg_Junk : Seg_L; }
static int *RightAudioBuffer(void) { return disableSound2 ? Seg_Junk : Seg_R; }

/* Gens.cpp Do_Genesis_Frame(false) with sound enabled. */
static int Do_Genesis_Frame(void)
{
    int *buf[2];
    int HInt_Counter;

    if ((CPU_Mode) && (VDP_Reg.Set2 & 0x8)) VDP_Num_Vis_Lines = 240;
    else VDP_Num_Vis_Lines = 224;

    YM_Buf[0] = PSG_Buf[0] = LeftAudioBuffer();
    YM_Buf[1] = PSG_Buf[1] = RightAudioBuffer();
    YM_Len = PSG_Len = 0;

    Cycles_M68K = Cycles_Z80 = 0;
    Last_BUS_REQ_Cnt = -1000;
    main68k_tripOdometer();
    z80_Clear_Odo(&M_Z80);

    Patch_Codes();

    VRam_Flag = 1;

    VDP_Status &= 0xFFF7;
    if (VDP_Reg.Set4 & 0x2) VDP_Status ^= 0x0010;

    HInt_Counter = VDP_Reg.H_Int;

    for (VDP_Current_Line = 0; VDP_Current_Line < VDP_Num_Vis_Lines; VDP_Current_Line++)
    {
        buf[0] = LeftAudioBuffer() + Sound_Extrapol[VDP_Current_Line][0];
        buf[1] = RightAudioBuffer() + Sound_Extrapol[VDP_Current_Line][0];
        YM2612_DacAndTimers_Update(buf, Sound_Extrapol[VDP_Current_Line][1]);
        YM_Len += Sound_Extrapol[VDP_Current_Line][1];
        PSG_Len += Sound_Extrapol[VDP_Current_Line][1];

        Fix_Controllers();
        Cycles_M68K += CPL_M68K;
        Cycles_Z80 += CPL_Z80;
        if (DMAT_Length) main68k_addCycles(Update_DMA());
        VDP_Status |= 0x0004;
        main68k_exec(Cycles_M68K - 404);
        VDP_Status &= 0xFFFB;

        if (--HInt_Counter < 0)
        {
            HInt_Counter = VDP_Reg.H_Int;
            VDP_Int |= 0x4;
            Update_IRQ_Line();
        }

        Render_Line();

        main68k_exec(Cycles_M68K);
        if (Z80_State == 3) z80_Exec(&M_Z80, Cycles_Z80);
        else z80_Set_Odo(&M_Z80, Cycles_Z80);
    }

    buf[0] = LeftAudioBuffer() + Sound_Extrapol[VDP_Current_Line][0];
    buf[1] = RightAudioBuffer() + Sound_Extrapol[VDP_Current_Line][0];
    YM2612_DacAndTimers_Update(buf, Sound_Extrapol[VDP_Current_Line][1]);
    YM_Len += Sound_Extrapol[VDP_Current_Line][1];
    PSG_Len += Sound_Extrapol[VDP_Current_Line][1];

    Fix_Controllers();
    Cycles_M68K += CPL_M68K;
    Cycles_Z80 += CPL_Z80;
    if (DMAT_Length) main68k_addCycles(Update_DMA());
    if (--HInt_Counter < 0)
    {
        VDP_Int |= 0x4;
        Update_IRQ_Line();
    }

    VDP_Status |= 0x000C;
    main68k_exec(Cycles_M68K - 360);
    if (Z80_State == 3) z80_Exec(&M_Z80, Cycles_Z80 - 168);
    else z80_Set_Odo(&M_Z80, Cycles_Z80 - 168);

    VDP_Status &= 0xFFFB;
    VDP_Status |= 0x0080;

    VDP_Int |= 0x8;
    Update_IRQ_Line();
    z80_Interrupt(&M_Z80, 0xFF);

    main68k_exec(Cycles_M68K);
    if (Z80_State == 3) z80_Exec(&M_Z80, Cycles_Z80);
    else z80_Set_Odo(&M_Z80, Cycles_Z80);

    for (VDP_Current_Line++; VDP_Current_Line < VDP_Num_Lines; VDP_Current_Line++)
    {
        buf[0] = LeftAudioBuffer() + Sound_Extrapol[VDP_Current_Line][0];
        buf[1] = RightAudioBuffer() + Sound_Extrapol[VDP_Current_Line][0];
        YM2612_DacAndTimers_Update(buf, Sound_Extrapol[VDP_Current_Line][1]);
        YM_Len += Sound_Extrapol[VDP_Current_Line][1];
        PSG_Len += Sound_Extrapol[VDP_Current_Line][1];

        Fix_Controllers();
        Cycles_M68K += CPL_M68K;
        Cycles_Z80 += CPL_Z80;
        if (DMAT_Length) main68k_addCycles(Update_DMA());
        VDP_Status |= 0x0004;
        main68k_exec(Cycles_M68K - 404);
        VDP_Status &= 0xFFFB;

        main68k_exec(Cycles_M68K);
        if (Z80_State == 3) z80_Exec(&M_Z80, Cycles_Z80);
        else z80_Set_Odo(&M_Z80, Cycles_Z80);
    }

    PSG_Special_Update();
    YM2612_Special_Update();
    return 1;
}


/* movie.cpp's playback branch, verbatim apart from reading from memory. */
static unsigned char *gmv_data;
static long gmv_size;
static int gmv_triple;

static void gmv_frame(unsigned int FrameCount)
{
    char PadData[3];
    memcpy(PadData, gmv_data + 64 + FrameCount * 3, 3);
    Controller_1_Up=(PadData[0]&1);
    Controller_1_Down=(PadData[0]&2)>>1;
    Controller_1_Left=(PadData[0]&4)>>2;
    Controller_1_Right=(PadData[0]&8)>>3;
    Controller_1_A=(PadData[0]&16)>>4;
    Controller_1_B=(PadData[0]&32)>>5;
    Controller_1_C=(PadData[0]&64)>>6;
    Controller_1_Start=(PadData[0]&128)>>7;
    if(gmv_triple)
    {
        Controller_1B_Up=(PadData[1]&1);
        Controller_1B_Down=(PadData[1]&2)>>1;
        Controller_1B_Left=(PadData[1]&4)>>2;
        Controller_1B_Right=(PadData[1]&8)>>3;
        Controller_1B_A=(PadData[1]&16)>>4;
        Controller_1B_B=(PadData[1]&32)>>5;
        Controller_1B_C=(PadData[1]&64)>>6;
        Controller_1B_Start=(PadData[1]&128)>>7;
        Controller_1C_Up=(PadData[2]&1);
        Controller_1C_Down=(PadData[2]&2)>>1;
        Controller_1C_Left=(PadData[2]&4)>>2;
        Controller_1C_Right=(PadData[2]&8)>>3;
        Controller_1C_A=(PadData[2]&16)>>4;
        Controller_1C_B=(PadData[2]&32)>>5;
        Controller_1C_C=(PadData[2]&64)>>6;
        Controller_1C_Start=(PadData[2]&128)>>7;
    }
    else
    {
        Controller_2_Up=(PadData[1]&1);
        Controller_2_Down=(PadData[1]&2)>>1;
        Controller_2_Left=(PadData[1]&4)>>2;
        Controller_2_Right=(PadData[1]&8)>>3;
        Controller_2_A=(PadData[1]&16)>>4;
        Controller_2_B=(PadData[1]&32)>>5;
        Controller_2_C=(PadData[1]&64)>>6;
        Controller_2_Start=(PadData[1]&128)>>7;
        Controller_1_X=(PadData[2]&1);
        Controller_1_Y=(PadData[2]&2)>>1;
        Controller_1_Z=(PadData[2]&4)>>2;
        Controller_1_Mode=(PadData[2]&8)>>3;
        Controller_2_X=(PadData[2]&16)>>4;
        Controller_2_Y=(PadData[2]&32)>>5;
        Controller_2_Z=(PadData[2]&64)>>6;
        Controller_2_Mode=(PadData[2]&128)>>7;
    }
}

/* G_main.cpp PlaySubMovie: controller settings from the movie. */
static int gmv_open(const char *path)
{
    FILE *f = fopen(path, "rb");
    int p1, p2;
    if (f == 0)
        return 0;
    fseek(f, 0, SEEK_END);
    gmv_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    gmv_data = (unsigned char *)malloc((size_t)gmv_size);
    if (gmv_data == 0 || gmv_size < 64 || fread(gmv_data, 1, (size_t)gmv_size, f) != (size_t)gmv_size)
        return 0;
    fclose(f);
    p1 = gmv_data[20] - '0';
    p2 = gmv_data[21] - '0';
    gmv_triple = gmv_data[15] >= 'A' ? (gmv_data[22] & 32) >> 5 : 0;
    if (gmv_triple) {
        Controller_1_Type |= 0x10;
        Controller_1B_Type &= ~1;
        Controller_1C_Type &= ~1;
    }
    else Controller_1_Type &= ~0x10;
    Controller_2_Type &= ~0x10;
    if (p1==3) Controller_1_Type &= ~1;
    if (p1==6) Controller_1_Type |= 1;
    if (p2==3) Controller_2_Type &= ~1;
    if (p2==6) Controller_2_Type |= 1;
    Make_IO_Table();
    return 1;
}

static int ends_with(const char *text, const char *suffix)
{
    size_t a = strlen(text), b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}

static void trace_frame(FILE *out, unsigned int frame)
{
    uint32_t m68k[20], z80[16], vdp[16];
    unsigned int i;
    for (i = 0; i < 8; ++i) m68k[i] = main68k_context.dreg[i];
    for (i = 0; i < 8; ++i) m68k[8 + i] = main68k_context.areg[i];
    m68k[16] = main68k_context.pc;
    m68k[17] = main68k_context.sr;
    m68k[18] = main68k_context.asp;
    m68k[19] = main68k_context.interrupts[0];
    z80[0] = M_Z80.AF.w.AF; z80[1] = M_Z80.BC.w.BC; z80[2] = M_Z80.DE.w.DE;
    z80[3] = M_Z80.HL.w.HL; z80[4] = M_Z80.IX.w.IX; z80[5] = M_Z80.IY.w.IY;
    z80[6] = M_Z80.SP.w.SP; z80[7] = (uint32_t)(M_Z80.PC.d - M_Z80.BasePC);
    z80[8] = M_Z80.AF2.w.AF2; z80[9] = M_Z80.BC2.w.BC2; z80[10] = M_Z80.DE2.w.DE2;
    z80[11] = M_Z80.HL2.w.HL2; z80[12] = M_Z80.IFF.w.IFF; z80[13] = M_Z80.I;
    z80[14] = M_Z80.Status; z80[15] = (uint32_t)Bank_Z80;
    vdp[0] = (uint32_t)VDP_Status; vdp[1] = (uint32_t)VDP_Int; vdp[2] = (uint32_t)DMAT_Length;
    vdp[3] = (uint32_t)DMAT_Type; vdp[4] = (uint32_t)DMAT_Tmp; vdp[5] = (uint32_t)Ctrl.Flag;
    vdp[6] = (uint32_t)Ctrl.Data; vdp[7] = (uint32_t)Ctrl.Write; vdp[8] = (uint32_t)Ctrl.Access;
    vdp[9] = (uint32_t)Ctrl.Address; vdp[10] = (uint32_t)Ctrl.DMA_Mode; vdp[11] = (uint32_t)Ctrl.DMA;
    vdp[12] = (uint32_t)Z80_State; vdp[13] = (uint32_t)VDP_Num_Vis_Lines;
    vdp[14] = (uint32_t)SRAM_ON; vdp[15] = (uint32_t)SRAM_Write;
    gens_trace_words(out, frame, "m68k", m68k, 20);
    gens_trace_words(out, frame, "z80", z80, 16);
    gens_trace_words(out, frame, "vdp", vdp, 16);
    gens_trace_block(out, frame, "vdp_reg", &VDP_Reg, sizeof(VDP_Reg));
    gens_trace_block(out, frame, "ram68k", Ram_68k, 65536);
    gens_trace_block(out, frame, "ramz80", Ram_Z80, 8192);
    gens_trace_block(out, frame, "vram", VRam, 65536);
    gens_trace_block(out, frame, "cram", CRam, 256);
    gens_trace_block(out, frame, "vsram", VSRam, 256);
    gens_trace_block(out, frame, "sram", SRAM, 65536);
    gens_trace_block(out, frame, "screen", Screen_16X, 336u * 240u * 2u);
    gens_trace_block(out, frame, "audio_l", Seg_L, (size_t)Seg_Length * 4u);
    gens_trace_block(out, frame, "audio_r", Seg_R, (size_t)Seg_Length * 4u);
    gens_trace_audio_dump(frame, Seg_L, Seg_R, Seg_Length);
    gens_trace_ym_flush(out, frame);
}

int main(int argc, char **argv)
{
    FILE *rom, *out;
    long length;
    unsigned int frames, frame, lag_frames = 0;
    GensTraceInput input;
    GensInputConfig config = {GENS_INPUT_TWO_PADS, 6, 6, 3, 3, 3};

    if (argc < 5)
    {
        fprintf(stderr, "usage: %s rom frames input|- trace [last-frame.ppm]\n", argv[0]);
        return 2;
    }
    frames = (unsigned int)strtoul(argv[2], 0, 0);
    if (!gens_trace_read_input(ends_with(argv[3], ".gmv") ? "-" : argv[3], frames, &input))
        return 2;

    /* G_main.cpp startup order. */
    /* save.cpp Load_Config defaults for the chip switches. */
    YM2612_Enable = PSG_Enable = DAC_Enable = 1;
    /* "Z80 State" defaults to 1: the Z80 is emulated. */
    Z80_State |= 1;
    YM2612_Improv = 0;
    PSG_Improv = 0;
    /* S68K_Init only prepares the Sega CD core and is omitted. */
    M68K_Init();
    Z80_Init();
    YM2612_Init(CLOCK_NTSC / 7, Sound_Rate, YM2612_Improv);
    PSG_Init(CLOCK_NTSC / 15, Sound_Rate);
    Init_Tab();
    /* G_main.cpp WinMain defaults and the Load_Config defaults for the layer toggles. */
    Sprite_Over = 1;
    VScrollAl = VScrollBl = VScrollAh = VScrollBh = 1;
    VSpritel = VSpriteh = 1;
    ScrollAOn = ScrollBOn = SpriteOn = 1;
    Sprite_Always_Top = 0;
    PinkBG = 0;
    {
        /* Gens' default color settings, RGB565. gens_color_lookup.c is the checked port of
         * Recalculate_Palettes and fills the same Palette and Palette32 tables. */
        GensColorProfile profile = {255, 255, 255, 100, 100, 0, 0, 0, 0};
        GensColorLookup lookup = {Palette, Palette32, 0x8000, 0x8000};
        Mode_555 = 0;
        if (!gens_color_lookup_generate(&lookup, &profile))
            return 3;
    }

    /* rom.cpp Load_Rom for a raw image. */
    memset(Rom_Data, 0, sizeof(Rom_Data));
    rom = fopen(argv[1], "rb");
    if (rom == 0)
        return 2;
    fseek(rom, 0, SEEK_END);
    length = ftell(rom);
    fseek(rom, 0, SEEK_SET);
    if (length <= 0 || length > (long)sizeof(Rom_Data))
        return 2;
    Rom_Size = (unsigned int)fread(Rom_Data, 1, (size_t)length, rom);
    fclose(rom);

    Init_Genesis();
    if (!gens_input_configure(&config))
        return 3;
    if (ends_with(argv[3], ".gmv"))
    {
        if (!gmv_open(argv[3]))
            return 2;
        if (frames > (unsigned int)((gmv_size - 64) / 3))
            frames = (unsigned int)((gmv_size - 64) / 3);
    }
    out = strcmp(argv[4], "-") == 0 ? stdout : fopen(argv[4], "w");
    if (out == 0)
        return 2;
    for (frame = 0; frame < frames; ++frame)
    {
        if (input.reset[frame])
            Reset_Genesis();
        if (gmv_data != 0)
            gmv_frame(frame);
        else
            gens_input_apply(gens_trace_packed(&input, frame));
        if (getenv("GENS_HEADLESS_PCLOG") != 0 && frame == (unsigned int)strtoul(getenv("GENS_HEADLESS_PCLOG"), 0, 0))
            pclog = 200000;
        Lag_Frame = 1;
        Do_Genesis_Frame();
        if (Lag_Frame)
            ++lag_frames;
        if (getenv("GENS_HEADLESS_DEBUG") != 0)
            fprintf(stderr, "frame %u pc=%06x sr=%04x set1=%02x set2=%02x set4=%02x status=%04x z80=%d\n",
                    frame, main68k_context.pc, main68k_context.sr, VDP_Reg.Set1, VDP_Reg.Set2,
                    VDP_Reg.Set4, VDP_Status & 0xffff, Z80_State);
        trace_frame(out, frame);
        {
            /* G_dsound.cpp's output step at Gens' defaults: master volume 128, stereo, MMX.
             * It also clears the mixer planes for the next frame. */
            static GensPcmOutput pcm;
            GensPcmOptions options = {128, 1, 0, 1, 0};
            if (!gens_pcm_render(&pcm, Seg_L, Seg_R, (size_t)Seg_Length, &options))
                return 6;
            gens_trace_pcm(out, frame, pcm.samples, pcm.frames);
        }
    }
    if (out != stdout)
        fclose(out);
    fprintf(stderr, "lag frames: %u of %u\n", lag_frames, frames);
    if (getenv("GENS_HEADLESS_DEBUG") != 0)
    {
        unsigned int i;
        fprintf(stderr, "cram:");
        for (i = 0; i < 64; ++i) fprintf(stderr, " %04x", CRam[i]);
        fprintf(stderr, "\nmd_palette:");
        for (i = 0; i < 16; ++i) fprintf(stderr, " %04x", MD_Palette[i]);
        fprintf(stderr, "\npalette[0xeee]=%04x screen:", Palette[0xeee]);
        for (i = 0; i < 16; ++i) fprintf(stderr, " %04x", Screen_16X[100u * 336u + 8u + 100u + i]);
        fprintf(stderr, "\nh_pix_begin=%d cram_flag=%d\n", H_Pix_Begin, CRam_Flag);
        fprintf(stderr, "regs:");
        for (i = 0; i < 24; ++i) fprintf(stderr, " %02x", ((unsigned int *)&VDP_Reg)[i] & 0xffu);
        fprintf(stderr, "\nscra=%p vram=%p scrb=%p win=%p\nplane a:", (void *)ScrA_Addr_Dbg(), (void *)VRam,
                (void *)0, (void *)0);
        for (i = 0; i < 32; ++i) fprintf(stderr, " %02x", VRam[0xc000u + i]);
        fprintf(stderr, "\ntile 0x11:");
        for (i = 0; i < 32; ++i) fprintf(stderr, " %02x", VRam[0x220u + i]);
        fprintf(stderr, "\nplane b:");
        for (i = 0; i < 32; ++i) fprintf(stderr, " %02x", VRam[0xe000u + i]);
        fprintf(stderr, "\n");
    }
    if (argc > 5)
        gens_trace_ppm(argv[5], Screen_16X, (VDP_Reg.Set4 & 1u) ? 320u : 256u,
                       (unsigned int)VDP_Num_Vis_Lines, 0u);
    (void)Lag_Frame_Unused;
    return 0;
}
