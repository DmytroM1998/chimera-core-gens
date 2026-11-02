/* Trace of the ported machine in the format of gens_headless_trace.h. */
#ifndef CHIMERA_GENS_HEADLESS_MACHINE_TRACE_H
#define CHIMERA_GENS_HEADLESS_MACHINE_TRACE_H

#include "gens_headless_trace.h"
#include "gens_machine.h"
#include "gens_m68k_cpu.h"
#include "gens_z80_cpu.h"

static void trace_frame(FILE *out, unsigned int frame)
{
    const GensM68k *cpu = gens_machine_m68k();
    const GensZ80 *z = gens_machine_z80();
    GensMachineView view;
    uint32_t m68k[20], z80[16], vdp[16];
    unsigned int i;

    gens_machine_view(&view);
    for (i = 0; i < 8; ++i) m68k[i] = cpu->dreg[i];
    for (i = 0; i < 8; ++i) m68k[8 + i] = cpu->areg[i];
    m68k[16] = cpu->pc;
    m68k[17] = cpu->sr;
    m68k[18] = cpu->asp;
    m68k[19] = cpu->interrupts[0];
    z80[0] = (uint32_t)z->a | (uint32_t)z->f << 8;
    z80[1] = (uint32_t)z->c | (uint32_t)z->b << 8;
    z80[2] = (uint32_t)z->e | (uint32_t)z->d << 8;
    z80[3] = (uint32_t)z->l | (uint32_t)z->h << 8;
    z80[4] = (uint32_t)z->ixl | (uint32_t)z->ixh << 8;
    z80[5] = (uint32_t)z->iyl | (uint32_t)z->iyh << 8;
    z80[6] = z->sp;
    z80[7] = gens_z80_get_pc(z);
    z80[8] = (uint32_t)z->a2 | (uint32_t)z->f2 << 8;
    z80[9] = (uint32_t)z->c2 | (uint32_t)z->b2 << 8;
    z80[10] = (uint32_t)z->e2 | (uint32_t)z->d2 << 8;
    z80[11] = (uint32_t)z->l2 | (uint32_t)z->h2 << 8;
    z80[12] = (uint32_t)z->iff1 | (uint32_t)z->iff2 << 8;
    z80[13] = z->i;
    z80[14] = z->status;
    memcpy(&z80[15], &z->ram[0x2000], 4);
    gens_machine_vdp_state(vdp);
    gens_trace_words(out, frame, "m68k", m68k, 20);
    gens_trace_words(out, frame, "z80", z80, 16);
    gens_trace_words(out, frame, "vdp", vdp, 16);
    gens_trace_block(out, frame, "vdp_reg", view.vdp_regs, 26u * 4u);
    gens_trace_block(out, frame, "ram68k", view.ram68k, 65536);
    gens_trace_block(out, frame, "ramz80", view.ramz80, 8192);
    gens_trace_block(out, frame, "vram", view.vram, 65536);
    gens_trace_block(out, frame, "cram", view.cram, 256);
    gens_trace_block(out, frame, "vsram", view.vsram, 256);
    gens_trace_block(out, frame, "sram", view.sram, 65536);
    gens_trace_block(out, frame, "screen", view.screen16, 336u * 240u * 2u);
    if (!gens_trace_no_audio)
    {
        gens_trace_block(out, frame, "audio_l", view.audio_left, (size_t)view.audio_samples * 4u);
        gens_trace_block(out, frame, "audio_r", view.audio_right, (size_t)view.audio_samples * 4u);
    }
    gens_trace_audio_dump(frame, view.audio_left, view.audio_right, view.audio_samples);
    gens_trace_ym_flush(out, frame);
}

#endif
