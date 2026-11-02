#ifndef CHIMERA_GENS_Z80_NATIVE_H
#define CHIMERA_GENS_Z80_NATIVE_H

#include "gens_z80_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensZ80Registers {
    uint32_t eax, ecx, edx;
} GensZ80Registers;

enum GensZ80NativeOperation {
    GENS_Z80_NATIVE_YM_READ,
    GENS_Z80_NATIVE_YM_WRITE,
    GENS_Z80_NATIVE_VDP_STATUS,
    GENS_Z80_NATIVE_V_COUNTER,
    GENS_Z80_NATIVE_H_COUNTER,
    GENS_Z80_NATIVE_VDP_BYTE,
    GENS_Z80_NATIVE_VDP_WORD,
    GENS_Z80_NATIVE_PSG,
    GENS_Z80_NATIVE_M68K_READ,
    GENS_Z80_NATIVE_M68K_WRITE
};

typedef struct GensZ80Native {
    GensZ80Memory *memory;
    void *opaque;
    int (*backend)(void *, GensM68kBusState *, enum GensZ80NativeOperation,
                   GensZ80Registers *);
} GensZ80Native;

int gens_z80_native_read(GensZ80Native *native, GensM68kBusState *state,
                         unsigned int size, GensZ80Registers *registers);
int gens_z80_native_write(GensZ80Native *native, GensM68kBusState *state,
                          unsigned int size, GensZ80Registers *registers);

#ifdef __cplusplus
}
#endif

#endif
