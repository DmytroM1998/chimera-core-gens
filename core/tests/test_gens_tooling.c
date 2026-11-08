#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gens_machine.h"
#include "gens_m68k_cpu.h"
#include "gens_z80_cpu.h"

extern int GetBusCount(void);
extern const char *GetBusName(int i);
extern int64_t GetBusSize(int i);
extern int GetBusWritable(int i);
extern int PeekBus(int bus, int address);
extern void PokeBus(int bus, int address, int value);
extern const uint8_t *ReadBus(int bus, int64_t address, int length);
extern int32_t GetRegisterCount(void);
extern const char *GetRegisterName(int32_t i);
extern int64_t GetRegisterValue(int32_t i);
extern int32_t GetRegisterBits(int32_t i);

int main(void)
{
    uint8_t rom[0x400] = {0}, ram_before[0x10000], z80_before[0x2000];
    GensMachineSettings settings = {
        -1, {0, 1, 2}, 44100, 0, 0, 1,
        {GENS_INPUT_TWO_PADS, 6, 6, 3, 3, 3},
        {255, 255, 255, 100, 100, 0, 0, 0, 0}
    };
    GensMachineView view;
    GensM68k cpu_before;
    GensZ80 z80_cpu_before;
    uint32_t vdp_before[16], vdp_after[16];
    const uint8_t *bytes;
    int i, input_before;

    assert(GetBusCount() == 1 && strcmp(GetBusName(0), "M68K BUS") == 0);
    assert(GetBusSize(0) == 0x1000000 && GetBusWritable(0));
    assert(GetBusName(1) == 0 && GetBusSize(1) == 0 && !GetBusWritable(1));
    assert(PeekBus(0, 0) == 0);
    PokeBus(0, 0xff0000, 0xff);
    rom[1] = 0xff; rom[2] = 0xff;
    rom[6] = 2;
    memcpy(rom + 0x100, "SEGA GENESIS", 12);
    rom[0x1f0] = 'U';
    rom[0x200] = 0x70; rom[0x201] = 0x7f; /* MOVEQ #127,D0 */
    rom[0x202] = 0x60; rom[0x203] = 0xfe; /* BRA.S to itself */
    assert(gens_machine_load(rom, sizeof(rom), "tooling.bin", &settings, 0) == GENS_MACHINE_OK);
    assert(gens_machine_frame(0) == GENS_MACHINE_OK);
    assert(GetRegisterValue(0) == 127);
    assert(GetRegisterCount() == 38);
    for (i = 0; i < 18; ++i)
    {
        char name[32];
        snprintf(name, sizeof(name), "M68K %s", GetRegisterName(i));
        assert(strcmp(GetRegisterName(20 + i), name) == 0);
        assert(GetRegisterValue(20 + i) == GetRegisterValue(i));
        assert(GetRegisterBits(20 + i) == GetRegisterBits(i));
    }
    assert(GetRegisterName(-1) == 0 && GetRegisterName(38) == 0);
    assert(GetRegisterBits(-1) == 0 && GetRegisterBits(38) == 0);
    assert(GetRegisterValue(38) == 0);
    assert(PeekBus(0, 0x100) == 'S' && PeekBus(0, 0x101) == 'E');
    PokeBus(0, 0xff0010, 0x12);
    PokeBus(0, 0xff0011, 0x34);
    PokeBus(0, 0xff0012, 0x56);
    PokeBus(0, 0xff0013, 0x78);
    bytes = ReadBus(0, 0xff0010, 4);
    assert(bytes != 0 && memcmp(bytes, "\x12\x34\x56\x78", 4) == 0);
    assert(ReadBus(0, 0xff0000, 65536) != 0);
    assert(ReadBus(0, 0xffffff, 1) != 0);
    assert(ReadBus(0, 0xffffff, 2) == 0);
    assert(ReadBus(0, -1, 1) == 0 && ReadBus(0, INT64_MAX, 2) == 0);
    assert(ReadBus(0, 0, 0) == 0 && ReadBus(0, 0, -1) == 0);
    assert(ReadBus(0, 0, 65537) == 0 && ReadBus(1, 0, 1) == 0);
    assert(PeekBus(1, 0x100) == 0 && PeekBus(0, -1) == 0);
    PokeBus(1, 0xff0010, 0xab);
    PokeBus(0, 0x100, 0);
    assert(PeekBus(0, 0xff0010) == 0x12 && PeekBus(0, 0x100) == 'S');

    gens_machine_view(&view);
    memcpy(ram_before, view.ram68k, sizeof(ram_before));
    memcpy(z80_before, view.ramz80, sizeof(z80_before));
    cpu_before = *gens_machine_m68k();
    z80_cpu_before = *gens_machine_z80();
    gens_machine_vdp_state(vdp_before);
    input_before = gens_machine_input_read();
    for (i = 0; i < 0x1000000; i += 65536)
        assert(ReadBus(0, i, 65536) != 0);
    gens_machine_vdp_state(vdp_after);
    assert(memcmp(&cpu_before, gens_machine_m68k(), sizeof(cpu_before)) == 0);
    assert(memcmp(&z80_cpu_before, gens_machine_z80(), sizeof(z80_cpu_before)) == 0);
    assert(memcmp(vdp_before, vdp_after, sizeof(vdp_before)) == 0);
    assert(memcmp(ram_before, view.ram68k, sizeof(ram_before)) == 0);
    assert(memcmp(z80_before, view.ramz80, sizeof(z80_before)) == 0);
    assert(input_before == gens_machine_input_read());
    puts("tooling ok");
    return 0;
}
