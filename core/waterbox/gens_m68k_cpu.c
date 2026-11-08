/* C port of the Gens-modified Starscream 68000 core (Main68k Star.c, -hog -name main68k_).
 * The upstream component terms apply to derived portions. See docs/M68K-CPU.md.
 *
 * Each routine mirrors the code the generator emits for its parameters. The original keeps the
 * condition codes in the x86 flag byte (AH, as LAHF stores it) and the V flag in AL. The port keeps
 * the same representation and computes flags with the same x86 instructions, so that undefined
 * x86 flag results match the original on the same host CPU.
 */
#include "gens_m68k_cpu.h"
#include "gens_m68k_tables.h"

#include <string.h>

#if !defined(__i386__) && !defined(__x86_64__)
#error "The Gens 68000 port reproduces x86 flag behavior and requires an x86 host"
#endif

typedef struct Run {
    GensM68k *c;
    uintptr_t esi, ebp;
    int32_t edi;
    uint32_t ebx, ecx, edx;
    uint8_t ah, al;
    int stop;
    uint32_t result;
    const GensM68kObserver *observer;
    uint32_t instruction_pc;
} Run;

static uint16_t dispatch[0x10000];
static int dispatch_ready;

/* ---------------------------------------------------------------------------------------------
 * x86 arithmetic with the original flag results. flags receives EFLAGS bits 0-7 (the LAHF byte)
 * and bit 11 (OF) in bit 8.
 */
static inline uint32_t pack(uintptr_t eflags)
{
    return (uint32_t)(eflags & 0xffu) | (uint32_t)((eflags >> 3) & 0x100u);
}

#define X86_BIN(name, insn, type)                                                              \
    static inline uint32_t name(uint32_t a, uint32_t b, uint32_t carry, uint32_t *flags)       \
    {                                                                                          \
        type x = (type)a, y = (type)b;                                                         \
        uintptr_t f;                                                                           \
        __asm__("btl $0, %k3\n\t" insn " %2, %0\n\tpushf\n\tpop %1"                            \
                : "+q"(x), "=&r"(f) : "q"(y), "r"(carry) : "cc");                              \
        *flags = pack(f);                                                                      \
        return x;                                                                              \
    }
X86_BIN(add8, "addb", uint8_t) X86_BIN(add16, "addw", uint16_t) X86_BIN(add32, "addl", uint32_t)
X86_BIN(adc8, "adcb", uint8_t) X86_BIN(adc16, "adcw", uint16_t) X86_BIN(adc32, "adcl", uint32_t)
X86_BIN(sub8, "subb", uint8_t) X86_BIN(sub16, "subw", uint16_t) X86_BIN(sub32, "subl", uint32_t)
X86_BIN(sbb8, "sbbb", uint8_t) X86_BIN(sbb16, "sbbw", uint16_t) X86_BIN(sbb32, "sbbl", uint32_t)
X86_BIN(and8, "andb", uint8_t) X86_BIN(and16, "andw", uint16_t) X86_BIN(and32, "andl", uint32_t)
X86_BIN(or8, "orb", uint8_t) X86_BIN(or16, "orw", uint16_t) X86_BIN(or32, "orl", uint32_t)
X86_BIN(xor8, "xorb", uint8_t) X86_BIN(xor16, "xorw", uint16_t) X86_BIN(xor32, "xorl", uint32_t)

/* Shifts and rotates start from the given arithmetic flags, because x86 leaves every flag
 * unchanged when the masked count is zero and RCL/RCR consume CF. old uses the packed format. */
#define X86_SHIFT(name, insn, type)                                                            \
    static inline uint32_t name(uint32_t a, uint32_t count, uint32_t old, uint32_t *flags)     \
    {                                                                                          \
        type x = (type)a;                                                                      \
        uintptr_t want = (old & 0xd5u) | ((uintptr_t)(old & 0x100u) << 3), f;                  \
        __asm__("pushf\n\tpop %1\n\tand %3, %1\n\tor %2, %1\n\tpush %1\n\tpopf\n\t"            \
                insn " %%cl, %0\n\tpushf\n\tpop %1"                                            \
                : "+q"(x), "=&r"(f) : "r"(want), "r"(~(uintptr_t)0x8d5u), "c"((uint8_t)count)  \
                : "cc");                                                                       \
        *flags = pack(f);                                                                      \
        return x;                                                                              \
    }
X86_SHIFT(shl8, "shlb", uint8_t) X86_SHIFT(shl16, "shlw", uint16_t) X86_SHIFT(shl32, "shll", uint32_t)
X86_SHIFT(shr8, "shrb", uint8_t) X86_SHIFT(shr16, "shrw", uint16_t) X86_SHIFT(shr32, "shrl", uint32_t)
X86_SHIFT(sar8, "sarb", uint8_t) X86_SHIFT(sar16, "sarw", uint16_t) X86_SHIFT(sar32, "sarl", uint32_t)
X86_SHIFT(rol8, "rolb", uint8_t) X86_SHIFT(rol16, "rolw", uint16_t) X86_SHIFT(rol32, "roll", uint32_t)
X86_SHIFT(ror8, "rorb", uint8_t) X86_SHIFT(ror16, "rorw", uint16_t) X86_SHIFT(ror32, "rorl", uint32_t)
X86_SHIFT(rcl8, "rclb", uint8_t) X86_SHIFT(rcl16, "rclw", uint16_t) X86_SHIFT(rcl32, "rcll", uint32_t)
X86_SHIFT(rcr8, "rcrb", uint8_t) X86_SHIFT(rcr16, "rcrw", uint16_t) X86_SHIFT(rcr32, "rcrl", uint32_t)

typedef uint32_t (*BinOp)(uint32_t, uint32_t, uint32_t, uint32_t *);
typedef uint32_t (*ShiftOp)(uint32_t, uint32_t, uint32_t, uint32_t *);

enum { OP_ADD, OP_ADC, OP_SUB, OP_SBB, OP_AND, OP_OR, OP_XOR, OP_CMP };
static const BinOp binops[8][3] = {
    {add8, add16, add32}, {adc8, adc16, adc32}, {sub8, sub16, sub32}, {sbb8, sbb16, sbb32},
    {and8, and16, and32}, {or8, or16, or32}, {xor8, xor16, xor32}, {sub8, sub16, sub32}
};
enum { SH_SHL, SH_SHR, SH_SAR, SH_ROL, SH_ROR, SH_RCL, SH_RCR };
static const ShiftOp shiftops[7][3] = {
    {shl8, shl16, shl32}, {shr8, shr16, shr32}, {sar8, sar16, sar32}, {rol8, rol16, rol32},
    {ror8, ror16, ror32}, {rcl8, rcl16, rcl32}, {rcr8, rcr16, rcr32}
};

static unsigned int size_index(unsigned int size)
{
    return size == 1u ? 0u : size == 2u ? 1u : 2u;
}

static uint32_t size_mask(unsigned int size)
{
    return size == 1u ? 0xffu : size == 2u ? 0xffffu : 0xffffffffu;
}

/* Runs op on the low size bytes of a. The bytes above size are kept, as an x86 partial
 * register or memory operation keeps them. */
static uint32_t binop(unsigned int op, unsigned int size, uint32_t a, uint32_t b, uint32_t carry,
                      uint32_t *flags)
{
    uint32_t mask = size_mask(size);
    uint32_t r = binops[op][size_index(size)](a & mask, b & mask, carry, flags);
    return (a & ~mask) | (r & mask);
}

static uint32_t shiftop(unsigned int op, unsigned int size, uint32_t a, uint32_t count,
                        uint32_t old, uint32_t *flags)
{
    uint32_t mask = size_mask(size);
    uint32_t r = shiftops[op][size_index(size)](a & mask, count, old, flags);
    return (a & ~mask) | (r & mask);
}

/* LAHF; SETO AL */
static void set_flags(Run *r, uint32_t flags)
{
    r->ah = (uint8_t)flags;
    r->al = (uint8_t)(flags >> 8 & 1u);
}

/* LAHF; MOV AL,0 */
static void set_flags_v0(Run *r, uint32_t flags)
{
    r->ah = (uint8_t)flags;
    r->al = 0;
}

/* TEST reg,reg */
static uint32_t test_flags(unsigned int size, uint32_t value)
{
    uint32_t flags;
    binop(OP_AND, size, value, value, 0, &flags);
    return flags;
}

/* DAA and DAS as the Intel manuals define them. They do not exist in 64-bit mode. OF is left
 * clear. The callers discard it. */
static uint8_t decimal_adjust(uint8_t al, uint32_t *flags, int subtract)
{
    uint8_t old = al;
    unsigned int old_cf = *flags & 1u, cf = 0, af;
    if ((al & 0x0fu) > 9u || (*flags & 0x10u) != 0u)
    {
        if (subtract)
        {
            cf = old_cf | (al < 6u);
            al = (uint8_t)(al - 6u);
        }
        else
        {
            cf = old_cf | (al > 0xf9u);
            al = (uint8_t)(al + 6u);
        }
        af = 1;
    }
    else
        af = 0;
    if (old > 0x99u || old_cf)
    {
        al = (uint8_t)(subtract ? al - 0x60u : al + 0x60u);
        cf = 1;
    }
    else if (!subtract)
        cf = 0;
    *flags = test_flags(1, al) & 0xc4u;
    *flags |= 0x02u | (af ? 0x10u : 0u) | cf;
    return al;
}

/* ---------------------------------------------------------------------------------------------
 * Flag register conversions, as cache_ccr, writeback_ccr, ccr2cl and cl2ccr.
 */
static void load_ccr(Run *r, uint8_t ccr)
{
    r->c->xflag = (uint8_t)(ccr >> 4 & 1u);
    r->ah = (uint8_t)((ccr & 0x0cu) << 4 | (ccr & 1u));
    r->al = (uint8_t)(ccr >> 1 & 1u);
}

static uint8_t current_ccr(const Run *r)
{
    return (uint8_t)((r->c->xflag & 1u) << 4 | (r->ah & 0xc0u) >> 4 | (r->al & 1u) << 1 |
                     (r->ah & 1u));
}

static uint16_t current_sr(const Run *r)
{
    return (uint16_t)((r->c->sr & 0xff00u) | current_ccr(r));
}

/* ---------------------------------------------------------------------------------------------
 * Program fetch.
 */
static uint16_t fetch16(const Run *r, uint32_t offset)
{
    const uint8_t *p = (const uint8_t *)(r->esi + offset);
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t fetch32(const Run *r, uint32_t offset)
{
    return (uint32_t)fetch16(r, offset) << 16 | fetch16(r, offset + 2u);
}

static uint32_t unbased_pc(const Run *r)
{
    return (uint32_t)(r->esi - r->ebp);
}

static void basefunction(Run *r)
{
    GensM68k *c = r->c;
    uint32_t pc = (uint32_t)r->esi, masked = pc & 0xffffffu, garbage = pc & 0xff000000u;
    const GensM68kFetch *region = c->fetch;
    for (;;)
    {
        if (masked >= region->start && masked <= region->end)
            break;
        if (region->start == 0xffffffffu)
        {
            r->ebp = 0;
            c->fetch_region_start = 0xffffffffu;
            c->fetch_region_end = 0;
            r->edi -= (int32_t)c->cycles_needed;
            c->cycles_needed = 0;
            c->execinfo |= 2u;
            return;
        }
        ++region;
    }
    c->fetch_region_start = region->start | garbage;
    c->fetch_region_end = region->end | garbage;
    r->ebp = region->base - garbage;
}

/* esi holds an unbased PC. */
static void cached_rebase(Run *r)
{
    uint32_t pc = (uint32_t)r->esi;
    if (pc < r->c->fetch_region_start || pc > r->c->fetch_region_end)
        basefunction(r);
    r->esi = (uint32_t)r->esi + r->ebp;
}

static void uncached_rebase(Run *r)
{
    basefunction(r);
    r->esi = (uint32_t)r->esi + r->ebp;
}

/* ---------------------------------------------------------------------------------------------
 * Data memory, as readmemory*, readmemorydec*, writememory* and writememorydec*.
 */
static uint8_t ram8(const GensM68k *c, uint32_t offset)
{
    return c->ram[offset];
}

/* x86 little-endian loads from the word-swapped RAM, including unaligned offsets. */
static uint16_t ram16(const GensM68k *c, uint32_t offset)
{
    return (uint16_t)(c->ram[offset] | c->ram[offset + 1u] << 8);
}

static void put16(GensM68k *c, uint32_t offset, uint32_t value)
{
    c->ram[offset] = (uint8_t)value;
    c->ram[offset + 1u] = (uint8_t)(value >> 8);
}

static void airlock_exit(Run *r)
{
    r->c->io_cycle_counter = r->edi;
    r->c->io_fetchbase = r->ebp;
    r->c->io_fetchbased_pc = r->esi;
}

static void airlock_enter(Run *r)
{
    r->edi = r->c->io_cycle_counter;
    r->ebp = r->c->io_fetchbase;
    r->esi = r->c->io_fetchbased_pc;
}

static uint32_t hook_pc(const Run *r)
{
    return (uint32_t)(r->esi - r->ebp) - 2u;
}

static void observed(Run *r, unsigned int kind, uint32_t address, uint32_t value, unsigned int size)
{
    const GensM68kObserver *o = r->observer;
    if (o != 0 && o->notify[kind] != 0)
        o->notify[kind](o->opaque, kind, address & 0xffffffu, value, size,
                        r->instruction_pc, current_sr(r));
}

/* Starscream's write paths emit `sub [_hook_pc],byte 2`, which NASM assembles as a byte-sized
 * subtraction. Only the low byte is decremented and no borrow reaches the upper bytes. */
static uint32_t write_hook_pc(const Run *r)
{
    uint32_t pc = (uint32_t)(r->esi - r->ebp);
    return (pc & 0xffffff00u) | ((pc - 2u) & 0xffu);
}

static void read_hook(Run *r, unsigned int size)
{
    GensM68k *c = r->c;
    if (c->hooks.read != 0)
        c->hooks.read(c->hooks.opaque, size, hook_pc(r), r->edx & 0xffffffu, r->ecx);
}

static void read8(Run *r)
{
    GensM68k *c = r->c;
    uint32_t address = r->edx & 0xffffffu;
    c->access_address = r->edx;
    r->ecx = 0;
    if (address >= 0xe00000u)
        r->ecx = ram8(c, (address & 0xffffu) ^ 1u);
    else
    {
        airlock_exit(r);
        r->ecx = c->bus.read_byte(c->bus.opaque, address);
        airlock_enter(r);
    }
    r->edx = c->access_address;
    observed(r, 1, address, r->ecx & 0xffu, 1);
    read_hook(r, 1);
}

static void read16(Run *r)
{
    GensM68k *c = r->c;
    uint32_t address = r->edx & 0xffffffu;
    c->access_address = r->edx;
    r->ecx = 0;
    if (address >= 0xe00000u)
        r->ecx = ram16(c, address & 0xffffu);
    else
    {
        airlock_exit(r);
        r->ecx = c->bus.read_word(c->bus.opaque, address);
        airlock_enter(r);
    }
    r->edx = c->access_address;
    observed(r, 1, address, r->ecx & 0xffffu, 2);
    read_hook(r, 2);
}

static void read32(Run *r, int decrement)
{
    GensM68k *c = r->c;
    uint32_t address = r->edx & 0xffffffu, low;
    c->access_address = r->edx;
    if (address >= 0xe00000u)
    {
        uint32_t value;
        if (decrement)
        {
            low = ram16(c, (address & 0xffffu) + 2u);
            observed(r, 1, address + 2u, low, 2);
            value = ram16(c, address & 0xffffu);
            observed(r, 1, address, value, 2);
            value |= low << 16;
        }
        else
        {
            value = ram16(c, address & 0xffffu);
            observed(r, 1, address, value, 2);
            low = ram16(c, (address & 0xffffu) + 2u);
            observed(r, 1, address + 2u, low, 2);
            value |= low << 16;
        }
        r->ecx = value << 16 | value >> 16;
    }
    else if (!decrement)
    {
        airlock_exit(r);
        r->ecx = (r->ecx & 0xffff0000u) | c->bus.read_word(c->bus.opaque, address);
        observed(r, 1, address, r->ecx & 0xffffu, 2);
        r->ecx <<= 16;
        r->ecx = (r->ecx & 0xffff0000u) | c->bus.read_word(c->bus.opaque, address + 2u);
        observed(r, 1, address + 2u, r->ecx & 0xffffu, 2);
        airlock_enter(r);
    }
    else
    {
        airlock_exit(r);
        low = c->bus.read_word(c->bus.opaque, address + 2u);
        observed(r, 1, address + 2u, low, 2);
        r->ecx = low | (uint32_t)c->bus.read_word(c->bus.opaque, address) << 16;
        observed(r, 1, address, r->ecx >> 16, 2);
        airlock_enter(r);
    }
    r->edx = c->access_address;
    read_hook(r, 4);
}

static void write_hook(Run *r, unsigned int size, uint32_t pc, uint32_t address, uint32_t value)
{
    GensM68k *c = r->c;
    if (c->hooks.write != 0)
        c->hooks.write(c->hooks.opaque, size, pc, address, value);
}

static void write8(Run *r)
{
    GensM68k *c = r->c;
    uint32_t address = r->edx & 0xffffffu, pc = write_hook_pc(r), value = r->ecx;
    c->access_address = r->edx;
    if (address >= 0xe00000u)
        c->ram[(address ^ 1u) & 0xffffu] = (uint8_t)r->ecx;
    else
    {
        airlock_exit(r);
        c->bus.write_byte(c->bus.opaque, address, (uint8_t)r->ecx);
        airlock_enter(r);
    }
    r->edx = c->access_address;
    observed(r, 2, address, value & 0xffu, 1);
    write_hook(r, 1, pc, address, value);
}

static void write16(Run *r)
{
    GensM68k *c = r->c;
    uint32_t address = r->edx & 0xffffffu, pc = write_hook_pc(r), value = r->ecx;
    c->access_address = r->edx;
    if (address >= 0xe00000u)
        put16(c, address & 0xffffu, r->ecx);
    else
    {
        airlock_exit(r);
        c->bus.write_word(c->bus.opaque, address, (uint16_t)r->ecx);
        airlock_enter(r);
    }
    r->edx = c->access_address;
    observed(r, 2, address, value & 0xffffu, 2);
    write_hook(r, 2, pc, address, value);
}

static void write32(Run *r, int decrement)
{
    GensM68k *c = r->c;
    uint32_t address = r->edx & 0xffffffu, pc = write_hook_pc(r), value = r->ecx;
    c->access_address = r->edx;
    if (address >= 0xe00000u)
    {
        if (decrement)
        {
            put16(c, (address & 0xffffu) + 2u, r->ecx);
            observed(r, 2, address + 2u, value & 0xffffu, 2);
            put16(c, address & 0xffffu, r->ecx >> 16);
            observed(r, 2, address, value >> 16, 2);
        }
        else
        {
            put16(c, address & 0xffffu, r->ecx >> 16);
            observed(r, 2, address, value >> 16, 2);
            put16(c, (address & 0xffffu) + 2u, r->ecx);
            observed(r, 2, address + 2u, value & 0xffffu, 2);
        }
    }
    else if (!decrement)
    {
        airlock_exit(r);
        c->bus.write_word(c->bus.opaque, address, (uint16_t)(r->ecx >> 16));
        observed(r, 2, address, value >> 16, 2);
        c->bus.write_word(c->bus.opaque, address + 2u, (uint16_t)r->ecx);
        observed(r, 2, address + 2u, value & 0xffffu, 2);
        airlock_enter(r);
    }
    else
    {
        airlock_exit(r);
        c->bus.write_word(c->bus.opaque, address + 2u, (uint16_t)r->ecx);
        observed(r, 2, address + 2u, value & 0xffffu, 2);
        c->bus.write_word(c->bus.opaque, address, (uint16_t)(r->ecx >> 16));
        observed(r, 2, address, value >> 16, 2);
        airlock_enter(r);
        /* writememorydecdword returns with ECX rotated by 16 on this path. Callers that set
         * flags from ECX afterwards (MOVE.L to -(An)) see the swapped value. */
        r->ecx = r->ecx << 16 | r->ecx >> 16;
    }
    r->edx = c->access_address;
    write_hook(r, 4, pc, address, value);
}

static void read_mem(Run *r, unsigned int size, int decrement)
{
    if (size == 1u) read8(r);
    else if (size == 2u) read16(r);
    else read32(r, decrement);
}

static void write_mem(Run *r, unsigned int size, int decrement)
{
    if (size == 1u) write8(r);
    else if (size == 2u) write16(r);
    else write32(r, decrement);
}

/* ---------------------------------------------------------------------------------------------
 * Supervisor state and exceptions.
 */
static void swap_stacks(GensM68k *c)
{
    uint32_t value = c->areg[7];
    c->areg[7] = c->asp;
    c->asp = value;
}

static void supervisor(Run *r)
{
    GensM68k *c = r->c;
    if ((c->sr & 0x2000u) != 0u)
        return;
    swap_stacks(c);
    c->fetch = c->s_fetch;
    c->sr |= 0x2000u;
}

/* cx2sr: value in r->ecx. */
static void set_sr(Run *r)
{
    GensM68k *c = r->c;
    uint32_t value = r->ecx & 0xffffu;
    if (((value ^ c->sr) & 0x2000u) != 0u)
    {
        swap_stacks(c);
        c->fetch = (value & 0x2000u) != 0u ? c->s_fetch : c->u_fetch;
    }
    c->sr = (uint16_t)((c->sr & 0x00ffu) | (value & 0xa700u));
    load_ccr(r, (uint8_t)value);
}

/* group_1_exception / group_2_exception: vector address in r->edx. Leaves the unbased new PC
 * in esi. */
static void group_exception(Run *r)
{
    GensM68k *c = r->c;
    uint32_t target, old_sr;
    c->interrupts[0] &= 0xefu;
    read32(r, 0);
    target = r->ecx;
    /* sr2cx only replaces CX, so the pushed value keeps the target's upper half. */
    old_sr = (target & 0xffff0000u) | current_sr(r);
    supervisor(r);
    c->sr &= 0x27ffu;
    c->trace_trickybit = 0;
    r->ecx = unbased_pc(r);
    r->edx = c->areg[7] - 4u;
    write32(r, 0);
    r->ecx = old_sr;
    r->edx -= 2u;
    write16(r);
    c->areg[7] = r->edx;
    r->esi = target;
}

/* flush_interrupts */
static void flush_interrupts(Run *r)
{
    GensM68k *c = r->c;
    uint8_t level;
    r->esi = unbased_pc(r);
    r->ebp = 0;
    level = (uint8_t)(c->interrupts[0] & 7u);
    if (level == 0u)
        return;
    r->edx = (level + 0x18u) * 4u;
    group_exception(r);
    level = (uint8_t)(c->interrupts[0] & 7u);
    r->edi -= 44;
    c->sr = (uint16_t)((c->sr & 0xf8ffu) | (uint16_t)level << 8);
    c->interrupts[0] = c->bus.int_ack != 0 ? c->bus.int_ack(c->bus.opaque, level) : 0;
}

/* ---------------------------------------------------------------------------------------------
 * Effective addresses, as ea_step_precalc, ea_step_read, ea_step_write and ea_step_postcalc.
 * reg -1 means the opcode register field in EBX.
 */
enum {
    M_DREG = GENS_M68K_DREG, M_AREG, M_AIND, M_AINC, M_ADEC, M_ADSP, M_AXDP, M_ABSW, M_ABSL,
    M_PCDP, M_PCXD, M_IMMD
};

static uint32_t *reg_slot(GensM68k *c, unsigned int index)
{
    return index < 8u ? &c->dreg[index] : &c->areg[index - 8u];
}

static unsigned int ea_reg(const Run *r, int reg)
{
    return reg < 0 ? (r->ebx & 0xffffu) : (unsigned int)reg;
}

/* decode_ext: brief extension word index plus displacement, ECX preserved. */
static uint32_t decode_ext(Run *r)
{
    uint32_t word = fetch16(r, 0), value;
    r->esi += 2u;
    value = *reg_slot(r->c, word >> 12);
    if ((word & 0x800u) == 0u)
        value = (uint32_t)(int32_t)(int16_t)value;
    return value + (uint32_t)(int32_t)(int8_t)word;
}

static int32_t byte_step(const Run *r, int reg)
{
    if (reg < 0)
        return (r->ebx & 0xffu) < 7u ? 1 : 2;
    return reg == 7 ? 2 : 1;
}

static void ea_precalc(Run *r, unsigned int size, int mode, int reg)
{
    GensM68k *c = r->c;
    unsigned int n = ea_reg(r, reg);
    switch (mode)
    {
    case M_AIND: case M_AINC: case M_ADEC:
        r->edx = c->areg[n];
        if (mode == M_ADEC)
            r->edx -= size == 1u ? (uint32_t)byte_step(r, reg) : size;
        break;
    case M_ADSP:
        r->edx = (uint32_t)(int32_t)(int16_t)fetch16(r, 0);
        r->esi += 2u;
        r->edx += c->areg[n];
        break;
    case M_AXDP:
        r->edx = decode_ext(r) + c->areg[n];
        break;
    case M_ABSW:
        r->edx = (uint32_t)(int32_t)(int16_t)fetch16(r, 0);
        r->esi += 2u;
        break;
    case M_ABSL:
        r->edx = fetch32(r, 0);
        r->esi += 4u;
        break;
    case M_PCDP:
        r->edx = (uint32_t)(int32_t)(int16_t)fetch16(r, 0) + unbased_pc(r);
        r->esi += 2u;
        break;
    case M_PCXD:
        r->edx = decode_ext(r);
        r->edx += unbased_pc(r) - 2u;
        break;
    default:
        break;
    }
}

static void ea_read(Run *r, unsigned int size, int mode, int reg)
{
    GensM68k *c = r->c;
    unsigned int n = ea_reg(r, reg);
    switch (mode)
    {
    case M_DREG: r->ecx = c->dreg[n]; break;
    case M_AREG: r->ecx = c->areg[n]; break;
    case M_ADEC: read_mem(r, size, 1); break;
    case M_IMMD:
        if (size == 4u)
        {
            r->ecx = fetch32(r, 0);
            r->esi += 4u;
        }
        else
        {
            r->ecx = (r->ecx & 0xffff0000u) | fetch16(r, 0);
            r->esi += 2u;
        }
        break;
    default: read_mem(r, size, 0); break;
    }
}

static void ea_read_signword(Run *r, int mode, int reg)
{
    GensM68k *c = r->c;
    unsigned int n = ea_reg(r, reg);
    switch (mode)
    {
    case M_DREG: r->ecx = (uint32_t)(int32_t)(int16_t)c->dreg[n]; break;
    case M_AREG: r->ecx = (uint32_t)(int32_t)(int16_t)c->areg[n]; break;
    case M_IMMD:
        r->ecx = (uint32_t)(int32_t)(int16_t)fetch16(r, 0);
        r->esi += 2u;
        break;
    default:
        read16(r);
        r->ecx = (uint32_t)(int32_t)(int16_t)r->ecx;
        break;
    }
}

static void ea_write(Run *r, unsigned int size, int mode, int reg)
{
    GensM68k *c = r->c;
    unsigned int n = ea_reg(r, reg);
    uint32_t mask = size_mask(size);
    if (mode == M_DREG)
        c->dreg[n] = (c->dreg[n] & ~mask) | (r->ecx & mask);
    else
        write_mem(r, size, mode == M_ADEC);
}

static void ea_postcalc(Run *r, unsigned int size, int mode, int reg)
{
    unsigned int n = ea_reg(r, reg);
    if (mode == M_AINC)
        r->edx += size == 1u ? (uint32_t)byte_step(r, reg) : size;
    if (mode == M_AINC || mode == M_ADEC)
        r->c->areg[n] = r->edx;
}

static void ea_load(Run *r, unsigned int size, int mode, int reg)
{
    ea_precalc(r, size, mode, reg);
    ea_read(r, size, mode, reg);
    ea_postcalc(r, size, mode, reg);
}

static void ea_load_signword(Run *r, int mode, int reg)
{
    ea_precalc(r, 2, mode, reg);
    ea_read_signword(r, mode, reg);
    ea_postcalc(r, 2, mode, reg);
}

static void ea_store(Run *r, unsigned int size, int mode, int reg)
{
    ea_precalc(r, size, mode, reg);
    ea_write(r, size, mode, reg);
    ea_postcalc(r, size, mode, reg);
}

static void ea_rmw_load(Run *r, unsigned int size, int mode, int reg)
{
    ea_precalc(r, size, mode, reg);
    ea_read(r, size, mode, reg);
}

static void ea_rmw_store(Run *r, unsigned int size, int mode, int reg)
{
    ea_write(r, size, mode, reg);
    ea_postcalc(r, size, mode, reg);
}

static int ea_cycles(unsigned int size, int mode)
{
    int l = size == 4u ? 4 : 0;
    switch (mode)
    {
    case M_AIND: case M_AINC: return l + 4;
    case M_ADEC: return l + 6;
    case M_ADSP: case M_ABSW: case M_PCDP: return l + 8;
    case M_AXDP: case M_PCXD: return l + 10;
    case M_ABSL: return l + 12;
    case M_IMMD: return l + 4;
    default: return 0;
    }
}

static void selective_usereg(Run *r, int mode)
{
    if (mode <= M_AXDP)
        r->ebx &= 7u;
}

/* ---------------------------------------------------------------------------------------------
 * Conditions (getcondition) and helpers.
 */
static int condition(const Run *r, int cc)
{
    unsigned int n = (r->ah >> 7) & 1u, v = r->al & 1u;
    switch (cc)
    {
    case 0x2: return (r->ah & 0x41u) == 0u;
    case 0x3: return (r->ah & 0x41u) != 0u;
    case 0x4: return (r->ah & 1u) == 0u;
    case 0x5: return (r->ah & 1u) != 0u;
    case 0x6: return (r->ah & 0x40u) == 0u;
    case 0x7: return (r->ah & 0x40u) != 0u;
    case 0x8: return v == 0u;
    case 0x9: return v != 0u;
    case 0xa: return n == 0u;
    case 0xb: return n != 0u;
    case 0xc: return (n ^ v) == 0u;
    case 0xd: return (n ^ v) != 0u;
    case 0xe: return ((n ^ v) | ((r->ah >> 6) & 1u)) == 0u;
    case 0xf: return ((n ^ v) | ((r->ah >> 6) & 1u)) != 0u;
    default: return 1;
    }
}

/* flag_to_z: copy a x86 condition into the 68K Z flag. */
static void flag_to_z(Run *r, int set)
{
    if (set)
        r->ah |= 0x40u;
    else
        r->ah &= 0xbfu;
}

/* adjzero: keep the previous Z flag when the result is zero. */
static void adjzero(Run *r, uint32_t flags, uint8_t old)
{
    if ((flags & 0x40u) != 0u)
        r->ah &= (uint8_t)(old | 0xbfu);
}

/* Routine endings. Plain values are ret_timing cycles. */
#define CHECKPOINT 0x40000000
#define INVALID (-1)

static int exception(Run *r, uint32_t vector, int back, int cycles)
{
    if (back)
        r->esi -= 2u;
    r->edx = vector;
    group_exception(r);
    cached_rebase(r);
    return cycles;
}

static int privilege_violation(Run *r)
{
    return exception(r, 0x20u, 1, 34);
}

static int invalid_instruction(Run *r)
{
    r->esi -= 2u;
    r->result = (uint32_t)(r->esi - r->ebp) & 0xffffffu;
    r->stop = 1;
    return INVALID;
}
/* ---------------------------------------------------------------------------------------------
 * Instruction routines. p holds the generator parameters of the routine.
 */
typedef const GensM68kRoutine Params;

static int i_move(Run *r, Params *p)
{
    unsigned int size = (unsigned int)p->size;
    int cycles, l = size == 4u ? 4 : 0;
    selective_usereg(r, p->eamode);
    ea_load(r, size, p->eamode, -1);
    ea_store(r, size, p->destmode, p->reg);
    set_flags_v0(r, test_flags(size, r->ecx));
    cycles = 4 + ea_cycles(size, p->eamode);
    switch (p->destmode)
    {
    case M_AIND: case M_AINC: case M_ADEC: cycles += l + 4; break;
    case M_ADSP: case M_ABSW: cycles += l + 8; break;
    case M_AXDP: cycles += l + 10; break;
    case M_ABSL: cycles += l + 12; break;
    default: break;
    }
    return cycles;
}

static int i_moveq(Run *r, Params *p)
{
    r->ecx = (uint32_t)(int32_t)(int8_t)r->ebx;
    r->c->dreg[p->reg] = r->ecx;
    set_flags_v0(r, test_flags(1, r->ecx));
    return 4;
}

static void load_signed(Run *r, Params *p)
{
    selective_usereg(r, p->eamode);
    if (p->size < 4)
        ea_load_signword(r, p->eamode, -1);
    else
        ea_load(r, 4, p->eamode, -1);
}

static int i_movea(Run *r, Params *p)
{
    load_signed(r, p);
    r->c->areg[p->reg] = r->ecx;
    return 4 + ea_cycles((unsigned int)p->size, p->eamode);
}

static int addsuba(Run *r, Params *p, int subtract)
{
    int base;
    load_signed(r, p);
    r->c->areg[p->reg] = subtract ? r->c->areg[p->reg] - r->ecx : r->c->areg[p->reg] + r->ecx;
    if (p->size == 4)
    {
        base = 6;
        if (p->eamode == M_AREG || p->eamode == M_DREG || p->eamode == M_IMMD)
            base += 2;
    }
    else
        base = 8;
    return base + ea_cycles((unsigned int)p->size, p->eamode);
}

static int i_cmpa(Run *r, Params *p)
{
    uint32_t flags;
    load_signed(r, p);
    binop(OP_CMP, 4, r->c->areg[p->reg], r->ecx, 0, &flags);
    set_flags(r, flags);
    return 6 + ea_cycles((unsigned int)p->size, p->eamode);
}

static int i_move_to_sr(Run *r, Params *p)
{
    if ((r->c->sr & 0x2000u) == 0u)
        return privilege_violation(r);
    selective_usereg(r, p->eamode);
    ea_load(r, 2, p->eamode, -1);
    set_sr(r);
    return CHECKPOINT | (12 + ea_cycles(2, p->eamode));
}

static int i_move_to_ccr(Run *r, Params *p)
{
    selective_usereg(r, p->eamode);
    ea_load(r, 2, p->eamode, -1);
    load_ccr(r, (uint8_t)r->ecx);
    return 12 + ea_cycles(2, p->eamode);
}

static int i_move_from_sr(Run *r, Params *p)
{
    int cycles;
    selective_usereg(r, p->eamode);
    r->ecx = (r->ecx & 0xffff0000u) | current_sr(r);
    ea_store(r, 2, p->eamode, -1);
    cycles = 8 + ea_cycles(2, p->eamode);
    if (p->eamode == M_DREG || p->eamode == M_AREG)
        cycles -= 2;
    return cycles;
}

static int op_to_ccr(Run *r, unsigned int op)
{
    uint32_t flags, value = current_ccr(r);
    value = binop(op, 1, value, fetch16(r, 0), 0, &flags);
    r->esi += 2u;
    r->ecx = (r->ecx & 0xffff0000u) | (value & 0xffu);
    load_ccr(r, (uint8_t)value);
    return 20;
}

static int op_to_sr(Run *r, unsigned int op)
{
    uint32_t flags;
    if ((r->c->sr & 0x2000u) == 0u)
        return privilege_violation(r);
    r->ecx = (r->ecx & 0xffff0000u) | current_sr(r);
    r->ecx = binop(op, 2, r->ecx, fetch16(r, 0), 0, &flags);
    r->esi += 2u;
    set_sr(r);
    return CHECKPOINT | 20;
}

static int i_clr(Run *r, Params *p)
{
    unsigned int size = (unsigned int)p->size;
    int cycles = ea_cycles(size, p->eamode);
    selective_usereg(r, p->eamode);
    r->ecx = 0;
    ea_store(r, size, p->eamode, -1);
    if (p->eamode == M_DREG || p->eamode == M_AREG)
        cycles += size == 4u ? 8 : 4;
    else
        cycles += size == 4u ? 12 : 6;
    r->ah = 0x40;
    r->al = 0;
    return cycles;
}

static int i_tst(Run *r, Params *p)
{
    unsigned int size = (unsigned int)p->size;
    selective_usereg(r, p->eamode);
    ea_load(r, size, p->eamode, -1);
    set_flags_v0(r, test_flags(size, r->ecx));
    return 4 + ea_cycles(size, p->eamode);
}

static const uint32_t quickvalue[8] = {8, 1, 2, 3, 4, 5, 6, 7};

static int op_quick(Run *r, Params *p, unsigned int op)
{
    unsigned int size = (unsigned int)p->size;
    uint32_t flags, q = quickvalue[p->qv];
    int cycles;
    selective_usereg(r, p->eamode);
    if (p->eamode == M_DREG)
    {
        uint32_t *d = &r->c->dreg[r->ebx];
        *d = binop(op, size, *d, q, 0, &flags);
        set_flags(r, flags);
        r->c->xflag = (uint8_t)(flags & 1u);
        cycles = 4;
    }
    else if (p->eamode == M_AREG)
    {
        uint32_t *a = &r->c->areg[r->ebx];
        *a = op == OP_ADD ? *a + q : *a - q;
        cycles = 4;
        if (size == 2u && op == OP_SUB)
            cycles += 4;
    }
    else
    {
        ea_rmw_load(r, size, p->eamode, -1);
        r->ecx = binop(op, size, r->ecx, q, 0, &flags);
        set_flags(r, flags);
        r->c->xflag = (uint8_t)(flags & 1u);
        ea_rmw_store(r, size, p->eamode, -1);
        cycles = 8 + ea_cycles(size, p->eamode);
    }
    if (size == 4u)
        cycles += 4;
    return cycles;
}

static int op_to_dn(Run *r, Params *p, unsigned int op, int affectx, int logical)
{
    unsigned int size = (unsigned int)p->size;
    uint32_t flags, *d = &r->c->dreg[p->reg], value;
    int cycles;
    selective_usereg(r, p->eamode);
    ea_load(r, size, p->eamode, -1);
    value = binop(op, size, *d, r->ecx, 0, &flags);
    if (op != OP_CMP)
        *d = value;
    if (logical) set_flags_v0(r, flags);
    else set_flags(r, flags);
    if (affectx)
        r->c->xflag = (uint8_t)(flags & 1u);
    cycles = 4 + ea_cycles(size, p->eamode);
    if (size == 4u)
    {
        cycles += 2;
        if (op != OP_CMP && (p->eamode == M_AREG || p->eamode == M_DREG || p->eamode == M_IMMD))
            cycles += 2;
    }
    return cycles;
}

static int op_to_ea(Run *r, Params *p, unsigned int op, int logical)
{
    unsigned int size = (unsigned int)p->size;
    uint32_t flags;
    int cycles;
    selective_usereg(r, p->eamode);
    ea_rmw_load(r, size, p->eamode, -1);
    r->ecx = binop(op, size, r->ecx, r->c->dreg[p->reg], 0, &flags);
    if (logical) set_flags_v0(r, flags);
    else
    {
        set_flags(r, flags);
        r->c->xflag = (uint8_t)(flags & 1u);
    }
    ea_rmw_store(r, size, p->eamode, -1);
    cycles = 8 + ea_cycles(size, p->eamode);
    if (size == 4u)
        cycles += 4;
    if (op == OP_XOR && p->eamode == M_DREG)
        cycles -= 4;
    return cycles;
}

static int im_to_ea(Run *r, Params *p, unsigned int op, int wback, int affectx, int logical,
                    int c1, int c2, int c3, int c4)
{
    unsigned int size = (unsigned int)p->size;
    uint32_t flags, immediate, value;
    int cycles;
    selective_usereg(r, p->eamode);
    if (size == 4u)
    {
        r->ecx = fetch32(r, 0);
        r->esi += 4u;
    }
    else
    {
        r->ecx = (r->ecx & 0xffff0000u) | fetch16(r, 0);
        r->esi += 2u;
    }
    if (p->eamode == M_DREG)
    {
        uint32_t *d = &r->c->dreg[r->ebx];
        value = binop(op, size, *d, r->ecx, 0, &flags);
        if (op != OP_CMP)
            *d = value;
        cycles = size < 4u ? c1 : c2;
    }
    else
    {
        immediate = r->ecx;
        if (wback) ea_rmw_load(r, size, p->eamode, -1);
        else ea_load(r, size, p->eamode, -1);
        value = binop(op, size, r->ecx, immediate, 0, &flags);
        if (op != OP_CMP)
            r->ecx = value;
        if (logical) set_flags_v0(r, flags);
        else set_flags(r, flags);
        if (affectx)
            r->c->xflag = (uint8_t)(flags & 1u);
        if (wback)
            ea_rmw_store(r, size, p->eamode, -1);
        return (size < 4u ? c3 : c4) + ea_cycles(size, p->eamode);
    }
    if (logical) set_flags_v0(r, flags);
    else set_flags(r, flags);
    if (affectx)
        r->c->xflag = (uint8_t)(flags & 1u);
    return cycles;
}
/* flick_reg: kind 0 LSx, 1 ASx, 2 ROx, 3 ROXx. */
static int flick_reg(Run *r, Params *p, int kind)
{
    GensM68k *c = r->c;
    unsigned int size = (unsigned int)p->size;
    int needxf = kind == 3, affectx = kind != 2, rotate = kind >= 2;
    int left = p->dr == 1, asl = kind == 1 && left, regcount = p->ir == 1;
    uint32_t count, flags, *d = &c->dreg[r->ebx & 7u];
    int cycles = size == 4u ? 8 : 6;
    r->ebx &= 7u;
    d = &c->dreg[r->ebx];
    if (regcount)
    {
        r->ecx = c->dreg[p->reg] & 63u;
        if (r->ecx == 0u)
        {
            r->ecx = *d;
            set_flags_v0(r, test_flags(size, r->ecx));
            if (needxf)
                r->ah = (uint8_t)((r->ah & 0xfeu) | c->xflag);
            return cycles;
        }
        r->edi -= 2 * (int32_t)r->ecx;
        count = r->ecx;
    }
    else
    {
        count = quickvalue[p->reg];
        cycles += 2 * (int)count;
    }
    if (asl)
    {
        if (!regcount && count == 1u)
        {
            *d = shiftop(SH_SHL, size, *d, 1, 0, &flags);
            set_flags(r, flags);
            if (affectx)
                c->xflag = (uint8_t)(flags & 1u);
            return cycles;
        }
        r->edx = *d;
        r->al = 0;
        for (;;)
        {
            r->edx = binop(OP_ADD, size, r->edx, r->edx, 0, &flags);
            if (regcount || count == 1u)
                r->ah = (uint8_t)flags;
            r->ecx = (r->ecx & 0xffff00ffu) | (flags & 0x100u);
            r->al |= (uint8_t)(flags >> 8 & 1u);
            if (regcount)
            {
                r->ecx = (r->ecx & 0xffffff00u) | ((r->ecx - 1u) & 0xffu);
                if ((r->ecx & 0xffu) == 0u)
                    break;
            }
            else if (--count == 0u)
                break;
        }
        *d = (*d & ~size_mask(size)) | (r->edx & size_mask(size));
        if (affectx)
        {
            r->ecx = (r->ecx & 0xffffff00u) | (r->ah & 1u);
            c->xflag = (uint8_t)(r->ah & 1u);
        }
        return cycles;
    }
    if (rotate)
    {
        unsigned int op = needxf ? (left ? SH_RCL : SH_RCR) : (left ? SH_ROL : SH_ROR);
        uint32_t carry;
        r->edx = *d;
        r->al = needxf ? c->xflag : 0u;
        flags = needxf ? (r->al & 1u) : 0u;
        if (needxf)
            r->al = (uint8_t)(r->al >> 1);
        if (regcount && (r->ecx & 0xffu) >= 32u)
        {
            r->ecx -= 31u;
            r->edx = shiftop(op, size, r->edx, 31, flags, &flags);
            r->edx = shiftop(op, size, r->edx, r->ecx & 0xffu, flags, &flags);
        }
        else
            r->edx = shiftop(op, size, r->edx, count, flags, &flags);
        carry = flags & 1u;
        r->al = (uint8_t)(r->al * 2u + carry);
        r->ah = (uint8_t)(test_flags(size, r->edx) | r->al);
        if (affectx)
            c->xflag = r->al;
        r->al = 0;
        *d = (*d & ~size_mask(size)) | (r->edx & size_mask(size));
        return cycles;
    }
    {
        unsigned int op = kind == 1 ? SH_SAR : left ? SH_SHL : SH_SHR;
        if (regcount && (r->ecx & 0xffu) >= 32u)
        {
            r->ecx -= 31u;
            *d = shiftop(op, size, *d, 31, 0, &flags);
        }
        else
            *d = shiftop(op, size, *d, count, 0, &flags);
        r->ah = (uint8_t)flags;
        r->al = 0;
        if (affectx)
            c->xflag = (uint8_t)(flags & 1u);
    }
    return cycles;
}

static int flick_mem(Run *r, Params *p, int kind)
{
    int needxf = kind == 3, affectx = kind != 2, rotate = kind >= 2, left = p->dr == 1;
    int vf = kind == 1 && left;
    uint32_t flags = 0;
    unsigned int op;
    selective_usereg(r, p->eamode);
    ea_rmw_load(r, 2, p->eamode, -1);
    if (needxf)
    {
        flags = r->c->xflag & 1u;
        r->al = (uint8_t)(r->c->xflag >> 1);
    }
    else if (rotate)
        r->al = 0;
    if (rotate)
        op = needxf ? (left ? SH_RCL : SH_RCR) : (left ? SH_ROL : SH_ROR);
    else
        op = kind == 1 ? (left ? SH_SHL : SH_SAR) : (left ? SH_SHL : SH_SHR);
    r->ecx = shiftop(op, 2, r->ecx, 1, flags, &flags);
    if (rotate)
    {
        r->al = (uint8_t)(r->al * 2u + (flags & 1u));
        r->ah = (uint8_t)(test_flags(2, r->ecx) | r->al);
        if (affectx)
            r->c->xflag = r->al;
        r->al = 0;
    }
    else
    {
        if (vf) set_flags(r, flags);
        else set_flags_v0(r, flags);
        if (affectx)
            r->c->xflag = (uint8_t)(flags & 1u);
    }
    ea_rmw_store(r, 2, p->eamode, -1);
    return 8 + ea_cycles(2, p->eamode);
}

/* r_bra_b and r_bra_w */
static int bra_b(Run *r)
{
    r->ebx = (uint32_t)(int32_t)(int8_t)r->ebx;
    r->esi += (uintptr_t)(intptr_t)(int32_t)r->ebx;
    r->ebx = 0;
    return 10;
}

static int bra_w(Run *r)
{
    r->ebx = (uint32_t)(int32_t)(int16_t)fetch16(r, 0);
    r->esi += (uintptr_t)(intptr_t)(int32_t)r->ebx;
    r->ebx = 0;
    return 10;
}

static void push_long(Run *r)
{
    ea_store(r, 4, M_ADEC, 7);
}

static int i_bsr_b(Run *r)
{
    r->ebx = (uint32_t)(int32_t)(int8_t)r->ebx;
    r->ecx = unbased_pc(r);
    r->esi += (uintptr_t)(intptr_t)(int32_t)r->ebx;
    r->ebx = 0;
    push_long(r);
    return 18;
}

static int i_bsr_w(Run *r)
{
    r->ebx = (uint32_t)(int32_t)(int16_t)fetch16(r, 0);
    r->ecx = unbased_pc(r) + 2u;
    r->esi += (uintptr_t)(intptr_t)(int32_t)r->ebx;
    r->ebx = 0;
    push_long(r);
    return 18;
}

/* r_dbra for the 68000 */
static int dbra(Run *r)
{
    uint32_t *d;
    r->ebx &= 7u;
    d = &r->c->dreg[r->ebx];
    if ((*d & 0xffffu) != 0u)
    {
        *d = (*d & 0xffff0000u) | ((*d - 1u) & 0xffffu);
        return bra_w(r);
    }
    *d |= 0xffffu;
    r->esi += 2u;
    return 14;
}

static int i_scc(Run *r, Params *p)
{
    int cycles;
    selective_usereg(r, p->eamode);
    if (p->cc > 1)
    {
        uint32_t value = (uint32_t)condition(r, p->cc);
        if (p->eamode == M_DREG || p->eamode == M_AREG)
        {
            r->ecx = value;
            r->edi -= 2 * (int32_t)value;
            cycles = 4;
        }
        else
            cycles = 8;
        r->ecx = (r->ecx & 0xffffff00u) | (value ? 0xffu : 0u);
    }
    else
    {
        r->ecx = (r->ecx & 0xffffff00u) | (uint32_t)((p->cc ^ 1) * 0xff);
        if (p->eamode == M_DREG || p->eamode == M_AREG)
            cycles = p->cc == 0 ? 6 : 4;
        else
            cycles = 8;
    }
    ea_store(r, 1, p->eamode, -1);
    return cycles + ea_cycles(1, p->eamode);
}

/* bitop: cc 0 BTST, 1 BCHG, 2 BCLR, 3 BSET. ECX holds the bit number. */
static int bitop(Run *r, Params *p, int static_cycles)
{
    uint32_t flags;
    int cycles;
    selective_usereg(r, p->eamode);
    if (p->eamode == M_DREG)
    {
        uint32_t *d = &r->c->dreg[r->ebx];
        r->ecx &= 31u;
        r->edx = 1u << r->ecx;
        if (p->cc == 0)
            flag_to_z(r, (*d & r->edx) == 0u);
        else
        {
            r->ecx = *d;
            flag_to_z(r, (r->ecx & r->edx) == 0u);
            if (p->cc == 1) r->ecx ^= r->edx;
            else if (p->cc == 2) r->ecx &= ~r->edx;
            else r->ecx |= r->edx;
            *d = r->ecx;
        }
        cycles = 6 + static_cycles;
        if (p->cc != 0) cycles += 2;
        if (p->cc == 2) cycles += 2;
        return cycles;
    }
    r->ecx &= 7u;
    if (p->cc == 0)
    {
        uint32_t bit = r->ecx;
        ea_load(r, 1, p->eamode, -1);
        r->edx = r->ecx;
        r->ecx = bit + 1u;
        shiftop(SH_SHR, 1, r->edx, r->ecx, 0, &flags);
        flag_to_z(r, (flags & 1u) == 0u);
    }
    else
    {
        uint32_t mask = 1u << (r->ecx & 7u), saved;
        r->edx = (r->edx & 0xffffff00u) | mask;
        saved = r->edx;
        ea_rmw_load(r, 1, p->eamode, -1);
        {
            uint32_t address = r->edx;
            r->edx = saved;
            flag_to_z(r, (r->ecx & mask) == 0u);
            if (p->cc == 1) r->ecx ^= mask;
            else if (p->cc == 2) r->ecx &= ~mask;
            else r->ecx |= mask;
            r->edx = address;
        }
        ea_rmw_store(r, 1, p->eamode, -1);
    }
    cycles = 4 + static_cycles + ea_cycles(1, p->eamode);
    if (p->cc != 0)
        cycles += 4;
    return cycles;
}

static int i_jmp(Run *r, Params *p)
{
    static const int cycles[12] = {0, 0, 8, 0, 0, 10, 14, 10, 12, 10, 14, 0};
    selective_usereg(r, p->eamode);
    ea_precalc(r, 0, p->eamode, -1);
    r->esi = r->edx;
    cached_rebase(r);
    return cycles[p->eamode];
}

static int i_jsr(Run *r, Params *p)
{
    static const int cycles[12] = {0, 0, 16, 0, 0, 18, 22, 18, 20, 18, 22, 0};
    selective_usereg(r, p->eamode);
    ea_precalc(r, 0, p->eamode, -1);
    r->ecx = unbased_pc(r);
    r->esi = r->edx;
    cached_rebase(r);
    push_long(r);
    return cycles[p->eamode];
}

static int i_rts(Run *r)
{
    ea_load(r, 4, M_AINC, 7);
    r->esi = r->ecx;
    cached_rebase(r);
    return 16;
}

static int i_rtr(Run *r)
{
    ea_load(r, 2, M_AINC, 7);
    load_ccr(r, (uint8_t)r->ecx);
    ea_load(r, 4, M_AINC, 7);
    r->esi = r->ecx;
    cached_rebase(r);
    return 20;
}

static int i_rte(Run *r)
{
    GensM68k *c = r->c;
    if ((c->sr & 0x2000u) == 0u)
        return privilege_violation(r);
    r->edx = c->areg[7];
    read16(r);
    r->edx += 2u;
    set_sr(r);
    if ((r->ecx & 0x2000u) != 0u)
        c->areg[7] += 6u;
    else
        c->asp += 6u;
    read32(r, 0);
    r->esi = r->ecx;
    cached_rebase(r);
    return CHECKPOINT | 20;
}

static int i_lea(Run *r, Params *p)
{
    static const int cycles[12] = {0, 0, 4, 0, 0, 8, 12, 8, 12, 8, 12, 0};
    selective_usereg(r, p->eamode);
    ea_precalc(r, 0, p->eamode, -1);
    r->c->areg[p->reg] = r->edx;
    return cycles[p->eamode];
}

static int i_pea(Run *r, Params *p)
{
    static const int cycles[12] = {0, 0, 12, 0, 0, 16, 20, 16, 20, 16, 20, 0};
    selective_usereg(r, p->eamode);
    ea_precalc(r, 0, p->eamode, -1);
    r->ecx = r->edx;
    push_long(r);
    return cycles[p->eamode];
}
static int i_movem_control(Run *r, Params *p)
{
    static const int store_cycles[12] = {0, 0, 8, 0, 0, 12, 14, 12, 16, 0, 0, 0};
    static const int load_cycles[12] = {0, 0, 12, 0, 0, 16, 18, 16, 20, 16, 18, 0};
    unsigned int size = (unsigned int)p->size;
    uint32_t mask;
    selective_usereg(r, p->eamode);
    mask = fetch16(r, 0);
    r->esi += 2u;
    ea_precalc(r, 0, p->eamode, -1);
    for (r->ebx = 0; r->ebx < 64u; r->ebx += 4u)
    {
        int bit = (int)(mask & 1u);
        mask >>= 1;
        if (!bit)
            continue;
        if (p->dr == 0)
        {
            r->ecx = *reg_slot(r->c, r->ebx / 4u);
            write_mem(r, size, 0);
        }
        else
        {
            read_mem(r, size, 0);
            if (size == 2u)
                r->ecx = (uint32_t)(int32_t)(int16_t)r->ecx;
            *reg_slot(r->c, r->ebx / 4u) = r->ecx;
        }
        r->edx += size;
        r->edi -= (int32_t)size * 2;
    }
    return p->dr == 0 ? store_cycles[p->eamode] : load_cycles[p->eamode];
}

static int i_movem_postinc(Run *r, Params *p)
{
    unsigned int size = (unsigned int)p->size, base;
    uint32_t mask;
    r->ebx &= 7u;
    mask = fetch16(r, 0);
    r->esi += 2u;
    r->edx = r->c->areg[r->ebx];
    base = r->ebx;
    for (r->ebx = 0; r->ebx < 64u; r->ebx += 4u)
    {
        int bit = (int)(mask & 1u);
        mask >>= 1;
        if (!bit)
            continue;
        read_mem(r, size, 0);
        if (size == 2u)
            r->ecx = (uint32_t)(int32_t)(int16_t)r->ecx;
        *reg_slot(r->c, r->ebx / 4u) = r->ecx;
        r->edx += size;
        r->edi -= (int32_t)size * 2;
    }
    r->ebx = base;
    r->c->areg[base] = r->edx;
    return 12;
}

static int i_movem_predec(Run *r, Params *p)
{
    unsigned int size = (unsigned int)p->size, base;
    uint32_t mask;
    int32_t index;
    r->ebx &= 7u;
    mask = fetch16(r, 0);
    r->esi += 2u;
    r->edx = r->c->areg[r->ebx];
    base = r->ebx;
    for (index = 60; index >= 0; index -= 4)
    {
        int bit = (int)(mask & 1u);
        mask >>= 1;
        r->ebx = (uint32_t)index;
        if (!bit)
            continue;
        r->ecx = *reg_slot(r->c, (unsigned int)index / 4u);
        r->edx -= size;
        r->edi -= (int32_t)size * 2;
        write_mem(r, size, 0);
    }
    r->ebx = base;
    r->c->areg[base] = r->edx;
    return 8;
}

static int i_link(Run *r)
{
    GensM68k *c = r->c;
    r->ebx &= 7u;
    r->ecx = c->areg[r->ebx];
    push_long(r);
    r->ecx = c->areg[7];
    c->areg[r->ebx] = r->ecx;
    r->edx = (uint32_t)(int32_t)(int16_t)fetch16(r, 0);
    r->ecx += r->edx;
    c->areg[7] = r->ecx;
    r->esi += 2u;
    return 16;
}

static int i_unlk(Run *r)
{
    GensM68k *c = r->c;
    r->ebx &= 7u;
    r->ecx = c->areg[r->ebx];
    c->areg[7] = r->ecx;
    ea_load(r, 4, M_AINC, 7);
    c->areg[r->ebx] = r->ecx;
    return 12;
}

static int i_move_usp(Run *r, int to_usp)
{
    GensM68k *c = r->c;
    if ((c->sr & 0x2000u) == 0u)
        return privilege_violation(r);
    r->ebx &= 7u;
    if (to_usp)
    {
        r->ecx = c->areg[r->ebx];
        c->asp = r->ecx;
    }
    else
    {
        r->ecx = c->asp;
        c->areg[r->ebx] = r->ecx;
    }
    return 4;
}

static int i_trap(Run *r)
{
    r->ebx &= 0x0fu;
    return exception(r, 0x80u + r->ebx * 4u, 0, 34);
}

static int i_trapv(Run *r)
{
    if ((r->al & 1u) == 0u)
        return 4;
    return exception(r, 0x1cu, 0, 38);
}

static int i_stop(Run *r)
{
    if ((r->c->sr & 0x2000u) == 0u)
        return privilege_violation(r);
    r->ecx = (r->ecx & 0xffff0000u) | fetch16(r, 0);
    r->esi += 2u;
    set_sr(r);
    r->c->interrupts[0] |= 0x10u;
    r->edi -= 4;
    if (r->edi >= 0)
        r->edi = -1;
    return 0;
}

static int i_extbw(Run *r)
{
    uint32_t *d;
    r->ebx &= 7u;
    d = &r->c->dreg[r->ebx];
    r->ecx = (r->ecx & 0xffff0000u) | ((uint32_t)(int32_t)(int8_t)*d & 0xffffu);
    *d = (*d & 0xffff0000u) | (r->ecx & 0xffffu);
    set_flags_v0(r, test_flags(2, r->ecx));
    return 4;
}

static int i_extwl(Run *r)
{
    uint32_t *d;
    r->ebx &= 7u;
    d = &r->c->dreg[r->ebx];
    r->ecx = (uint32_t)(int32_t)(int16_t)*d;
    *d = r->ecx;
    set_flags_v0(r, test_flags(4, r->ecx));
    return 4;
}

static int i_swap(Run *r)
{
    uint32_t *d;
    r->ebx &= 7u;
    d = &r->c->dreg[r->ebx];
    r->ecx = *d << 16 | *d >> 16;
    *d = r->ecx;
    set_flags_v0(r, test_flags(4, r->ecx));
    return 4;
}

/* main_cc 1 is MULS. */
static int i_mul(Run *r, Params *p)
{
    uint32_t source, counter = 0, flags, product, i;
    uint16_t shifter;
    selective_usereg(r, p->eamode);
    ea_load(r, 2, p->eamode, -1);
    source = r->ecx;
    shifter = (uint16_t)r->ecx;
    for (i = 0; i < 16u; ++i)
    {
        uint32_t f;
        shifter = (uint16_t)add16(shifter, shifter, 0, &f);
        counter += p->cc == 1 ? (f >> 8 & 1u) : (f & 1u);
    }
    r->ecx = (r->ecx & 0xffff0000u) | shifter;
    r->ebx &= 0xffffff00u;
    r->edx = counter & 127u;
    r->edi -= 2 * (int32_t)r->edx;
    if (p->cc == 1)
        product = (uint32_t)((int32_t)(int16_t)source * (int32_t)(int16_t)r->c->dreg[p->reg]);
    else
        product = (source & 0xffffu) * (r->c->dreg[p->reg] & 0xffffu);
    r->edx = product >> 16 << 16;
    r->ecx = product;
    flags = test_flags(4, r->ecx);
    set_flags_v0(r, flags);
    r->c->dreg[p->reg] = r->ecx;
    return 38 + ea_cycles(2, p->eamode);
}

/* main_cc 1 is DIVS. */
static int i_div(Run *r, Params *p)
{
    int base = p->cc ? 150 : 133;
    uint32_t dividend, divisor, quotient, remainder;
    selective_usereg(r, p->eamode);
    ea_load(r, 2, p->eamode, -1);
    if ((r->ecx & 0xffffu) == 0u)
        return exception(r, 0x14u, 0, 38 + ea_cycles(2, p->eamode));
    dividend = r->c->dreg[p->reg];
    if (p->cc)
    {
        int32_t d = (int32_t)(int16_t)r->ecx, n = (int32_t)dividend, q;
        r->ecx = (uint32_t)d;
        /* The original IDIV faults on the host for this pair. Real hardware reports overflow. */
        if (n == INT32_MIN && d == -1)
            goto overflow;
        q = n / d;
        quotient = (uint32_t)q;
        remainder = (uint32_t)(n % d);
        if (!(q >= -0x8000 && q <= 0x7fff))
            goto overflow;
    }
    else
    {
        divisor = r->ecx & 0xffffu;
        r->ecx = divisor;
        quotient = dividend / divisor;
        remainder = dividend % divisor;
        if ((quotient & 0xffff0000u) != 0u)
            goto overflow;
    }
    r->edx = remainder << 16 | (quotient & 0xffffu);
    set_flags_v0(r, test_flags(2, r->edx));
    r->c->dreg[p->reg] = r->edx;
    return base + ea_cycles(2, p->eamode);
overflow:
    r->ah = 0;
    r->al = 1;
    return base + ea_cycles(2, p->eamode);
}

static int i_neg(Run *r, Params *p)
{
    unsigned int size = (unsigned int)p->size;
    uint32_t flags;
    int cycles = size == 4u ? 6 : 4;
    selective_usereg(r, p->eamode);
    if (p->eamode == M_DREG)
    {
        uint32_t *d = &r->c->dreg[r->ebx];
        *d = binop(OP_SUB, size, *d & ~size_mask(size), *d, 0, &flags) | (*d & ~size_mask(size));
        set_flags(r, flags);
        r->c->xflag = (uint8_t)(flags & 1u);
        return cycles;
    }
    cycles *= 2;
    ea_rmw_load(r, size, p->eamode, -1);
    r->ecx = binop(OP_SUB, size, r->ecx & ~size_mask(size), r->ecx, 0, &flags);
    set_flags(r, flags);
    r->c->xflag = (uint8_t)(flags & 1u);
    ea_rmw_store(r, size, p->eamode, -1);
    return cycles + ea_cycles(size, p->eamode);
}

static int i_negx(Run *r, Params *p)
{
    unsigned int size = (unsigned int)p->size;
    uint32_t flags, mask = size_mask(size), x = r->c->xflag & 1u;
    uint8_t old = r->ah;
    int cycles = size == 4u ? 6 : 4;
    selective_usereg(r, p->eamode);
    if (p->eamode == M_DREG)
    {
        uint32_t *d = &r->c->dreg[r->ebx];
        r->ecx = (r->ecx & 0xffffff00u) | r->c->xflag;
        r->ecx = (r->ecx & 0xffffff00u) | (r->c->xflag >> 1);
        r->ecx &= size == 1u ? 0xffffff00u : 0u;
        r->ecx = binop(OP_SBB, size, r->ecx, *d, x, &flags);
        r->edx = (uint32_t)r->ah << 8 | r->al;
        set_flags(r, flags);
        r->c->xflag = (uint8_t)(flags & 1u);
        adjzero(r, flags, old);
        *d = (*d & ~mask) | (r->ecx & mask);
        return cycles;
    }
    cycles *= 2;
    ea_rmw_load(r, size, p->eamode, -1);
    {
        uint32_t ebx = r->ebx;
        ebx = (ebx & 0xffffff00u) | (r->c->xflag >> 1);
        ebx &= size == 1u ? 0xffffff00u : 0u;
        ebx = binop(OP_SBB, size, ebx, r->ecx, x, &flags);
        r->ecx = ebx;
        set_flags(r, flags);
        r->c->xflag = (uint8_t)(flags & 1u);
        adjzero(r, flags, old);
    }
    ea_rmw_store(r, size, p->eamode, -1);
    return cycles + ea_cycles(size, p->eamode);
}

/* NBCD as Gens generates it: the operand is overwritten by X and only 0 - X is adjusted. */
static int i_nbcd(Run *r, Params *p)
{
    uint32_t flags;
    uint8_t old = r->ah, value;
    selective_usereg(r, p->eamode);
    ea_rmw_load(r, 1, p->eamode, -1);
    value = (uint8_t)sbb8(0, 0, r->c->xflag & 1u, &flags);
    value = decimal_adjust(value, &flags, 1);
    r->ecx = (r->ecx & 0xffff0000u) | (uint32_t)old << 8 | value;
    set_flags_v0(r, flags);
    r->c->xflag = (uint8_t)(flags & 1u);
    adjzero(r, flags, old);
    ea_rmw_store(r, 1, p->eamode, -1);
    return p->eamode == M_DREG ? 6 : 8 + ea_cycles(1, p->eamode);
}

/* TAS as Gens generates it: memory operands are only read and not written back. */
static int i_tas(Run *r, Params *p)
{
    selective_usereg(r, p->eamode);
    ea_rmw_load(r, 1, p->eamode, -1);
    set_flags_v0(r, test_flags(1, r->ecx));
    if (p->eamode == M_DREG || p->eamode == M_AREG)
    {
        r->ecx |= 0x80u;
        ea_rmw_store(r, 1, p->eamode, -1);
        return 4;
    }
    return 14 + ea_cycles(1, p->eamode);
}

static int i_not(Run *r, Params *p)
{
    unsigned int size = (unsigned int)p->size;
    uint32_t flags;
    int cycles = size == 4u ? 6 : 4;
    selective_usereg(r, p->eamode);
    if (p->eamode == M_DREG)
    {
        uint32_t *d = &r->c->dreg[r->ebx];
        *d = binop(OP_XOR, size, *d, 0xffffffffu, 0, &flags);
        set_flags_v0(r, flags);
        return cycles;
    }
    cycles *= 2;
    ea_rmw_load(r, size, p->eamode, -1);
    r->ecx = binop(OP_XOR, size, r->ecx, 0xffffffffu, 0, &flags);
    set_flags_v0(r, flags);
    ea_rmw_store(r, size, p->eamode, -1);
    return cycles + ea_cycles(size, p->eamode);
}

static int i_exg(Run *r, Params *p)
{
    uint32_t *first, *second;
    r->ebx &= 7u;
    first = reg_slot(r->c, (unsigned int)p->reg + (unsigned int)p->dr / 4u);
    second = reg_slot(r->c, (unsigned int)p->ir / 4u + r->ebx);
    r->ecx = *first;
    r->edx = *second;
    *first = r->edx;
    *second = r->ecx;
    return 6;
}

static int i_cmpm(Run *r, Params *p)
{
    unsigned int size = (unsigned int)p->size;
    uint32_t flags, source;
    r->ebx &= 7u;
    ea_load(r, size, M_AINC, -1);
    source = r->ecx;
    ea_load(r, size, M_AINC, p->reg);
    binop(OP_CMP, size, r->ecx, source, 0, &flags);
    set_flags(r, flags);
    return size == 4u ? 20 : 12;
}

/* ADDX, SUBX, ABCD, SBCD with data registers. adjust 1 DAA, 2 DAS. */
static int opx_dreg(Run *r, Params *p, unsigned int op, int adjust, unsigned int size)
{
    GensM68k *c = r->c;
    uint32_t flags, value, *d = &c->dreg[p->reg];
    uint8_t old = r->ah;
    r->ebx &= 7u;
    r->ecx = (r->ecx & 0xffff0000u) | (uint32_t)old << 8 | (uint32_t)(c->xflag >> 1);
    value = binop(op, size, *d, c->dreg[r->ebx], c->xflag & 1u, &flags);
    if (adjust)
        value = (value & 0xffffff00u) | decimal_adjust((uint8_t)value, &flags, adjust == 2);
    *d = (*d & ~size_mask(size)) | (value & size_mask(size));
    if (adjust) set_flags_v0(r, flags);
    else set_flags(r, flags);
    c->xflag = (uint8_t)(flags & 1u);
    adjzero(r, flags, old);
    if (size <= 2u)
        return adjust ? 6 : 4;
    return 8;
}

static int opx_adec(Run *r, Params *p, unsigned int op, int adjust, unsigned int size)
{
    GensM68k *c = r->c;
    uint32_t flags, source, value;
    uint8_t old = r->ah;
    r->ebx &= 7u;
    ea_load(r, size, M_ADEC, -1);
    source = r->ecx;
    r->ebx = source;
    ea_rmw_load(r, size, M_ADEC, p->reg);
    value = binop(op, size, r->ecx, source, c->xflag & 1u, &flags);
    if (adjust)
        value = (value & 0xffffff00u) | decimal_adjust((uint8_t)value, &flags, adjust == 2);
    if (adjust) set_flags_v0(r, flags);
    else set_flags(r, flags);
    c->xflag = (uint8_t)(flags & 1u);
    adjzero(r, flags, old);
    r->ecx = value;
    r->ebx = 0;
    ea_rmw_store(r, size, M_ADEC, p->reg);
    return size <= 2u ? 18 : 30;
}

static int i_movep_mem2reg(Run *r, Params *p)
{
    GensM68k *c = r->c;
    r->ebx &= 7u;
    r->edx = (uint32_t)(int32_t)(int16_t)fetch16(r, 0) + c->areg[r->ebx];
    r->esi += 2u;
    read8(r);
    r->ebx = (r->ebx & 0xffff00ffu) | (r->ecx & 0xffu) << 8;
    r->edx += 2u;
    read8(r);
    r->ebx = (r->ebx & 0xffffff00u) | (r->ecx & 0xffu);
    if (p->size == 2)
    {
        c->dreg[p->reg] = (c->dreg[p->reg] & 0xffff0000u) | (r->ebx & 0xffffu);
        return 16;
    }
    r->edx += 2u;
    r->ebx <<= 16;
    read8(r);
    r->ebx = (r->ebx & 0xffff00ffu) | (r->ecx & 0xffu) << 8;
    r->edx += 2u;
    read8(r);
    r->ebx = (r->ebx & 0xffffff00u) | (r->ecx & 0xffu);
    c->dreg[p->reg] = r->ebx;
    r->ebx = 0;
    return 24;
}

static int i_movep_reg2mem(Run *r, Params *p)
{
    GensM68k *c = r->c;
    r->ebx &= 7u;
    r->edx = (uint32_t)(int32_t)(int16_t)fetch16(r, 0) + c->areg[r->ebx];
    r->esi += 2u;
    r->ebx = c->dreg[p->reg];
    if (p->size == 4)
        r->ebx = r->ebx << 16 | r->ebx >> 16;
    r->ecx = (r->ecx & 0xffffff00u) | (r->ebx >> 8 & 0xffu);
    write8(r);
    r->edx += 2u;
    r->ecx = (r->ecx & 0xffffff00u) | (r->ebx & 0xffu);
    write8(r);
    if (p->size == 4)
    {
        r->edx += 2u;
        r->ebx = r->ebx << 16 | r->ebx >> 16;
        r->ecx = (r->ecx & 0xffffff00u) | (r->ebx >> 8 & 0xffu);
        write8(r);
        r->edx += 2u;
        r->ecx = (r->ecx & 0xffffff00u) | (r->ebx & 0xffu);
        write8(r);
        r->ebx = 0;
        return 24;
    }
    r->ebx = 0;
    return 16;
}

static int i_chk(Run *r, Params *p)
{
    uint32_t value;
    selective_usereg(r, p->eamode);
    ea_load(r, 2, p->eamode, -1);
    value = r->c->dreg[p->reg];
    if ((int16_t)value < 0)
    {
        r->ah = 0x80;
        r->al = 0;
    }
    else
    {
        r->ah = 0;
        r->al = 0;
        if ((int16_t)value <= (int16_t)r->ecx)
            return 10 + ea_cycles(2, p->eamode);
    }
    return exception(r, 0x18u, 0, 40 + ea_cycles(2, p->eamode));
}

static int i_reset(Run *r)
{
    GensM68k *c = r->c;
    if ((c->sr & 0x2000u) == 0u)
        return privilege_violation(r);
    if (c->bus.reset == 0)
        return invalid_instruction(r);
    airlock_exit(r);
    c->bus.reset(c->bus.opaque);
    airlock_enter(r);
    return 132;
}
/* ---------------------------------------------------------------------------------------------
 * Dispatch.
 */
static int run_routine(Run *r, Params *p)
{
    switch (p->handler)
    {
    case GENS_M68K_MOVE: return i_move(r, p);
    case GENS_M68K_MOVEQ: return i_moveq(r, p);
    case GENS_M68K_MOVEA: return i_movea(r, p);
    case GENS_M68K_ADDA: return addsuba(r, p, 0);
    case GENS_M68K_SUBA: return addsuba(r, p, 1);
    case GENS_M68K_CMPA: return i_cmpa(r, p);
    case GENS_M68K_MOVE_TO_SR: return i_move_to_sr(r, p);
    case GENS_M68K_MOVE_TO_CCR: return i_move_to_ccr(r, p);
    case GENS_M68K_MOVE_FROM_SR: return i_move_from_sr(r, p);
    case GENS_M68K_ORI_CCR: return op_to_ccr(r, OP_OR);
    case GENS_M68K_ANDI_CCR: return op_to_ccr(r, OP_AND);
    case GENS_M68K_EORI_CCR: return op_to_ccr(r, OP_XOR);
    case GENS_M68K_ORI_SR: return op_to_sr(r, OP_OR);
    case GENS_M68K_ANDI_SR: return op_to_sr(r, OP_AND);
    case GENS_M68K_EORI_SR: return op_to_sr(r, OP_XOR);
    case GENS_M68K_CLR: return i_clr(r, p);
    case GENS_M68K_TST: return i_tst(r, p);
    case GENS_M68K_ADDQ: return op_quick(r, p, OP_ADD);
    case GENS_M68K_SUBQ: return op_quick(r, p, OP_SUB);
    case GENS_M68K_CMP_DN: return op_to_dn(r, p, OP_CMP, 0, 0);
    case GENS_M68K_ADD_DN: return op_to_dn(r, p, OP_ADD, 1, 0);
    case GENS_M68K_SUB_DN: return op_to_dn(r, p, OP_SUB, 1, 0);
    case GENS_M68K_AND_DN: return op_to_dn(r, p, OP_AND, 0, 1);
    case GENS_M68K_OR_DN: return op_to_dn(r, p, OP_OR, 0, 1);
    case GENS_M68K_EOR_EA: return op_to_ea(r, p, OP_XOR, 1);
    case GENS_M68K_ADD_EA: return op_to_ea(r, p, OP_ADD, 0);
    case GENS_M68K_SUB_EA: return op_to_ea(r, p, OP_SUB, 0);
    case GENS_M68K_AND_EA: return op_to_ea(r, p, OP_AND, 1);
    case GENS_M68K_OR_EA: return op_to_ea(r, p, OP_OR, 1);
    case GENS_M68K_ADDI: return im_to_ea(r, p, OP_ADD, 1, 1, 0, 8, 16, 12, 20);
    case GENS_M68K_SUBI: return im_to_ea(r, p, OP_SUB, 1, 1, 0, 8, 16, 12, 20);
    case GENS_M68K_CMPI: return im_to_ea(r, p, OP_CMP, 0, 0, 0, 8, 14, 8, 12);
    case GENS_M68K_ANDI: return im_to_ea(r, p, OP_AND, 1, 0, 1, 8, 14, 12, 20);
    case GENS_M68K_ORI: return im_to_ea(r, p, OP_OR, 1, 0, 1, 8, 16, 12, 20);
    case GENS_M68K_EORI: return im_to_ea(r, p, OP_XOR, 1, 0, 1, 8, 16, 12, 20);
    case GENS_M68K_LSX_REG: return flick_reg(r, p, 0);
    case GENS_M68K_ASX_REG: return flick_reg(r, p, 1);
    case GENS_M68K_ROX_REG: return flick_reg(r, p, 2);
    case GENS_M68K_RXX_REG: return flick_reg(r, p, 3);
    case GENS_M68K_LSX_MEM: return flick_mem(r, p, 0);
    case GENS_M68K_ASX_MEM: return flick_mem(r, p, 1);
    case GENS_M68K_ROX_MEM: return flick_mem(r, p, 2);
    case GENS_M68K_RXX_MEM: return flick_mem(r, p, 3);
    case GENS_M68K_BRA_B: return bra_b(r);
    case GENS_M68K_BRA_W: return bra_w(r);
    case GENS_M68K_BSR_B: return i_bsr_b(r);
    case GENS_M68K_BSR_W: return i_bsr_w(r);
    case GENS_M68K_BCC_B: return condition(r, p->cc) ? bra_b(r) : 8;
    case GENS_M68K_BCC_W:
        if (condition(r, p->cc))
            return bra_w(r);
        r->esi += 2u;
        return 12;
    case GENS_M68K_DBRA: return dbra(r);
    case GENS_M68K_DBTR:
        r->esi += 2u;
        return 8;
    case GENS_M68K_DBCC:
        if (!condition(r, p->cc))
            return dbra(r);
        r->esi += 2u;
        return 12;
    case GENS_M68K_SCC: return i_scc(r, p);
    case GENS_M68K_BITOP_IMM:
        r->ecx = (r->ecx & 0xffffff00u) | (fetch16(r, 0) & 0xffu);
        r->esi += 2u;
        return bitop(r, p, 4);
    case GENS_M68K_BITOP_REG:
        r->ecx = (r->ecx & 0xffffff00u) | (r->c->dreg[p->reg] & 0xffu);
        return bitop(r, p, 0);
    case GENS_M68K_JMP: return i_jmp(r, p);
    case GENS_M68K_JSR: return i_jsr(r, p);
    case GENS_M68K_RTS: return i_rts(r);
    case GENS_M68K_RTR: return i_rtr(r);
    case GENS_M68K_RTE: return i_rte(r);
    case GENS_M68K_LEA: return i_lea(r, p);
    case GENS_M68K_PEA: return i_pea(r, p);
    case GENS_M68K_NOP: return 4;
    case GENS_M68K_MOVEM_CONTROL: return i_movem_control(r, p);
    case GENS_M68K_MOVEM_POSTINC: return i_movem_postinc(r, p);
    case GENS_M68K_MOVEM_PREDEC: return i_movem_predec(r, p);
    case GENS_M68K_LINK: return i_link(r);
    case GENS_M68K_UNLK: return i_unlk(r);
    case GENS_M68K_MOVE_FROM_USP: return i_move_usp(r, 0);
    case GENS_M68K_MOVE_TO_USP: return i_move_usp(r, 1);
    case GENS_M68K_TRAP: return i_trap(r);
    case GENS_M68K_TRAPV: return i_trapv(r);
    case GENS_M68K_STOP: return i_stop(r);
    case GENS_M68K_EXTBW: return i_extbw(r);
    case GENS_M68K_EXTWL: return i_extwl(r);
    case GENS_M68K_SWAP: return i_swap(r);
    case GENS_M68K_MUL: return i_mul(r, p);
    case GENS_M68K_DIV: return i_div(r, p);
    case GENS_M68K_NEG: return i_neg(r, p);
    case GENS_M68K_NEGX: return i_negx(r, p);
    case GENS_M68K_NBCD: return i_nbcd(r, p);
    case GENS_M68K_TAS: return i_tas(r, p);
    case GENS_M68K_NOT: return i_not(r, p);
    case GENS_M68K_EXG: return i_exg(r, p);
    case GENS_M68K_CMPM: return i_cmpm(r, p);
    case GENS_M68K_ADDX_DREG: return opx_dreg(r, p, OP_ADC, 0, (unsigned int)p->size);
    case GENS_M68K_SUBX_DREG: return opx_dreg(r, p, OP_SBB, 0, (unsigned int)p->size);
    case GENS_M68K_ABCD_DREG: return opx_dreg(r, p, OP_ADC, 1, 1);
    case GENS_M68K_SBCD_DREG: return opx_dreg(r, p, OP_SBB, 2, 1);
    case GENS_M68K_ADDX_ADEC: return opx_adec(r, p, OP_ADC, 0, (unsigned int)p->size);
    case GENS_M68K_SUBX_ADEC: return opx_adec(r, p, OP_SBB, 0, (unsigned int)p->size);
    case GENS_M68K_ABCD_ADEC: return opx_adec(r, p, OP_ADC, 1, 1);
    case GENS_M68K_SBCD_ADEC: return opx_adec(r, p, OP_SBB, 2, 1);
    case GENS_M68K_MOVEP_MEM2REG: return i_movep_mem2reg(r, p);
    case GENS_M68K_MOVEP_REG2MEM: return i_movep_reg2mem(r, p);
    case GENS_M68K_CHK: return i_chk(r, p);
    case GENS_M68K_ILLEGAL: return exception(r, 0x10u, 1, 34);
    case GENS_M68K_ALINE: return exception(r, 0x28u, 1, 34);
    case GENS_M68K_FLINE: return exception(r, 0x2cu, 1, 34);
    case GENS_M68K_RESET: return i_reset(r);
    default: return invalid_instruction(r);
    }
}

static void build_dispatch(void)
{
    unsigned int run, opcode = 0, i;
    if (dispatch_ready)
        return;
    for (run = 0; run < GENS_M68K_RUN_COUNT; ++run)
        for (i = 0; i < gens_m68k_runs[run].count; ++i)
            dispatch[opcode++] = gens_m68k_runs[run].routine;
    dispatch_ready = 1;
}

/* The checkpoint compares levels in CL/CH, and those bytes stay in ECX for the next instruction.
 * They can surface in the unused upper bits of a hook value. */
static int pending_interrupt(Run *r, int masked)
{
    GensM68k *c = r->c;
    uint8_t level = masked ? (uint8_t)(c->interrupts[0] & 7u) : c->interrupts[0];
    uint8_t mask = (uint8_t)(c->sr >> 8 & 7u);
    r->ecx = (r->ecx & 0xffff0000u) | (uint32_t)level << 8 | mask;
    return level == 7u || mask < level;
}

static void exec_hook(Run *r)
{
    GensM68k *c = r->c;
    if (c->hooks.exec != 0)
        c->hooks.exec(c->hooks.opaque, hook_pc(r));
}

uint32_t gens_m68k_exec(GensM68k *c, uint32_t odometer)
{
    return gens_m68k_exec_observed(c, odometer, 0);
}

uint32_t gens_m68k_exec_observed(GensM68k *c, uint32_t odometer, const GensM68kObserver *observer)
{
    Run run, *r = &run;
    uint32_t wanted = odometer - c->odometer;
    int cycles, writeback = 0;

    if (odometer <= c->odometer)
        return GENS_M68K_ALREADY_DONE;
    if ((c->interrupts[0] & 0x10u) != 0u)
    {
        if ((c->pc & 1u) != 0u)
            return GENS_M68K_DOUBLE_FAULT;
        c->odometer += wanted;
        return GENS_M68K_STOPPED;
    }
    memset(r, 0, sizeof(*r));
    r->c = c;
    r->observer = observer;
    r->instruction_pc = c->pc & 0xffffffu;
    c->cycles_needed = wanted;
    r->edi = (int32_t)wanted - 1;
    r->esi = c->pc;
    load_ccr(r, (uint8_t)c->sr);
    c->execinfo = 1;
    uncached_rebase(r);
    if ((c->execinfo & 2u) != 0u)
        goto bounderror;
    c->cycles_leftover = 0;
    goto checkpoint_noquit;

checkpoint:
    if (r->edi < 0)
        goto execquit;
checkpoint_noquit:
    if (pending_interrupt(r, 0))
    {
        r->instruction_pc = unbased_pc(r) & 0xffffffu;
        flush_interrupts(r);
        uncached_rebase(r);
        if (r->edi < 0)
            goto execquit;
        if ((c->execinfo & 2u) != 0u)
            goto bounderror;
    }
execloop:
    writeback = 0;
    for (;;)
    {
        /* Execution after a failed rebase would read through an invalid pointer in the
         * original as well. Report the bound error instead. */
        if (r->ebp == 0 && (c->execinfo & 2u) != 0u)
            goto bounderror;
        r->instruction_pc = unbased_pc(r) & 0xffffffu;
        r->ebx = fetch16(r, 0);
        r->esi += 2u;
        if (writeback)
            c->sr = (uint16_t)((c->sr & 0xff00u) | current_ccr(r));
        exec_hook(r);
        observed(r, 0, r->instruction_pc, 0, 0);
        cycles = run_routine(r, &gens_m68k_routines[dispatch[r->ebx]]);
        if (r->stop)
            goto execexit;
        if ((cycles & CHECKPOINT) != 0)
        {
            r->edi -= cycles & ~CHECKPOINT;
            goto checkpoint;
        }
        r->edi -= cycles;
        if (r->edi < 0)
            goto execquit;
        writeback = 1;
    }

execquit:
    if (pending_interrupt(r, 1))
    {
        r->instruction_pc = unbased_pc(r) & 0xffffffu;
        flush_interrupts(r);
        uncached_rebase(r);
        if ((c->execinfo & 2u) != 0u)
            goto bounderror;
    }
    r->edi += (int32_t)c->cycles_leftover;
    c->cycles_leftover = 0;
    if (r->edi >= 0)
        goto execloop;
    r->result = GENS_M68K_OK;
    goto execexit;

bounderror:
    r->result = GENS_M68K_OUT_OF_BOUNDS;
execexit:
    c->pc = unbased_pc(r);
    c->sr = (uint16_t)((c->sr & 0xff00u) | current_ccr(r));
    ++r->edi;
    c->odometer += c->cycles_needed - (uint32_t)r->edi;
    c->execinfo = 0;
    c->cycles_needed = 0;
    c->io_cycle_counter = -1;
    return r->result;
}

/* ---------------------------------------------------------------------------------------------
 * Public API mirroring the Starscream entry points used by Gens.
 */
int gens_m68k_init(GensM68k *c)
{
    build_dispatch();
    if (c != 0)
        c->io_cycle_counter = -1;
    return 0;
}

int gens_m68k_reset(GensM68k *c)
{
    Run run;
    uint32_t value;
    if ((c->execinfo & 1u) != 0u || c->s_fetch == 0)
        return 1;
    c->execinfo = 0;
    memset(c->dreg, 0, sizeof(c->dreg));
    memset(c->areg, 0, sizeof(c->areg));
    c->asp = 0;
    c->sr = 0x2700;
    c->fetch = c->s_fetch;
    c->pc = 1;
    c->interrupts[0] = 0x10;
    memset(&run, 0, sizeof(run));
    run.c = c;
    run.esi = 0;
    basefunction(&run);
    if ((c->execinfo & 2u) != 0u)
        return 1;
    run.esi = run.ebp;
    value = fetch32(&run, 0);
    c->areg[7] = value;
    value = fetch32(&run, 4);
    c->pc = value;
    c->interrupts[0] = 0;
    return (value & 1u) != 0u ? -1 : 0;
}

int gens_m68k_interrupt(GensM68k *c, unsigned int level)
{
    c->interrupts[0] = (uint8_t)level;
    c->cycles_leftover += (uint32_t)(c->io_cycle_counter + 1);
    c->io_cycle_counter = -1;
    return 0;
}

void gens_m68k_flush_interrupts(GensM68k *c)
{
    Run run;
    if ((c->execinfo & 1u) != 0u)
        return;
    memset(&run, 0, sizeof(run));
    run.c = c;
    if (!pending_interrupt(&run, 0))
        return;
    run.esi = c->pc;
    load_ccr(&run, (uint8_t)c->sr);
    flush_interrupts(&run);
    c->odometer -= (uint32_t)run.edi;
    c->pc = (uint32_t)run.esi;
}

uint32_t gens_m68k_read_odometer(const GensM68k *c)
{
    return c->cycles_needed - (uint32_t)c->io_cycle_counter - 1u - c->cycles_leftover +
        c->odometer;
}

uint32_t gens_m68k_trip_odometer(GensM68k *c)
{
    uint32_t value;
    c->odometer += c->cycles_needed - (uint32_t)c->io_cycle_counter - 1u - c->cycles_leftover;
    c->cycles_needed = (uint32_t)c->io_cycle_counter + 1u;
    value = c->odometer;
    c->odometer = 0;
    return value;
}

uint32_t gens_m68k_control_odometer(GensM68k *c, uint32_t clear)
{
    return clear ? gens_m68k_trip_odometer(c) : gens_m68k_read_odometer(c);
}

void gens_m68k_release_timeslice(GensM68k *c)
{
    c->io_cycle_counter -= (int32_t)c->cycles_needed;
    c->cycles_needed = 0;
}

void gens_m68k_release_cycles(GensM68k *c, uint32_t cycles)
{
    c->io_cycle_counter -= (int32_t)cycles;
}

void gens_m68k_add_cycles(GensM68k *c, uint32_t cycles)
{
    c->odometer += cycles;
}

uint32_t gens_m68k_read_pc(const GensM68k *c)
{
    if ((c->execinfo & 1u) == 0u)
        return c->pc;
    return (uint32_t)(c->io_fetchbased_pc - c->io_fetchbase);
}

uint32_t gens_m68k_fetch_word(GensM68k *c, uint32_t address)
{
    Run run;
    const GensM68kFetch *fetch = c->fetch;
    uint32_t start = c->fetch_region_start, end = c->fetch_region_end, value = 0xffffffffu;
    uint8_t execinfo = c->execinfo;
    memset(&run, 0, sizeof(run));
    run.c = c;
    c->fetch = c->s_fetch;
    c->execinfo &= 0xfdu;
    run.esi = address;
    basefunction(&run);
    if ((c->execinfo & 2u) == 0u)
    {
        run.esi = (uint32_t)run.esi + run.ebp;
        value = fetch16(&run, 0);
    }
    c->execinfo = execinfo;
    c->fetch_region_end = end;
    c->fetch_region_start = start;
    c->fetch = fetch;
    return value;
}