/* Whole-machine wiring and the frame scheduler follow the pinned Gens-Rerecording Gens.cpp,
 * G_main.cpp, G_dsound.cpp, Cpu_68k.c, Cpu_Z80.c and Mem_Z80.asm.
 * The upstream component terms apply to derived portions.
 *
 * The machine is a single instance, like the original. The sound chips and the controller
 * port use Gens' global variables, so those globals are defined here.
 */
#include "gens_machine.h"

#include <stddef.h>
#include <string.h>

#include "gens_genesis_bus.h"
#include "gens_io.h"
#include "gens_m68k_cpu.h"
#include "gens_sound.h"
#include "gens_vdp_machine.h"
#include "gens_vdp_reset.h"
#include "gens_z80_cpu.h"
#include "gens_z80_native.h"
#include "io.h"
#include "psg.h"
#include "ym2612.h"

_Static_assert(sizeof(int) == 4, "Gens machine globals require 32-bit int");
_Static_assert(offsetof(GensZ80, ram) % 4u == 0u, "Bank_Z80 alias must be aligned");

#define CLOCK_NTSC 53693175
#define CLOCK_PAL 53203424
#define ROM_BYTES (6u * 1024u * 1024u)

/* Globals that the original sound chips and audio cursor read by name. */
int Seg_L[882], Seg_R[882], Seg_Junk[882];
int Seg_Length;
unsigned int Sound_Interpol[882];
unsigned int Sound_Extrapol[312][2];
int VDP_Current_Line, VDP_Num_Lines;
int disableSound2;
int GYM_Dumping;

int Update_GYM_Dump(char v0, char v1, char v2)
{
    (void)v0; (void)v1; (void)v2;
    return 0;
}

/* In Gens' bss, Ram_68k is directly followed by Rom_Data and SRAM. Word and long accesses at the
 * end of RAM and fetches through the 1 MiB 0xEF0000 mirror read the following bytes, so the
 * same order is kept here. */
typedef struct MachineMemory {
    uint8_t ram68k[0x10000];
    uint8_t rom[ROM_BYTES];
    uint8_t sram[0x10000];
    uint8_t tail[0x10000];
} MachineMemory;

typedef struct Machine {
    int loaded, error;
    const char *error_site;
    uint32_t error_address;
    GensMachineSettings settings;
    size_t image_size;
    GensRomEncoding encoding;

    GensM68k cpu;
    GensM68kFetch fetch[5];
    GensZ80 z80;
    GensVdpMachine vdp;
    GensVdpReset vdp_reset;
    GensGenesisBus bus;
    GensZ80Control z80_control;
    GensZ80Memory z80_memory;
    GensZ80Native z80_native;

    /* Gens machine globals, kept with their original widths. */
    int sram_start, sram_end, sram_on, sram_write, sram_custom;
    int z80_state, game_mode, cpu_mode, gen_version;
    int last_bus_req_cnt, last_bus_req_st, fake_fetch;
    int cycles_m68k, cycles_z80, cpl_m68k, cpl_z80;
    int z80_m68k_cycle_tab[512];
    int visible_lines, rom_size, sega_cd_started, dma_tmp;
    uint8_t fake_screen;
} Machine;

static MachineMemory memory;
static uint8_t image[ROM_BYTES];
static Machine machine;

static void fail_at(const char *site)
{
    if (!machine.error)
        machine.error_site = site;
    machine.error = 1;
}

/* Bank_Z80 is the dword after Ram_Z80 in Mem_Z80.asm. */
static int *bank_z80(void)
{
    return (int *)(void *)&machine.z80.ram[0x2000];
}

/* ---------------------------------------------------------------------------------------------
 * 68000 bus. The shared GensM68kBusState carries the live I/O cycle counter while a device
 * handler runs. The 68000 core keeps the authoritative copy at all other times.
 */
static void state_from_cpu(GensM68kBusState *state)
{
    state->io_cycle_counter = (uint32_t)machine.cpu.io_cycle_counter;
}

static void cpu_from_state(const GensM68kBusState *state)
{
    machine.cpu.io_cycle_counter = (int32_t)state->io_cycle_counter;
}

static uint8_t m68k_read_byte(void *opaque, uint32_t address)
{
    uint8_t value = 0;
    (void)opaque;
    state_from_cpu(&machine.bus.state);
    if (!machine.bus.cpu.read_byte(machine.bus.cpu.opaque, &machine.bus.state, address, &value))
        fail_at("68000 byte read");
    cpu_from_state(&machine.bus.state);
    return value;
}

static uint16_t m68k_read_word(void *opaque, uint32_t address)
{
    uint16_t value = 0;
    (void)opaque;
    state_from_cpu(&machine.bus.state);
    if (!machine.bus.cpu.read_word(machine.bus.cpu.opaque, &machine.bus.state, address, &value))
        fail_at("68000 word read");
    cpu_from_state(&machine.bus.state);
    return value;
}

static void m68k_write_byte(void *opaque, uint32_t address, uint8_t value)
{
    (void)opaque;
    state_from_cpu(&machine.bus.state);
    if (!machine.bus.cpu.write_byte(machine.bus.cpu.opaque, &machine.bus.state, address, value))
        fail_at("68000 byte write");
    cpu_from_state(&machine.bus.state);
}

static void m68k_write_word(void *opaque, uint32_t address, uint16_t value)
{
    (void)opaque;
    state_from_cpu(&machine.bus.state);
    if (!machine.bus.cpu.write_word(machine.bus.cpu.opaque, &machine.bus.state, address, value))
    {
        fail_at("68000 word write");
        if (machine.error_address == 0)
            machine.error_address = address | 0x80000000u;
    }
    cpu_from_state(&machine.bus.state);
}

/* vdp_io.asm Int_Ack. */
static uint8_t m68k_int_ack(void *opaque, uint8_t level)
{
    uint32_t eax = level;
    (void)opaque;
    if (!gens_vdp_irq_ack(&machine.vdp.irq, &eax))
        fail_at("interrupt acknowledge");
    return (uint8_t)eax;
}

/* Cpu_68k.c M68K_Reset_Handler does nothing. */
static void m68k_reset_instruction(void *opaque)
{
    (void)opaque;
}

/* main68k_readOdometer and main68k_interrupt as seen from a device handler. */
static int read_m68k_odometer(void *opaque, GensM68kBusState *state, uint32_t *value)
{
    (void)opaque;
    cpu_from_state(state);
    *value = gens_m68k_read_odometer(&machine.cpu);
    return 1;
}

static int request_m68k_interrupt(void *opaque, GensM68kBusState *state, unsigned int level,
                                  int vector)
{
    (void)opaque;
    (void)vector;
    cpu_from_state(state);
    gens_m68k_interrupt(&machine.cpu, level);
    state_from_cpu(state);
    return 1;
}

/* ggenie.cpp Fix_Codes with no active codes. */
static int fix_codes(void *opaque, GensM68kBusState *state, uint32_t address, unsigned int size)
{
    (void)opaque; (void)state; (void)address; (void)size;
    return 1;
}

static void bus_hook(void *opaque, GensM68kBusState *state, enum GensM68kBusHook kind)
{
    (void)opaque; (void)state; (void)kind;
}

static int genesis_device_read(void *opaque, GensM68kBusState *state,
                               enum GensGenesisDevice device, uint32_t address,
                               unsigned int size, uint16_t *value)
{
    (void)opaque; (void)state; (void)device; (void)address; (void)size; (void)value;
    return 0;
}

static int genesis_device_write(void *opaque, GensM68kBusState *state,
                                enum GensGenesisDevice device, uint32_t address,
                                unsigned int size, uint16_t value)
{
    if (device == GENS_GENESIS_PSG)
        return gens_sound_genesis_write(opaque, state, device, address, size, value);
    return 0;
}

/* ---------------------------------------------------------------------------------------------
 * Z80 side.
 */
static int z80_window_read(void *opaque, GensM68kBusState *state, enum GensZ80Device device,
                           uint32_t address, uint16_t *value)
{
    (void)opaque;
    if (device == GENS_Z80_YM2612)
        return gens_sound_z80_read(0, state, device, address, value);
    return gens_vdp_window_read(&machine.vdp.vdp, state, device, address, value);
}

static int z80_window_write(void *opaque, GensM68kBusState *state, enum GensZ80Device device,
                            uint32_t address, uint8_t value)
{
    (void)opaque;
    if (device == GENS_Z80_YM2612 || device == GENS_Z80_PSG)
        return gens_sound_z80_write(0, state, device, address, value);
    if (device == GENS_Z80_VDP_DATA)
        return gens_vdp_device_write(&machine.vdp.vdp, state, GENS_GENESIS_VDP,
                                     UINT32_C(0xc00000), 1u, value);
    return 0;
}

static int z80_read_odometer_cb(void *opaque, GensM68kBusState *state, uint32_t *value)
{
    return read_m68k_odometer(opaque, state, value);
}

static int z80_set_odometer_cb(void *opaque, GensM68kBusState *state, uint32_t target)
{
    (void)opaque; (void)state;
    gens_z80_set_odo(&machine.z80, target);
    return 1;
}

static int z80_execute_cb(void *opaque, GensM68kBusState *state, uint32_t target)
{
    (void)opaque; (void)state;
    gens_z80_exec(&machine.z80, target);
    return 1;
}

static int z80_reset_cb(void *opaque, GensM68kBusState *state)
{
    (void)opaque; (void)state;
    gens_z80_reset(&machine.z80);
    return 1;
}

static int ym_reset_cb(void *opaque, GensM68kBusState *state)
{
    return gens_sound_reset_ym2612(opaque, state);
}

/* Device operations of the native Z80 handlers. C callees leave ECX and EDX unchanged here. */
static int z80_native_backend(void *opaque, GensM68kBusState *state,
                              enum GensZ80NativeOperation operation, GensZ80Registers *registers)
{
    uint8_t byte;
    (void)opaque;
    switch (operation)
    {
    case GENS_Z80_NATIVE_YM_READ:
        registers->eax = (uint32_t)YM2612_Read();
        return 1;
    case GENS_Z80_NATIVE_YM_WRITE:
        registers->eax = (uint32_t)YM2612_Write((unsigned char)(registers->ecx & 3u),
                                                (unsigned char)registers->edx);
        return 1;
    case GENS_Z80_NATIVE_VDP_STATUS:
    case GENS_Z80_NATIVE_V_COUNTER:
    case GENS_Z80_NATIVE_H_COUNTER:
        return gens_vdp_native_device_read(&machine.vdp.vdp, state, operation, registers);
    case GENS_Z80_NATIVE_VDP_BYTE:
        return gens_vdp_device_write(&machine.vdp.vdp, state, GENS_GENESIS_VDP,
                                     UINT32_C(0xc00000), 1u, (uint8_t)registers->edx);
    case GENS_Z80_NATIVE_VDP_WORD:
        return gens_vdp_device_write(&machine.vdp.vdp, state, GENS_GENESIS_VDP,
                                     UINT32_C(0xc00000), 2u, (uint16_t)registers->edx);
    case GENS_Z80_NATIVE_PSG:
        PSG_Write((int)(registers->edx & 0xffu));
        return 1;
    case GENS_Z80_NATIVE_M68K_READ:
        if (!machine.bus.cpu.read_byte(machine.bus.cpu.opaque, state, registers->ecx, &byte))
            return 0;
        registers->eax = byte;
        return 1;
    case GENS_Z80_NATIVE_M68K_WRITE:
        return machine.bus.cpu.write_byte(machine.bus.cpu.opaque, state, registers->ecx,
                                          (uint8_t)registers->edx);
    }
    return 0;
}

static uint8_t z80_read_b(GensZ80 *z, uint32_t address)
{
    GensZ80Registers registers = {0, address & 0xffffu, 0};
    if (!gens_z80_native_read(&machine.z80_native, &machine.bus.state, 1u, &registers))
        fail_at("Z80 byte read");
    z->io_ecx = registers.ecx;
    return (uint8_t)registers.eax;
}

static uint16_t z80_read_w(GensZ80 *z, uint32_t address)
{
    GensZ80Registers registers = {0, address & 0xffffu, 0};
    (void)z;
    if (!gens_z80_native_read(&machine.z80_native, &machine.bus.state, 2u, &registers))
        fail_at("Z80 word read");
    return (uint16_t)registers.eax;
}

static void z80_write_b(GensZ80 *z, uint32_t address, uint8_t value)
{
    GensZ80Registers registers = {0, address & 0xffffu, value};
    (void)z;
    if (!gens_z80_native_write(&machine.z80_native, &machine.bus.state, 1u, &registers))
        fail_at("Z80 byte write");
}

static void z80_write_w(GensZ80 *z, uint32_t address, uint16_t value)
{
    GensZ80Registers registers = {0, address & 0xffffu, value};
    (void)z;
    if (!gens_z80_native_write(&machine.z80_native, &machine.bus.state, 2u, &registers))
        fail_at("Z80 word write");
}

/* Cpu_Z80.c Z80_Init. Every page uses the native handlers, which dispatch on the address like
 * the original per-range handlers. */
static void z80_init(void)
{
    GensZ80 *z = &machine.z80;
    gens_z80_init(z);
    gens_z80_add_fetch(z, 0x00, 0x1f, z->ram);
    gens_z80_add_fetch(z, 0x20, 0x3f, z->ram);
    gens_z80_add_read_b(z, 0x00, 0xff, z80_read_b);
    gens_z80_add_read_w(z, 0x00, 0xff, z80_read_w);
    gens_z80_add_write_b(z, 0x00, 0xff, z80_write_b);
    gens_z80_add_write_w(z, 0x00, 0xff, z80_write_w);
}

/* Cpu_Z80.c Z80_Reset. */
static void z80_machine_reset(void)
{
    memset(machine.z80.ram, 0, 0x2000);
    *bank_z80() = 0xff8000;
    machine.z80_state &= 1;
    machine.last_bus_req_cnt = 0;
    machine.last_bus_req_st = 0;
    gens_z80_reset(&machine.z80);
}

/* ---------------------------------------------------------------------------------------------
 * Initialization.
 */
static int round_double(double value)
{
    if ((value - (double)(int)value) > 0.5) return (int)(value + 1);
    return (int)value;
}

/* Gens.cpp Init_Tab, the part the Genesis uses. */
static void init_tab(void)
{
    int x;
    for (x = 0; x < 512; ++x)
        machine.z80_m68k_cycle_tab[x] = (int)((double)x * 7.0 / 15.0);
}

/* G_dsound.cpp Init_Sound, table part. */
static int init_sound_tables(void)
{
    int i, lines = machine.cpu_mode ? 312 : 262;
    switch (machine.settings.sound_rate)
    {
    case 11025: Seg_Length = machine.cpu_mode ? 220 : 184; break;
    case 22050: Seg_Length = machine.cpu_mode ? 441 : 368; break;
    case 44100: Seg_Length = machine.cpu_mode ? 882 : 735; break;
    default: return 0;
    }
    for (i = 0; i < lines; ++i)
    {
        Sound_Extrapol[i][0] = (unsigned int)((Seg_Length * i) / lines);
        Sound_Extrapol[i][1] = (unsigned int)(((Seg_Length * (i + 1)) / lines) -
                                              (int)Sound_Extrapol[i][0]);
    }
    for (i = 0; i < Seg_Length; ++i)
        Sound_Interpol[i] = (unsigned int)((lines * i) / Seg_Length);
    memset(Seg_L, 0, sizeof(Seg_L));
    memset(Seg_R, 0, sizeof(Seg_R));
    return 1;
}

/* Cpu_68k.c M68K_Reset(GENESIS, 1) with Init_Memory_M68K. */
static int m68k_machine_reset(void)
{
    GensM68k *cpu = &machine.cpu;
    memset(memory.ram68k, 0, sizeof(memory.ram68k));
    machine.fetch[0] = (GensM68kFetch){0, (uint32_t)machine.rom_size - 1u,
                                       (uintptr_t)memory.rom};
    machine.fetch[1] = (GensM68kFetch){0xff0000, 0xffffff,
                                       (uintptr_t)memory.ram68k - 0xff0000u};
    machine.fetch[2] = (GensM68kFetch){0xf00000, 0xf0ffff,
                                       (uintptr_t)memory.ram68k - 0xf00000u};
    machine.fetch[3] = (GensM68kFetch){0xef0000, 0xefffff,
                                       (uintptr_t)memory.ram68k - 0xef0000u};
    machine.fetch[4] = (GensM68kFetch){0xffffffffu, 0xffffffffu, 0};
    gens_m68k_reset(cpu);
    gens_genesis_bus_reset_mapping(&machine.bus);
    return 1;
}

/* Cpu_68k.c M68K_Init. */
static void m68k_init(void)
{
    GensM68k *cpu = &machine.cpu;
    memset(cpu, 0, sizeof(*cpu));
    cpu->fetch = cpu->s_fetch = cpu->u_fetch = machine.fetch;
    cpu->ram = memory.ram68k;
    cpu->bus = (GensM68kCpuBus){0, m68k_read_byte, m68k_read_word, m68k_write_byte,
                                m68k_write_word, m68k_int_ack, m68k_reset_instruction};
    gens_m68k_init(cpu);
}

static int vdp_machine_reset(void)
{
    int ok;
    state_from_cpu(&machine.bus.state);
    ok = gens_vdp_reset(&machine.vdp_reset, &machine.bus.state);
    cpu_from_state(&machine.bus.state);
    return ok;
}

static int wire_machine(void)
{
    GensVdpMachineHost host;
    GensGenesisBusConfig bus;
    GensVdpMachine *vdp = &machine.vdp;

    host = (GensVdpMachineHost){
        &machine.cycles_m68k, &machine.cpl_m68k, &machine.visible_lines,
        memory.rom, memory.ram68k, ROM_BYTES, sizeof(memory.ram68k),
        &machine.rom_size, &machine.sega_cd_started,
        0, read_m68k_odometer, &machine.cpu.interrupts[0], 0, request_m68k_interrupt
    };
    if (!gens_vdp_machine_init(vdp, &host))
        return 0;
    vdp->sprite_over = machine.settings.sprite_limit;
    vdp->mode555 = machine.settings.colors.mode555;
    vdp->pink = (uint8_t)machine.settings.colors.pink;
    if (!gens_vdp_machine_set_colors(vdp, &machine.settings.colors))
        return 0;
    machine.vdp_reset = (GensVdpReset){
        &vdp->vdp, &vdp->irq, vdp->md_screen16, vdp->md_palette,
        GENS_VDP_MACHINE_SCREEN_PIXELS, 256, vdp->md_screen32, vdp->md_palette32,
        vdp->sprite_struct, vdp->sprite_visible, (uint32_t *)(void *)vdp->vsram_over,
        GENS_VDP_MACHINE_SCREEN_PIXELS, 256, 2048, 256, 8, &vdp->pal_lock, &machine.dma_tmp
    };

    machine.z80_memory = (GensZ80Memory){
        machine.z80.ram, 0x2000, bank_z80(), 0, z80_window_read, z80_window_write
    };
    machine.z80_control = (GensZ80Control){
        &machine.z80_state, &machine.last_bus_req_cnt, &machine.last_bus_req_st,
        &machine.fake_fetch, &machine.cycles_m68k, &machine.cycles_z80,
        machine.z80_m68k_cycle_tab, 512, 0, z80_read_odometer_cb, z80_set_odometer_cb,
        z80_execute_cb, z80_reset_cb, ym_reset_cb
    };
    machine.z80_native = (GensZ80Native){&machine.z80_memory, 0, z80_native_backend};
    bus = (GensGenesisBusConfig){
        memory.ram68k, sizeof(memory.ram68k), memory.rom, ROM_BYTES, memory.sram,
        sizeof(memory.sram), &machine.sram_start, &machine.sram_end, &machine.sram_on,
        &machine.sram_write, &machine.sram_custom, &machine.z80_state, &machine.game_mode,
        &machine.cpu_mode, &machine.gen_version, 0, genesis_device_read,
        genesis_device_write, fix_codes, bus_hook, &machine.z80_control,
        &machine.z80_memory, &vdp->vdp
    };
    return gens_genesis_bus_init(&machine.bus, &bus);
}

/* Gens.cpp Misc_Genesis_Init, Genesis fields. */
static void misc_genesis_init(void)
{
    GensZ80 *z = &machine.z80;
    memset(&z->a, 0, (size_t)((uint8_t *)(&z->cycle_sup + 1) - &z->a));
    Controller_1_Delay = 0;
    Controller_2_Delay = 0;
    machine.vdp.control[5] = 0;
    machine.vdp.regs[25] = 0;
    machine.vdp.current_line = VDP_Current_Line = 0;
    machine.cycles_m68k = machine.cycles_z80 = 0;
    Lag_Frame = 0;
    machine.cpu.cycles_leftover = 0;
    machine.cpu.xflag = 0;
    machine.cpu.io_cycle_counter = -1;
    machine.cpu.io_fetchbase = 0;
    machine.cpu.io_fetchbased_pc = 0;
    machine.cpu.access_address = 0;
    machine.cpu.odometer = 0;
}

/* Gens.cpp Init_Genesis from the loaded, unswapped image. */
static GensMachineStatus init_genesis(void)
{
    GensRomSetup setup;
    GensRomStatus status;

    memset(memory.rom, 0, sizeof(memory.rom));
    memcpy(memory.rom, image, machine.image_size);
    status = gens_rom_setup(memory.rom, (size_t)machine.rom_size, machine.settings.country,
                            machine.settings.country_order, &setup);
    if (status != GENS_ROM_OK)
        return GENS_MACHINE_BAD_ROM;

    misc_genesis_init();
    machine.sram_start = (int)setup.sram_start;
    machine.sram_end = (int)setup.sram_end;
    machine.sram_on = setup.sram_enabled;
    machine.sram_write = setup.sram_writable;
    machine.sram_custom = setup.sram_custom;
    machine.game_mode = setup.game_mode;
    machine.cpu_mode = setup.cpu_mode;
    Controller_1_COM = Controller_2_COM = 0;
    memset(memory.sram, 0, sizeof(memory.sram));

    machine.visible_lines = 224;
    machine.gen_version = 0x20;
    if (gens_rom_swap_words(memory.rom, (size_t)machine.rom_size) != GENS_ROM_OK)
        return GENS_MACHINE_BAD_ROM;

    m68k_init();
    m68k_machine_reset();
    z80_machine_reset();
    if (!vdp_machine_reset())
        return GENS_MACHINE_INIT_FAILED;

    if (machine.cpu_mode)
    {
        machine.cpl_z80 = round_double((((double)CLOCK_PAL / 15.0) / 50.0) / 312.0);
        machine.cpl_m68k = round_double((((double)CLOCK_PAL / 7.0) / 50.0) / 312.0);
        VDP_Num_Lines = 312;
        machine.vdp.status |= 0x0001;
    }
    else
    {
        machine.cpl_z80 = round_double((((double)CLOCK_NTSC / 15.0) / 60.0) / 262.0);
        machine.cpl_m68k = round_double((((double)CLOCK_NTSC / 7.0) / 60.0) / 262.0);
        VDP_Num_Lines = 262;
        machine.vdp.status &= 0xfffe;
    }
    if (!gens_sound_init(machine.cpu_mode, machine.settings.sound_rate,
                         machine.settings.ym2612_improvement, machine.settings.psg_improvement))
        return GENS_MACHINE_INIT_FAILED;
    if (!init_sound_tables())
        return GENS_MACHINE_INIT_FAILED;
    return GENS_MACHINE_OK;
}

static int valid_settings(const GensMachineSettings *s)
{
    int seen = 0, i;
    if (s == 0 || s->country < -1 || s->country > 3 ||
        (s->sound_rate != 11025 && s->sound_rate != 22050 && s->sound_rate != 44100) ||
        (s->ym2612_improvement != 0 && s->ym2612_improvement != 1) ||
        (s->psg_improvement != 0 && s->psg_improvement != 1) ||
        (s->sprite_limit != 0 && s->sprite_limit != 1) || !gens_color_profile_valid(&s->colors))
        return 0;
    for (i = 0; i < 3; ++i)
    {
        if (s->country_order[i] > 2)
            return 0;
        seen |= 1 << s->country_order[i];
    }
    return seen == 7;
}

GensMachineStatus gens_machine_load(const uint8_t *data, size_t size, const char *name,
                                    const GensMachineSettings *settings,
                                    GensRomStatus *rom_status)
{
    size_t rom_size;
    GensRomStatus status;
    GensMachineStatus result;

    if (data == 0 || name == 0 || !valid_settings(settings) || size > sizeof(image))
        return GENS_MACHINE_INVALID_ARGUMENT;
    memset(&machine, 0, sizeof(machine));
    machine.settings = *settings;

    /* rom.cpp Load_Rom zeroes the whole buffer before reading the file. */
    memset(image, 0, sizeof(image));
    memcpy(image, data, size);
    status = gens_rom_normalize(image, size, sizeof(image), name, &rom_size, &machine.encoding);
    if (rom_status != 0)
        *rom_status = status;
    if (status != GENS_ROM_OK)
        return GENS_MACHINE_BAD_ROM;
    machine.image_size = sizeof(image);
    machine.rom_size = (int)rom_size;

    /* G_main.cpp startup: Z80_Init, sound defaults and Init_Tab. */
    YM2612_Enable = PSG_Enable = DAC_Enable = 1;
    /* Load_Config's "Z80 State" defaults to 1: the Z80 is emulated. Z80_Reset keeps bit 0. */
    machine.z80_state = 1;
    init_tab();
    z80_init();
    if (!wire_machine())
        return GENS_MACHINE_INIT_FAILED;
    if (!gens_input_configure(&machine.settings.input))
        return GENS_MACHINE_INVALID_ARGUMENT;
    result = init_genesis();
    if (result != GENS_MACHINE_OK)
        return result;
    machine.loaded = 1;
    return GENS_MACHINE_OK;
}

GensMachineStatus gens_machine_power(void)
{
    GensMachineStatus result;
    if (!machine.loaded)
        return GENS_MACHINE_NOT_LOADED;
    result = init_genesis();
    if (result != GENS_MACHINE_OK)
        machine.loaded = 0;
    return result;
}

/* Gens.cpp Reset_Genesis. */
GensMachineStatus gens_machine_reset(void)
{
    if (!machine.loaded)
        return GENS_MACHINE_NOT_LOADED;
    Controller_1_COM = Controller_2_COM = 0;
    if (machine.rom_size <= 2 * 1024 * 1024)
    {
        machine.sram_on = 1;
        machine.sram_write = 1;
    }
    else
    {
        machine.sram_on = 0;
        machine.sram_write = 0;
    }
    m68k_machine_reset();
    z80_machine_reset();
    if (!vdp_machine_reset())
        return GENS_MACHINE_BACKEND_ERROR;
    YM2612_Reset();
    if (machine.cpu_mode)
        machine.vdp.status |= 1;
    else
        machine.vdp.status &= ~1;
    return GENS_MACHINE_OK;
}

/* ---------------------------------------------------------------------------------------------
 * Gens.cpp Do_Genesis_Frame(false) with sound enabled.
 */
static int *left_buffer(void) { return disableSound2 ? Seg_Junk : Seg_L; }
static int *right_buffer(void) { return disableSound2 ? Seg_Junk : Seg_R; }

static void set_line(int line)
{
    machine.vdp.current_line = line;
    VDP_Current_Line = line;
}

static void sound_line(void)
{
    int *buffer[2];
    int line = machine.vdp.current_line;
    buffer[0] = left_buffer() + Sound_Extrapol[line][0];
    buffer[1] = right_buffer() + Sound_Extrapol[line][0];
    YM2612_DacAndTimers_Update(buffer, (int)Sound_Extrapol[line][1]);
    YM_Len += (int)Sound_Extrapol[line][1];
    PSG_Len += (int)Sound_Extrapol[line][1];
}

static void m68k_exec(int target, const GensM68kObserver *observer)
{
    gens_m68k_exec_observed(&machine.cpu, (uint32_t)target, observer);
}

static void z80_to(int target)
{
    if (machine.z80_state == 3)
        gens_z80_exec(&machine.z80, (uint32_t)target);
    else
        gens_z80_set_odo(&machine.z80, (uint32_t)target);
}

static void update_dma(void)
{
    uint32_t cycles;
    if (machine.vdp.dma_length == 0)
        return;
    if (!gens_vdp_dma_tick(&machine.vdp.vdp, &cycles))
    {
        fail_at("DMA countdown");
        return;
    }
    gens_m68k_add_cycles(&machine.cpu, cycles);
}

static void update_irq_line(void)
{
    state_from_cpu(&machine.bus.state);
    if (!gens_vdp_irq_update(&machine.vdp.irq, &machine.bus.state))
        fail_at("interrupt update");
    cpu_from_state(&machine.bus.state);
}

static void line_start(void)
{
    sound_line();
    Fix_Controllers();
    machine.cycles_m68k += machine.cpl_m68k;
    machine.cycles_z80 += machine.cpl_z80;
    update_dma();
}

static void run_frame(const GensM68kObserver *observer)
{
    GensVdpMachine *vdp = &machine.vdp;
    int hint_counter, line;

    if (machine.cpu_mode && (vdp->regs[1] & 8u)) machine.visible_lines = 240;
    else machine.visible_lines = 224;

    YM_Buf[0] = PSG_Buf[0] = left_buffer();
    YM_Buf[1] = PSG_Buf[1] = right_buffer();
    YM_Len = PSG_Len = 0;

    machine.cycles_m68k = machine.cycles_z80 = 0;
    machine.last_bus_req_cnt = -1000;
    gens_m68k_trip_odometer(&machine.cpu);
    gens_z80_clear_odo(&machine.z80);

    vdp->vram_flag = 1;
    vdp->status &= 0xfff7;
    if (vdp->regs[12] & 0x2u) vdp->status ^= 0x0010;
    hint_counter = (int)vdp->regs[10];

    for (line = 0; line < machine.visible_lines; ++line)
    {
        set_line(line);
        line_start();
        vdp->status |= 0x0004;
        m68k_exec(machine.cycles_m68k - 404, observer);
        vdp->status &= 0xfffb;
        if (--hint_counter < 0)
        {
            hint_counter = (int)vdp->regs[10];
            vdp->vdp_int |= 0x4;
            update_irq_line();
        }
        if (!gens_vdp_machine_render_line(vdp))
            fail_at("Render_Line");
        m68k_exec(machine.cycles_m68k, observer);
        z80_to(machine.cycles_z80);
    }
    set_line(line);

    /* Render_MD_Screen walks VDP_Current_Line over the visible lines and leaves it there. */
    machine.fake_screen = 0;
    if (!gens_vdp_machine_render_screen(vdp, &machine.fake_screen))
        fail_at("Render_MD_Screen");
    set_line(vdp->current_line);

    line_start();
    if (--hint_counter < 0)
    {
        vdp->vdp_int |= 0x4;
        update_irq_line();
    }
    vdp->status |= 0x000c;
    m68k_exec(machine.cycles_m68k - 360, observer);
    z80_to(machine.cycles_z80 - 168);
    vdp->status &= 0xfffb;
    vdp->status |= 0x0080;
    vdp->vdp_int |= 0x8;
    update_irq_line();
    gens_z80_interrupt(&machine.z80, 0xff);
    m68k_exec(machine.cycles_m68k, observer);
    z80_to(machine.cycles_z80);

    for (line = vdp->current_line + 1; line < VDP_Num_Lines; ++line)
    {
        set_line(line);
        line_start();
        vdp->status |= 0x0004;
        m68k_exec(machine.cycles_m68k - 404, observer);
        vdp->status &= 0xfffb;
        m68k_exec(machine.cycles_m68k, observer);
        z80_to(machine.cycles_z80);
    }
    set_line(line);
    PSG_Special_Update();
    YM2612_Special_Update();
}

GensMachineStatus gens_machine_frame(uint64_t packed_input)
{
    return gens_machine_frame_observed(packed_input, 0);
}

GensMachineStatus gens_machine_frame_observed(uint64_t packed_input, const GensM68kObserver *observer)
{
    if (!machine.loaded)
        return GENS_MACHINE_NOT_LOADED;
    machine.error = 0;
    machine.error_site = 0;
    gens_input_apply(packed_input);
    Lag_Frame = 1;
    run_frame(observer);
    return machine.error ? GENS_MACHINE_BACKEND_ERROR : GENS_MACHINE_OK;
}

const char *gens_machine_error_site(void)
{
    return machine.error_site;
}

uint32_t gens_machine_error_address(void)
{
    return machine.error_address & 0xffffffu;
}

int gens_machine_input_read(void)
{
    return Lag_Frame == 0;
}

uint8_t gens_machine_peek(uint32_t address)
{
    return machine.loaded ? gens_genesis_bus_peek(&machine.bus, address) : 0;
}

void gens_machine_poke(uint32_t address, uint8_t value)
{
    if (machine.loaded)
        gens_genesis_bus_poke(&machine.bus, address, value);
}

void gens_machine_view(GensMachineView *view)
{
    GensVdpMachine *vdp = &machine.vdp;
    view->ram68k = memory.ram68k;
    view->rom = memory.rom;
    view->rom_size = (size_t)machine.rom_size;
    view->sram = memory.sram;
    view->ramz80 = machine.z80.ram;
    view->vram = vdp->vram;
    view->cram = vdp->cram;
    view->vsram = vdp->vsram;
    view->vdp_regs = vdp->regs;
    view->screen16 = vdp->screen16x;
    view->screen32 = vdp->md_screen32;
    view->audio_left = Seg_L;
    view->audio_right = Seg_R;
    view->audio_samples = Seg_Length;
    view->visible_lines = machine.visible_lines;
    view->width = (vdp->regs[12] & 1u) ? 320 : 256;
    view->cpu_mode = machine.cpu_mode;
    view->game_mode = machine.game_mode;
}

const struct GensM68k *gens_machine_m68k(void)
{
    return &machine.cpu;
}

const struct GensZ80 *gens_machine_z80(void)
{
    return &machine.z80;
}

void gens_machine_vdp_state(uint32_t out[16])
{
    GensVdpMachine *vdp = &machine.vdp;
    out[0] = (uint32_t)vdp->status;
    out[1] = (uint32_t)vdp->vdp_int;
    out[2] = (uint32_t)vdp->dma_length;
    out[3] = (uint32_t)vdp->dma_type;
    out[4] = (uint32_t)machine.dma_tmp;
    out[5] = (uint32_t)vdp->control[0];
    out[6] = (uint32_t)vdp->control[1];
    out[7] = (uint32_t)vdp->control[2];
    out[8] = (uint32_t)vdp->control[3];
    out[9] = (uint32_t)vdp->control[4];
    out[10] = (uint32_t)vdp->control[5];
    out[11] = (uint32_t)vdp->control[6];
    out[12] = (uint32_t)machine.z80_state;
    out[13] = (uint32_t)machine.visible_lines;
    out[14] = (uint32_t)machine.sram_on;
    out[15] = (uint32_t)machine.sram_write;
}
