#ifndef CHIMERA_GENS_Z80_MEMORY_H
#define CHIMERA_GENS_Z80_MEMORY_H

#include "gens_m68k_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

enum GensZ80Device {
    GENS_Z80_YM2612,
    GENS_Z80_VDP_STATUS,
    GENS_Z80_V_COUNTER,
    GENS_Z80_H_COUNTER,
    GENS_Z80_VDP_DATA,
    GENS_Z80_PSG
};

typedef struct GensZ80Memory {
    uint8_t *ram;
    size_t ram_size;
    int *bank;
    void *opaque;
    int (*read_device)(void *, GensM68kBusState *, enum GensZ80Device,
                       uint32_t, uint16_t *);
    int (*write_device)(void *, GensM68kBusState *, enum GensZ80Device,
                        uint32_t, uint8_t);
} GensZ80Memory;

int gens_z80_memory_valid(const GensZ80Memory *memory);
int gens_z80_window_read(GensZ80Memory *memory, GensM68kBusState *state,
                         uint32_t address, unsigned int size, uint16_t *value);
int gens_z80_window_write(GensZ80Memory *memory, GensM68kBusState *state,
                          uint32_t address, unsigned int size, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
