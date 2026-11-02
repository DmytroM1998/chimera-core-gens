#ifndef CHIMERA_GENS_M68K_BUS_H
#define CHIMERA_GENS_M68K_BUS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum GensM68kBusSize {
    GENS_M68K_BYTE = 1,
    GENS_M68K_WORD = 2,
    GENS_M68K_LONG = 4
};

enum GensM68kBusOrder {
    GENS_M68K_NORMAL = 0,
    GENS_M68K_PREDECREMENT = 1
};

enum GensM68kBusHook {
    GENS_M68K_READ_BYTE = 0,
    GENS_M68K_READ_WORD,
    GENS_M68K_READ_LONG,
    GENS_M68K_WRITE_BYTE,
    GENS_M68K_WRITE_WORD,
    GENS_M68K_WRITE_LONG
};

typedef enum GensM68kBusStatus {
    GENS_M68K_BUS_OK = 0,
    GENS_M68K_BUS_INVALID_ARGUMENT,
    GENS_M68K_BUS_MISSING_CALLBACK,
    GENS_M68K_BUS_RAM_BOUNDARY,
    GENS_M68K_BUS_BACKEND_ERROR
} GensM68kBusStatus;

typedef struct GensM68kLive {
    uintptr_t fetch_pc;
    uintptr_t fetch_base;
    uint32_t cycle_counter;
    uint32_t address;
} GensM68kLive;

typedef struct GensM68kBusState {
    uintptr_t io_fetch_pc;
    uintptr_t io_fetch_base;
    uint32_t io_cycle_counter;
    uint32_t access_address;
    uint32_t hook_pc;
    uint32_t hook_address;
    uint32_t hook_value;
} GensM68kBusState;

typedef struct GensM68kBus {
    uint8_t *ram;
    size_t ram_size;
    void *opaque;
    GensM68kBusState *state;
    int (*read_byte)(void *, GensM68kBusState *, uint32_t, uint8_t *);
    int (*read_word)(void *, GensM68kBusState *, uint32_t, uint16_t *);
    int (*write_byte)(void *, GensM68kBusState *, uint32_t, uint8_t);
    int (*write_word)(void *, GensM68kBusState *, uint32_t, uint16_t);
    void (*hook)(void *, GensM68kBusState *, enum GensM68kBusHook);
} GensM68kBus;

GensM68kBusStatus gens_m68k_read(GensM68kBus *bus, GensM68kLive *live,
                                  enum GensM68kBusSize size,
                                  enum GensM68kBusOrder order, uint32_t *value);
GensM68kBusStatus gens_m68k_write(GensM68kBus *bus, GensM68kLive *live,
                                   enum GensM68kBusSize size,
                                   enum GensM68kBusOrder order, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif
