#ifndef CHIMERA_GENS_Z80_CONTROL_H
#define CHIMERA_GENS_Z80_CONTROL_H

#include "gens_m68k_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensZ80Control {
    int *z80_state, *last_request_cycles, *last_request_state, *fake_fetch;
    int *cycles_m68k, *cycles_z80;
    const int *cycle_table;
    size_t cycle_table_size;
    void *opaque;
    int (*read_odometer)(void *, GensM68kBusState *, uint32_t *);
    int (*set_odometer)(void *, GensM68kBusState *, uint32_t);
    int (*execute)(void *, GensM68kBusState *, uint32_t);
    int (*reset_z80)(void *, GensM68kBusState *);
    int (*reset_ym2612)(void *, GensM68kBusState *);
} GensZ80Control;

int gens_z80_control_valid(const GensZ80Control *control);
int gens_z80_request_read(GensZ80Control *control, GensM68kBusState *state,
                          unsigned int size, uint16_t *value);
int gens_z80_request_write(GensZ80Control *control, GensM68kBusState *state,
                           unsigned int size, uint16_t value);
int gens_z80_reset_write(GensZ80Control *control, GensM68kBusState *state,
                         unsigned int size, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
