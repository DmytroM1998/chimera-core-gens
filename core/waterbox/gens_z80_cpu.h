#ifndef CHIMERA_GENS_Z80_CPU_H
#define CHIMERA_GENS_Z80_CPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GENS_Z80_RUNNING 0x01u
#define GENS_Z80_HALTED 0x02u
#define GENS_Z80_FAULTED 0x10u

/* Returned by gens_z80_exec when the next instruction is not ported yet. */
#define GENS_Z80_EXEC_UNIMPLEMENTED 0x40000000u
#define GENS_Z80_EXEC_NO_CYCLES 0xffffffffu

/* The original keeps work RAM in a separate 8 KiB array. A word access at 0x1fff and a sequential
 * instruction fetch past the end both touch the bytes that follow it. In Gens those are the
 * Bank_Z80 register and later globals. The machine must keep the spare bytes consistent. */
#define GENS_Z80_RAM_SIZE 0x2100u

typedef struct GensZ80 GensZ80;

typedef uint8_t (*GensZ80ReadByte)(GensZ80 *z, uint32_t address);
typedef uint16_t (*GensZ80ReadWord)(GensZ80 *z, uint32_t address);
typedef void (*GensZ80WriteByte)(GensZ80 *z, uint32_t address, uint8_t value);
typedef void (*GensZ80WriteWord)(GensZ80 *z, uint32_t address, uint16_t value);

/* Candidate port of the pinned Z80 core. Fields follow the original context names. */
struct GensZ80 {
    uint8_t a, f, fxyw, fxyb;
    uint8_t b, c, d, e, h, l;
    uint8_t ixh, ixl, iyh, iyl;
    uint16_t sp;
    uint8_t a2, f2, fxyw2, fxyb2;
    uint8_t b2, c2, d2, e2, h2, l2;
    uint8_t iff1, iff2, r1, r2, i, im, int_vect, int_line;
    uint32_t status;
    uintptr_t pc, base_pc;
    uint32_t cycle_cnt, cycle_td, cycle_io, cycle_sup;
    GensZ80ReadByte read_b[256];
    GensZ80WriteByte write_b[256];
    GensZ80ReadWord read_w[256];
    GensZ80WriteWord write_w[256];
    uintptr_t fetch[256];
    uint8_t (*in)(GensZ80 *z, uint32_t port);
    void (*out)(GensZ80 *z, uint32_t port, uint8_t value);
    void *opaque;
    /* Set to the address before a byte read handler runs. A handler may store the value the
     * original handler would leave in ECX. BIT 3/5 on memory copies its bits 8-15 into X/Y. */
    uint32_t io_ecx;
    /* Optional. Called before each instruction like the original GENS_LOG hook. */
    void (*trace)(GensZ80 *z, uint32_t pc);
    /* DAA executions with H set. See docs/Z80-CPU.md for why these may differ from Gens. */
    uint32_t daa_overflows;
    uint8_t ram[GENS_Z80_RAM_SIZE];
    uint8_t default_memory[0x10000];
};

void gens_z80_init(GensZ80 *);
void gens_z80_reset(GensZ80 *);
void gens_z80_set_opaque(GensZ80 *, void *);

/* Handler ranges are inclusive page numbers. Fetch regions map the range linearly. */
void gens_z80_add_read_b(GensZ80 *, uint32_t low, uint32_t high, GensZ80ReadByte);
void gens_z80_add_read_w(GensZ80 *, uint32_t low, uint32_t high, GensZ80ReadWord);
void gens_z80_add_write_b(GensZ80 *, uint32_t low, uint32_t high, GensZ80WriteByte);
void gens_z80_add_write_w(GensZ80 *, uint32_t low, uint32_t high, GensZ80WriteWord);
void gens_z80_add_fetch(GensZ80 *, uint32_t low, uint32_t high, const uint8_t *region);

uint32_t gens_z80_read_odo(const GensZ80 *);
void gens_z80_clear_odo(GensZ80 *);
void gens_z80_set_odo(GensZ80 *, uint32_t);
void gens_z80_add_cycles(GensZ80 *, uint32_t);
uint32_t gens_z80_exec(GensZ80 *, uint32_t odo);
void gens_z80_nmi(GensZ80 *);
void gens_z80_interrupt(GensZ80 *, uint32_t vector);

uint32_t gens_z80_get_pc(const GensZ80 *);
uint32_t gens_z80_get_af(const GensZ80 *);
uint32_t gens_z80_get_af2(const GensZ80 *);
void gens_z80_set_pc(GensZ80 *, uint32_t);
void gens_z80_set_af(GensZ80 *, uint32_t);
void gens_z80_set_af2(GensZ80 *, uint32_t);

#ifdef __cplusplus
}
#endif

#endif
