#ifndef CHIMERA_GENS_VDP_READ_H
#define CHIMERA_GENS_VDP_READ_H

#include "gens_genesis_bus.h"
#include "gens_z80_native.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpRead {
    uint8_t *vram, *cram, *vsram, *h_counter;
    size_t vram_size, cram_size, vsram_size, h_counter_size;
    int *status, *control_flag, *control_access, *control_address;
    unsigned int *set2, *set4, *auto_increment;
    int *cycles_m68k, *cpl_m68k, *current_line;
    void *opaque;
    int (*read_odometer)(void *, GensM68kBusState *, uint32_t *);
} GensVdpRead;

int gens_vdp_read_valid(const GensVdpRead *);
int gens_vdp_h_counter_init(uint8_t *, size_t);
int gens_vdp_data_read(GensVdpRead *, GensM68kBusState *, uint16_t *);
int gens_vdp_status_read(GensVdpRead *, GensM68kBusState *, uint16_t *);
int gens_vdp_h_counter_read(GensVdpRead *, GensM68kBusState *, uint32_t *);
int gens_vdp_v_counter_read(GensVdpRead *, GensM68kBusState *, uint32_t *);
int gens_vdp_genesis_read(void *, GensM68kBusState *, enum GensGenesisDevice,
                          uint32_t, unsigned int, uint16_t *);
int gens_vdp_z80_read(void *, GensM68kBusState *, enum GensZ80Device, uint32_t, uint16_t *);
int gens_vdp_native_read(void *, GensM68kBusState *, enum GensZ80NativeOperation,
                         GensZ80Registers *);

#ifdef __cplusplus
}
#endif

#endif
