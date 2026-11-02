/* Differential fuzzer: unchanged z80.asm against gens_z80_cpu.c in one i386 process.
 * Build (i386 only): nasm -f win32|elf32 z80.asm, then compile this file with the C core.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FASTCALL __attribute__((fastcall))
#include "z80.h"
#include "gens_z80_cpu.h"
#include "gens_z80_tables.h"

uint8_t ref_ram[GENS_Z80_RAM_SIZE] __asm__("Ram_Z80");
extern uint8_t Def_z80_Mem[0x10000];

static GensZ80 port;
/* Random code may run past either end. Guards keep it inside deterministic, never-written bytes. */
static uint8_t code_area[0x30000];
#define code (code_area + 0x10000)
static uint8_t ref_io[0x10000], port_io[0x10000];

typedef struct Log {
    uint32_t entries[4096];
    unsigned int count;
} Log;
static Log ref_log, port_log;

static void log_access(Log *log, uint32_t kind, uint32_t address, uint32_t value)
{
    if (log->count + 2u <= 4096u)
    {
        log->entries[log->count++] = kind << 24 | (address & 0xffffffu);
        log->entries[log->count++] = value;
    }
}

/* Interrupt requests raised from a write handler exercise the in-run CycleSup path. */
static int ref_running_irq, port_running_irq;

uint8_t FASTCALL ref_rb(uint32_t address) __asm__("ref_rb_c");
uint8_t FASTCALL ref_rb(uint32_t address)
{
    log_access(&ref_log, 1, address, ref_io[address & 0xffffu]);
    return ref_io[address & 0xffffu];
}

uint16_t FASTCALL ref_rw(uint32_t address) __asm__("ref_rw_c");
uint16_t FASTCALL ref_rw(uint32_t address)
{
    uint16_t value = (uint16_t)(ref_io[address & 0xffffu] | ref_io[(address + 1u) & 0xffffu] << 8);
    log_access(&ref_log, 2, address, value);
    return value;
}

void FASTCALL ref_wb(uint32_t address, uint8_t value) __asm__("ref_wb_c");
void FASTCALL ref_wb(uint32_t address, uint8_t value)
{
    log_access(&ref_log, 3, address, value);
    ref_io[address & 0xffffu] = value;
    if ((address & 0xffffu) == 0x7f11u && ref_running_irq)
        z80_Interrupt(&M_Z80, value);
}

void FASTCALL ref_ww(uint32_t address, uint16_t value) __asm__("ref_ww_c");
void FASTCALL ref_ww(uint32_t address, uint16_t value)
{
    log_access(&ref_log, 4, address, value);
    ref_io[address & 0xffffu] = (uint8_t)value;
    ref_io[(address + 1u) & 0xffffu] = (uint8_t)(value >> 8);
}

uint8_t FASTCALL ref_rb_thunk(uint32_t) __asm__("ref_rb_thunk");
uint16_t FASTCALL ref_rw_thunk(uint32_t) __asm__("ref_rw_thunk");
void FASTCALL ref_wb_thunk(uint32_t, uint8_t) __asm__("ref_wb_thunk");
void FASTCALL ref_ww_thunk(uint32_t, uint16_t) __asm__("ref_ww_thunk");

static uint8_t port_rb(GensZ80 *z, uint32_t address)
{
    (void)z;
    log_access(&port_log, 1, address, port_io[address & 0xffffu]);
    return port_io[address & 0xffffu];
}

static uint16_t port_rw(GensZ80 *z, uint32_t address)
{
    uint16_t value = (uint16_t)(port_io[address & 0xffffu] | port_io[(address + 1u) & 0xffffu] << 8);
    (void)z;
    log_access(&port_log, 2, address, value);
    return value;
}

static void port_wb(GensZ80 *z, uint32_t address, uint8_t value)
{
    log_access(&port_log, 3, address, value);
    port_io[address & 0xffffu] = value;
    if ((address & 0xffffu) == 0x7f11u && port_running_irq)
        gens_z80_interrupt(z, value);
}

static void port_ww(GensZ80 *z, uint32_t address, uint16_t value)
{
    (void)z;
    log_access(&port_log, 4, address, value);
    port_io[address & 0xffffu] = (uint8_t)value;
    port_io[(address + 1u) & 0xffffu] = (uint8_t)(value >> 8);
}

static uint32_t ref_trace[1u << 16], port_trace[1u << 16];
static unsigned int ref_trace_count, port_trace_count;

/* Called by a GENS_LOG=1 copy of z80.asm before every dispatched instruction. The Windows
 * fastcall decoration matches the asm's @z80log@4 reference. ELF builds keep GENS_LOG=0. */
#ifdef _WIN32
void FASTCALL z80log(unsigned int pc)
{
    if (ref_trace_count < (1u << 16)) ref_trace[ref_trace_count++] = pc;
}
#endif

static unsigned int escaped_ram;

static void port_log_pc(GensZ80 *z, uint32_t pc)
{
    uintptr_t host = z->base_pc + pc, ram = (uintptr_t)z->ram;
    /* Execution beyond Z80 RAM reads unrelated neighbouring memory in each implementation. */
    if ((z->base_pc == ram || z->base_pc == ram - 0x2000u) && (host < ram || host >= ram + 0x2000u))
        escaped_ram = 1;
    if (port_trace_count < (1u << 16)) port_trace[port_trace_count++] = pc;
}

static uint64_t seed;

static uint32_t next(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return (uint32_t)(seed >> 16);
}

typedef struct State {
    uint8_t a, f, fxy, b, c, d, e, h, l, ixh, ixl, iyh, iyl;
    uint8_t a2, f2, fxy2, b2, c2, d2, e2, h2, l2;
    uint16_t sp;
    uint8_t iff1, iff2, r, i, im, int_vect, int_line;
    uint32_t status, cycles, pc;
} State;

static State ref_state(void)
{
    State s;
    memset(&s, 0, sizeof(s));
    s.a = M_Z80.AF.b.A; s.f = M_Z80.AF.b.F; s.fxy = M_Z80.AF.b.FXY;
    s.b = M_Z80.BC.b.B; s.c = M_Z80.BC.b.C; s.d = M_Z80.DE.b.D; s.e = M_Z80.DE.b.E;
    s.h = M_Z80.HL.b.H; s.l = M_Z80.HL.b.L;
    s.ixh = M_Z80.IX.b.IXH; s.ixl = M_Z80.IX.b.IXL; s.iyh = M_Z80.IY.b.IYH; s.iyl = M_Z80.IY.b.IYL;
    s.a2 = M_Z80.AF2.b.A2; s.f2 = M_Z80.AF2.b.F2; s.fxy2 = M_Z80.AF2.b.FXY2;
    s.b2 = M_Z80.BC2.b.B2; s.c2 = M_Z80.BC2.b.C2; s.d2 = M_Z80.DE2.b.D2; s.e2 = M_Z80.DE2.b.E2;
    s.h2 = M_Z80.HL2.b.H2; s.l2 = M_Z80.HL2.b.L2;
    s.sp = M_Z80.SP.w.SP;
    s.iff1 = M_Z80.IFF.b.IFF1; s.iff2 = M_Z80.IFF.b.IFF2; s.r = M_Z80.R.b.R1;
    s.i = M_Z80.I; s.im = M_Z80.IM; s.int_vect = M_Z80.IntVect; s.int_line = M_Z80.IntLine;
    s.status = M_Z80.Status; s.cycles = M_Z80.CycleCnt; s.pc = z80_Get_PC(&M_Z80);
    return s;
}

static State port_state(void)
{
    State s;
    memset(&s, 0, sizeof(s));
    s.a = port.a; s.f = port.f; s.fxy = port.fxyb;
    s.b = port.b; s.c = port.c; s.d = port.d; s.e = port.e; s.h = port.h; s.l = port.l;
    s.ixh = port.ixh; s.ixl = port.ixl; s.iyh = port.iyh; s.iyl = port.iyl;
    s.a2 = port.a2; s.f2 = port.f2; s.fxy2 = port.fxyb2;
    s.b2 = port.b2; s.c2 = port.c2; s.d2 = port.d2; s.e2 = port.e2; s.h2 = port.h2; s.l2 = port.l2;
    s.sp = port.sp;
    s.iff1 = port.iff1; s.iff2 = port.iff2; s.r = port.r1;
    s.i = port.i; s.im = port.im; s.int_vect = port.int_vect; s.int_line = port.int_line;
    s.status = port.status; s.cycles = port.cycle_cnt; s.pc = gens_z80_get_pc(&port);
    return s;
}

static void print_state(const char *name, const State *s)
{
    printf("  %s AF=%02x%02x/%02x BC=%02x%02x DE=%02x%02x HL=%02x%02x IX=%02x%02x IY=%02x%02x SP=%04x"
           " AF'=%02x%02x/%02x BC'=%02x%02x DE'=%02x%02x HL'=%02x%02x IFF=%u/%u R=%02x I=%02x IM=%u"
           " vec=%02x line=%02x st=%x cyc=%u pc=%04x\n",
           name, s->a, s->f, s->fxy, s->b, s->c, s->d, s->e, s->h, s->l, s->ixh, s->ixl,
           s->iyh, s->iyl, s->sp, s->a2, s->f2, s->fxy2, s->b2, s->c2, s->d2, s->e2, s->h2,
           s->l2, s->iff1, s->iff2, s->r, s->i, s->im, s->int_vect, s->int_line, s->status,
           s->cycles, s->pc);
}

static unsigned int blocked[8][256];

static void note_blocked(void)
{
    uint32_t pc = gens_z80_get_pc(&port);
    uint8_t first = code[pc], second = code[pc + 1u];
    GensZ80Op op = gens_z80_op_table[first];
    if (op.family == GENS_Z80_PREFIX_CB) blocked[1][second]++;
    else if (op.family == GENS_Z80_PREFIX_ED) blocked[2][second]++;
    else if (op.family == GENS_Z80_PREFIX_DD || op.family == GENS_Z80_PREFIX_FD)
    {
        if (gens_z80_dd_table[second].family == GENS_Z80_PREFIX_DDCB) blocked[5][code[pc + 3u]]++;
        else blocked[first == 0xddu ? 3 : 4][second]++;
    }
    else blocked[0][first]++;
}

static int compare(unsigned int scenario, uint32_t ref_ret, uint32_t port_ret)
{
    State r = ref_state(), p = port_state();
    unsigned int i;
    /* Gens ignores the exec result. A halted entry returns leftover registers in the original. */
    int bad = memcmp(&r, &p, sizeof(r)) != 0 ||
        (ref_ret == 0xffffffffu) != (port_ret == GENS_Z80_EXEC_NO_CYCLES);
    if (!bad && memcmp(ref_ram, port.ram, GENS_Z80_RAM_SIZE) != 0) bad = 2;
    if (!bad && memcmp(ref_io, port_io, sizeof(ref_io)) != 0) bad = 3;
    if (!bad && memcmp(Def_z80_Mem, port.default_memory, 0x10000) != 0) bad = 4;
    if (!bad && (ref_log.count != port_log.count ||
                 memcmp(ref_log.entries, port_log.entries, ref_log.count * 4u) != 0)) bad = 5;
    if (!bad)
        return 1;
    printf("MISMATCH scenario %u kind %d ret %x/%x\n", scenario, bad, ref_ret, port_ret);
    print_state("ref ", &r);
    print_state("port", &p);
    for (i = 0; i < 0x2000u; ++i)
        if (ref_ram[i] != port.ram[i]) { printf("  ram[%04x] %02x/%02x\n", i, ref_ram[i], port.ram[i]); break; }
    for (i = 0; i < ref_log.count && i < port_log.count; ++i)
        if (ref_log.entries[i] != port_log.entries[i]) { printf("  log[%u] %08x/%08x\n", i, ref_log.entries[i], port_log.entries[i]); break; }
    if (ref_log.count != port_log.count) printf("  log count %u/%u\n", ref_log.count, port_log.count);
    return 0;
}

static void setup(unsigned int mode)
{
    unsigned int i, length;
    uint8_t regs[24];
    uint32_t cycles;

    memset(code_area, 0, sizeof(code_area));
    length = mode & 1u ? 0x10000u : 64u + next() % 512u;
    for (i = 0; i < length; ++i) code[i] = (uint8_t)next();
    for (i = 0; i < 0x10000u; ++i) code_area[i] = code_area[0x20000u + i] = (uint8_t)(i * 13u + 5u);
    /* Keep a few runs free of HALT so long sequences are exercised. */
    if (mode & 2u)
        for (i = 0; i < length; ++i) if (code[i] == 0x76u) code[i] = 0;
    for (i = 0; i < GENS_Z80_RAM_SIZE; ++i) ref_ram[i] = (uint8_t)next();
    for (i = 0; i < 0x10000u; ++i) ref_io[i] = (uint8_t)next();
    for (i = 0; i < 24u; ++i) regs[i] = (uint8_t)next();
    cycles = next() % 2000u;

    z80_Init(&M_Z80);
    gens_z80_init(&port);
    for (i = 0; i < 0x10000u; ++i) Def_z80_Mem[i] = port.default_memory[i] = (uint8_t)(i * 7u + 3u);
    z80_Add_ReadB(&M_Z80, 0x40, 0xff, ref_rb_thunk);
    z80_Add_ReadW(&M_Z80, 0x40, 0xff, ref_rw_thunk);
    z80_Add_WriteB(&M_Z80, 0x40, 0xff, ref_wb_thunk);
    z80_Add_WriteW(&M_Z80, 0x40, 0xff, ref_ww_thunk);
    z80_Add_Fetch(&M_Z80, 0x00, 0xff, code);
    if (mode & 32u)
    {
        z80_Add_Fetch(&M_Z80, 0x00, 0x1f, ref_ram);
        z80_Add_Fetch(&M_Z80, 0x20, 0x3f, ref_ram);
    }
    gens_z80_add_read_b(&port, 0x40, 0xff, port_rb);
    gens_z80_add_read_w(&port, 0x40, 0xff, port_rw);
    gens_z80_add_write_b(&port, 0x40, 0xff, port_wb);
    gens_z80_add_write_w(&port, 0x40, 0xff, port_ww);
    gens_z80_add_fetch(&port, 0x00, 0xff, code);
    if (mode & 32u)
    {
        gens_z80_add_fetch(&port, 0x00, 0x1f, port.ram);
        gens_z80_add_fetch(&port, 0x20, 0x3f, port.ram);
    }
    z80_Reset(&M_Z80);
    gens_z80_reset(&port);
    port.trace = port_log_pc;

    memcpy(port.ram, ref_ram, sizeof(ref_ram));
    memcpy(port_io, ref_io, sizeof(ref_io));
    M_Z80.AF.b.A = port.a = regs[0];
    M_Z80.AF.b.F = port.f = (uint8_t)(regs[1] & 0xd7u);
    M_Z80.AF.b.FXY = port.fxyb = regs[2];
    M_Z80.BC.b.B = port.b = regs[3]; M_Z80.BC.b.C = port.c = regs[4];
    M_Z80.DE.b.D = port.d = regs[5]; M_Z80.DE.b.E = port.e = regs[6];
    M_Z80.HL.b.H = port.h = regs[7]; M_Z80.HL.b.L = port.l = regs[8];
    M_Z80.IX.b.IXH = port.ixh = regs[9]; M_Z80.IX.b.IXL = port.ixl = regs[10];
    M_Z80.IY.b.IYH = port.iyh = regs[11]; M_Z80.IY.b.IYL = port.iyl = regs[12];
    M_Z80.SP.w.SP = port.sp = (uint16_t)(regs[13] | regs[14] << 8);
    M_Z80.AF2.b.A2 = port.a2 = regs[15];
    M_Z80.AF2.b.F2 = port.f2 = (uint8_t)(regs[16] & 0xd7u);
    M_Z80.AF2.b.FXY2 = port.fxyb2 = regs[17];
    M_Z80.BC2.b.B2 = port.b2 = regs[18]; M_Z80.BC2.b.C2 = port.c2 = regs[19];
    M_Z80.DE2.b.D2 = port.d2 = regs[20]; M_Z80.DE2.b.E2 = port.e2 = regs[21];
    M_Z80.HL2.b.H2 = port.h2 = regs[22]; M_Z80.HL2.b.L2 = port.l2 = regs[23];
    M_Z80.I = port.i = (uint8_t)next();
    M_Z80.R.b.R1 = port.r1 = (uint8_t)next();
    M_Z80.IM = port.im = (uint8_t)(next() % 3u);
    if (next() & 1u)
    {
        M_Z80.IFF.b.IFF1 = port.iff1 = 4;
        M_Z80.IFF.b.IFF2 = port.iff2 = 4;
    }
    M_Z80.CycleCnt = port.cycle_cnt = cycles;
    ref_log.count = port_log.count = 0;
    ref_running_irq = port_running_irq = (mode & 4u) != 0u;
    if (mode & 8u)
    {
        uint32_t vector = next() & 255u;
        z80_Interrupt(&M_Z80, vector);
        gens_z80_interrupt(&port, vector);
    }
    else if (mode & 16u)
    {
        z80_NMI(&M_Z80);
        gens_z80_nmi(&port);
    }
}

static int quiet_equal(void)
{
    State r = ref_state(), p = port_state();
    return memcmp(&r, &p, sizeof(r)) == 0 && memcmp(ref_ram, port.ram, 0x2000) == 0 &&
        memcmp(ref_io, port_io, sizeof(ref_io)) == 0;
}

/* Re-runs a scenario one instruction at a time and reports the first diverging instruction. */
static void replay(uint64_t start_seed)
{
    unsigned int step, i;
    uint32_t pc, odo;
    uint8_t bytes[4];
    State before;
    seed = start_seed;
    setup(next());
    for (step = 0; step < 200000u; ++step)
    {
        before = ref_state();
        pc = before.pc;
        for (i = 0; i < 4u; ++i) bytes[i] = code[(pc + i) & 0xffffu];
        odo = M_Z80.CycleCnt + 1u;
        z80_Exec(&M_Z80, odo);
        gens_z80_exec(&port, odo);
        if (!quiet_equal())
        {
            State r = ref_state(), p = port_state();
            printf("  first divergence at step %u, pc %04x bytes %02x %02x %02x %02x\n",
                   step, pc, bytes[0], bytes[1], bytes[2], bytes[3]);
            print_state("prev", &before);
            print_state("ref ", &r);
            print_state("port", &p);
            return;
        }
        if (M_Z80.Status & 2u)
            break;
    }
    printf("  replay did not diverge in %u steps\n", step);
}

int main(int argc, char **argv)
{
    unsigned int scenarios = argc > 1 ? (unsigned int)strtoul(argv[1], 0, 0) : 20000u;
    uint32_t daa_before;
    unsigned int scenario, matched = 0, skipped = 0, failed = 0, escaped = 0, round, i, j, top;
    uint32_t odo, ref_ret, port_ret;
    seed = argc > 2 ? strtoull(argv[2], 0, 0) : UINT64_C(0x5eed2080);

    for (scenario = 0; scenario < scenarios; ++scenario)
    {
        uint64_t scenario_seed = seed;
        unsigned int mode = next();
        int skip = 0;
        setup(mode);
        for (round = 0; round < 1u + mode % 4u && !skip; ++round)
        {
            odo = M_Z80.CycleCnt + 1u + next() % 3000u;
            ref_trace_count = port_trace_count = 0;
            escaped_ram = 0;
            daa_before = port.daa_overflows;
            ref_ret = z80_Exec(&M_Z80, odo);
            port_ret = gens_z80_exec(&port, odo);
#ifdef GENS_Z80_DIFF_NEGATIVE_CONTROL
            if ((port.f & 0x10u) != 0u)
                port.f ^= 0x01u;
#endif
            if (port.daa_overflows != daa_before)
            {
                skip = 1;
                break;
            }
            if (escaped_ram)
            {
                skip = 3;
                break;
            }
            if (port_ret == GENS_Z80_EXEC_UNIMPLEMENTED)
            {
                note_blocked();
                skip = 1;
                break;
            }
            if (!compare(scenario, ref_ret, port_ret))
            {
                uint64_t resume = seed;
                unsigned int k, n = ref_trace_count < port_trace_count ? ref_trace_count : port_trace_count;
                for (k = 0; k < n && ref_trace[k] == port_trace[k]; ++k) {}
                printf("  round %u odo %u, traces %u/%u, first PC difference at %u\n",
                       round, odo, ref_trace_count, port_trace_count, k);
                for (i = k > 6u ? k - 6u : 0u; i < k + 2u && i < n + 1u; ++i)
                    printf("    [%u] ref %04x port %04x  bytes %02x %02x %02x %02x\n", i,
                           i < ref_trace_count ? ref_trace[i] : 0xffffffffu,
                           i < port_trace_count ? port_trace[i] : 0xffffffffu,
                           code[ref_trace[i] & 0xffffu], code[(ref_trace[i] + 1u) & 0xffffu],
                           code[(ref_trace[i] + 2u) & 0xffffu], code[(ref_trace[i] + 3u) & 0xffffu]);
                replay(scenario_seed);
                seed = resume;
                if (++failed >= 5)
                    return 1;
                skip = 2;
                break;
            }
            if (next() % 3u == 0u)
            {
                uint32_t vector = next() & 255u;
                z80_Interrupt(&M_Z80, vector);
                gens_z80_interrupt(&port, vector);
            }
        }
        if (skip == 3) ++escaped;
        else if (skip == 1) ++skipped;
        else if (skip == 0) ++matched;
    }
    printf("z80 diff: %u matched, %u used DAA with H set, %u left Z80 RAM, %u mismatched\n",
           matched, skipped, escaped, failed);
    for (top = 0; top < 12; ++top)
    {
        unsigned int best = 0, bt = 0, bo = 0;
        for (i = 0; i < 6; ++i)
            for (j = 0; j < 256; ++j)
                if (blocked[i][j] > best) { best = blocked[i][j]; bt = i; bo = j; }
        if (best == 0) break;
        printf("  blocked %s%02X: %u\n", (const char *[]){"", "CB ", "ED ", "DD ", "FD ", "DDCB "}[bt], bo, best);
        blocked[bt][bo] = 0;
    }
    return failed != 0;
}
