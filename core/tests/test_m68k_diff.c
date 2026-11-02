/* Differential fuzzer: Gens' generated Starscream 68000 (main68k.asm) against gens_m68k_cpu.c.
 * Both cores run in one i386 process with identical memory, callbacks and interrupt requests.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gens_m68k_cpu.h"

/* ----- Reference core symbols (generated with -hog -name main68k_) ----- */
typedef struct RefFetch { uint32_t low, high, offset; } RefFetch;
typedef struct RefContext {
    void *fetch, *readbyte, *readword, *writebyte, *writeword;
    void *s_fetch, *s_readbyte, *s_readword, *s_writebyte, *s_writeword;
    void *u_fetch, *u_readbyte, *u_readword, *u_writebyte, *u_writeword;
    void *resethandler;
    uint32_t dreg[8], areg[7], a7, asp, pc, odometer;
    uint8_t interrupts[8];
    uint16_t sr, filler0;
    uint32_t cycles_needed, cycles_leftover, fetch_region_start, fetch_region_end;
    uint8_t xflag, execinfo, trace_trickybit, filler1;
    int32_t io_cycle_counter;
    uint32_t io_fetchbase, io_fetchbased_pc, access_address, save_01, save_02;
} RefContext;

extern RefContext ref __asm__("_main68k_context");
int ref_init(void) __asm__("_main68k_init");
uint32_t ref_exec(uint32_t) __asm__("ref_exec_zero_ecx");
int ref_interrupt(int, int) __asm__("_main68k_interrupt");
uint32_t ref_read_odometer(void) __asm__("_main68k_readOdometer");

#define RAM_BYTES (0x10000u + 16u)
uint8_t ref_ram[RAM_BYTES] __asm__("Ram_68k");
uint8_t *rom_data_symbol __asm__("Rom_Data");
uint32_t rom_size_symbol __asm__("Rom_Size");
uint32_t ref_hook_pc __asm__("_hook_pc");
uint32_t ref_hook_address __asm__("_hook_address");
uint32_t ref_hook_value __asm__("_hook_value");

static GensM68k port;
static uint8_t port_ram[RAM_BYTES];

/* ROM is stored word-swapped, as Gens keeps it. Guards absorb runaway fetches. */
#define ROM_BYTES 0x100000u
static uint8_t rom_area[ROM_BYTES + 0x20000u];
#define rom (rom_area + 0x10000u)

static uint8_t ref_io[0x10000], port_io[0x10000];

typedef struct Log {
    uint32_t entries[1u << 18];
    unsigned int count;
} Log;
static Log ref_log, port_log;
static int which; /* 0 reference, 1 port */
#ifndef LOG_CONTEXT
#define LOG_CONTEXT 24u
#endif

static void record(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Log *log = which ? &port_log : &ref_log;
    /* Byte and word hook values carry leftover ECX bits above the access size in Starscream.
     * They are host register residue, so only the accessed bits are compared. */
    if (a == 7u || a == 10u) d &= 0xffu;
    else if (a == 8u || a == 11u) d &= 0xffffu;
    if (log->count + 4u <= (1u << 18))
    {
        log->entries[log->count++] = a;
        log->entries[log->count++] = b;
        log->entries[log->count++] = c;
        log->entries[log->count++] = d;
    }
}

static uint64_t seed;

static uint32_t next(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return (uint32_t)(seed >> 16);
}

/* Deterministic per-core random stream for values handed out by callbacks. */
static uint32_t ack_counter[2];
static int irq_from_io;

static uint32_t odometer_now(void)
{
    return which ? gens_m68k_read_odometer(&port) : ref_read_odometer();
}

static void raise_irq(unsigned int level)
{
    if (which) gens_m68k_interrupt(&port, level);
    else ref_interrupt((int)level, -1);
}

static uint8_t *io(void)
{
    return which ? port_io : ref_io;
}

static uint8_t bus_rb(uint32_t address)
{
    uint8_t value = io()[address & 0xffffu];
    record(1, address, value, odometer_now());
    return value;
}

static uint16_t bus_rw(uint32_t address)
{
    uint16_t value = (uint16_t)(io()[address & 0xffffu] << 8 | io()[(address + 1u) & 0xffffu]);
    record(2, address, value, odometer_now());
    return value;
}

static void bus_wb(uint32_t address, uint8_t value)
{
    record(3, address, value, odometer_now());
    io()[address & 0xffffu] = value;
    if ((address & 0xff00u) == 0xc000u && irq_from_io)
        raise_irq(value & 7u);
}

static void bus_ww(uint32_t address, uint16_t value)
{
    record(4, address, value, odometer_now());
    io()[address & 0xffffu] = (uint8_t)(value >> 8);
    io()[(address + 1u) & 0xffffu] = (uint8_t)value;
    if ((address & 0xff00u) == 0xc000u && irq_from_io)
        raise_irq(value & 7u);
}

static uint8_t ack(uint8_t level)
{
    uint32_t n = ack_counter[which]++;
    record(5, level, n, 0);
    return (uint8_t)((n * 2654435761u >> 13) % 3u == 0u ? (n >> 3) & 7u : 0u);
}

/* Relative branches do not rebase in Starscream, so code can run past the end of a fetch region
 * into whatever host memory follows it. The ROM has shared guard bytes. Past the RAM arrays the
 * two cores see different host memory, so such scenarios are counted but not compared. */
static int escaped;
static void note_region(uint32_t pc, uint32_t start, uint32_t end)
{
    /* RAM has 16 spare bytes and the ROM has guard bytes, which cover the longest instruction. */
    if (pc >= start && pc <= end + 6u) return;
    if (start == 0u && pc < ROM_BYTES + 0xfff0u) return;
    escaped = 1;
}

/* Reference callbacks (cdecl, address pushed first). */uint8_t ref_rb(uint32_t address) __asm__("ref_rb_c");
uint8_t ref_rb(uint32_t address) { return bus_rb(address); }
uint16_t ref_rw(uint32_t address) __asm__("ref_rw_c");
uint16_t ref_rw(uint32_t address) { return bus_rw(address); }
void ref_wb(uint32_t address, uint32_t value) __asm__("ref_wb_c");
void ref_wb(uint32_t address, uint32_t value) { bus_wb(address, (uint8_t)value); }
void ref_ww(uint32_t address, uint32_t value) __asm__("ref_ww_c");
void ref_ww(uint32_t address, uint32_t value) { bus_ww(address, (uint16_t)value); }
uint32_t int_ack_c(uint32_t level) __asm__("int_ack_c");
uint32_t int_ack_c(uint32_t level) { return ack((uint8_t)level); }

void ref_hook_exec(void) __asm__("_hook_exec");
void ref_hook_exec(void) { note_region(ref_hook_pc, ref.fetch_region_start, ref.fetch_region_end); record(6, ref_hook_pc, 0, 0); }
void ref_hook_rb(void) __asm__("_hook_read_byte");
void ref_hook_rb(void) { record(7, ref_hook_pc, ref_hook_address, ref_hook_value); }
void ref_hook_rw(void) __asm__("_hook_read_word");
void ref_hook_rw(void) { record(8, ref_hook_pc, ref_hook_address, ref_hook_value); }
void ref_hook_rl(void) __asm__("_hook_read_dword");
void ref_hook_rl(void) { record(9, ref_hook_pc, ref_hook_address, ref_hook_value); }
void ref_hook_wb(void) __asm__("_hook_write_byte");
void ref_hook_wb(void) { record(10, ref_hook_pc, ref_hook_address, ref_hook_value); }
void ref_hook_ww(void) __asm__("_hook_write_word");
void ref_hook_ww(void) { record(11, ref_hook_pc, ref_hook_address, ref_hook_value); }
void ref_hook_wl(void) __asm__("_hook_write_dword");
void ref_hook_wl(void) { record(12, ref_hook_pc, ref_hook_address, ref_hook_value); }

/* Port callbacks */
static uint8_t p_rb(void *o, uint32_t a) { (void)o; return bus_rb(a); }
static uint16_t p_rw(void *o, uint32_t a) { (void)o; return bus_rw(a); }
static void p_wb(void *o, uint32_t a, uint8_t v) { (void)o; bus_wb(a, v); }
static void p_ww(void *o, uint32_t a, uint16_t v) { (void)o; bus_ww(a, v); }
static uint8_t p_ack(void *o, uint8_t level) { (void)o; return ack(level); }
static void p_exec(void *o, uint32_t pc) { (void)o; note_region(pc, port.fetch_region_start, port.fetch_region_end); record(6, pc, 0, 0); }
static void p_read(void *o, unsigned int size, uint32_t pc, uint32_t a, uint32_t v)
{
    (void)o;
    record(size == 1u ? 7u : size == 2u ? 8u : 9u, pc, a, v);
}
static void p_write(void *o, unsigned int size, uint32_t pc, uint32_t a, uint32_t v)
{
    (void)o;
    record(size == 1u ? 10u : size == 2u ? 11u : 12u, pc, a, v);
}

static RefFetch ref_fetch[5];
static GensM68kFetch port_fetch[5];

static void setup_fetch(void)
{
    static const uint32_t ram_low[3] = {0xff0000u, 0xf00000u, 0xef0000u};
    unsigned int i;
    ref_fetch[0] = (RefFetch){0, ROM_BYTES - 1u, (uint32_t)(uintptr_t)rom};
    port_fetch[0] = (GensM68kFetch){0, ROM_BYTES - 1u, (uintptr_t)rom};
    for (i = 0; i < 3u; ++i)
    {
        ref_fetch[i + 1u] = (RefFetch){ram_low[i], ram_low[i] + 0xffffu,
                                       (uint32_t)(uintptr_t)ref_ram - ram_low[i]};
        port_fetch[i + 1u] = (GensM68kFetch){ram_low[i], ram_low[i] + 0xffffu,
                                             (uintptr_t)port_ram - ram_low[i]};
    }
    ref_fetch[4] = (RefFetch){0xffffffffu, 0xffffffffu, 0};
    port_fetch[4] = (GensM68kFetch){0xffffffffu, 0xffffffffu, 0};
}

static void put_word(uint32_t address, uint16_t value)
{
    rom[address] = (uint8_t)value;
    rom[address + 1u] = (uint8_t)(value >> 8);
}

/* Biased instruction words: plain random words plus families that need well-formed operands. */
static uint16_t random_word(unsigned int mode)
{
    uint32_t r = next();
    switch (r % (mode & 1u ? 16u : 8u))
    {
    case 0: return (uint16_t)(0x4e71u);                      /* NOP */
    case 1: return (uint16_t)(0x6000u | (r >> 8 & 0x0f00u) | ((r >> 4) & 0x1eu)); /* Bcc forward */
    case 2: return (uint16_t)(0xe000u | (r >> 8 & 0x0fffu)); /* shifts */
    case 3: return (uint16_t)(0x51c8u | (r >> 8 & 0x0e07u)); /* DBcc */
    default: return (uint16_t)(r >> 8);
    }
}

typedef struct State {
    uint32_t dreg[8], areg[8], asp, pc, odometer, leftover;
    uint16_t sr;
    uint8_t interrupts, execinfo;
} State;

static State ref_state(void)
{
    State s;
    memset(&s, 0, sizeof(s));
    memcpy(s.dreg, ref.dreg, sizeof(s.dreg));
    memcpy(s.areg, ref.areg, 7 * 4);
    s.areg[7] = ref.a7;
    s.asp = ref.asp; s.pc = ref.pc; s.odometer = ref.odometer; s.leftover = ref.cycles_leftover;
    s.sr = ref.sr; s.interrupts = ref.interrupts[0]; s.execinfo = ref.execinfo;
    return s;
}

static State port_state(void)
{
    State s;
    memset(&s, 0, sizeof(s));
    memcpy(s.dreg, port.dreg, sizeof(s.dreg));
    memcpy(s.areg, port.areg, sizeof(s.areg));
    s.asp = port.asp; s.pc = port.pc; s.odometer = port.odometer; s.leftover = port.cycles_leftover;
    s.sr = port.sr; s.interrupts = port.interrupts[0]; s.execinfo = port.execinfo;
    return s;
}

static void print_state(const char *name, const State *s)
{
    unsigned int i;
    printf("  %s pc=%08x sr=%04x odo=%u left=%u int=%02x info=%02x asp=%08x\n     D:", name, s->pc,
           s->sr, s->odometer, s->leftover, s->interrupts, s->execinfo, s->asp);
    for (i = 0; i < 8; ++i) printf(" %08x", s->dreg[i]);
    printf("\n     A:");
    for (i = 0; i < 8; ++i) printf(" %08x", s->areg[i]);
    printf("\n");
}

static uint16_t rom_word(uint32_t pc)
{
    return (uint16_t)(rom[pc & (ROM_BYTES - 1u)] | rom[(pc + 1u) & (ROM_BYTES - 1u)] << 8);
}

static int compare(unsigned int scenario, unsigned int round, uint32_t rr, uint32_t pr)
{
    State a = ref_state(), b = port_state();
    unsigned int i;
    int bad = 0;
    if (memcmp(&a, &b, sizeof(a)) != 0 || rr != pr) bad = 1;
    else if (memcmp(ref_ram, port_ram, RAM_BYTES) != 0) bad = 2;
    else if (memcmp(ref_io, port_io, sizeof(ref_io)) != 0) bad = 3;
    else if (ref_log.count != port_log.count ||
             memcmp(ref_log.entries, port_log.entries, ref_log.count * 4u) != 0) bad = 4;
    if (!bad)
        return 1;
    printf("MISMATCH scenario %u round %u kind %d ret %08x/%08x\n", scenario, round, bad, rr, pr);
    print_state("ref ", &a);
    print_state("port", &b);
    for (i = 0; i < RAM_BYTES; ++i)
        if (ref_ram[i] != port_ram[i]) { printf("  ram[%05x] %02x/%02x\n", i, ref_ram[i], port_ram[i]); break; }
    for (i = 0; i + 4u <= ref_log.count && i + 4u <= port_log.count; i += 4u)
        if (memcmp(&ref_log.entries[i], &port_log.entries[i], 16) != 0)
        {
            unsigned int j, from = i >= LOG_CONTEXT ? i - LOG_CONTEXT : 0u;
            for (j = from; j <= i; j += 4u)
                printf("  log[%u] ref %u %08x %08x %08x | port %u %08x %08x %08x%s\n", j / 4u,
                       ref_log.entries[j], ref_log.entries[j + 1], ref_log.entries[j + 2],
                       ref_log.entries[j + 3], port_log.entries[j], port_log.entries[j + 1],
                       port_log.entries[j + 2], port_log.entries[j + 3], j == i ? " <==" : "");
            for (j = from; j <= i; j += 4u)
                if (ref_log.entries[j] == 6u)
                    printf("    pc %06x op %04x %04x %04x\n", ref_log.entries[j + 1],
                           rom_word(ref_log.entries[j + 1]), rom_word(ref_log.entries[j + 1] + 2u),
                           rom_word(ref_log.entries[j + 1] + 4u));
            break;
        }
    if (ref_log.count != port_log.count) printf("  log count %u/%u\n", ref_log.count / 4u, port_log.count / 4u);
    return 0;
}

static void setup(unsigned int mode)
{
    uint32_t i, pc, sp, sr;
    memset(rom_area, 0, sizeof(rom_area));
    for (i = 0; i < 0x10000u; ++i) rom_area[i] = rom_area[ROM_BYTES + 0x10000u + i] = (uint8_t)(i * 7u + 1u);
    /* Exception vectors point to random even handler addresses inside ROM. */
    for (i = 0; i < 0x400u; i += 4u)
    {
        uint32_t target = 0x1000u + (next() % 0x7f000u & ~1u);
        put_word(i, (uint16_t)(target >> 16));
        put_word(i + 2u, (uint16_t)target);
    }
    for (i = 0x400u; i < ROM_BYTES; i += 2u)
        put_word(i, random_word(mode));
    for (i = 0; i < RAM_BYTES; ++i) ref_ram[i] = port_ram[i] = (uint8_t)next();
    for (i = 0; i < 0x10000u; ++i) ref_io[i] = port_io[i] = (uint8_t)next();

    memset(&ref, 0, sizeof(ref));
    memset(&port, 0, sizeof(port));
    setup_fetch();
    ref.fetch = ref.s_fetch = ref.u_fetch = ref_fetch;
    ref.io_cycle_counter = -1;
    port.fetch = port.s_fetch = port.u_fetch = port_fetch;
    port.ram = port_ram;
    port.bus = (GensM68kCpuBus){0, p_rb, p_rw, p_wb, p_ww, p_ack, 0};
    port.hooks = (GensM68kHooks){0, p_exec, p_read, p_write};
    gens_m68k_init(&port);

    pc = 0x1000u + (next() % 0x7f000u & ~1u);
    sp = (mode & 2u) ? 0xff8000u + (next() & 0x3ffeu) : 0x00e00000u | (next() & 0xfffeu);
    sr = (uint32_t)(mode & 4u ? 0x2000u : 0u) | (next() & 0x071fu);
    for (i = 0; i < 8; ++i) ref.dreg[i] = port.dreg[i] = next() ^ next() << 16;
    for (i = 0; i < 7; ++i)
    {
        uint32_t a = (next() & 1u) ? 0xff0000u | (next() & 0xfffeu) : (next() ^ next() << 16);
        ref.areg[i] = port.areg[i] = a;
    }
    ref.a7 = port.areg[7] = sp;
    ref.asp = port.asp = 0xffc000u | (next() & 0x3ffeu);
    ref.pc = port.pc = pc;
    ref.sr = port.sr = (uint16_t)sr;
    ref.odometer = port.odometer = next() % 1000u;
    ref.interrupts[0] = port.interrupts[0] = (uint8_t)(mode & 8u ? next() % 8u : 0u);
    ack_counter[0] = ack_counter[1] = 0;
    irq_from_io = (mode & 16u) != 0u;
    ref_log.count = port_log.count = 0;
}

int main(int argc, char **argv)
{
    unsigned int scenarios = argc > 1 ? (unsigned int)strtoul(argv[1], 0, 0) : 5000u;
    unsigned int scenario, round, matched = 0, failed = 0, escapes = 0;
    uint32_t odo, rr, pr;
    seed = argc > 2 ? strtoull(argv[2], 0, 0) : UINT64_C(0x68000);
    rom_data_symbol = rom;
    rom_size_symbol = ROM_BYTES;
    ref_init();
    for (scenario = 0; scenario < scenarios; ++scenario)
    {
        unsigned int mode = next();
        int ok = 1;
        setup(mode);
        escaped = 0;
        for (round = 0; round < 1u + mode % 4u && ok; ++round)
        {
            odo = ref.odometer + 1u + next() % 4000u;
            ref_log.count = port_log.count = 0;
            which = 0;
            rr = ref_exec(odo);
            which = 1;
            pr = gens_m68k_exec(&port, odo);
#ifdef GENS_M68K_DIFF_NEGATIVE_CONTROL
            port.dreg[3] ^= (port.sr & 1u);
#endif
            if (escaped)
            {
                ok = 0;
                ++escapes;
                break;
            }
            if (!compare(scenario, round, rr, pr))
            {
                ok = 0;
                if (++failed >= 3)
                    return 1;
            }
            if (next() % 3u == 0u)
            {
                unsigned int level = next() % 8u;
                which = 0; ref_interrupt((int)level, -1);
                which = 1; gens_m68k_interrupt(&port, level);
            }
        }
        if (ok) ++matched;
    }
    printf("m68k diff: %u matched, %u escaped a fetch region, %u mismatched\n", matched, escapes, failed);
    return failed != 0;
}
