#ifndef CHIMERA_GENS_VDP_H
#define CHIMERA_GENS_VDP_H

#include "gens_vdp_read.h"
#include "gens_vdp_registers.h"
#include "gens_vdp_memory_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpConfig {
    GensVdpRegisters registers;
    int *control;
    size_t control_count;
    uint8_t *cram, *vsram, *h_counter;
    size_t cram_size, vsram_size, h_counter_size;
    int *status, *vram_flag, *cram_flag;
    int *current_line, *visible_lines, *cycles_m68k, *cycles_per_line;
    int *dma_length, *dma_type;
    const uint8_t *rom, *ram;
    size_t rom_backing_size, ram_size;
    int *rom_size, *sega_cd_started;
    void *odometer_opaque;
    int (*read_odometer)(void *, GensM68kBusState *, uint32_t *);
} GensVdpConfig;

typedef struct GensVdp {
    GensVdpRead read;
    GensVdpWrite write;
    GensVdpRegisters registers;
    GensVdpDmaTiming timing;
    GensVdpMemoryDma memory;
    int *control;
    size_t control_count;
} GensVdp;

int gens_vdp_init(GensVdp *, const GensVdpConfig *);
int gens_vdp_valid(const GensVdp *);
int gens_vdp_device_read(void *, GensM68kBusState *, enum GensGenesisDevice,
                         uint32_t, unsigned int, uint16_t *);
int gens_vdp_device_write(void *, GensM68kBusState *, enum GensGenesisDevice,
                          uint32_t, unsigned int, uint16_t);
int gens_vdp_window_read(void *, GensM68kBusState *, enum GensZ80Device,
                         uint32_t, uint16_t *);
int gens_vdp_native_device_read(void *, GensM68kBusState *, enum GensZ80NativeOperation,
                                GensZ80Registers *);
int gens_vdp_dma_tick(GensVdp *, uint32_t *);

#ifdef __cplusplus
}
#endif

#endif
