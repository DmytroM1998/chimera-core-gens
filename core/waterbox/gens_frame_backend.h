#ifndef CHIMERA_GENS_FRAME_BACKEND_H
#define CHIMERA_GENS_FRAME_BACKEND_H

#include "Mem_M68k.h"
#include "vdp_io.h"
#include "io.h"
#include "ym2612.h"
#include "psg.h"
#include "ggenie.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int Inside_Frame;
extern int Sound_Rate;
extern int Seg_Length;
extern int Seg_L[882], Seg_R[882];
extern unsigned int Sound_Interpol[882];
extern unsigned int Sound_Extrapol[312][2];

void gens_frame_backend_m68k_trip(void);
void gens_frame_backend_m68k_add_cycles(unsigned int cycles);
void gens_frame_backend_m68k_exec(int cycles);
void gens_frame_backend_z80_clear(void);
void gens_frame_backend_z80_exec(int cycles);
void gens_frame_backend_z80_set_odo(unsigned int cycles);
void gens_frame_backend_z80_interrupt(unsigned int vector);
void gens_frame_backend_render_line(void);
void gens_frame_backend_render_screen(void);
void gens_frame_backend_psg_finish(void);
void gens_frame_backend_ym_finish(void);

#ifdef __cplusplus
}
#endif

#endif
