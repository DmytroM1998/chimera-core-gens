#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gens_z80_native.h"

#ifdef GENS_Z80_NATIVE_REFERENCE
extern uint8_t Ram_Z80[8192];
extern int Bank_Z80;
void reference_z80_native(GensZ80Registers *, unsigned int, unsigned int);
#else
static uint8_t Ram_Z80[8192];
static int Bank_Z80;
#endif

static uint64_t hash;
static unsigned int calls;
#ifndef GENS_Z80_NATIVE_REFERENCE
static unsigned int fail_call;
#endif

_Static_assert(offsetof(GensZ80Registers, eax) == 0 &&
               offsetof(GensZ80Registers, ecx) == 4 &&
               offsetof(GensZ80Registers, edx) == 8,
               "reference register offsets");
_Static_assert(GENS_Z80_NATIVE_YM_READ == 0 && GENS_Z80_NATIVE_YM_WRITE == 1 &&
               GENS_Z80_NATIVE_VDP_STATUS == 2 && GENS_Z80_NATIVE_V_COUNTER == 3 &&
               GENS_Z80_NATIVE_H_COUNTER == 4 && GENS_Z80_NATIVE_VDP_BYTE == 5 &&
               GENS_Z80_NATIVE_VDP_WORD == 6 && GENS_Z80_NATIVE_PSG == 7 &&
               GENS_Z80_NATIVE_M68K_READ == 8 && GENS_Z80_NATIVE_M68K_WRITE == 9,
               "reference backend operation numbering");

static void record(uint32_t value)
{
    unsigned int shift;
    for (shift = 0; shift < 32; shift += 8)
    {
        hash ^= (value >> shift) & 0xffu;
        hash *= UINT64_C(1099511628211);
    }
}

void reference_native_backend(unsigned int operation, GensZ80Registers *registers)
{
    ++calls;
    record(operation);
    record(registers->eax);
    record(registers->ecx);
    record(registers->edx);
    registers->eax = UINT32_C(0xdea50000) |
        (((registers->ecx * 37u + operation * 19u) ^ UINT32_C(0x5a5a)) & UINT32_C(0xffff));
    registers->ecx += UINT32_C(0x10203040);
    registers->edx ^= UINT32_C(0xabcdef01);
}

#ifndef GENS_Z80_NATIVE_REFERENCE
static int backend(void *opaque, GensM68kBusState *state,
                    enum GensZ80NativeOperation operation, GensZ80Registers *registers)
{
    (void)opaque;
    ++state->io_cycle_counter;
    reference_native_backend((unsigned int)operation, registers);
    return fail_call == 0u || calls != fail_call;
}

static void failure_cases(void)
{
    GensM68kBusState state = {0};
    GensZ80Memory memory = {Ram_Z80, sizeof(Ram_Z80), &Bank_Z80, 0, 0, 0};
    GensZ80Native native = {&memory, 0, backend};
    GensZ80Registers registers = {0x12345678, 0x1fff, 0xabcdef12};
    Bank_Z80 = 0x456789ab;
    assert(gens_z80_native_write(&native, &state, 2, &registers));
    assert(Ram_Z80[0x1fff] == 0x12 && Bank_Z80 == 0x456789ef);
    registers.ecx = 0x3fff;
    assert(gens_z80_native_read(&native, &state, 2, &registers));
    assert(registers.ecx == 0x1fff && registers.eax == 0x1234ef12);
    registers.ecx = 0x10000;
    assert(!gens_z80_native_read(&native, &state, 1, &registers));
    registers.ecx = 0x4003;
    calls = 0;
    fail_call = 1;
    assert(!gens_z80_native_write(&native, &state, 2, &registers));
    assert(calls == 1 && registers.ecx == 0x10203043);
    registers.ecx = 0x4003;
    calls = 0;
    fail_call = 2;
    assert(!gens_z80_native_write(&native, &state, 2, &registers));
    assert(calls == 2 && registers.ecx == 0x20406084);
    registers.ecx = 0x4003;
    calls = 0;
    fail_call = 1;
    assert(!gens_z80_native_read(&native, &state, 2, &registers));
    assert(calls == 1 && registers.ecx == 3);
    fail_call = 0;
    native.backend = 0;
    registers.ecx = 0x7f04;
    assert(!gens_z80_native_read(&native, &state, 2, &registers));
    registers.ecx = 0x123;
    assert(gens_z80_native_write(&native, &state, 1, &registers));
    assert(state.io_cycle_counter == 4);
}
#endif

static uint32_t random_value(uint32_t *value)
{
    *value ^= *value << 13;
    *value ^= *value >> 17;
    *value ^= *value << 5;
    return *value;
}

int main(void)
{
    static const uint32_t addresses[] = {
        0, 1, 0x1ffe, 0x1fff, 0x2000, 0x3ffe, 0x3fff, 0x4000, 0x4001, 0x4003, 0x5fff,
        0x6000, 0x60ff, 0x6100, 0x7000, 0x7f03, 0x7f04, 0x7f05, 0x7f07,
        0x7f08, 0x7f09, 0x7f11, 0x7fff, 0x8000, 0x80ff, 0xfffe, 0xffff
    };
    unsigned int write, size, i;
#ifndef GENS_Z80_NATIVE_REFERENCE
    GensM68kBusState state = {0};
    GensZ80Memory memory = {Ram_Z80, sizeof(Ram_Z80), &Bank_Z80, 0, 0, 0};
    GensZ80Native native = {&memory, 0, backend};
    failure_cases();
#endif
    for (write = 0; write < 2; ++write)
    {
        for (size = 1; size <= 2; ++size)
        {
            uint32_t rng = UINT32_C(0x68008000);
            hash = UINT64_C(14695981039346656037);
            for (i = 0; i < sizeof(Ram_Z80); ++i)
                Ram_Z80[i] = (uint8_t)(i ^ (i >> 5) ^ 0x96u);
            for (i = 0; i < 2048; ++i)
            {
                uint32_t bank = random_value(&rng);
                GensZ80Registers registers;
                memcpy(&Bank_Z80, &bank, sizeof(bank));
                registers.eax = random_value(&rng);
                registers.edx = random_value(&rng);
                registers.ecx = i < sizeof(addresses) / sizeof(addresses[0]) ?
                    addresses[i] : random_value(&rng) & UINT32_C(0xffff);
                calls = 0;
#ifdef GENS_Z80_NATIVE_REFERENCE
                reference_z80_native(&registers, size, write);
#else
                if (write)
                    assert(gens_z80_native_write(&native, &state, size, &registers));
                else
                    assert(gens_z80_native_read(&native, &state, size, &registers));
#ifdef GENS_Z80_NATIVE_NEGATIVE_CONTROL
                if (i == 0)
                    registers.eax ^= 1u;
#endif
#endif
                record(registers.eax);
                record(registers.ecx);
                record(registers.edx);
                record(calls);
                memcpy(&bank, &Bank_Z80, sizeof(bank));
                record(bank);
            }
            for (i = 0; i < sizeof(Ram_Z80); ++i)
                record(Ram_Z80[i]);
            printf("native %u %u %016" PRIx64 "\n", write, size, hash);
        }
    }
    return 0;
}
