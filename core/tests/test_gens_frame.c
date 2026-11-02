#include <assert.h>
#include <limits.h>
#include <string.h>

#include "gens_frame.h"
#include "gens_frame_backend.h"
#include "gens_vdp_dma.h"
#include "gens_vdp_irq.h"

int Inside_Frame, Sound_Rate, Seg_Length;
int Seg_L[882], Seg_R[882];
unsigned int Sound_Interpol[882], Sound_Extrapol[312][2];
int CPU_Mode, CPL_M68K, CPL_Z80, Cycles_M68K, Cycles_Z80;
int Last_BUS_REQ_Cnt, Z80_State;
struct Reg_VDP_Type VDP_Reg;
int VDP_Current_Line, VDP_Num_Lines, VDP_Num_Vis_Lines;
int VRam_Flag, VDP_Status, VDP_Int, DMAT_Length, DMAT_Type;
int *YM_Buf[2], *PSG_Buf[2], YM_Len, PSG_Len;
unsigned char Lag_Frame;

enum EventType {
    TRIP, ZCLEAR, PATCH, AUDIO, FIX, DMA, ADD, M68K, ZEXEC, ZODO,
    IRQ, RENDER_LINE, RENDER_SCREEN, ZINT, PSG_FINISH, YM_FINISH, CPU_IRQ
};

struct Event {
    enum EventType type;
    int line;
    int value;
    int status;
};

static struct Event events[4096];
static int event_count;
static int fake_screen;
static int dma_inject_line = -1, dma_inject_length, dma_fail_line = -1;
static struct GensVdpDmaTiming dma_timing;
static struct GensVdpIrq frame_irq;
static struct GensM68kBusState frame_state;
static uint8_t cpu_interrupts;
static unsigned int fail_irq_level;

static void record(enum EventType type, int value)
{
    assert(Inside_Frame == 1);
    assert(event_count < (int)(sizeof(events) / sizeof(events[0])));
    events[event_count].type = type;
    events[event_count].line = VDP_Current_Line;
    events[event_count].value = value;
    events[event_count].status = VDP_Status;
    ++event_count;
}

void gens_frame_backend_m68k_trip(void) { record(TRIP, 0); }
void gens_frame_backend_m68k_add_cycles(unsigned int cycles) { record(ADD, (int)cycles); }
void gens_frame_backend_m68k_exec(int cycles)
{
    record(M68K, cycles);
    if (VDP_Current_Line == dma_inject_line && cycles == Cycles_M68K)
    {
        DMAT_Length = dma_inject_length;
        VDP_Status |= 2;
        dma_inject_line = -1;
    }
    if (VDP_Current_Line == dma_fail_line && cycles == Cycles_M68K)
    {
        dma_timing.length = 0;
        dma_fail_line = -1;
    }
}
void gens_frame_backend_z80_clear(void) { record(ZCLEAR, 0); }
void gens_frame_backend_z80_exec(int cycles) { record(ZEXEC, cycles); }
void gens_frame_backend_z80_set_odo(unsigned int cycles) { record(ZODO, (int)cycles); }
void gens_frame_backend_z80_interrupt(unsigned int vector) { record(ZINT, (int)vector); }
void gens_frame_backend_render_line(void) { record(RENDER_LINE, 0); }
void gens_frame_backend_render_screen(void) { fake_screen = 0; record(RENDER_SCREEN, 0); }
void gens_frame_backend_psg_finish(void) { record(PSG_FINISH, PSG_Len); PSG_Len = 0; }
void gens_frame_backend_ym_finish(void) { record(YM_FINISH, YM_Len); YM_Len = 0; }
void Patch_Codes(void) { record(PATCH, 0); }
void Fix_Controllers(void) { record(FIX, 0); }
unsigned int Update_DMA(void) { record(DMA, 0); return 7; }
void Update_IRQ_Line(void) { record(IRQ, VDP_Int); }
static int request_irq(void *opaque, GensM68kBusState *state, unsigned int level, int vector)
{
    (void)opaque;
    assert(state == &frame_state && vector == -1);
    record(CPU_IRQ, (int)level);
    return level != fail_irq_level;
}

void YM2612_DacAndTimers_Update(int **buffer, int length)
{
    unsigned int offset = Sound_Extrapol[VDP_Current_Line][0];
    assert(buffer[0] == Seg_L + offset && buffer[1] == Seg_R + offset);
    record(AUDIO, length);
}

static void expect(int *index, enum EventType type, int line, int value, int hblank)
{
    const struct Event *event;
    assert(*index < event_count);
    event = &events[(*index)++];
    assert(event->type == type);
    assert(event->line == line);
    assert(event->value == value);
    if (hblank >= 0)
        assert(((event->status & 4) != 0) == hblank);
}

static void check_audio_tables(void)
{
    static const int rates[3] = {11025, 22050, 44100};
    static const int ntsc_samples[3] = {184, 368, 735};
    static const int pal_samples[3] = {220, 441, 882};
    int pal, rate, i;
    for (pal = 0; pal < 2; ++pal)
    {
        for (rate = 0; rate < 3; ++rate)
        {
            int lines = pal ? 312 : 262;
            int samples = pal ? pal_samples[rate] : ntsc_samples[rate];
            for (i = 0; i < 882; ++i)
                Seg_L[i] = Seg_R[i] = 123;
            for (i = 0; i < 312; ++i)
                Sound_Extrapol[i][0] = Sound_Extrapol[i][1] = 9999;
            CPU_Mode = pal;
            Sound_Rate = rates[rate];
            assert(gens_frame_audio_init());
            assert(Seg_Length == samples);
            for (i = 0; i < lines; ++i)
            {
                unsigned int start = (unsigned int)(samples * i / lines);
                assert(Sound_Extrapol[i][0] == start);
                assert(Sound_Extrapol[i][1] ==
                       (unsigned int)(samples * (i + 1) / lines) - start);
            }
            for (i = 0; i < samples; ++i)
            {
                assert(Sound_Interpol[i] == (unsigned int)(lines * i / samples));
                assert(Seg_L[i] == 0 && Seg_R[i] == 0);
            }
            if (samples < 882)
                assert(Seg_L[samples] == 123 && Seg_R[samples] == 123);
            if (lines < 312)
                assert(Sound_Extrapol[lines][0] == 9999);
        }
    }
}

static void check_frame(int pal, int wide_pal, int running, int dma, int hint)
{
    int index = 0, line, hint_counter = hint, irq = 0;
    int visible = pal && wide_pal ? 240 : 224;
    int lines = pal ? 312 : 262;
    int samples = pal ? 882 : 735;
    enum EventType z80 = running ? ZEXEC : ZODO;

    CPU_Mode = pal;
    Sound_Rate = 44100;
    assert(gens_frame_audio_init());
    VDP_Num_Lines = lines;
    VDP_Reg.Set2 = wide_pal ? 8u : 0u;
    VDP_Reg.Set4 = 2;
    VDP_Reg.H_Int = (unsigned int)hint;
    CPL_M68K = 488;
    CPL_Z80 = 228;
    Z80_State = running ? 3 : 0;
    DMAT_Length = dma;
    VDP_Status = 0x0009;
    VDP_Int = 0;
    VDP_Current_Line = -1;
    Inside_Frame = 0;
    Lag_Frame = 1;
    event_count = 0;
    fake_screen = 1;
    assert(gens_frame_run());
    assert(Inside_Frame == 0 && VDP_Current_Line == lines);
    assert(VDP_Num_Vis_Lines == visible && fake_screen == 0);
    assert(Cycles_M68K == lines * CPL_M68K && Cycles_Z80 == lines * CPL_Z80);
    assert(Last_BUS_REQ_Cnt == -1000 && VRam_Flag == 1 && Lag_Frame == 1);
    assert(YM_Buf[0] == Seg_L && YM_Buf[1] == Seg_R);
    assert(PSG_Buf[0] == Seg_L && PSG_Buf[1] == Seg_R);
    assert(YM_Len == 0 && PSG_Len == 0);
    expect(&index, TRIP, -1, 0, -1);
    expect(&index, ZCLEAR, -1, 0, -1);
    expect(&index, PATCH, -1, 0, -1);

    for (line = 0; line < lines; ++line)
    {
        int cycles68 = (line + 1) * CPL_M68K;
        int cyclesz = (line + 1) * CPL_Z80;
        if (line == visible)
            expect(&index, RENDER_SCREEN, line, 0, 0);
        expect(&index, AUDIO, line, (int)Sound_Extrapol[line][1], -1);
        expect(&index, FIX, line, 0, -1);
        if (dma)
        {
            expect(&index, DMA, line, 0, -1);
            expect(&index, ADD, line, 7, -1);
        }
        if (line == visible)
        {
            if (--hint_counter < 0)
            {
                irq |= 4;
                expect(&index, IRQ, line, irq, 0);
            }
            expect(&index, M68K, line, cycles68 - 360, 1);
            expect(&index, z80, line, cyclesz - 168, 1);
            irq |= 8;
            expect(&index, IRQ, line, irq, 0);
            expect(&index, ZINT, line, 0xff, 0);
            expect(&index, M68K, line, cycles68, 0);
            expect(&index, z80, line, cyclesz, 0);
        }
        else
        {
            expect(&index, M68K, line, cycles68 - 404, 1);
            if (line < visible)
            {
                if (--hint_counter < 0)
                {
                    hint_counter = hint;
                    irq |= 4;
                    expect(&index, IRQ, line, irq, 0);
                }
                expect(&index, RENDER_LINE, line, 0, 0);
            }
            expect(&index, M68K, line, cycles68, 0);
            expect(&index, z80, line, cyclesz, 0);
        }
    }
    expect(&index, PSG_FINISH, lines, samples, 0);
    expect(&index, YM_FINISH, lines, samples, 0);
    assert(index == event_count);
    assert((VDP_Status & 0x8c) == 0x88);
    assert((VDP_Status & 0x10) != 0);
}

static void check_invalid_state(void)
{
    CPU_Mode = 0;
    Sound_Rate = 44100;
    assert(gens_frame_audio_init());
    VDP_Num_Lines = 262;
    CPL_M68K = 488;
    CPL_Z80 = 228;
    VDP_Reg.H_Int = 3;
    Inside_Frame = 0;
    event_count = 0;
    Sound_Extrapol[1][0]++;
    assert(!gens_frame_run() && event_count == 0 && Inside_Frame == 0);
    Sound_Extrapol[1][0]--;
    VDP_Num_Lines = 312;
    assert(!gens_frame_run() && event_count == 0);
    VDP_Num_Lines = 262;
    CPL_M68K = INT_MAX;
    assert(!gens_frame_run() && event_count == 0);
    CPL_M68K = 488;
    VDP_Reg.H_Int = 256;
    assert(!gens_frame_run() && event_count == 0);
    VDP_Reg.H_Int = 3;
    CPL_Z80 = INT_MAX;
    assert(!gens_frame_run() && event_count == 0);
    CPL_Z80 = 228;
    Inside_Frame = 1;
    assert(!gens_frame_run() && event_count == 0);
    Inside_Frame = 0;
    Seg_L[0] = 123;
    Sound_Rate = 48000;
    assert(!gens_frame_audio_init());
    assert(Seg_Length == 735 && Seg_L[0] == 123);
    CPU_Mode = 2;
    Sound_Rate = 44100;
    assert(!gens_frame_audio_init() && !gens_frame_run() && event_count == 0);
}

static void configure_timing(void)
{
    dma_timing.set2 = &VDP_Reg.Set2;
    dma_timing.set4 = &VDP_Reg.Set4;
    dma_timing.current_line = &VDP_Current_Line;
    dma_timing.visible_lines = &VDP_Num_Vis_Lines;
    dma_timing.cycles_per_line = &CPL_M68K;
    dma_timing.length = &DMAT_Length;
    dma_timing.type = &DMAT_Type;
    dma_timing.status = &VDP_Status;
}

static void setup_dma_frame(int pal)
{
    CPU_Mode = pal;
    Sound_Rate = 44100;
    assert(gens_frame_audio_init());
    VDP_Num_Lines = pal ? 312 : 262;
    VDP_Reg.Set2 = pal ? 0x48 : 0x40;
    VDP_Reg.Set4 = 1;
    VDP_Reg.H_Int = 255;
    CPL_M68K = 488;
    CPL_Z80 = 228;
    Z80_State = 0;
    DMAT_Length = 0;
    DMAT_Type = 0;
    VDP_Status = 3;
    VDP_Int = 0;
    VDP_Current_Line = -1;
    Inside_Frame = 0;
    event_count = 0;
    dma_inject_line = dma_fail_line = -1;
    configure_timing();
}

static void check_dma_frame(int pal, int kind, int phase)
{
    static const int capacities[4] = {9, 18, 17, 9};
    int i, adds = 0, audio = 0;
    int visible = pal ? 240 : 224;
    int start = phase == 0 ? 0 : (phase == 1 ? visible : visible + 2);
    int capacity = phase == 0 ? capacities[kind] : 102;
    int full_cost = kind & 2 ? 0 : 488;
    int partial_cost = kind & 2 ? 0 : (int)((488u << 16) / (unsigned int)capacity >> 16);
    setup_dma_frame(pal);
    DMAT_Type = kind;
    if (phase == 0)
        DMAT_Length = capacity + 1;
    else
    {
        dma_inject_line = start - 1;
        dma_inject_length = capacity + 1;
    }
    assert(gens_frame_run_dma(&dma_timing));
    assert(Inside_Frame == 0 && DMAT_Length == 0 && VDP_Current_Line == VDP_Num_Lines);
    for (i = 0; i < event_count; ++i)
    {
        assert(events[i].type != DMA);
        if (events[i].type == ADD)
        {
            assert(events[i].line == start + adds);
            assert(events[i].value == (adds == 0 ? full_cost : partial_cost));
            ++adds;
        }
        if (events[i].type == AUDIO)
            audio += events[i].value;
    }
    assert(adds == 2 && audio == (pal ? 882 : 735));
}

static void check_shared_dma(void)
{
    int pal, kind;
    for (pal = 0; pal < 2; ++pal)
    {
        for (kind = 0; kind < 4; ++kind)
            check_dma_frame(pal, kind, 0);
        check_dma_frame(pal, 0, 1);
        check_dma_frame(pal, 0, 2);
    }
    setup_dma_frame(0);
    assert(!gens_frame_run_dma(0) && event_count == 0 && Inside_Frame == 0);
    dma_timing.status = &VDP_Int;
    assert(!gens_frame_run_dma(&dma_timing) && event_count == 0);
    configure_timing();
    dma_timing.length = &VDP_Int;
    assert(!gens_frame_run_dma(&dma_timing) && event_count == 0);
    configure_timing();
    DMAT_Length = 20;
    dma_fail_line = 0;
    assert(!gens_frame_run_dma(&dma_timing));
    assert(Inside_Frame == 0 && VDP_Current_Line == 1 && DMAT_Length == 11);
    assert(events[event_count - 1].type == FIX);
    configure_timing();
}

static void configure_irq(void)
{
    frame_irq.set1 = &VDP_Reg.Set1;
    frame_irq.set2 = &VDP_Reg.Set2;
    frame_irq.pending = &VDP_Int;
    frame_irq.cpu_interrupts = &cpu_interrupts;
    frame_irq.opaque = 0;
    frame_irq.interrupt = request_irq;
    cpu_interrupts = 0xab;
    fail_irq_level = 0;
}

static void check_vdp_frame(int pal, int horizontal, int vertical)
{
    int i, h_requests = 0, v_requests = 0, adds = 0;
    int visible = pal ? 240 : 224;
    setup_dma_frame(pal);
    configure_irq();
    VDP_Reg.Set1 = horizontal ? 0x10 : 0;
    VDP_Reg.Set2 |= vertical ? 0x20 : 0;
    VDP_Reg.H_Int = 0;
    DMAT_Length = 10;
    assert(gens_frame_run_vdp(&dma_timing, &frame_irq, &frame_state));
    assert(Inside_Frame == 0 && VDP_Current_Line == VDP_Num_Lines);
    for (i = 0; i < event_count; ++i)
    {
        assert(events[i].type != IRQ && events[i].type != DMA);
        if (events[i].type == ADD)
        {
            assert(events[i].line == adds && events[i].value == (adds == 0 ? 488 : 54));
            ++adds;
        }
        if (events[i].type != CPU_IRQ)
            continue;
        assert(i > 0 && i + 1 < event_count);
        if (events[i].value == 6)
        {
            ++v_requests;
            assert(vertical && events[i].line == visible);
            assert((events[i].status & 0x80) != 0);
            assert(events[i - 1].type == ZODO && events[i + 1].type == ZINT);
        }
        else
        {
            ++h_requests;
            assert(horizontal && events[i].value == 4);
            if (events[i].line < visible)
            {
                assert(events[i - 1].type == M68K && events[i + 1].type == RENDER_LINE);
                assert(events[i - 1].value == (events[i].line + 1) * CPL_M68K - 404);
                assert((events[i].status & 4) == 0);
            }
            else if ((events[i].status & 0x80) == 0)
            {
                assert(events[i].line == visible);
                assert(events[i - 1].type == FIX && events[i + 1].type == M68K);
                assert(events[i + 1].value == (visible + 1) * CPL_M68K - 360);
            }
            else
            {
                assert(!vertical && events[i].line == visible);
                assert(events[i - 1].type == ZODO && events[i + 1].type == ZINT);
            }
        }
    }
    assert(v_requests == vertical);
    assert(adds == 2 && DMAT_Length == 0);
    assert(h_requests == (horizontal ? visible + 1 + !vertical : 0));
    assert(cpu_interrupts == (horizontal ? 0xab : 0xa0));
    assert(VDP_Int == 12);
}

static void check_shared_vdp(void)
{
    int pal, horizontal, vertical;
    for (pal = 0; pal < 2; ++pal)
        for (horizontal = 0; horizontal < 2; ++horizontal)
            for (vertical = 0; vertical < 2; ++vertical)
                check_vdp_frame(pal, horizontal, vertical);
    setup_dma_frame(0);
    configure_irq();
    assert(!gens_frame_run_vdp(&dma_timing, &frame_irq, 0) && event_count == 0);
    dma_timing.current_line = &VDP_Int;
    assert(!gens_frame_run_vdp(&dma_timing, &frame_irq, &frame_state) && event_count == 0);
    configure_timing();
    frame_irq.pending = &VDP_Status;
    assert(!gens_frame_run_vdp(&dma_timing, &frame_irq, &frame_state) && event_count == 0);
    configure_irq();
    frame_irq.interrupt = 0;
    assert(!gens_frame_run_vdp(&dma_timing, &frame_irq, &frame_state) && event_count == 0);

    configure_irq();
    VDP_Reg.Set1 = 0x10;
    VDP_Reg.H_Int = 0;
    fail_irq_level = 4;
    assert(!gens_frame_run_vdp(&dma_timing, &frame_irq, &frame_state));
    assert(Inside_Frame == 0 && VDP_Current_Line == 0 && VDP_Int == 4);
    assert(events[event_count - 1].type == CPU_IRQ && events[event_count - 1].value == 4);

    setup_dma_frame(0);
    configure_irq();
    VDP_Reg.Set1 = 0;
    VDP_Reg.Set2 |= 0x20;
    fail_irq_level = 6;
    assert(!gens_frame_run_vdp(&dma_timing, &frame_irq, &frame_state));
    assert(Inside_Frame == 0 && VDP_Current_Line == 224 && VDP_Int == 8);
    assert(events[event_count - 1].type == CPU_IRQ && events[event_count - 1].value == 6);
    fail_irq_level = 0;
}

int main(void)
{
    check_audio_tables();
    check_frame(0, 0, 1, 1, 3);
    check_frame(1, 1, 1, 1, 0);
    check_frame(1, 0, 0, 0, 255);
    check_invalid_state();
    check_shared_dma();
    check_shared_vdp();
    return 0;
}
