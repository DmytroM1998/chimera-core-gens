/* Candidate port of the pinned z80.asm core.
 * The upstream component terms apply to derived portions.
 */
#include "gens_z80_cpu.h"
#include "gens_z80_tables.h"

#include <string.h>

#define FLAG_C 0x01u
#define FLAG_N 0x02u
#define FLAG_P 0x04u
#define FLAG_H 0x10u
#define FLAG_S 0x80u
#define FLAG_Z 0x40u

typedef struct Run {
    GensZ80 *z;
    int32_t cycles;
    uintptr_t pc;
    uint8_t opcode;
    int skip_check, force_quit;
    /* The original keeps the access address in ECX. Some flag paths read it afterwards. */
    uint32_t ecx;
    /* EDX at a forced quit. The original quit path adds the remaining cycles when bit 1 is set. */
    uint32_t quit_edx;
} Run;

static uint8_t *reg8(GensZ80 *z, unsigned int reg)
{
    switch (reg)
    {
    case GENS_Z80_B: return &z->b;
    case GENS_Z80_C: return &z->c;
    case GENS_Z80_D: return &z->d;
    case GENS_Z80_E: return &z->e;
    case GENS_Z80_H: return &z->h;
    case GENS_Z80_L: return &z->l;
    case GENS_Z80_A: return &z->a;
    case GENS_Z80_IXH: return &z->ixh;
    case GENS_Z80_IXL: return &z->ixl;
    case GENS_Z80_IYH: return &z->iyh;
    default: return &z->iyl;
    }
}

static uint32_t pair(uint8_t high, uint8_t low)
{
    return (uint32_t)high << 8 | low;
}

static uint32_t get_pair(const GensZ80 *z, unsigned int pair_id)
{
    switch (pair_id)
    {
    case GENS_Z80_PAIR_BC: return pair(z->b, z->c);
    case GENS_Z80_PAIR_DE: return pair(z->d, z->e);
    case GENS_Z80_PAIR_HL: return pair(z->h, z->l);
    case GENS_Z80_PAIR_SP: return z->sp;
    case GENS_Z80_PAIR_IX: return pair(z->ixh, z->ixl);
    default: return pair(z->iyh, z->iyl);
    }
}

static void set_pair(GensZ80 *z, unsigned int pair_id, uint32_t value)
{
    uint8_t high = (uint8_t)(value >> 8), low = (uint8_t)value;
    switch (pair_id)
    {
    case GENS_Z80_PAIR_BC: z->b = high; z->c = low; break;
    case GENS_Z80_PAIR_DE: z->d = high; z->e = low; break;
    case GENS_Z80_PAIR_HL: z->h = high; z->l = low; break;
    case GENS_Z80_PAIR_SP: z->sp = (uint16_t)value; break;
    case GENS_Z80_PAIR_IX: z->ixh = high; z->ixl = low; break;
    default: z->iyh = high; z->iyl = low; break;
    }
}

static void swap_bytes(uint8_t *first, uint8_t *second)
{
    uint8_t value = *first;
    *first = *second;
    *second = value;
}

static uint8_t code_byte(const Run *r, unsigned int offset)
{
    return *(const uint8_t *)(r->pc + offset);
}

static void rebase(GensZ80 *z, Run *r, uint32_t address)
{
    uintptr_t base = z->fetch[(address >> 8) & 0xffu];
    r->pc = (uintptr_t)address + base;
    z->base_pc = base;
}

static uint8_t read_byte(Run *r, uint32_t address)
{
    GensZ80 *z = r->z;
    uint8_t value;
    if (address <= 0x3fffu)
    {
        r->ecx = address & 0x1fffu;
        return z->ram[r->ecx];
    }
    z->cycle_io = (uint32_t)r->cycles;
    z->io_ecx = address;
    value = z->read_b[address >> 8 & 0xffu](z, address);
    r->ecx = z->io_ecx;
    r->cycles = (int32_t)z->cycle_io;
    return value;
}

static void write_byte(Run *r, uint32_t address, uint8_t value)
{
    GensZ80 *z = r->z;
    if (address <= 0x3fffu)
    {
        z->ram[address & 0x1fffu] = value;
        return;
    }
    z->cycle_io = (uint32_t)r->cycles;
    z->write_b[address >> 8 & 0xffu](z, address, value);
    r->cycles = (int32_t)z->cycle_io;
}

static uint16_t read_word(Run *r, uint32_t address)
{
    GensZ80 *z = r->z;
    uint16_t value;
    if (address <= 0x3fffu)
    {
        address &= 0x1fffu;
        return (uint16_t)(z->ram[address] | z->ram[address + 1u] << 8);
    }
    z->cycle_io = (uint32_t)r->cycles;
    value = z->read_w[address >> 8 & 0xffu](z, address);
    r->cycles = (int32_t)z->cycle_io;
    return value;
}

static void write_word(Run *r, uint32_t address, uint16_t value)
{
    GensZ80 *z = r->z;
    if (address <= 0x3fffu)
    {
        address &= 0x1fffu;
        z->ram[address] = (uint8_t)value;
        z->ram[address + 1u] = (uint8_t)(value >> 8);
        return;
    }
    z->cycle_io = (uint32_t)r->cycles;
    z->write_w[address >> 8 & 0xffu](z, address, value);
    r->cycles = (int32_t)z->cycle_io;
}

static void push_pc(Run *r)
{
    GensZ80 *z = r->z;
    uint32_t sp = ((uint32_t)z->sp - 2u) & 0xffffu;
    uint32_t pc = (uint32_t)(r->pc - z->base_pc);
    z->sp = (uint16_t)sp;
    write_word(r, sp, (uint16_t)pc);
}

static void do_nmi(Run *r)
{
    GensZ80 *z = r->z;
    push_pc(r);
    z->iff2 = 0;
    z->int_line &= (uint8_t)~0x80u;
    z->status = (z->status & ~0xffu) | ((z->status & 0xffu) & (uint8_t)~GENS_Z80_HALTED);
    rebase(z, r, 0x66u);
}

static void do_int(Run *r)
{
    GensZ80 *z = r->z;
    uint32_t vector;
    push_pc(r);
    z->status = (z->status & ~0xffu) | ((z->status & 0xffu) & (uint8_t)~GENS_Z80_HALTED);
    z->int_line &= 0x80u;
    z->iff1 = z->iff2 = 0;
    if (z->im == 0)
    {
        r->cycles -= 13;
        rebase(z, r, (uint8_t)(z->int_vect - 0xc7u));
    }
    else if (z->im == 1)
    {
        r->cycles -= 13;
        rebase(z, r, 0x38u);
    }
    else
    {
        r->cycles -= 19;
        vector = read_word(r, (uint32_t)z->i << 8 | z->int_vect);
        rebase(z, r, vector & 0xffffu);
    }
}

static void check_int(Run *r)
{
    GensZ80 *z = r->z;
    uint8_t line = z->int_line;
    if (line == 0)
        return;
    if ((line & 0x80u) != 0u)
        do_nmi(r);
    else if ((line & z->iff2) != 0u)
        do_int(r);
}

static void flags_ld_a_ir(GensZ80 *z, uint8_t value, uint8_t carry)
{
    uint8_t flags = (uint8_t)(carry | z->iff2);
    if (value == 0u)
        flags |= FLAG_Z;
    if ((value & 0x80u) != 0u)
        flags |= FLAG_S;
    z->a = value;
    z->f = flags;
    z->fxyb = value;
}

static uint8_t parity_flag(uint8_t value)
{
    value ^= (uint8_t)(value >> 4);
    value ^= (uint8_t)(value >> 2);
    value ^= (uint8_t)(value >> 1);
    return (value & 1u) == 0u ? FLAG_P : 0u;
}

static void alu8(GensZ80 *z, unsigned int operation, uint8_t source, uint8_t compare_fxy)
{
    unsigned int a = z->a, carry_in = z->f & FLAG_C, result, half = 0, carry = 0, overflow = 0;
    uint8_t value, flags;

    switch (operation)
    {
    case 0:
    case 1:
        carry_in = operation == 1u ? carry_in : 0u;
        result = a + source + carry_in;
        half = ((a & 15u) + (source & 15u) + carry_in) > 15u;
        carry = result > 0xffu;
        overflow = ((~(a ^ source) & (a ^ result)) & 0x80u) != 0u;
        break;
    case 2:
    case 3:
    case 7:
        carry_in = operation == 3u ? carry_in : 0u;
        result = a - source - carry_in;
        half = (a & 15u) < ((source & 15u) + carry_in);
        carry = (source + carry_in) > a;
        overflow = (((a ^ source) & (a ^ result)) & 0x80u) != 0u;
        break;
    case 4:
        result = a & source;
        break;
    case 5:
        result = a ^ source;
        break;
    default:
        result = a | source;
        break;
    }
    value = (uint8_t)result;
    flags = (uint8_t)(value & FLAG_S);
    if (value == 0u)
        flags |= FLAG_Z;
    if (operation >= 4u && operation <= 6u)
    {
        flags |= parity_flag(value);
        if (operation == 4u)
            flags |= FLAG_H;
    }
    else
    {
        if (half)
            flags |= FLAG_H;
        if (carry)
            flags |= FLAG_C;
        if (overflow)
            flags |= FLAG_P;
        if (operation >= 2u && operation != 4u)
            flags |= FLAG_N;
    }
    z->f = flags;
    if (operation == 7u)
        z->fxyb = compare_fxy;
    else
    {
        z->a = value;
        z->fxyb = value;
    }
}

static uint32_t index_address(const GensZ80 *z, const Run *r, unsigned int index)
{
    uint32_t base = index == 0u ? pair(z->ixh, z->ixl) : pair(z->iyh, z->iyl);
    return (base + (uint32_t)(int32_t)(int8_t)code_byte(r, 1u)) & 0xffffu;
}

static uint8_t incdec_value(GensZ80 *z, uint8_t value, unsigned int decrement)
{
    uint8_t result = (uint8_t)(decrement ? value - 1u : value + 1u);
    z->f = (uint8_t)((z->f & FLAG_C) |
                     (decrement ? gens_z80_dec_table[value] : gens_z80_inc_table[value]));
    z->fxyb = result;
    return result;
}

static void adc_sbc_hl(GensZ80 *z, unsigned int source_pair, int subtract)
{
    uint32_t hl = pair(z->h, z->l), source = get_pair(z, source_pair), carry = z->f & FLAG_C;
    uint32_t result;
    uint8_t flags;
    if (subtract)
    {
        result = hl - source - carry;
        flags = FLAG_N;
        if ((hl & 0xfffu) < (source & 0xfffu) + carry)
            flags |= FLAG_H;
        if (source + carry > hl)
            flags |= FLAG_C;
        if ((((hl ^ source) & (hl ^ result)) & 0x8000u) != 0u)
            flags |= FLAG_P;
    }
    else
    {
        result = hl + source + carry;
        flags = 0;
        if ((hl & 0xfffu) + (source & 0xfffu) + carry > 0xfffu)
            flags |= FLAG_H;
        if (result > 0xffffu)
            flags |= FLAG_C;
        if (((~(hl ^ source) & (hl ^ result)) & 0x8000u) != 0u)
            flags |= FLAG_P;
    }
    result &= 0xffffu;
    flags |= (uint8_t)((result >> 8) & FLAG_S);
    if (result == 0u)
        flags |= FLAG_Z;
    z->f = flags;
    z->h = (uint8_t)(result >> 8);
    z->l = (uint8_t)result;
    z->fxyb = z->h;
}

/* Sign, zero and parity bits as LAHF would report them after a logic test of value. */
static uint8_t szp(uint8_t value)
{
    return (uint8_t)((value & FLAG_S) | (value == 0u ? FLAG_Z : 0u) | parity_flag(value));
}

/* Applies one CB rotate or shift. Returns the flag byte the original leaves in F before the
 * SLL increment. *carry_out receives the x86 carry. */
static uint8_t rotate(unsigned int operation, uint8_t value, uint8_t flags, uint8_t *result)
{
    unsigned int carry = flags & FLAG_C, out;
    uint8_t shifted;
    switch (operation)
    {
    case 0: shifted = (uint8_t)(value << 1 | value >> 7); out = value >> 7; break;
    case 1: shifted = (uint8_t)(value >> 1 | value << 7); out = value & 1u; break;
    case 2: shifted = (uint8_t)(value << 1 | carry); out = value >> 7; break;
    case 3: shifted = (uint8_t)(value >> 1 | carry << 7); out = value & 1u; break;
    case 4:
    case 6: shifted = (uint8_t)(value << 1); out = value >> 7; break;
    case 5: shifted = (uint8_t)((value >> 1) | (value & 0x80u)); out = value & 1u; break;
    default: shifted = (uint8_t)(value >> 1); out = value & 1u; break;
    }
    *result = shifted;
    return (uint8_t)(szp(shifted) | out);
}

/* LAHF after CMP A,value masked with S, Z, H and N. LAHF always sets bit 1, which is N. */
static uint8_t compare_flags(uint8_t a, uint8_t value)
{
    uint8_t difference = (uint8_t)(a - value);
    return (uint8_t)((difference & FLAG_S) | (difference == 0u ? FLAG_Z : 0u) |
                     ((a & 15u) < (value & 15u) ? FLAG_H : 0u) | FLAG_N);
}

static int condition(const GensZ80 *z, GensZ80Op op)
{
    int set = (z->f & op.first) != 0u;
    return op.second ? !set : set;
}

static void call_to(Run *r, unsigned int length)
{
    GensZ80 *z = r->z;
    uint32_t sp = ((uint32_t)z->sp - 2u) & 0xffffu;
    uint32_t back = (uint32_t)(r->pc + length - z->base_pc);
    z->sp = (uint16_t)sp;
    write_word(r, sp, (uint16_t)back);
    rebase(z, r, pair(code_byte(r, 2u), code_byte(r, 1u)));
}

static void return_from(Run *r)
{
    GensZ80 *z = r->z;
    uint32_t target = read_word(r, z->sp);
    z->sp = (uint16_t)(z->sp + 2u);
    rebase(z, r, target);
}

/* Moves HL one step and returns the old value, like the inc/dec zxHL sequences. */
static uint32_t step_hl(GensZ80 *z, unsigned int decrement)
{
    uint32_t hl = pair(z->h, z->l);
    set_pair(z, GENS_Z80_PAIR_HL, (hl + (decrement ? 0xffffu : 1u)) & 0xffffu);
    return hl;
}

/* Flags produced by the original INI/IND/OUTI/OUTD tails: lahf after dec B, then a carry check
 * of operand + value and the value's sign moved into N. */
static uint8_t block_io_flags(uint8_t b_after, uint8_t b_before, uint8_t operand, uint8_t value)
{
    uint8_t flags = (uint8_t)(szp(b_after) & (FLAG_S | FLAG_Z | FLAG_P));
    (void)b_before;
    if ((unsigned int)operand + value > 0xffu)
        flags |= FLAG_H | FLAG_C;
    return (uint8_t)(flags | (value >> 7) << 1);
}

/* Returns the cycle cost or zero when the family is not ported. */
static unsigned int execute(Run *r, GensZ80Op op)
{
    GensZ80 *z = r->z;
    uint32_t address, write_pair_word;
    uint8_t value, result;

    switch (op.family)
    {
    case GENS_Z80_NOP:
        r->pc += 1u;
        return 4;
    case GENS_Z80_LD_R_R:
        value = *reg8(z, op.second);
        r->pc += 1u;
        *reg8(z, op.first) = value;
        return 4;
    case GENS_Z80_LD_R_N:
        value = code_byte(r, 1u);
        r->pc += 2u;
        *reg8(z, op.first) = value;
        return 7;
    case GENS_Z80_LD_R_MHL:
        address = pair(z->h, z->l);
        r->pc += 1u;
        value = read_byte(r, address);
        *reg8(z, op.first) = value;
        return 7;
    case GENS_Z80_LD_MHL_R:
        address = pair(z->h, z->l);
        r->pc += 1u;
        write_byte(r, address, *reg8(z, op.first));
        return 7;
    case GENS_Z80_LD_MHL_N:
        value = code_byte(r, 1u);
        address = pair(z->h, z->l);
        r->pc += 2u;
        write_byte(r, address, value);
        return 10;
    case GENS_Z80_LD_R_MXYD:
    case GENS_Z80_LD_MXYD_R:
        address = op.second == 0u ? pair(z->ixh, z->ixl) : pair(z->iyh, z->iyl);
        address = (address + (uint32_t)(int32_t)(int8_t)code_byte(r, 1u)) & 0xffffu;
        r->pc += 2u;
        if (op.family == GENS_Z80_LD_R_MXYD)
        {
            value = read_byte(r, address);
            *reg8(z, op.first) = value;
        }
        else
            write_byte(r, address, *reg8(z, op.first));
        return 15;
    case GENS_Z80_LD_MXYD_N:
        address = op.second == 0u ? pair(z->ixh, z->ixl) : pair(z->iyh, z->iyl);
        address = (address + (uint32_t)(int32_t)(int8_t)code_byte(r, 1u)) & 0xffffu;
        value = code_byte(r, 2u);
        r->pc += 3u;
        write_byte(r, address, value);
        return 15;
    case GENS_Z80_LD_A_MBC:
    case GENS_Z80_LD_A_MDE:
        address = op.family == GENS_Z80_LD_A_MBC ? pair(z->b, z->c) : pair(z->d, z->e);
        r->pc += 1u;
        z->a = read_byte(r, address);
        return 7;
    case GENS_Z80_LD_A_MNN:
    case GENS_Z80_LD_MNN_A:
        address = pair(code_byte(r, 2u), code_byte(r, 1u));
        r->pc += 3u;
        if (op.family == GENS_Z80_LD_A_MNN)
            z->a = read_byte(r, address);
        else
            write_byte(r, address, z->a);
        return 13;
    case GENS_Z80_LD_MBC_A:
    case GENS_Z80_LD_MDE_A:
        address = op.family == GENS_Z80_LD_MBC_A ? pair(z->b, z->c) : pair(z->d, z->e);
        r->pc += 1u;
        write_byte(r, address, z->a);
        return 7;
    case GENS_Z80_LD_A_I:
        flags_ld_a_ir(z, z->i, (uint8_t)(z->f & FLAG_C));
        r->pc += 2u;
        return 9;
    case GENS_Z80_LD_A_R:
        value = (uint8_t)((z->cycle_cnt - (uint32_t)r->cycles + z->cycle_td) >> 2);
        value = (uint8_t)((uint8_t)(value + z->r1) & 0x7fu);
        flags_ld_a_ir(z, value, (uint8_t)(z->f & FLAG_C));
        r->pc += 2u;
        return 9;
    case GENS_Z80_LD_I_A:
        r->pc += 2u;
        z->i = z->a;
        return 9;
    case GENS_Z80_LD_R_A:
        r->pc += 2u;
        z->r1 = z->a;
        return 9;
    case GENS_Z80_LD_RR_NN:
        set_pair(z, op.first, pair(code_byte(r, 2u), code_byte(r, 1u)));
        r->pc += 3u;
        return 10;
    case GENS_Z80_LD_RR_MNN:
    case GENS_Z80_LD_MNN_RR:
        if (op.second != 0u)
        {
            address = pair(code_byte(r, 3u), code_byte(r, 2u));
            r->pc += 4u;
        }
        else
        {
            address = pair(code_byte(r, 2u), code_byte(r, 1u));
            r->pc += 3u;
        }
        if (op.family == GENS_Z80_LD_RR_MNN)
            set_pair(z, op.first, read_word(r, address));
        else
            write_word(r, address, (uint16_t)get_pair(z, op.first));
        return op.second != 0u ? 20u : 16u;
    case GENS_Z80_LD_SP_RR:
        z->sp = (uint16_t)get_pair(z, op.first);
        r->pc += 1u;
        return 6;
    case GENS_Z80_PUSH:
        address = ((uint32_t)z->sp - 2u) & 0xffffu;
        r->pc += 1u;
        z->sp = (uint16_t)address;
        if (op.first == GENS_Z80_PAIR_AF)
            write_word(r, address, (uint16_t)pair(z->a, (uint8_t)((z->f & ~0x28u) | (z->fxyb & 0x28u))));
        else
            write_word(r, address, (uint16_t)get_pair(z, op.first));
        return 11;
    case GENS_Z80_POP:
        r->pc += 1u;
        address = read_word(r, z->sp);
        if (op.first == GENS_Z80_PAIR_AF)
        {
            z->f = z->fxyb = (uint8_t)address;
            z->a = (uint8_t)(address >> 8);
        }
        else
            set_pair(z, op.first, address);
        z->sp = (uint16_t)(z->sp + 2u);
        return 10;
    case GENS_Z80_EX_DE_HL:
        swap_bytes(&z->h, &z->d);
        swap_bytes(&z->l, &z->e);
        r->pc += 1u;
        return 4;
    case GENS_Z80_EX_AF_AF2:
        swap_bytes(&z->a, &z->a2);
        swap_bytes(&z->f, &z->f2);
        swap_bytes(&z->fxyb, &z->fxyb2);
        r->pc += 1u;
        return 4;
    case GENS_Z80_EXX:
        swap_bytes(&z->b, &z->b2);
        swap_bytes(&z->c, &z->c2);
        swap_bytes(&z->d, &z->d2);
        swap_bytes(&z->e, &z->e2);
        swap_bytes(&z->h, &z->h2);
        swap_bytes(&z->l, &z->l2);
        r->pc += 1u;
        return 4;
    case GENS_Z80_EX_MSP:
        r->pc += 1u;
        address = read_word(r, z->sp);
        write_pair_word = get_pair(z, op.first);
        set_pair(z, op.first, address);
        write_word(r, z->sp, (uint16_t)write_pair_word);
        return 19;
    case GENS_Z80_ALU_R:
        value = *reg8(z, op.second);
        r->pc += 1u;
        alu8(z, op.first, value,
             (op.second == GENS_Z80_A || op.second == GENS_Z80_H || op.second == GENS_Z80_L) ?
             r->opcode : value);
        return 4;
    case GENS_Z80_ALU_N:
        value = code_byte(r, 1u);
        r->pc += 2u;
        alu8(z, op.first, value, value);
        return 7;
    case GENS_Z80_ALU_MHL:
        address = pair(z->h, z->l);
        r->pc += 1u;
        value = read_byte(r, address);
        alu8(z, op.first, value, value);
        return 7;
    case GENS_Z80_ALU_MXYD:
        address = index_address(z, r, op.second);
        r->pc += 2u;
        value = read_byte(r, address);
        alu8(z, op.first, value, value);
        return 15;
    case GENS_Z80_INCDEC_R:
        value = *reg8(z, op.first);
        r->pc += 1u;
        *reg8(z, op.first) = incdec_value(z, value, op.second);
        return 4;
    case GENS_Z80_INCDEC_MHL:
        address = pair(z->h, z->l);
        r->pc += 1u;
        value = read_byte(r, address);
        write_byte(r, address, incdec_value(z, value, op.second));
        return 11;
    case GENS_Z80_INCDEC_MXYD:
        address = index_address(z, r, op.first);
        r->pc += 2u;
        value = read_byte(r, address);
        write_byte(r, address, incdec_value(z, value, op.second));
        return 22;
    case GENS_Z80_INCDEC_RR:
        set_pair(z, op.first, (get_pair(z, op.first) + (op.second ? 0xffffu : 1u)) & 0xffffu);
        r->pc += 1u;
        return 6;
    case GENS_Z80_DAA:
        /* The original adds H as a 0x1000 byte offset, past its 2048-word table, so the result
         * depends on the original executable layout. The port uses the intended H half (word
         * 0x400) and counts each occurrence so tooling can report a possible desync source. */
        address = z->a | (uint32_t)(z->f & 3u) << 8;
        if ((z->f & FLAG_H) != 0u)
        {
            address |= 0x400u;
            ++z->daa_overflows;
        }
        address = gens_z80_daa_table[address];
        z->a = (uint8_t)address;
        z->f = (uint8_t)(address >> 8);
        z->fxyb = z->f;
        r->pc += 1u;
        return 4;
    case GENS_Z80_CPL:
        z->a = (uint8_t)~z->a;
        z->f |= FLAG_H | FLAG_N;
        z->fxyb = z->a;
        r->pc += 1u;
        return 4;
    case GENS_Z80_NEG:
        value = (uint8_t)(0u - z->a);
        z->f = (uint8_t)((value & FLAG_S) | FLAG_N | ((z->a & 15u) != 0u ? FLAG_H : 0u) |
                         (z->a != 0u ? FLAG_C : 0u) | (z->a == 0x80u ? FLAG_P : 0u) |
                         (value == 0u ? FLAG_Z : 0u));
        z->a = z->fxyb = value;
        r->pc += 2u;
        return 8;
    case GENS_Z80_CCF:
        value = (uint8_t)((z->f & FLAG_C) << 4);
        z->f = (uint8_t)(((z->f ^ FLAG_C) & (FLAG_S | FLAG_Z | FLAG_P | FLAG_C)) | value);
        r->pc += 1u;
        return 4;
    case GENS_Z80_SCF:
        z->f = (uint8_t)((z->f & (FLAG_S | FLAG_Z | FLAG_P)) | FLAG_C);
        z->fxyb = z->a;
        r->pc += 1u;
        return 4;
    case GENS_Z80_HALT:
        z->status |= GENS_Z80_HALTED;
        r->pc += 1u;
        r->cycles = -1;
        r->quit_edx = z->status;
        r->force_quit = 1;
        return 1;
    case GENS_Z80_DI:
        z->iff1 = z->iff2 = 0;
        r->pc += 1u;
        r->skip_check = 1;
        return 4;
    case GENS_Z80_EI:
        z->cycle_sup = (uint32_t)r->cycles;
        z->iff1 = z->iff2 = FLAG_P;
        r->pc += 1u;
        r->cycles = 0;
        r->skip_check = 1;
        return 4;
    case GENS_Z80_IM:
        z->im = (uint8_t)op.first;
        r->pc += 2u;
        return 8;
    case GENS_Z80_ADD16:
        address = get_pair(z, op.first) + get_pair(z, op.second);
        value = (uint8_t)(((get_pair(z, op.first) & 0xfffu) + (get_pair(z, op.second) & 0xfffu)) >
                          0xfffu ? FLAG_H : 0u);
        z->f = (uint8_t)((z->f & (FLAG_S | FLAG_Z | FLAG_P)) | value |
                         (address > 0xffffu ? FLAG_C : 0u));
        set_pair(z, op.first, address & 0xffffu);
        z->fxyb = (uint8_t)(address >> 8);
        r->pc += 1u;
        return 11;
    case GENS_Z80_ADC_HL:
    case GENS_Z80_SBC_HL:
        adc_sbc_hl(z, op.first, op.family == GENS_Z80_SBC_HL);
        r->pc += 2u;
        return 15;
    case GENS_Z80_ROT_A:
        value = z->a;
        address = z->f & FLAG_C;
        switch (op.first)
        {
        case 0:
            z->a = (uint8_t)(value << 1 | value >> 7);
            address = (uint32_t)value >> 7;
            break;
        case 1:
            z->a = (uint8_t)(value << 1 | address);
            address = (uint32_t)value >> 7;
            break;
        case 2:
            z->a = (uint8_t)(value >> 1 | value << 7);
            address = (uint32_t)(value & 1u);
            break;
        default:
            z->a = (uint8_t)(value >> 1 | address << 7);
            address = (uint32_t)(value & 1u);
            break;
        }
        z->f = (uint8_t)((z->f & (FLAG_S | FLAG_Z | FLAG_P)) | address);
        z->fxyb = z->a;
        r->pc += 1u;
        return 4;
    case GENS_Z80_ROT_R:
        value = *reg8(z, op.second);
        r->pc += 2u;
        z->f = rotate(op.first, value, z->f, &result);
        z->fxyb = result;
        if (op.first == 6u)
        {
            z->f |= FLAG_P;
            /* B to E are stored before the increment, A, H and L afterwards. */
            if (op.second == GENS_Z80_A || op.second == GENS_Z80_H || op.second == GENS_Z80_L)
                result = (uint8_t)(result + 1u);
        }
        *reg8(z, op.second) = result;
        return 8;
    case GENS_Z80_ROT_MHL:
        address = pair(z->h, z->l);
        r->pc += 2u;
        value = read_byte(r, address);
        z->f = rotate(op.first, value, z->f, &result);
        z->fxyb = result;
        if (op.first == 6u)
        {
            result = (uint8_t)(result + 1u);
            z->f |= FLAG_P;
        }
        write_byte(r, pair(z->h, z->l), result);
        return 15;
    case GENS_Z80_ROT_MXYD:
        address = index_address(z, r, op.first >> 3);
        r->pc += 3u;
        value = read_byte(r, address);
        z->f = rotate(op.first & 7u, value, z->f, &result);
        z->fxyb = result;
        if ((op.first & 7u) == 6u)
        {
            result = (uint8_t)(result + 1u);
            z->f |= FLAG_P;
        }
        if (op.second != 0u)
            *reg8(z, op.second - 1u) = result;
        write_byte(r, address, result);
        return 23;
    case GENS_Z80_BIT_R:
    case GENS_Z80_BIT_MHL:
    case GENS_Z80_BIT_MXYD:
        z->f &= FLAG_C;
        if (op.family == GENS_Z80_BIT_R)
        {
            value = *reg8(z, op.second);
            result = value;
            r->pc += 2u;
        }
        else
        {
            address = op.family == GENS_Z80_BIT_MHL ? pair(z->h, z->l) :
                index_address(z, r, op.second);
            value = read_byte(r, address);
            result = (uint8_t)(r->ecx >> 8);
            r->pc += op.family == GENS_Z80_BIT_MHL ? 2u : 3u;
        }
        if ((value & (1u << op.first)) == 0u)
        {
            z->fxyb = 0;
            z->f |= FLAG_Z | FLAG_H | FLAG_P;
        }
        else if (op.first == 7u)
        {
            z->fxyb = 0;
            z->f |= FLAG_S | FLAG_H;
        }
        else if (op.first == 5u || op.first == 3u)
        {
            z->fxyb = result;
            z->f |= (uint8_t)((op.first == 5u ? 0x20u : 0x08u) | FLAG_H);
        }
        else
        {
            z->fxyb = 0;
            z->f |= FLAG_H;
        }
        return op.family == GENS_Z80_BIT_R ? 8u : op.family == GENS_Z80_BIT_MHL ? 12u : 16u;
    case GENS_Z80_SETRES_R:
        if ((op.first & 8u) != 0u)
            *reg8(z, op.second) |= (uint8_t)(1u << (op.first & 7u));
        else
            *reg8(z, op.second) &= (uint8_t)~(1u << (op.first & 7u));
        r->pc += 2u;
        return 8;
    case GENS_Z80_SETRES_MHL:
    case GENS_Z80_SETRES_MXYD:
        if (op.family == GENS_Z80_SETRES_MHL)
        {
            address = pair(z->h, z->l);
            r->pc += 2u;
        }
        else
        {
            address = index_address(z, r, op.first >> 4);
            r->pc += 3u;
        }
        value = read_byte(r, address);
        if ((op.first & 8u) != 0u)
            value |= (uint8_t)(1u << (op.first & 7u));
        else
            value &= (uint8_t)~(1u << (op.first & 7u));
        if (op.family == GENS_Z80_SETRES_MHL)
            address = pair(z->h, z->l);
        else if (op.second != 0u)
            *reg8(z, op.second - 1u) = value;
        write_byte(r, address, value);
        return op.family == GENS_Z80_SETRES_MHL ? 15u : 19u;
    case GENS_Z80_JP:
        rebase(z, r, pair(code_byte(r, 2u), code_byte(r, 1u)));
        return 10;
    case GENS_Z80_JPCC:
        if (condition(z, op))
            rebase(z, r, pair(code_byte(r, 2u), code_byte(r, 1u)));
        else
            r->pc += 3u;
        return 10;
    case GENS_Z80_JR:
        r->pc += (uintptr_t)(2 + (intptr_t)(int8_t)code_byte(r, 1u));
        return 12;
    case GENS_Z80_JRCC:
        if (!condition(z, op))
        {
            r->pc += 2u;
            return 7;
        }
        r->pc += (uintptr_t)(2 + (intptr_t)(int8_t)code_byte(r, 1u));
        return 12;
    case GENS_Z80_JP_RR:
        rebase(z, r, get_pair(z, op.first));
        return 4;
    case GENS_Z80_DJNZ:
        value = (uint8_t)(z->b - 1u);
        z->b = value;
        if (value == 0u)
        {
            r->pc += 2u;
            return 10;
        }
        r->pc += (uintptr_t)(2 + (intptr_t)(int8_t)code_byte(r, 1u));
        return 13;
    case GENS_Z80_CALL:
        call_to(r, 3u);
        return 17;
    case GENS_Z80_CALLCC:
        if (!condition(z, op))
        {
            r->pc += 3u;
            return 10;
        }
        call_to(r, 3u);
        return 17;
    case GENS_Z80_RET:
        return_from(r);
        return 10;
    case GENS_Z80_RETCC:
        if (!condition(z, op))
        {
            r->pc += 1u;
            return 5;
        }
        return_from(r);
        return 17;
    case GENS_Z80_RETN:
        /* The original copies IFF2 onto itself, so IFF1 is not restored. */
        return_from(r);
        return 14;
    case GENS_Z80_RST:
        address = code_byte(r, 0u) & 0x38u;
        write_pair_word = (uint32_t)(r->pc + 1u - z->base_pc);
        z->sp = (uint16_t)(z->sp - 2u);
        write_word(r, z->sp, (uint16_t)write_pair_word);
        rebase(z, r, address);
        return 11;
    case GENS_Z80_IN_N:
        /* Gens builds the core with port I/O disabled: the input byte is zero and A is kept. */
        r->pc += 2u;
        return 11;
    case GENS_Z80_IN_RBC:
        r->pc += 2u;
        z->f = (uint8_t)(FLAG_Z | FLAG_P | (z->f & FLAG_C));
        z->fxyb = 0;
        if (op.first != 0xffu)
            *reg8(z, op.first) = 0;
        return 12;
    case GENS_Z80_OUT_N:
        r->pc += 2u;
        return 11;
    case GENS_Z80_OUT_RBC:
        r->pc += 2u;
        return 12;
    case GENS_Z80_INX:
        r->pc += 2u;
        z->f = 0;
        address = step_hl(z, op.first);
        write_byte(r, address, 0);
        value = (uint8_t)(z->b - 1u);
        z->f = block_io_flags(value, z->b, (uint8_t)(z->c + 1u), 0);
        z->b = value;
        return 16;
    case GENS_Z80_INXR:
        for (;;)
        {
            z->f = 0;
            address = step_hl(z, op.first);
            write_byte(r, address, 0);
            value = (uint8_t)(z->b - 1u);
            if (value == 0u)
            {
                z->f = FLAG_Z | FLAG_P;
                z->fxyb = 0;
                z->b = 0;
                r->pc += 2u;
                return 16;
            }
            r->cycles -= 21;
            z->b = value;
            if (r->cycles < 0)
            {
                z->f = (uint8_t)(value & FLAG_S);
                z->fxyb = value;
                r->quit_edx = value;
                r->force_quit = 1;
                return 1;
            }
        }
    case GENS_Z80_OUTX:
        r->pc += 2u;
        result = read_byte(r, pair(z->h, z->l));
        z->f = result;
        step_hl(z, op.first);
        value = (uint8_t)(z->b - 1u);
        z->f = block_io_flags(value, z->b, z->l, result);
        z->b = value;
        return 16;
    case GENS_Z80_OUTXR:
        for (;;)
        {
            result = read_byte(r, pair(z->h, z->l));
            z->f = result;
            step_hl(z, op.first);
            value = (uint8_t)(z->b - 1u);
            if (value == 0u)
            {
                z->f = FLAG_Z | FLAG_P;
                if ((unsigned int)z->l + result > 0xffu)
                    z->f |= FLAG_H | FLAG_C;
                z->f |= (uint8_t)((result >> 7) << 1);
                z->fxyb = 0;
                z->b = 0;
                r->pc += 2u;
                return 16;
            }
            r->cycles -= 21;
            z->b = value;
            if (r->cycles < 0)
            {
                z->f = (uint8_t)(value & FLAG_S);
                if ((unsigned int)z->l + result > 0xffu)
                    z->f |= FLAG_H | FLAG_C;
                z->f |= (uint8_t)((result >> 7) << 1);
                z->fxyb = value;
                r->quit_edx = value;
                r->force_quit = 1;
                return 1;
            }
        }
    case GENS_Z80_LDX:
        r->pc += 2u;
        value = read_byte(r, pair(z->h, z->l));
        write_byte(r, pair(z->d, z->e), value);
        z->f &= FLAG_S | FLAG_Z | FLAG_C;
        step_hl(z, op.first);
        set_pair(z, GENS_Z80_PAIR_DE, (pair(z->d, z->e) + (op.first ? 0xffffu : 1u)) & 0xffffu);
        address = (pair(z->b, z->c) - 1u) & 0xffffu;
        set_pair(z, GENS_Z80_PAIR_BC, address);
        if (address != 0u)
            z->f |= FLAG_P;
        return 16;
    case GENS_Z80_LDXR:
        for (;;)
        {
            value = read_byte(r, pair(z->h, z->l));
            write_byte(r, pair(z->d, z->e), value);
            step_hl(z, op.first);
            set_pair(z, GENS_Z80_PAIR_DE, (pair(z->d, z->e) + (op.first ? 0xffffu : 1u)) & 0xffffu);
            address = (pair(z->b, z->c) - 1u) & 0xffffu;
            set_pair(z, GENS_Z80_PAIR_BC, address);
            if (address == 0u)
            {
                r->pc += 2u;
                z->f &= FLAG_S | FLAG_Z | FLAG_C;
                return 16;
            }
            r->cycles -= 21;
            if (r->cycles < 0)
            {
                z->f &= FLAG_S | FLAG_Z | FLAG_C;
                r->quit_edx = address;
                r->force_quit = 1;
                return 1;
            }
        }
    case GENS_Z80_CPX:
        r->pc += 2u;
        value = read_byte(r, pair(z->h, z->l));
        result = (uint8_t)(z->f & FLAG_C);
        step_hl(z, op.first);
        z->f = (uint8_t)(compare_flags(z->a, value) | result);
        address = (pair(z->b, z->c) - 1u) & 0xffffu;
        set_pair(z, GENS_Z80_PAIR_BC, address);
        if (address != 0u)
            z->f |= FLAG_P;
        return 16;
    case GENS_Z80_CPXR:
        z->f &= FLAG_C;
        for (;;)
        {
            value = read_byte(r, pair(z->h, z->l));
            address = (pair(z->b, z->c) - 1u) & 0xffffu;
            step_hl(z, op.first);
            if (address == 0u)
            {
                set_pair(z, GENS_Z80_PAIR_BC, 0);
                z->f = (uint8_t)(compare_flags(z->a, value) | z->f);
                r->pc += 2u;
                return 18;
            }
            set_pair(z, GENS_Z80_PAIR_BC, address);
            if (z->a == value)
            {
                z->f = (uint8_t)(compare_flags(z->a, value) | z->f | FLAG_P);
                r->pc += 2u;
                return 18;
            }
            r->cycles -= 21;
            if (r->cycles < 0)
            {
                z->f = (uint8_t)(compare_flags(z->a, value) | z->f | FLAG_P);
                r->quit_edx = value;
                r->force_quit = 1;
                return 1;
            }
        }
    case GENS_Z80_RLD:
    case GENS_Z80_RRD:
        r->pc += 2u;
        value = read_byte(r, pair(z->h, z->l));
        result = (uint8_t)(z->f & FLAG_C);
        if (op.family == GENS_Z80_RLD)
        {
            write_pair_word = (uint8_t)(value << 4 | (z->a & 0xf0u));
            z->a = (uint8_t)((z->a & 0xf0u) | value >> 4);
        }
        else
        {
            write_pair_word = (uint8_t)((z->a << 4) | value >> 4);
            z->a = (uint8_t)((z->a & 0xf0u) | (value & 0x0fu));
        }
        z->f = (uint8_t)(szp(z->a) | result);
        z->fxyb = z->a;
        write_byte(r, pair(z->h, z->l), (uint8_t)write_pair_word);
        return 18;
    default:
        return 0;
    }
}

static uint8_t default_read_b(GensZ80 *z, uint32_t address)
{
    return z->default_memory[address & 0xffffu];
}

static uint16_t default_read_w(GensZ80 *z, uint32_t address)
{
    address &= 0xffffu;
    return (uint16_t)(z->default_memory[address] | z->default_memory[(address + 1u) & 0xffffu] << 8);
}

static void default_write_b(GensZ80 *z, uint32_t address, uint8_t value)
{
    z->default_memory[address & 0xffffu] = value;
}

static void default_write_w(GensZ80 *z, uint32_t address, uint16_t value)
{
    address &= 0xffffu;
    z->default_memory[address] = (uint8_t)value;
    z->default_memory[(address + 1u) & 0xffffu] = (uint8_t)(value >> 8);
}

void gens_z80_init(GensZ80 *z)
{
    unsigned int i;
    memset(z, 0, sizeof(*z));
    for (i = 0; i < 256u; ++i)
    {
        z->read_b[i] = default_read_b;
        z->read_w[i] = default_read_w;
        z->write_b[i] = default_write_b;
        z->write_w[i] = default_write_w;
        z->fetch[i] = (uintptr_t)z->default_memory;
    }
    z->in = default_read_b;
    z->out = default_write_b;
}

void gens_z80_set_opaque(GensZ80 *z, void *opaque)
{
    z->opaque = opaque;
}

void gens_z80_reset(GensZ80 *z)
{
    Run r;
    uint32_t cycles = z->cycle_cnt;
    memset(&r, 0, sizeof(r));
    r.z = z;
    z->a = z->f = z->fxyw = z->fxyb = 0;
    z->b = z->c = z->d = z->e = z->h = z->l = 0;
    z->ixh = z->ixl = z->iyh = z->iyl = 0xffu;
    z->sp = 0;
    z->a2 = z->f2 = z->fxyw2 = z->fxyb2 = 0;
    z->b2 = z->c2 = z->d2 = z->e2 = z->h2 = z->l2 = 0;
    z->iff1 = z->iff2 = z->r1 = z->r2 = z->i = z->im = z->int_vect = z->int_line = 0;
    z->status = 0;
    z->cycle_cnt = cycles;
    z->cycle_td = z->cycle_io = z->cycle_sup = 0;
    rebase(z, &r, 0);
    z->pc = r.pc;
    z->f = 0x40u;
}

static void add_range(uint32_t *low, uint32_t *high)
{
    *low &= 0xffu;
    *high &= 0xffu;
}

void gens_z80_add_read_b(GensZ80 *z, uint32_t low, uint32_t high, GensZ80ReadByte handler)
{
    add_range(&low, &high);
    for (; low <= high; ++low)
        z->read_b[low] = handler;
}

void gens_z80_add_read_w(GensZ80 *z, uint32_t low, uint32_t high, GensZ80ReadWord handler)
{
    add_range(&low, &high);
    for (; low <= high; ++low)
        z->read_w[low] = handler;
}

void gens_z80_add_write_b(GensZ80 *z, uint32_t low, uint32_t high, GensZ80WriteByte handler)
{
    add_range(&low, &high);
    for (; low <= high; ++low)
        z->write_b[low] = handler;
}

void gens_z80_add_write_w(GensZ80 *z, uint32_t low, uint32_t high, GensZ80WriteWord handler)
{
    add_range(&low, &high);
    for (; low <= high; ++low)
        z->write_w[low] = handler;
}

void gens_z80_add_fetch(GensZ80 *z, uint32_t low, uint32_t high, const uint8_t *region)
{
    uintptr_t base;
    add_range(&low, &high);
    base = (uintptr_t)region - ((uintptr_t)low << 8);
    for (; low <= high; ++low)
        z->fetch[low] = base;
}

uint32_t gens_z80_read_odo(const GensZ80 *z)
{
    if ((z->status & GENS_Z80_RUNNING) == 0u)
        return z->cycle_cnt;
    return z->cycle_cnt + z->cycle_td - z->cycle_io;
}

void gens_z80_clear_odo(GensZ80 *z)
{
    z->cycle_cnt = 0;
}

void gens_z80_set_odo(GensZ80 *z, uint32_t odo)
{
    z->cycle_cnt = odo;
}

void gens_z80_add_cycles(GensZ80 *z, uint32_t cycles)
{
    if ((z->status & GENS_Z80_RUNNING) != 0u)
        z->cycle_io -= cycles;
    else
        z->cycle_cnt += cycles;
}

uint32_t gens_z80_exec(GensZ80 *z, uint32_t odo)
{
    Run r;
    GensZ80Op op;
    uintptr_t start;
    int32_t start_cycles;
    uint32_t sup;
    unsigned int cost;

    if (odo <= z->cycle_cnt)
        return GENS_Z80_EXEC_NO_CYCLES;
    r.z = z;
    r.skip_check = r.force_quit = 0;
    r.cycles = (int32_t)(odo - z->cycle_cnt) - 1;
    r.pc = z->pc;
    check_int(&r);
    sup = z->status;
    if ((sup & (GENS_Z80_HALTED | GENS_Z80_FAULTED | GENS_Z80_RUNNING)) != 0u)
        goto cannot_run;
    z->status = sup | GENS_Z80_RUNNING;
    z->cycle_sup = 0;
    z->cycle_td = (uint32_t)r.cycles;
    if (z->trace != 0)
        z->trace(z, (uint32_t)(r.pc - z->base_pc));
    r.opcode = code_byte(&r, 0);
    op = gens_z80_op_table[r.opcode];

    for (;;)
    {
        start = r.pc;
        start_cycles = r.cycles;
        for (;;)
        {
            if (op.family == GENS_Z80_PREFIX_CB)
            {
                r.opcode = code_byte(&r, 1);
                op = gens_z80_cb_table[r.opcode];
            }
            else if (op.family == GENS_Z80_PREFIX_ED)
            {
                r.opcode = code_byte(&r, 1);
                op = gens_z80_ed_table[r.opcode];
            }
            else if (op.family == GENS_Z80_PREFIX_DDCB)
            {
                r.opcode = code_byte(&r, 2);
                op = gens_z80_ddcb_table[r.opcode];
            }
            else if (op.family == GENS_Z80_PREFIX_FDCB)
            {
                r.opcode = code_byte(&r, 2);
                op = gens_z80_fdcb_table[r.opcode];
            }
            else if (op.family == GENS_Z80_PREFIX_DD || op.family == GENS_Z80_PREFIX_FD)
            {
                r.opcode = code_byte(&r, 1);
                op = (op.family == GENS_Z80_PREFIX_DD ? gens_z80_dd_table : gens_z80_fd_table)
                    [r.opcode];
                r.cycles -= 4;
                r.pc += 1u;
            }
            else
                break;
        }
        cost = execute(&r, op);
        if (cost == 0u)
        {
            r.pc = start;
            r.cycles = start_cycles;
            z->pc = r.pc;
            z->cycle_cnt = z->cycle_td - (uint32_t)r.cycles + z->cycle_cnt;
            z->status &= ~GENS_Z80_RUNNING;
            return GENS_Z80_EXEC_UNIMPLEMENTED;
        }
        if (r.force_quit)
        {
            sup = r.quit_edx;
            z->pc = r.pc;
            z->status &= ~GENS_Z80_RUNNING;
            z->cycle_cnt = z->cycle_td - (uint32_t)r.cycles + z->cycle_cnt;
            goto cannot_run;
        }
        r.cycles -= (int32_t)cost;
        if (r.skip_check)
            r.skip_check = 0;
        else if (r.cycles < 0)
        {
            sup = z->cycle_sup;
            r.cycles += (int32_t)sup;
            z->cycle_sup = 0;
            if (r.cycles < 0)
            {
                z->pc = r.pc;
                z->status &= ~GENS_Z80_RUNNING;
                z->cycle_cnt = z->cycle_td - (uint32_t)r.cycles + z->cycle_cnt;
                goto cannot_run;
            }
            check_int(&r);
        }
        if (z->trace != 0)
            z->trace(z, (uint32_t)(r.pc - z->base_pc));
        r.opcode = code_byte(&r, 0);
        op = gens_z80_op_table[r.opcode];
    }

cannot_run:
    if ((sup & GENS_Z80_HALTED) != 0u)
        z->cycle_cnt += (uint32_t)r.cycles;
    return 0;
}

void gens_z80_nmi(GensZ80 *z)
{
    z->int_vect = 0x66u;
    z->int_line = 0x80u;
    if ((z->status & GENS_Z80_RUNNING) != 0u)
    {
        z->cycle_sup = z->cycle_io;
        z->cycle_io = 0;
    }
}

void gens_z80_interrupt(GensZ80 *z, uint32_t vector)
{
    z->int_vect = (uint8_t)vector;
    z->int_line = FLAG_P;
    if ((z->status & GENS_Z80_RUNNING) != 0u)
    {
        z->cycle_sup = z->cycle_io;
        z->cycle_io = 0;
    }
}

uint32_t gens_z80_get_pc(const GensZ80 *z)
{
    if ((z->status & GENS_Z80_RUNNING) != 0u)
        return 0xffffffffu;
    return (uint32_t)(z->pc - z->base_pc);
}

static uint32_t get_af(uint8_t a, uint8_t f, uint8_t fxyb)
{
    return (uint32_t)a << 8 | (uint8_t)((f & ~0x28u) | (fxyb & 0x28u));
}

uint32_t gens_z80_get_af(const GensZ80 *z)
{
    return (z->status & GENS_Z80_RUNNING) != 0u ? 0xffffffffu : get_af(z->a, z->f, z->fxyb);
}

uint32_t gens_z80_get_af2(const GensZ80 *z)
{
    return (z->status & GENS_Z80_RUNNING) != 0u ? 0xffffffffu : get_af(z->a2, z->f2, z->fxyb2);
}

void gens_z80_set_pc(GensZ80 *z, uint32_t pc)
{
    Run r;
    memset(&r, 0, sizeof(r));
    r.z = z;
    if ((z->status & GENS_Z80_RUNNING) != 0u)
        return;
    rebase(z, &r, pc & 0xffffu);
    z->pc = r.pc;
}

void gens_z80_set_af(GensZ80 *z, uint32_t af)
{
    if ((z->status & GENS_Z80_RUNNING) != 0u)
        return;
    z->fxyb = (uint8_t)(af & 0x28u);
    z->f = (uint8_t)(af & ~0x28u);
    z->a = (uint8_t)(af >> 8);
}

void gens_z80_set_af2(GensZ80 *z, uint32_t af)
{
    if ((z->status & GENS_Z80_RUNNING) != 0u)
        return;
    z->fxyb2 = (uint8_t)(af & 0x28u);
    z->f2 = (uint8_t)(af & ~0x28u);
    z->a2 = (uint8_t)(af >> 8);
}
