/* Derived from Gens-Rerecording Gens.cpp and G_dsound.cpp at the pinned
 * revision. The upstream component terms apply to the derived portions.
 */
#include "gens_frame.h"
#include "gens_frame_backend.h"
#include "gens_vdp_dma.h"
#include "gens_vdp_irq.h"

#include <limits.h>
#include <string.h>

static int samples_for_rate(int rate, int pal)
{
    switch (rate)
    {
        case 11025: return pal ? 220 : 184;
        case 22050: return pal ? 441 : 368;
        case 44100: return pal ? 882 : 735;
        default: return 0;
    }
}

int gens_frame_audio_init(void)
{
    int lines, samples, i;
    if (CPU_Mode != 0 && CPU_Mode != 1)
        return 0;
    lines = CPU_Mode ? 312 : 262;
    samples = samples_for_rate(Sound_Rate, CPU_Mode);
    if (samples == 0)
        return 0;

    Seg_Length = samples;
    for (i = 0; i < lines; ++i)
    {
        Sound_Extrapol[i][0] = (unsigned int)(samples * i / lines);
        Sound_Extrapol[i][1] = (unsigned int)(samples * (i + 1) / lines) -
                               Sound_Extrapol[i][0];
    }
    for (i = 0; i < samples; ++i)
        Sound_Interpol[i] = (unsigned int)(lines * i / samples);
    memset(Seg_L, 0, (size_t)samples * sizeof(Seg_L[0]));
    memset(Seg_R, 0, (size_t)samples * sizeof(Seg_R[0]));
    return 1;
}

static int valid_frame_state(void)
{
    int i, lines, samples;
    if (Inside_Frame != 0 || (CPU_Mode != 0 && CPU_Mode != 1))
        return 0;
    lines = CPU_Mode ? 312 : 262;
    samples = samples_for_rate(Sound_Rate, CPU_Mode);
    if (samples == 0 || VDP_Num_Lines != lines || Seg_Length != samples ||
        CPL_M68K <= 0 || CPL_Z80 <= 0 ||
        CPL_M68K > INT_MAX / lines || CPL_Z80 > INT_MAX / lines ||
        VDP_Reg.H_Int > 255u)
        return 0;
    for (i = 0; i < lines; ++i)
    {
        unsigned int offset = (unsigned int)(samples * i / lines);
        unsigned int count = (unsigned int)(samples * (i + 1) / lines) - offset;
        if (Sound_Extrapol[i][0] != offset || Sound_Extrapol[i][1] != count)
            return 0;
    }
    return 1;
}

static void sound_line(void)
{
    int *buffer[2];
    unsigned int offset = Sound_Extrapol[VDP_Current_Line][0];
    unsigned int count = Sound_Extrapol[VDP_Current_Line][1];
    buffer[0] = Seg_L + offset;
    buffer[1] = Seg_R + offset;
    YM2612_DacAndTimers_Update(buffer, (int)count);
    YM_Len += (int)count;
    PSG_Len += (int)count;
}

static void z80_to(int cycles)
{
    if (Z80_State == 3)
        gens_frame_backend_z80_exec(cycles);
    else
        gens_frame_backend_z80_set_odo((unsigned int)cycles);
}

static int dma_line(GensVdpDmaTiming *timing)
{
    uint32_t cycles;
    if (DMAT_Length == 0)
        return 1;
    if (timing == 0)
        cycles = Update_DMA();
    else if (!gens_vdp_dma_update(timing, &cycles))
        return 0;
    gens_frame_backend_m68k_add_cycles(cycles);
    return 1;
}

static int update_irq(GensVdpIrq *irq, GensM68kBusState *state)
{
    if (irq != 0)
        return gens_vdp_irq_update(irq, state);
    Update_IRQ_Line();
    return 1;
}

static int run_frame(GensVdpDmaTiming *timing, GensVdpIrq *irq, GensM68kBusState *state)
{
    int hint_counter;
    if (!valid_frame_state())
        return 0;
    Inside_Frame = 1;
    VDP_Num_Vis_Lines = (CPU_Mode && (VDP_Reg.Set2 & 0x8)) ? 240 : 224;
    YM_Buf[0] = PSG_Buf[0] = Seg_L;
    YM_Buf[1] = PSG_Buf[1] = Seg_R;
    YM_Len = PSG_Len = 0;
    Cycles_M68K = Cycles_Z80 = 0;
    Last_BUS_REQ_Cnt = -1000;
    gens_frame_backend_m68k_trip();
    gens_frame_backend_z80_clear();
    Patch_Codes();
    VRam_Flag = 1;
    VDP_Status &= 0xfff7;
    if (VDP_Reg.Set4 & 0x2)
        VDP_Status ^= 0x0010;
    hint_counter = (int)VDP_Reg.H_Int;

    for (VDP_Current_Line = 0; VDP_Current_Line < VDP_Num_Vis_Lines; ++VDP_Current_Line)
    {
        sound_line();
        Fix_Controllers();
        Cycles_M68K += CPL_M68K;
        Cycles_Z80 += CPL_Z80;
        if (!dma_line(timing))
        {
            Inside_Frame = 0;
            return 0;
        }
        VDP_Status |= 0x0004;
        gens_frame_backend_m68k_exec(Cycles_M68K - 404);
        VDP_Status &= 0xfffb;
        if (--hint_counter < 0)
        {
            hint_counter = (int)VDP_Reg.H_Int;
            VDP_Int |= 0x4;
            if (!update_irq(irq, state))
            {
                Inside_Frame = 0;
                return 0;
            }
        }
        gens_frame_backend_render_line();
        gens_frame_backend_m68k_exec(Cycles_M68K);
        z80_to(Cycles_Z80);
    }

    gens_frame_backend_render_screen();
    sound_line();
    Fix_Controllers();
    Cycles_M68K += CPL_M68K;
    Cycles_Z80 += CPL_Z80;
    if (!dma_line(timing))
    {
        Inside_Frame = 0;
        return 0;
    }
    if (--hint_counter < 0)
    {
        VDP_Int |= 0x4;
        if (!update_irq(irq, state))
        {
            Inside_Frame = 0;
            return 0;
        }
    }
    VDP_Status |= 0x000c;
    gens_frame_backend_m68k_exec(Cycles_M68K - 360);
    z80_to(Cycles_Z80 - 168);
    VDP_Status &= 0xfffb;
    VDP_Status |= 0x0080;
    VDP_Int |= 0x8;
    if (!update_irq(irq, state))
    {
        Inside_Frame = 0;
        return 0;
    }
    gens_frame_backend_z80_interrupt(0xff);
    gens_frame_backend_m68k_exec(Cycles_M68K);
    z80_to(Cycles_Z80);

    for (++VDP_Current_Line; VDP_Current_Line < VDP_Num_Lines; ++VDP_Current_Line)
    {
        sound_line();
        Fix_Controllers();
        Cycles_M68K += CPL_M68K;
        Cycles_Z80 += CPL_Z80;
        if (!dma_line(timing))
        {
            Inside_Frame = 0;
            return 0;
        }
        VDP_Status |= 0x0004;
        gens_frame_backend_m68k_exec(Cycles_M68K - 404);
        VDP_Status &= 0xfffb;
        gens_frame_backend_m68k_exec(Cycles_M68K);
        z80_to(Cycles_Z80);
    }
    gens_frame_backend_psg_finish();
    gens_frame_backend_ym_finish();
    Inside_Frame = 0;
    return 1;
}

int gens_frame_run(void)
{
    return run_frame(0, 0, 0);
}

static int valid_dma_binding(const GensVdpDmaTiming *timing)
{
    return gens_vdp_dma_timing_valid(timing) &&
        timing->set2 == &VDP_Reg.Set2 && timing->set4 == &VDP_Reg.Set4 &&
        timing->current_line == &VDP_Current_Line &&
        timing->visible_lines == &VDP_Num_Vis_Lines &&
        timing->cycles_per_line == &CPL_M68K && timing->length == &DMAT_Length &&
        timing->type == &DMAT_Type && timing->status == &VDP_Status;
}

int gens_frame_run_dma(GensVdpDmaTiming *timing)
{
    if (!valid_dma_binding(timing))
        return 0;
    return run_frame(timing, 0, 0);
}

int gens_frame_run_vdp(GensVdpDmaTiming *timing, GensVdpIrq *irq, GensM68kBusState *state)
{
    if (!valid_dma_binding(timing) || !gens_vdp_irq_valid(irq) || state == 0 ||
        irq->set1 != &VDP_Reg.Set1 || irq->set2 != &VDP_Reg.Set2 ||
        irq->pending != &VDP_Int || irq->interrupt == 0)
        return 0;
    return run_frame(timing, irq, state);
}
