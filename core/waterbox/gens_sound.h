#ifndef CHIMERA_GENS_SOUND_H
#define CHIMERA_GENS_SOUND_H

#include "gens_genesis_bus.h"
#include "gens_pcm.h"

#ifdef __cplusplus
extern "C" {
#endif

int gens_sound_init(int pal, int rate, int ym_interpolation, int psg_improvement);
int gens_sound_z80_read(void *, GensM68kBusState *, enum GensZ80Device,
                        uint32_t, uint16_t *);
int gens_sound_z80_write(void *, GensM68kBusState *, enum GensZ80Device,
                         uint32_t, uint8_t);
int gens_sound_genesis_write(void *, GensM68kBusState *, enum GensGenesisDevice,
                             uint32_t, unsigned int, uint16_t);
int gens_sound_reset_ym2612(void *, GensM68kBusState *);
int gens_sound_output(GensPcmOutput *, const GensPcmOptions *);
void gens_frame_backend_psg_finish(void);
void gens_frame_backend_ym_finish(void);

#ifdef __cplusplus
}
#endif

#endif
