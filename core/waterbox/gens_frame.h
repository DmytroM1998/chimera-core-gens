#ifndef CHIMERA_GENS_FRAME_H
#define CHIMERA_GENS_FRAME_H

#ifdef __cplusplus
extern "C" {
#endif

struct GensVdpDmaTiming;
struct GensVdpIrq;
struct GensM68kBusState;

/* Initialize the original scanline/sample tables after setting CPU_Mode and Sound_Rate. */
int gens_frame_audio_init(void);

/* Run the sound-enabled, fully rendered Genesis frame. */
int gens_frame_run(void);

/* Use the shared VDP DMA countdown instead of the legacy Update_DMA symbol. */
int gens_frame_run_dma(struct GensVdpDmaTiming *);

/* Use shared DMA and IRQ adapters with the machine's CPU I/O state. */
int gens_frame_run_vdp(struct GensVdpDmaTiming *, struct GensVdpIrq *,
                        struct GensM68kBusState *);

#ifdef __cplusplus
}
#endif

#endif
