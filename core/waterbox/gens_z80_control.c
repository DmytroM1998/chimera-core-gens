/* Genesis bus arbitration follows the pinned Mem_M68k.asm.
 * The upstream component terms apply to derived portions.
 */
#include "gens_z80_control.h"

#include <string.h>

#include "io.h"

_Static_assert(sizeof(int) == 4, "Gens machine globals require 32-bit int");

static uint32_t bits(const int *value)
{
    uint32_t result;
    memcpy(&result, value, sizeof(result));
    return result;
}

static void store(int *target, uint32_t value)
{
    memcpy(target, &value, sizeof(value));
}

static void low_byte(int *target, uint8_t value)
{
    store(target, (bits(target) & UINT32_C(0xffffff00)) | value);
}

int gens_z80_control_valid(const GensZ80Control *control)
{
    return control != 0 && control->z80_state != 0 &&
        control->last_request_cycles != 0 && control->last_request_state != 0 &&
        control->fake_fetch != 0 && control->cycles_m68k != 0 &&
        control->cycles_z80 != 0 && control->cycle_table != 0 &&
        control->cycle_table_size >= 512u;
}

int gens_z80_request_read(GensZ80Control *control, GensM68kBusState *state,
                          unsigned int size, uint16_t *value)
{
    uint32_t now;
    uint8_t high;
    if (!gens_z80_control_valid(control) || state == 0 || value == 0 ||
        (size != 1u && size != 2u))
        return 0;
    if ((bits(control->z80_state) & 2u) != 0u)
        high = UINT8_C(0x81);
    else
    {
        if (control->read_odometer == 0 ||
            !control->read_odometer(control->opaque, state, &now))
            return 0;
        if ((uint32_t)(now - bits(control->last_request_cycles)) > 16u)
            high = UINT8_C(0x80);
        else if (size == 1u)
            high = (uint8_t)(bits(control->last_request_state) | UINT32_C(0x80));
        else
            high = (uint8_t)(bits(control->last_request_state) + UINT32_C(0x80));
    }
    if (size == 1u)
        *value = high;
    else
    {
        uint8_t low = (uint8_t)(bits(control->fake_fetch) ^ UINT32_C(0xff));
        low_byte(control->fake_fetch, low);
        *value = (uint16_t)((uint16_t)high << 8 | low);
    }
    return 1;
}

static int target_cycle(GensZ80Control *control, uint32_t remaining,
                         uint32_t z80_cycles, uint32_t *target)
{
    if (remaining >= 512u)
        return 0;
    *target = z80_cycles - bits(&control->cycle_table[remaining]);
    return 1;
}

int gens_z80_request_write(GensZ80Control *control, GensM68kBusState *state,
                           unsigned int size, uint16_t value)
{
    uint32_t now, remaining, target;
    uint8_t previous;
    unsigned int request;
    if (!gens_z80_control_valid(control) || state == 0 ||
        (size != 1u && size != 2u))
        return 0;
    request = (size == 1u ? value : value >> 8) & 1u;
    previous = (uint8_t)bits(control->z80_state);
    Controller_1_Counter = 0;
    Controller_1_Delay = 0;
    Controller_2_Counter = 0;
    Controller_2_Delay = 0;
    if (request == 0u)
    {
        if ((previous & 2u) != 0u)
            return 1;
        low_byte(control->z80_state, (uint8_t)(previous | 2u));
        remaining = bits(control->cycles_m68k);
        if (control->read_odometer == 0 ||
            !control->read_odometer(control->opaque, state, &now))
            return 0;
        remaining -= now;
        if (!target_cycle(control, remaining, bits(control->cycles_z80), &target) ||
            control->set_odometer == 0)
            return 0;
        return control->set_odometer(control->opaque, state, target);
    }
    if (control->read_odometer == 0 ||
        !control->read_odometer(control->opaque, state, &now))
        return 0;
    previous = (uint8_t)bits(control->z80_state);
    store(control->last_request_cycles, now);
    low_byte(control->last_request_state, (uint8_t)((previous & 2u) != 0u));
    if ((previous & 2u) == 0u)
        return 1;
    remaining = bits(control->cycles_m68k) - now;
    low_byte(control->z80_state, (uint8_t)(previous & ~2u));
    if (!target_cycle(control, remaining, bits(control->cycles_z80), &target) ||
        control->execute == 0)
        return 0;
    return control->execute(control->opaque, state, target);
}

int gens_z80_reset_write(GensZ80Control *control, GensM68kBusState *state,
                         unsigned int size, uint16_t value)
{
    unsigned int released;
    if (!gens_z80_control_valid(control) || state == 0 ||
        (size != 1u && size != 2u))
        return 0;
    released = (size == 1u ? value : value >> 8) & 1u;
    if (released != 0u)
    {
        low_byte(control->z80_state, (uint8_t)(bits(control->z80_state) & ~4u));
        return 1;
    }
    if (control->reset_z80 == 0 || !control->reset_z80(control->opaque, state))
        return 0;
    low_byte(control->z80_state, (uint8_t)(bits(control->z80_state) | 4u));
    if (control->reset_ym2612 == 0)
        return 0;
    return control->reset_ym2612(control->opaque, state);
}
