#ifndef CHIMERA_GENS_M68K_CPU_H
#define CHIMERA_GENS_M68K_CPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes of gens_m68k_exec, as in Starscream. */
#define GENS_M68K_OK 0x80000000u
#define GENS_M68K_OUT_OF_BOUNDS 0x80000001u
#define GENS_M68K_ALREADY_DONE 0x80000003u
#define GENS_M68K_STOPPED 0x80000004u
#define GENS_M68K_DOUBLE_FAULT 0xffffffffu

/* One fetch region. The terminating entry has start 0xffffffff. Host addresses of the region are
 * base + address, with the base kept at pointer width. */
typedef struct GensM68kFetch {
    uint32_t start, end;
    uintptr_t base;
} GensM68kFetch;

typedef struct GensM68k GensM68k;

/* Bus callbacks mirror Gens' M68K_RB, M68K_RW, M68K_WB, M68K_WW and Int_Ack. Addresses are the
 * values the original pushes. While a callback runs the CPU's I/O state is live, so it may call
 * gens_m68k_read_odometer, gens_m68k_release_cycles and gens_m68k_interrupt. */
typedef struct GensM68kCpuBus {
    void *opaque;
    uint8_t (*read_byte)(void *opaque, uint32_t address);
    uint16_t (*read_word)(void *opaque, uint32_t address);
    void (*write_byte)(void *opaque, uint32_t address, uint8_t value);
    void (*write_word)(void *opaque, uint32_t address, uint16_t value);
    /* Receives the acknowledged level and returns the new pending level. */
    uint8_t (*int_ack)(void *opaque, uint8_t level);
    /* RESET instruction. Without a handler RESET is an invalid instruction. */
    void (*reset)(void *opaque);
} GensM68kCpuBus;

/* Optional observers at the original HOOKS_ENABLED call sites. pc, address and value are the
 * original _hook_pc, _hook_address and _hook_value. */
typedef struct GensM68kHooks {
    void *opaque;
    void (*exec)(void *opaque, uint32_t pc);
    void (*read)(void *opaque, unsigned int size, uint32_t pc, uint32_t address, uint32_t value);
    void (*write)(void *opaque, unsigned int size, uint32_t pc, uint32_t address, uint32_t value);
} GensM68kHooks;

/* Starscream context fields that Gens or a saved state may inspect. */
struct GensM68k {
    const GensM68kFetch *fetch, *s_fetch, *u_fetch;
    uint32_t dreg[8], areg[8];
    uint32_t asp, pc, odometer;
    uint8_t interrupts[8];
    uint16_t sr;
    uint32_t cycles_needed, cycles_leftover;
    uint32_t fetch_region_start, fetch_region_end;
    uint8_t xflag, execinfo, trace_trickybit;
    int32_t io_cycle_counter;
    uintptr_t io_fetchbase, io_fetchbased_pc;
    uint32_t access_address;
    /* Gens' Ram_68k: 64 KiB of word-swapped work RAM. Word and long accesses near the end read
     * past it, so the buffer must have GENS_M68K_RAM_SPARE extra bytes. */
    uint8_t *ram;
    GensM68kCpuBus bus;
    GensM68kHooks hooks;
};

#define GENS_M68K_RAM_SPARE 4u

int gens_m68k_init(GensM68k *);
int gens_m68k_reset(GensM68k *);
uint32_t gens_m68k_exec(GensM68k *, uint32_t odometer);
int gens_m68k_interrupt(GensM68k *, unsigned int level);
void gens_m68k_flush_interrupts(GensM68k *);
uint32_t gens_m68k_read_odometer(const GensM68k *);
uint32_t gens_m68k_trip_odometer(GensM68k *);
uint32_t gens_m68k_control_odometer(GensM68k *, uint32_t clear);
void gens_m68k_release_timeslice(GensM68k *);
void gens_m68k_release_cycles(GensM68k *, uint32_t cycles);
void gens_m68k_add_cycles(GensM68k *, uint32_t cycles);
uint32_t gens_m68k_read_pc(const GensM68k *);
uint32_t gens_m68k_fetch_word(GensM68k *, uint32_t address);

#ifdef __cplusplus
}
#endif

#endif
