/* Headless runner for the ported machine. Emits the same trace as
 * reference/gens_headless_reference.c so the two can be compared frame by frame. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gens_machine.h"
#include "gens_sound.h"
#include "gens_m68k_cpu.h"
#include "gens_z80_cpu.h"
#include "gens_headless_trace.h"
#include "gens_headless_machine_trace.h"

uint32_t gens_machine_error_address(void);

#ifdef GENS_HEADLESS_TRACE_YM
/* The runner build compiles ym2612.c with YM2612_Write renamed so writes can be logged. */
int YM2612_Write_C(unsigned char adr, unsigned char data);
int YM2612_Write(unsigned char adr, unsigned char data)
{
    gens_trace_ym_write(adr, data);
    return YM2612_Write_C(adr, data);
}
#endif

int main(int argc, char **argv)
{
    FILE *rom, *out;
    long length;
    uint8_t *data;
    unsigned int frames, frame;
    GensTraceInput input;
    GensRomStatus rom_status;
    GensMachineStatus status;
    GensMachineSettings settings = {
        -1, {0, 1, 2}, 44100, 0, 0, 1,
        {GENS_INPUT_TWO_PADS, 6, 6, 3, 3, 3},
        {255, 255, 255, 100, 100, 0, 0, 0, 0}
    };

    if (argc < 5)
    {
        fprintf(stderr, "usage: %s rom frames input|- trace [last-frame.ppm]\n", argv[0]);
        return 2;
    }
    frames = (unsigned int)strtoul(argv[2], 0, 0);
    if (!gens_trace_read_input(argv[3], frames, &input))
        return 2;
    rom = fopen(argv[1], "rb");
    if (rom == 0)
        return 2;
    fseek(rom, 0, SEEK_END);
    length = ftell(rom);
    fseek(rom, 0, SEEK_SET);
    if (length <= 0)
        return 2;
    data = (uint8_t *)malloc((size_t)length);
    if (data == 0 || fread(data, 1, (size_t)length, rom) != (size_t)length)
        return 2;
    fclose(rom);

    status = gens_machine_load(data, (size_t)length, argv[1], &settings, &rom_status);
    if (status != GENS_MACHINE_OK)
    {
        fprintf(stderr, "load failed: machine %d rom %d\n", (int)status, (int)rom_status);
        return 3;
    }
    out = strcmp(argv[4], "-") == 0 ? stdout : fopen(argv[4], "w");
    if (out == 0)
        return 2;
    for (frame = 0; frame < frames; ++frame)
    {
        if (input.reset[frame] && gens_machine_reset() != GENS_MACHINE_OK)
            return 4;
        status = gens_machine_frame(gens_trace_packed(&input, frame));
        if (getenv("GENS_HEADLESS_DEBUG") != 0)
        {
            const GensM68k *cpu = gens_machine_m68k();
            GensMachineView view;
            uint32_t vdp[16];
            gens_machine_view(&view);
            gens_machine_vdp_state(vdp);
            fprintf(stderr, "frame %u pc=%06x sr=%04x set1=%02x set2=%02x set4=%02x status=%04x z80=%u\n",
                    frame, cpu->pc, cpu->sr, view.vdp_regs[0], view.vdp_regs[1], view.vdp_regs[12],
                    vdp[0] & 0xffffu, vdp[12]);
        }
        if (status != GENS_MACHINE_OK)
        {
            fprintf(stderr, "frame %u failed: %d at %s address %06x\n", frame, (int)status, gens_machine_error_site() ? gens_machine_error_site() : "?", gens_machine_error_address());
            return 5;
        }
#ifdef GENS_MACHINE_NEGATIVE_CONTROL
        if (frame == 100u)
        {
            GensMachineView view;
            gens_machine_view(&view);
            view.ram68k[0x100] ^= 1u;
        }
#endif
        trace_frame(out, frame);
        {
            static GensPcmOutput pcm;
            GensPcmOptions options = {128, 1, 0, 1, 0};
            if (!gens_sound_output(&pcm, &options))
                return 6;
            gens_trace_pcm(out, frame, pcm.samples, pcm.frames);
        }
    }
    if (out != stdout)
        fclose(out);
    if (argc > 5)
    {
        GensMachineView view;
        gens_machine_view(&view);
        gens_trace_ppm(argv[5], view.screen16, (unsigned int)view.width,
                       (unsigned int)view.visible_lines, 0u);
    }
    return 0;
}
