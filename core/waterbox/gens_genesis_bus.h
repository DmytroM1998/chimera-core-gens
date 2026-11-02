#ifndef CHIMERA_GENS_GENESIS_BUS_H
#define CHIMERA_GENS_GENESIS_BUS_H

#include "gens_m68k_bus.h"
#include "gens_z80_control.h"
#include "gens_z80_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

enum GensGenesisDevice {
    GENS_GENESIS_Z80_MEMORY,
    GENS_GENESIS_Z80_BUS_REQUEST,
    GENS_GENESIS_Z80_RESET,
    GENS_GENESIS_VDP,
    GENS_GENESIS_PSG
};

struct GensVdp;

typedef struct GensGenesisBusConfig {
    uint8_t *ram;
    size_t ram_size;
    uint8_t *rom;
    size_t rom_size;
    uint8_t *sram;
    size_t sram_size;
    int *sram_start, *sram_end, *sram_on, *sram_write, *sram_custom;
    int *z80_state, *game_mode, *cpu_mode, *gen_version;
    void *opaque;
    int (*device_read)(void *, GensM68kBusState *, enum GensGenesisDevice,
                       uint32_t, unsigned int, uint16_t *);
    int (*device_write)(void *, GensM68kBusState *, enum GensGenesisDevice,
                        uint32_t, unsigned int, uint16_t);
    int (*fix_codes)(void *, GensM68kBusState *, uint32_t, unsigned int);
    void (*hook)(void *, GensM68kBusState *, enum GensM68kBusHook);
    GensZ80Control *z80_control;
    GensZ80Memory *z80_memory;
    struct GensVdp *vdp;
} GensGenesisBusConfig;

typedef struct GensGenesisBus {
    GensM68kBus cpu;
    GensM68kBusState state;
    GensGenesisBusConfig config;
    uint8_t read_map[32];
} GensGenesisBus;

int gens_genesis_bus_init(GensGenesisBus *bus, const GensGenesisBusConfig *config);
void gens_genesis_bus_reset_mapping(GensGenesisBus *bus);

#ifdef __cplusplus
}
#endif

#endif
