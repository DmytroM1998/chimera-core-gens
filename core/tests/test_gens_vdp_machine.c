#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gens_vdp_machine.h"

static GensVdpMachine machine;
static uint8_t rom[4], ram[65536], cpu_interrupts[8];
static int cycles_m68k, cycles_per_line = 488, visible_lines = 224, rom_size, sega_cd;

static int odometer(void *opaque, GensM68kBusState *state, uint32_t *value)
{
    (void)opaque;
    (void)state;
    *value = 0;
    return 1;
}

static int interrupt(void *opaque, GensM68kBusState *state, unsigned int level, int active)
{
    (void)opaque;
    (void)state;
    (void)level;
    (void)active;
    return 1;
}

static GensVdpMachineHost host = {
    &cycles_m68k, &cycles_per_line, &visible_lines, rom, ram, sizeof(rom), sizeof(ram),
    &rom_size, &sega_cd, 0, odometer, cpu_interrupts, 0, interrupt
};

int main(void)
{
    GensVdpMachineHost bad = host;
    uint8_t fake = 1;
    unsigned int i;

    bad.visible_lines = 0;
    assert(!gens_vdp_machine_init(&machine, &bad));
    assert(!gens_vdp_machine_init(0, &host));
    assert(gens_vdp_machine_init(&machine, &host));
    assert(gens_vdp_machine_valid(&machine));
    assert(machine.vdp.timing.visible_lines == &visible_lines);
    assert(machine.rows[239] == 239u * 336u);
    machine.regs[1] = 0x40;
    assert(!gens_vdp_machine_render_line(&machine));

    machine.hcell = 32;
    machine.hpix = 256;
    machine.hpix_begin = 32;
    machine.hwin_mul = 5;
    machine.hscroll_cmul = 5;
    machine.hscroll_cmask = machine.vscroll_cmask = 31;
    machine.scr_b = machine.vram + 0x2000;
    machine.scr_a = machine.vram + 0x4000;
    machine.window = machine.vram + 0x6000;
    machine.hscroll = machine.vram + 0x800;
    machine.vram[0x802] = 0xf8; machine.vram[0x803] = 0x03;
    machine.vram[0x2000] = 0x01; machine.vram[0x2001] = 0x20;
    machine.vram[0x2002] = 0x01; machine.vram[0x2003] = 0x20;
    machine.vram[32] = 0x21; machine.vram[33] = 0x43;
    machine.vram[34] = 0x65; machine.vram[35] = 0x87;
    machine.cram[40] = 0xee; machine.cram[41] = 0x0e;
    machine.sprite_struct[1] = 500;
    machine.sprite_struct[5] = 500;
    machine.palette16[0x4eee] = 0x1234;
    machine.palette32[0x4eee] = 0x00abcdefu;
    for (i = 0; i < 336; ++i) machine.screen16x[i] = 0x4040;
    assert(gens_vdp_machine_render_line(&machine));
    assert(machine.screen16x[8] == 0x4eee);

    assert(gens_vdp_machine_render_screen(&machine, &fake));
    assert(fake == 0 && machine.current_line == 224);
    assert(machine.md_screen16[8] == 0x1234 && machine.md_screen32[8] == 0x00abcdefu);
    assert(!gens_vdp_machine_render_screen(&machine, 0));

    machine.current_line = 240;
    assert(!gens_vdp_machine_render_line(&machine));
    machine.current_line = 0;
    machine.regs[7] = 0;
#ifndef GENS_AST_NO_FLOAT
    {
        GensColorProfile profile = {255, 255, 255, 0, 0, 0, 0, 0, 0};
        machine.cram_flag = 0;
        assert(gens_vdp_machine_set_colors(&machine, &profile));
        assert((machine.cram_flag & 1) != 0);
    }
#endif
    printf("vdp machine ok\n");
    return 0;
}
