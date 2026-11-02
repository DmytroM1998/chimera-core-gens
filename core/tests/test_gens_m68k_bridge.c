#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "gens_m68k_bus.h"

#ifndef MAP_FIXED_NOREPLACE
#error MAP_FIXED_NOREPLACE is required for the fixed-address test
#endif

typedef void (*Bridge)(void);

#define DECLARE_BRIDGE(name) extern void gens_m68k_bridge_##name(void)
DECLARE_BRIDGE(read_byte);
DECLARE_BRIDGE(read_byte_predecrement);
DECLARE_BRIDGE(read_word);
DECLARE_BRIDGE(read_word_predecrement);
DECLARE_BRIDGE(read_long);
DECLARE_BRIDGE(read_long_predecrement);
DECLARE_BRIDGE(write_byte);
DECLARE_BRIDGE(write_byte_predecrement);
DECLARE_BRIDGE(write_word);
DECLARE_BRIDGE(write_word_predecrement);
DECLARE_BRIDGE(write_long);
DECLARE_BRIDGE(write_long_predecrement);

struct Registers {
    uint64_t rax, rbx, rcx, rdx, rsi, rbp, rdi, r8, r9, r11;
    uint64_t r12, r13, r14, r15, r10, carry;
};

_Static_assert(offsetof(struct Registers, r11) == 72 &&
               offsetof(struct Registers, r12) == 80 &&
               offsetof(struct Registers, r10) == 112 &&
               offsetof(struct Registers, carry) == 120 &&
               sizeof(struct Registers) == 128, "unexpected test harness layout");

extern int bridge_bad_stack_alignment;
extern void run_m68k_bridge(GensM68kBus *, struct Registers *, Bridge, uint64_t);

struct Probe {
    unsigned calls, hooks, fail_call;
    uint32_t addresses[2];
    uint16_t values[2];
};

struct Arena {
    uint8_t ram[65536];
    GensM68kBus bus;
    GensM68kBusState state;
    struct Probe probe;
};

static int after_call(struct Probe *probe, GensM68kBusState *state)
{
    ++probe->calls;
    state->io_fetch_pc += (uintptr_t)UINT64_C(0x100000000);
    state->io_fetch_base += 0x20u;
    state->io_cycle_counter += 9u;
    state->access_address = UINT32_C(0xfe123456);
    return probe->calls != probe->fail_call;
}

static int read_word(void *opaque, GensM68kBusState *state, uint32_t address, uint16_t *value)
{
    struct Probe *probe = opaque;
    assert(probe->calls < 2);
    probe->addresses[probe->calls] = address;
    *value = probe->calls == 0 ? UINT16_C(0x1122) : UINT16_C(0x3344);
    return after_call(probe, state);
}

static int write_word(void *opaque, GensM68kBusState *state, uint32_t address, uint16_t value)
{
    struct Probe *probe = opaque;
    assert(probe->calls < 2);
    probe->addresses[probe->calls] = address;
    probe->values[probe->calls] = value;
    return after_call(probe, state);
}

static void hook(void *opaque, GensM68kBusState *state, enum GensM68kBusHook kind)
{
    struct Probe *probe = opaque;
    assert(state != NULL);
    assert(kind >= GENS_M68K_READ_BYTE && kind <= GENS_M68K_WRITE_LONG);
    ++probe->hooks;
}

static void reset_arena(struct Arena *arena)
{
    memset(arena, 0, sizeof(*arena));
    arena->bus.ram = arena->ram;
    arena->bus.ram_size = sizeof(arena->ram);
    arena->bus.opaque = &arena->probe;
    arena->bus.state = &arena->state;
    arena->bus.read_word = read_word;
    arena->bus.write_word = write_word;
    arena->bus.hook = hook;
}

static struct Registers registers_for(uint32_t address)
{
    struct Registers r = {
        UINT64_C(0xdeadbeef12345678), UINT64_C(0x1212121234343434),
        UINT64_C(0x55667788a1b2c3d4), UINT64_C(0xabcdef0000000000) | address,
        UINT64_C(0x36fab001236), UINT64_C(0x36f00002000),
        UINT64_C(0xfeedbeef00001388), UINT64_C(0x0102030405060708),
        UINT64_C(0x1112131415161718), UINT64_C(0x2122232425262728),
        0, UINT64_C(0x3132333435363738), UINT64_C(0x4142434445464748),
        UINT64_C(0x5152535455565758), 0, 0
    };
    return r;
}

static void check_preserved(const struct Registers *actual, const struct Registers *before,
                            const GensM68kBus *bus)
{
    assert(actual->rax == before->rax && actual->rbx == before->rbx);
    assert(actual->r8 == before->r8 && actual->r9 == before->r9 &&
           actual->r11 == before->r11 && actual->r13 == before->r13 &&
           actual->r14 == before->r14 && actual->r15 == before->r15);
    assert(actual->r12 == (uintptr_t)bus);
}

struct BridgeCase {
    Bridge function;
    unsigned size;
    int write;
};

int main(void)
{
    static const struct BridgeCase cases[] = {
        {gens_m68k_bridge_read_byte, 1, 0},
        {gens_m68k_bridge_read_byte_predecrement, 1, 0},
        {gens_m68k_bridge_read_word, 2, 0},
        {gens_m68k_bridge_read_word_predecrement, 2, 0},
        {gens_m68k_bridge_read_long, 4, 0},
        {gens_m68k_bridge_read_long_predecrement, 4, 0},
        {gens_m68k_bridge_write_byte, 1, 1},
        {gens_m68k_bridge_write_byte_predecrement, 1, 1},
        {gens_m68k_bridge_write_word, 2, 1},
        {gens_m68k_bridge_write_word_predecrement, 2, 1},
        {gens_m68k_bridge_write_long, 4, 1},
        {gens_m68k_bridge_write_long_predecrement, 4, 1}
    };
    const uintptr_t guest_base = (uintptr_t)UINT64_C(0x36f00000000);
    struct Arena *arena;
    unsigned skew;
    size_t i;

    arena = mmap((void *)guest_base, sizeof(*arena), PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (arena == MAP_FAILED)
    {
        perror("fixed-address bridge test mmap");
        return 1;
    }
    if ((uintptr_t)arena != guest_base)
    {
        fprintf(stderr, "fixed-address bridge test mapped an unexpected address\n");
        if (munmap(arena, sizeof(*arena)) != 0)
            perror("fixed-address bridge test munmap");
        return 1;
    }

    for (skew = 0; skew <= 8; skew += 8)
    {
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        {
            struct Registers before = registers_for(UINT32_C(0xafe00020));
            struct Registers after = before;
            uint32_t expected = cases[i].size == 1 ? UINT32_C(0x11) :
                                cases[i].size == 2 ? UINT32_C(0x1122) :
                                UINT32_C(0x11223344);
            reset_arena(arena);
            arena->ram[0x20] = 0x22;
            arena->ram[0x21] = 0x11;
            arena->ram[0x22] = 0x44;
            arena->ram[0x23] = 0x33;
            run_m68k_bridge(&arena->bus, &after, cases[i].function, skew);
            check_preserved(&after, &before, &arena->bus);
            assert(after.r10 == GENS_M68K_BUS_OK && after.carry == 0);
            assert(after.rsi == before.rsi && after.rbp == before.rbp);
            assert(after.rdi == (uint32_t)before.rdi && after.rdx == (uint32_t)before.rdx);
            assert(after.rcx == (cases[i].write ? before.rcx : expected));
            assert(arena->probe.hooks == 1 && arena->probe.calls == 0);
            if (cases[i].write && cases[i].size == 1)
                assert(arena->ram[0x21] == 0xd4);
            if (cases[i].write && cases[i].size == 2)
                assert(arena->ram[0x20] == 0xd4 && arena->ram[0x21] == 0xc3);
            if (cases[i].write && cases[i].size == 4)
                assert(arena->ram[0x20] == 0xb2 && arena->ram[0x21] == 0xa1 &&
                       arena->ram[0x22] == 0xd4 && arena->ram[0x23] == 0xc3);
        }

        {
            struct Registers before = registers_for(UINT32_C(0x00c00000));
            struct Registers after = before;
            reset_arena(arena);
            run_m68k_bridge(&arena->bus, &after, gens_m68k_bridge_read_long, skew);
            check_preserved(&after, &before, &arena->bus);
            assert(after.r10 == GENS_M68K_BUS_OK && after.carry == 0);
            assert(after.rcx == UINT32_C(0x11223344));
            assert(after.rsi == before.rsi + UINT64_C(0x200000000));
            assert(after.rbp == before.rbp + 0x40);
            assert(after.rdi == (uint32_t)before.rdi + 18);
            assert(after.rdx == UINT32_C(0xfe123456));
            assert(arena->probe.addresses[0] == UINT32_C(0xc00000) &&
                   arena->probe.addresses[1] == UINT32_C(0xc00002));
            assert(arena->probe.calls == 2 && arena->probe.hooks == 1);
        }
        {
            struct Registers before = registers_for(UINT32_C(0x00c00000));
            struct Registers after = before;
            reset_arena(arena);
            run_m68k_bridge(&arena->bus, &after, gens_m68k_bridge_read_long_predecrement, skew);
            check_preserved(&after, &before, &arena->bus);
            assert(after.r10 == GENS_M68K_BUS_OK && after.carry == 0);
            assert(after.rcx == UINT32_C(0x33441122));
            assert(arena->probe.addresses[0] == UINT32_C(0xc00002) &&
                   arena->probe.addresses[1] == UINT32_C(0xc00000));
            assert(after.rsi == before.rsi + UINT64_C(0x200000000));
            assert(after.rbp == before.rbp + 0x40 &&
                   after.rdi == (uint32_t)before.rdi + 18 &&
                   after.rdx == UINT32_C(0xfe123456));
            assert(arena->probe.calls == 2 && arena->probe.hooks == 1);
        }
        {
            struct Registers before = registers_for(UINT32_C(0x00c00000));
            struct Registers after = before;
            reset_arena(arena);
            run_m68k_bridge(&arena->bus, &after, gens_m68k_bridge_write_long_predecrement, skew);
            check_preserved(&after, &before, &arena->bus);
            assert(after.r10 == GENS_M68K_BUS_OK && after.carry == 0 && after.rcx == before.rcx);
            assert(arena->probe.addresses[0] == UINT32_C(0xc00002) &&
                   arena->probe.addresses[1] == UINT32_C(0xc00000));
            assert(arena->probe.values[0] == UINT16_C(0xc3d4) &&
                   arena->probe.values[1] == UINT16_C(0xa1b2));
            assert(after.rsi == before.rsi + UINT64_C(0x200000000));
            assert(after.rbp == before.rbp + 0x40 &&
                   after.rdi == (uint32_t)before.rdi + 18 &&
                   after.rdx == UINT32_C(0xfe123456));
            assert(arena->probe.calls == 2 && arena->probe.hooks == 1);
        }
        {
            struct Registers before = registers_for(UINT32_C(0x00c00000));
            struct Registers after = before;
            reset_arena(arena);
            arena->probe.fail_call = 2;
            run_m68k_bridge(&arena->bus, &after, gens_m68k_bridge_read_long, skew);
            check_preserved(&after, &before, &arena->bus);
            assert(after.r10 == GENS_M68K_BUS_BACKEND_ERROR && after.carry == 1);
            assert(after.rcx == before.rcx && after.rdx == UINT32_C(0xfe123456));
            assert(after.rsi == before.rsi + UINT64_C(0x200000000));
            assert(after.rbp == before.rbp + 0x40 && after.rdi == (uint32_t)before.rdi + 18);
            assert(arena->probe.calls == 2 && arena->probe.hooks == 0);
        }
        {
            struct Registers before = registers_for(UINT32_C(0x00c00000));
            struct Registers after = before;
            reset_arena(arena);
            arena->probe.fail_call = 2;
            run_m68k_bridge(&arena->bus, &after, gens_m68k_bridge_write_long, skew);
            check_preserved(&after, &before, &arena->bus);
            assert(after.r10 == GENS_M68K_BUS_BACKEND_ERROR && after.carry == 1);
            assert(after.rcx == before.rcx && after.rdx == UINT32_C(0xfe123456));
            assert(arena->probe.addresses[0] == UINT32_C(0xc00000) &&
                   arena->probe.addresses[1] == UINT32_C(0xc00002));
            assert(arena->probe.values[0] == UINT16_C(0xa1b2) &&
                   arena->probe.values[1] == UINT16_C(0xc3d4));
            assert(after.rsi == before.rsi + UINT64_C(0x200000000));
            assert(after.rbp == before.rbp + 0x40 && after.rdi == (uint32_t)before.rdi + 18);
            assert(arena->probe.calls == 2 && arena->probe.hooks == 0);
        }
        {
            struct Registers before = registers_for(UINT32_C(0x00c00000));
            struct Registers after = before;
            reset_arena(arena);
            run_m68k_bridge(&arena->bus, &after, gens_m68k_bridge_read_byte, skew);
            check_preserved(&after, &before, &arena->bus);
            assert(after.r10 == GENS_M68K_BUS_MISSING_CALLBACK && after.carry == 1);
            assert(after.rcx == before.rcx && after.rdx == (uint32_t)before.rdx);
            assert(after.rsi == before.rsi && after.rbp == before.rbp &&
                   after.rdi == (uint32_t)before.rdi);
            assert(arena->probe.calls == 0 && arena->probe.hooks == 0);
        }
        {
            struct Registers before = registers_for(UINT32_C(0x00e0ffff));
            struct Registers after = before;
            reset_arena(arena);
            run_m68k_bridge(&arena->bus, &after, gens_m68k_bridge_write_word, skew);
            check_preserved(&after, &before, &arena->bus);
            assert(after.r10 == GENS_M68K_BUS_RAM_BOUNDARY && after.carry == 1);
            assert(after.rcx == before.rcx && after.rdx == (uint32_t)before.rdx);
            assert(arena->probe.calls == 0 && arena->probe.hooks == 0);
        }
    }
    assert(bridge_bad_stack_alignment == 0);
    if (munmap(arena, sizeof(*arena)) != 0)
    {
        perror("fixed-address bridge test munmap");
        return 1;
    }
    puts("x86-64 memory bridge checks passed");
    return 0;
}
