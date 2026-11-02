#include <assert.h>
#include <stdint.h>

#include "gens_z80_control.h"
#include "io.h"

static int flags, last_cycles, last_state, fake_fetch, cycles_m68k, cycles_z80;
static int table[512];
static unsigned int calls, fail_kind;
static uint32_t now, target;

static int read_odometer(void *opaque, GensM68kBusState *state, uint32_t *value)
{
    (void)opaque;
    (void)state;
    ++calls;
    *value = now;
    return fail_kind != 1u;
}

static int set_odometer(void *opaque, GensM68kBusState *state, uint32_t value)
{
    (void)opaque;
    (void)state;
    ++calls;
    target = value;
    return fail_kind != 2u;
}

static int execute(void *opaque, GensM68kBusState *state, uint32_t value)
{
    (void)opaque;
    (void)state;
    ++calls;
    target = value;
    return fail_kind != 3u;
}

static int reset_z80(void *opaque, GensM68kBusState *state)
{
    (void)opaque;
    (void)state;
    assert((flags & 4) == 0);
    ++calls;
    return fail_kind != 4u;
}

static int reset_ym2612(void *opaque, GensM68kBusState *state)
{
    (void)opaque;
    (void)state;
    assert((flags & 4) != 0);
    ++calls;
    return fail_kind != 5u;
}

int main(void)
{
    GensM68kBusState state = {0};
    GensZ80Control control = {
        &flags, &last_cycles, &last_state, &fake_fetch, &cycles_m68k, &cycles_z80,
        table, 512, 0, read_odometer, set_odometer, execute, reset_z80, reset_ym2612
    };
    GensZ80Control invalid = control;
    uint16_t value = 0x5555;
    unsigned int i;
    assert(gens_z80_control_valid(&control));
    invalid.cycle_table_size = 511;
    assert(!gens_z80_control_valid(&invalid));
    assert(!gens_z80_request_read(&invalid, &state, 2, &value));
    assert(value == 0x5555 && calls == 0);
    assert(!gens_z80_request_write(&control, &state, 4, 0));
    assert(!gens_z80_reset_write(&control, 0, 1, 0));
    for (i = 0; i < 512; ++i)
        table[i] = (int)(i * 7u / 15u);
    flags = 0x12345600;
    fake_fetch = 0x234567cc;
    last_cycles = -16;
    last_state = 0x345678ab;
    now = 0;
    assert(gens_z80_request_read(&control, &state, 1, &value) && value == 0xab);
    assert(gens_z80_request_read(&control, &state, 2, &value) && value == 0x2b33);
    assert(fake_fetch == 0x23456733);
    now = 1;
    assert(gens_z80_request_read(&control, &state, 2, &value) && value == 0x80cc);
    fail_kind = 1;
    value = 0x5555;
    assert(!gens_z80_request_read(&control, &state, 2, &value));
    assert(value == 0x5555 && fake_fetch == 0x234567cc);
    control.read_odometer = 0;
    flags |= 2;
    assert(gens_z80_request_read(&control, &state, 2, &value) && value == 0x8133);
    flags &= ~2;
    Controller_1_Counter = Controller_1_Delay = 0xffffffffu;
    Controller_2_Counter = Controller_2_Delay = 0xffffffffu;
    assert(!gens_z80_request_write(&control, &state, 1, 0));
    assert(flags == 0x12345602);
    assert(Controller_1_Counter == 0 && Controller_1_Delay == 0 &&
           Controller_2_Counter == 0 && Controller_2_Delay == 0);
    control.read_odometer = read_odometer;
    fail_kind = 0;
    flags = 0x12345600;
    now = 100;
    cycles_m68k = 611;
    cycles_z80 = 1000;
    assert(gens_z80_request_write(&control, &state, 2, 1));
    assert(flags == 0x12345602 && target == 1000u - (511u * 7u / 15u));
    flags = 0;
    fail_kind = 2;
    assert(!gens_z80_request_write(&control, &state, 1, 0));
    assert(flags == 2);
    fail_kind = 0;
    flags = 0;
    cycles_z80 = 0;
    assert(gens_z80_request_write(&control, &state, 1, 0));
    assert(target == UINT32_C(0xffffff12));
    flags = 0;
    now = UINT32_C(0xffffffcd);
    cycles_m68k = 49;
    cycles_z80 = 100;
    assert(gens_z80_request_write(&control, &state, 1, 0));
    assert(target == 54);
    now = 100;
    cycles_z80 = 1000;
    cycles_m68k = 612;
    flags = 0x12345600;
    calls = 0;
    assert(!gens_z80_request_write(&control, &state, 1, 0));
    assert(flags == 0x12345602 && calls == 1);
    cycles_m68k = 99;
    flags = 0;
    assert(!gens_z80_request_write(&control, &state, 1, 0));
    cycles_m68k = 200;
    flags = 2;
    fail_kind = 3;
    assert(!gens_z80_request_write(&control, &state, 2, 0x100));
    assert(flags == 0 && last_cycles == 100 && last_state == 0x34567801);
    flags = 0;
    fail_kind = 4;
    assert(!gens_z80_reset_write(&control, &state, 1, 0));
    assert(flags == 0);
    fail_kind = 5;
    assert(!gens_z80_reset_write(&control, &state, 2, 1));
    assert(flags == 4);
    calls = 0;
    control.reset_z80 = 0;
    control.reset_ym2612 = 0;
    assert(gens_z80_reset_write(&control, &state, 2, 0x100));
    assert(flags == 0 && calls == 0);
    assert(!gens_z80_reset_write(&control, &state, 1, 0));
    return 0;
}
