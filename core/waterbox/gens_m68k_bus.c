/* Based on the pinned Gens-Rerecording Starscream memory access generators.
 * The upstream component terms apply to derived portions.
 */
#include "gens_m68k_bus.h"

_Static_assert(sizeof(uintptr_t) == 8, "the ported bus path requires a 64-bit host");
_Static_assert(offsetof(GensM68kLive, fetch_pc) == 0 &&
               offsetof(GensM68kLive, fetch_base) == 8 &&
               offsetof(GensM68kLive, cycle_counter) == 16 &&
               offsetof(GensM68kLive, address) == 20 &&
               sizeof(GensM68kLive) == 24, "unexpected x86-64 bridge layout");
_Static_assert(sizeof(enum GensM68kBusSize) == 4 &&
               sizeof(enum GensM68kBusOrder) == 4 &&
               sizeof(GensM68kBusStatus) == 4, "unexpected x86-64 bridge enum ABI");
_Static_assert(GENS_M68K_BYTE == 1 && GENS_M68K_WORD == 2 && GENS_M68K_LONG == 4 &&
               GENS_M68K_NORMAL == 0 && GENS_M68K_PREDECREMENT == 1 &&
               GENS_M68K_BUS_OK == 0, "unexpected x86-64 bridge constants");

static GensM68kBusStatus validate(const GensM68kBus *bus, const GensM68kLive *live,
                                   enum GensM68kBusSize size, enum GensM68kBusOrder order)
{
    uint32_t physical, offset;
    if (bus == 0 || live == 0 || bus->state == 0 || bus->ram == 0 ||
        bus->ram_size < 65536u ||
        (size != GENS_M68K_BYTE && size != GENS_M68K_WORD && size != GENS_M68K_LONG) ||
        (order != GENS_M68K_NORMAL && order != GENS_M68K_PREDECREMENT))
        return GENS_M68K_BUS_INVALID_ARGUMENT;
    if (bus->hook == 0)
        return GENS_M68K_BUS_MISSING_CALLBACK;
    physical = live->address & UINT32_C(0x00ffffff);
    offset = physical & UINT32_C(0xffff);
    if (physical >= UINT32_C(0x00e00000) &&
        size != GENS_M68K_BYTE && offset > 65536u - (unsigned int)size)
        return GENS_M68K_BUS_RAM_BOUNDARY;
    return GENS_M68K_BUS_OK;
}

static void save_io(GensM68kBusState *state, const GensM68kLive *live)
{
    state->io_fetch_pc = live->fetch_pc;
    state->io_fetch_base = live->fetch_base;
    state->io_cycle_counter = live->cycle_counter;
}

static void restore_io(const GensM68kBusState *state, GensM68kLive *live)
{
    live->fetch_pc = state->io_fetch_pc;
    live->fetch_base = state->io_fetch_base;
    live->cycle_counter = state->io_cycle_counter;
    live->address = state->access_address;
}

static uint32_t hook_pc(const GensM68kLive *live)
{
    return (uint32_t)(live->fetch_pc - live->fetch_base - (uintptr_t)2u);
}

static void call_hook(GensM68kBus *bus, GensM68kLive *live, enum GensM68kBusHook kind)
{
    GensM68kLive saved = *live;
    bus->state->hook_pc &= UINT32_C(0x00ffffff);
    bus->hook(bus->opaque, bus->state, kind);
    *live = saved;
}

GensM68kBusStatus gens_m68k_read(GensM68kBus *bus, GensM68kLive *live,
                                  enum GensM68kBusSize size,
                                  enum GensM68kBusOrder order, uint32_t *value)
{
    GensM68kBusStatus status = validate(bus, live, size, order);
    uint32_t physical, offset, result = 0;
    GensM68kBusState *state;
    if (status != GENS_M68K_BUS_OK)
        return status;
    if (value == 0)
        return GENS_M68K_BUS_INVALID_ARGUMENT;
    physical = live->address & UINT32_C(0x00ffffff);
    offset = physical & UINT32_C(0xffff);
    if (physical < UINT32_C(0x00e00000) &&
        ((size == GENS_M68K_BYTE && bus->read_byte == 0) ||
         (size != GENS_M68K_BYTE && bus->read_word == 0)))
        return GENS_M68K_BUS_MISSING_CALLBACK;
    state = bus->state;
    state->access_address = live->address;

    if (physical >= UINT32_C(0x00e00000))
    {
        if (size == GENS_M68K_BYTE)
            result = bus->ram[offset ^ 1u];
        else if (size == GENS_M68K_WORD)
            result = (uint32_t)bus->ram[offset] | ((uint32_t)bus->ram[offset + 1u] << 8);
        else
            result = ((uint32_t)bus->ram[offset + 1u] << 24) |
                     ((uint32_t)bus->ram[offset] << 16) |
                     ((uint32_t)bus->ram[offset + 3u] << 8) | bus->ram[offset + 2u];
        live->address = state->access_address;
    }
    else
    {
        int ok;
        uint8_t byte = 0;
        uint16_t first = 0, second = 0;
        save_io(state, live);
        if (size == GENS_M68K_BYTE)
        {
            ok = bus->read_byte(bus->opaque, state, physical, &byte);
            result = byte;
        }
        else if (size == GENS_M68K_WORD)
        {
            ok = bus->read_word(bus->opaque, state, physical, &first);
            result = first;
        }
        else if (order == GENS_M68K_PREDECREMENT)
        {
            ok = bus->read_word(bus->opaque, state, physical + 2u, &second);
            if (ok)
                ok = bus->read_word(bus->opaque, state, physical, &first);
            result = ((uint32_t)first << 16) | second;
        }
        else
        {
            ok = bus->read_word(bus->opaque, state, physical, &first);
            if (ok)
                ok = bus->read_word(bus->opaque, state, physical + 2u, &second);
            result = ((uint32_t)first << 16) | second;
        }
        restore_io(state, live);
        if (!ok)
            return GENS_M68K_BUS_BACKEND_ERROR;
    }

    state->hook_pc = hook_pc(live);
    state->hook_address = state->access_address & UINT32_C(0x00ffffff);
    state->hook_value = result;
    call_hook(bus, live, size == GENS_M68K_BYTE ? GENS_M68K_READ_BYTE :
              (size == GENS_M68K_WORD ? GENS_M68K_READ_WORD : GENS_M68K_READ_LONG));
    *value = result;
    return GENS_M68K_BUS_OK;
}

GensM68kBusStatus gens_m68k_write(GensM68kBus *bus, GensM68kLive *live,
                                   enum GensM68kBusSize size,
                                   enum GensM68kBusOrder order, uint32_t value)
{
    GensM68kBusStatus status = validate(bus, live, size, order);
    uint32_t physical, offset;
    GensM68kBusState *state;
    if (status != GENS_M68K_BUS_OK)
        return status;
    physical = live->address & UINT32_C(0x00ffffff);
    offset = physical & UINT32_C(0xffff);
    if (physical < UINT32_C(0x00e00000) &&
        ((size == GENS_M68K_BYTE && bus->write_byte == 0) ||
         (size != GENS_M68K_BYTE && bus->write_word == 0)))
        return GENS_M68K_BUS_MISSING_CALLBACK;
    state = bus->state;
    state->access_address = live->address;
    state->hook_pc = hook_pc(live);
    state->hook_address = physical;
    state->hook_value = value;

    if (physical >= UINT32_C(0x00e00000))
    {
        if (size == GENS_M68K_BYTE)
            bus->ram[offset ^ 1u] = (uint8_t)value;
        else if (size == GENS_M68K_WORD)
        {
            bus->ram[offset] = (uint8_t)value;
            bus->ram[offset + 1u] = (uint8_t)(value >> 8);
        }
        else
        {
            bus->ram[offset] = (uint8_t)(value >> 16);
            bus->ram[offset + 1u] = (uint8_t)(value >> 24);
            bus->ram[offset + 2u] = (uint8_t)value;
            bus->ram[offset + 3u] = (uint8_t)(value >> 8);
        }
        live->address = state->access_address;
    }
    else
    {
        int ok;
        save_io(state, live);
        if (size == GENS_M68K_BYTE)
            ok = bus->write_byte(bus->opaque, state, physical, (uint8_t)value);
        else if (size == GENS_M68K_WORD)
            ok = bus->write_word(bus->opaque, state, physical, (uint16_t)value);
        else if (order == GENS_M68K_PREDECREMENT)
        {
            ok = bus->write_word(bus->opaque, state, physical + 2u, (uint16_t)value);
            if (ok)
                ok = bus->write_word(bus->opaque, state, physical, (uint16_t)(value >> 16));
        }
        else
        {
            ok = bus->write_word(bus->opaque, state, physical, (uint16_t)(value >> 16));
            if (ok)
                ok = bus->write_word(bus->opaque, state, physical + 2u, (uint16_t)value);
        }
        restore_io(state, live);
        if (!ok)
            return GENS_M68K_BUS_BACKEND_ERROR;
    }

    call_hook(bus, live, size == GENS_M68K_BYTE ? GENS_M68K_WRITE_BYTE :
              (size == GENS_M68K_WORD ? GENS_M68K_WRITE_WORD : GENS_M68K_WRITE_LONG));
    return GENS_M68K_BUS_OK;
}
