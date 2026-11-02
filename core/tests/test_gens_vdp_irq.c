#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "gens_vdp_irq.h"

#ifdef GENS_VDP_IRQ_REFERENCE
#define STORAGE extern
void Update_IRQ_Line(void);
uint32_t reference_irq_ack(uint32_t);
#else
#define STORAGE static
#endif

STORAGE unsigned int VDP_Reg[26];
STORAGE int VDP_Int;
STORAGE uint32_t _main68k_context[36];
static unsigned int calls, last_level;
static int last_vector;

void reference_interrupt(unsigned int level, int vector)
{
    ++calls;
    last_level = level;
    last_vector = vector;
}

#ifndef GENS_VDP_IRQ_REFERENCE
static GensM68kBusState state;
static int interrupt_result = 1;

static int interrupt(void *opaque, GensM68kBusState *current, unsigned int level, int vector)
{
    (void)opaque;
    assert(current == &state);
    if (!interrupt_result)
        return 0;
    reference_interrupt(level, vector);
    return 1;
}

static GensVdpIrq irq = {
    &VDP_Reg[0], &VDP_Reg[1], &VDP_Int, (uint8_t *)&_main68k_context[35], 0, interrupt
};

static void unit_cases(void)
{
    uint32_t eax = 0x1234abcd;
    GensVdpIrq invalid = irq;
    invalid.pending = 0;
    assert(!gens_vdp_irq_valid(&invalid));
    assert(!gens_vdp_irq_ack(&invalid, &eax) && eax == 0x1234abcd);
    assert(!gens_vdp_irq_update(&irq, 0));
    VDP_Reg[0] = 0x10;
    VDP_Reg[1] = 0x20;
    VDP_Int = 0x5678000c;
    _main68k_context[35] = UINT32_C(0x876543ab);
    assert(gens_vdp_irq_update(&irq, &state));
    assert(calls == 1 && last_level == 6 && last_vector == -1);
    assert(_main68k_context[35] == UINT32_C(0x876543ab));
    assert(gens_vdp_irq_ack(&irq, &eax));
    assert(eax == 0x12340404 && VDP_Int == 0x56780004);
    assert(_main68k_context[35] == UINT32_C(0x876543ab));
    assert(gens_vdp_irq_update(&irq, &state));
    assert(calls == 2 && last_level == 4);
    assert(gens_vdp_irq_ack(&irq, &eax));
    assert(eax == 0x12340400 && VDP_Int == 0x56780000);
    assert(gens_vdp_irq_update(&irq, &state));
    assert(_main68k_context[35] == UINT32_C(0x876543a0) && calls == 2);

    VDP_Int = 0x5678000c;
    interrupt_result = 0;
    assert(!gens_vdp_irq_update(&irq, &state));
    assert(calls == 2 && VDP_Int == 0x5678000c);
    irq.interrupt = 0;
    assert(!gens_vdp_irq_update(&irq, &state));
    assert(calls == 2 && VDP_Int == 0x5678000c);
    VDP_Reg[0] = VDP_Reg[1] = 0;
    _main68k_context[35] |= 15u;
    assert(gens_vdp_irq_update(&irq, &state));
    assert(_main68k_context[35] == UINT32_C(0x876543a0) && calls == 2);
    irq.interrupt = interrupt;
    interrupt_result = 1;
}
#endif

static uint64_t hash;

static void record(uint32_t value)
{
    unsigned int shift;
    for (shift = 0; shift < 32; shift += 8)
    {
        hash ^= (value >> shift) & 255u;
        hash *= UINT64_C(1099511628211);
    }
}

static uint32_t acknowledge(uint32_t eax)
{
#ifdef GENS_VDP_IRQ_REFERENCE
    return reference_irq_ack(eax);
#else
    assert(gens_vdp_irq_ack(&irq, &eax));
#ifdef GENS_VDP_IRQ_NEGATIVE_CONTROL
    eax ^= 0x100u;
#endif
    return eax;
#endif
}

static void update(void)
{
#ifdef GENS_VDP_IRQ_REFERENCE
    Update_IRQ_Line();
#else
    assert(gens_vdp_irq_update(&irq, &state));
#endif
}

int main(void)
{
    static const uint32_t seeds[] = {0, 0x1234abcd, UINT32_C(0xffffffff)};
    unsigned int horizontal, vertical, pending, byte, seed;
#ifndef GENS_VDP_IRQ_REFERENCE
    unit_cases();
#endif
    hash = UINT64_C(14695981039346656037);
    for (horizontal = 0; horizontal < 2; ++horizontal)
        for (vertical = 0; vertical < 2; ++vertical)
            for (pending = 0; pending < 256; ++pending)
                for (byte = 0; byte < 256; ++byte)
                {
                    VDP_Reg[0] = 0x12340000u | (horizontal ? 0x10u : 0u);
                    VDP_Reg[1] = 0x56780000u | (vertical ? 0x20u : 0u);
                    VDP_Int = (int)(0x67890000u | pending);
                    _main68k_context[35] = 0x87654300u | byte;
                    calls = last_level = 0;
                    last_vector = 123;
                    update();
                    record(calls); record(last_level); record((uint32_t)last_vector);
                    record((uint32_t)VDP_Int); record(_main68k_context[35]);
                }
    printf("vdp irq update %016" PRIx64 "\n", hash);
    hash = UINT64_C(14695981039346656037);
    for (horizontal = 0; horizontal < 2; ++horizontal)
        for (vertical = 0; vertical < 2; ++vertical)
            for (pending = 0; pending < 256; ++pending)
                for (seed = 0; seed < 3; ++seed)
                {
                    VDP_Reg[0] = 0x12340000u | (horizontal ? 0x10u : 0u);
                    VDP_Reg[1] = 0x56780000u | (vertical ? 0x20u : 0u);
                    VDP_Int = (int)(0x67890000u | pending);
                    _main68k_context[35] = 0x876543ab;
                    record(acknowledge(seeds[seed]));
                    record((uint32_t)VDP_Int);
                    record(acknowledge(seeds[seed]));
                    record((uint32_t)VDP_Int);
                    record(_main68k_context[35]);
                }
    printf("vdp irq acknowledge %016" PRIx64 "\n", hash);
    return 0;
}
