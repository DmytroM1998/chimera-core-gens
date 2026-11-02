/* Native callback register effects follow the pinned Mem_Z80.asm.
 * The upstream component terms apply to derived portions.
 */
#include "gens_z80_native.h"

#include <string.h>

_Static_assert(sizeof(GensZ80Registers) == 12, "Z80 reference register layout");

static int valid(GensZ80Native *native, GensM68kBusState *state,
                  unsigned int size, const GensZ80Registers *registers)
{
    return native != 0 && gens_z80_memory_valid(native->memory) && state != 0 &&
        registers != 0 && registers->ecx <= UINT32_C(0xffff) &&
        (size == 1u || size == 2u);
}

static uint32_t bank_value(const GensZ80Native *native)
{
    uint32_t value;
    memcpy(&value, native->memory->bank, sizeof(value));
    return value;
}

static int backend(GensZ80Native *native, GensM68kBusState *state,
                    enum GensZ80NativeOperation operation, GensZ80Registers *registers)
{
    if (native->backend == 0)
        return 0;
    return native->backend(native->opaque, state, operation, registers);
}

int gens_z80_native_read(GensZ80Native *native, GensM68kBusState *state,
                         unsigned int size, GensZ80Registers *registers)
{
    uint32_t address;
    if (!valid(native, state, size, registers))
        return 0;
    address = registers->ecx;
    if (address < UINT32_C(0x4000))
    {
        registers->ecx &= UINT32_C(0x1fff);
        if (size == 1u)
            registers->eax = (registers->eax & UINT32_C(0xffffff00)) |
                native->memory->ram[registers->ecx];
        else
        {
            /* Bank_Z80 immediately follows RAM in the original assembly layout. */
            uint32_t high = registers->ecx == UINT32_C(0x1fff) ?
                bank_value(native) & UINT32_C(0xff) :
                native->memory->ram[registers->ecx + 1u];
            registers->eax = (registers->eax & UINT32_C(0xffff0000)) |
                native->memory->ram[registers->ecx] |
                high << 8;
        }
        return 1;
    }
    if (address < UINT32_C(0x6000))
    {
        int ok;
        registers->ecx &= 3u;
        address = registers->ecx;
        ok = backend(native, state, GENS_Z80_NATIVE_YM_READ, registers);
        registers->ecx = address;
        if (!ok)
            return 0;
        if (size == 2u)
            registers->eax &= UINT32_C(0xffff00ff);
        return 1;
    }
    if (address < UINT32_C(0x7000))
    {
        registers->eax &= size == 1u ? UINT32_C(0xffffff00) : UINT32_C(0xffff0000);
        return 1;
    }
    if (address < UINT32_C(0x8000))
    {
        if (address < UINT32_C(0x7f04) || address > UINT32_C(0x7f09))
        {
            registers->eax &= UINT32_C(0xffffff00);
            return 1;
        }
        if (address < UINT32_C(0x7f08))
        {
            if (!backend(native, state, GENS_Z80_NATIVE_VDP_STATUS, registers))
                return 0;
            if (size == 1u && (registers->ecx & 1u) == 0u)
                registers->eax = (registers->eax & UINT32_C(0xffffff00)) |
                    ((registers->eax >> 8) & UINT32_C(0xff));
            return 1;
        }
        if (size == 1u)
            return backend(native, state, (address & 1u) != 0u ?
                           GENS_Z80_NATIVE_H_COUNTER : GENS_Z80_NATIVE_V_COUNTER, registers);
        if (!backend(native, state, GENS_Z80_NATIVE_V_COUNTER, registers))
            return 0;
        registers->ecx = (registers->ecx & UINT32_C(0xffffff00)) |
            (registers->eax & UINT32_C(0xff));
        if (!backend(native, state, GENS_Z80_NATIVE_H_COUNTER, registers))
            return 0;
        registers->eax = (registers->eax & UINT32_C(0xffff00ff)) |
            ((registers->ecx & UINT32_C(0xff)) << 8);
        return 1;
    }
    registers->eax = bank_value(native);
    registers->ecx = (registers->ecx & UINT32_C(0x7fff)) + registers->eax;
    if (size == 1u)
    {
        int ok;
        address = registers->ecx;
        ok = backend(native, state, GENS_Z80_NATIVE_M68K_READ, registers);
        registers->ecx = address;
        return ok;
    }
    if (!backend(native, state, GENS_Z80_NATIVE_M68K_READ, registers))
        return 0;
    if (size == 2u)
    {
        ++registers->ecx;
        registers->edx = (registers->edx & UINT32_C(0xffffff00)) |
            (registers->eax & UINT32_C(0xff));
        if (!backend(native, state, GENS_Z80_NATIVE_M68K_READ, registers))
            return 0;
        registers->eax = (registers->eax & UINT32_C(0xffff00ff)) |
            ((registers->eax & UINT32_C(0xff)) << 8);
        registers->eax = (registers->eax & UINT32_C(0xffffff00)) |
            (registers->edx & UINT32_C(0xff));
    }
    return 1;
}

int gens_z80_native_write(GensZ80Native *native, GensM68kBusState *state,
                          unsigned int size, GensZ80Registers *registers)
{
    uint32_t address;
    if (!valid(native, state, size, registers))
        return 0;
    address = registers->ecx;
    if (address < UINT32_C(0x4000))
    {
        registers->ecx &= UINT32_C(0x1fff);
        native->memory->ram[registers->ecx] = (uint8_t)registers->edx;
        if (size == 2u)
        {
            if (registers->ecx == UINT32_C(0x1fff))
            {
                uint32_t bank = (bank_value(native) & UINT32_C(0xffffff00)) |
                    ((registers->edx >> 8) & UINT32_C(0xff));
                memcpy(native->memory->bank, &bank, sizeof(bank));
            }
            else
                native->memory->ram[registers->ecx + 1u] = (uint8_t)(registers->edx >> 8);
        }
        return 1;
    }
    if (address < UINT32_C(0x6000))
    {
        registers->ecx &= 3u;
        if (!backend(native, state, GENS_Z80_NATIVE_YM_WRITE, registers))
            return 0;
        if (size == 2u)
        {
            ++registers->ecx;
            return backend(native, state, GENS_Z80_NATIVE_YM_WRITE, registers);
        }
        return 1;
    }
    if (address < UINT32_C(0x7000))
    {
        if (address <= UINT32_C(0x60ff))
        {
            registers->ecx = bank_value(native) & UINT32_C(0xff0000);
            registers->edx = (registers->edx & 1u) << 23;
            registers->ecx >>= 1;
            registers->edx += registers->ecx;
            memcpy(native->memory->bank, &registers->edx, sizeof(registers->edx));
        }
        return 1;
    }
    if (address < UINT32_C(0x8000))
    {
        uint32_t saved_data = registers->edx;
        int ok;
        if (address == UINT32_C(0x7f11))
            ok = backend(native, state, GENS_Z80_NATIVE_PSG, registers);
        else if (address <= UINT32_C(0x7f03))
            ok = backend(native, state, size == 1u ?
                         GENS_Z80_NATIVE_VDP_BYTE : GENS_Z80_NATIVE_VDP_WORD, registers);
        else
            return 1;
        registers->edx = saved_data;
        return ok;
    }
    registers->eax = bank_value(native);
    registers->ecx = (registers->ecx & UINT32_C(0x7fff)) + registers->eax;
    if (size == 1u)
    {
        uint32_t saved_address = registers->ecx, saved_data = registers->edx;
        int ok = backend(native, state, GENS_Z80_NATIVE_M68K_WRITE, registers);
        registers->ecx = saved_address;
        registers->edx = saved_data;
        return ok;
    }
    if (!backend(native, state, GENS_Z80_NATIVE_M68K_WRITE, registers))
        return 0;
    registers->edx >>= 8;
    ++registers->ecx;
    return backend(native, state, GENS_Z80_NATIVE_M68K_WRITE, registers);
}
