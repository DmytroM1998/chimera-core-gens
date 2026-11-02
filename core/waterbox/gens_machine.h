#ifndef CHIMERA_GENS_MACHINE_H
#define CHIMERA_GENS_MACHINE_H

#include <stddef.h>
#include <stdint.h>

#include "gens_color_lookup.h"
#include "gens_input.h"
#include "gens_rom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Settings that Gens takes from its configuration file. */
typedef struct GensMachineSettings {
    int country;                  /* -1 automatic, otherwise enum GensCountry. */
    uint8_t country_order[3];     /* Gens' "Prefered Country" order. Default 0, 1, 2. */
    int sound_rate;               /* 11025, 22050 or 44100. */
    int ym2612_improvement;       /* "YM2612 Improvement", default 0. */
    int psg_improvement;          /* PSG_Improv, default 0. */
    int sprite_limit;             /* "Sprite limit" (Sprite_Over), default 1. */
    GensInputConfig input;
    GensColorProfile colors;
} GensMachineSettings;

typedef enum GensMachineStatus {
    GENS_MACHINE_OK = 0,
    GENS_MACHINE_INVALID_ARGUMENT,
    GENS_MACHINE_BAD_ROM,
    GENS_MACHINE_INIT_FAILED,
    GENS_MACHINE_NOT_LOADED,
    GENS_MACHINE_BACKEND_ERROR
} GensMachineStatus;

/* Read-only view of machine state for traces, memory domains and video/audio export. */
typedef struct GensMachineView {
    uint8_t *ram68k;              /* 64 KiB, word-swapped like Gens' Ram_68k. */
    uint8_t *rom;                 /* Word-swapped ROM buffer. */
    size_t rom_size;
    uint8_t *sram;                /* 64 KiB. */
    uint8_t *ramz80;              /* 8 KiB. */
    uint8_t *vram, *cram, *vsram; /* 64 KiB, 256 and 256 bytes in Gens' layout. */
    const uint32_t *vdp_regs;     /* 26 VDP register slots. */
    const uint16_t *screen16;     /* Screen_16X, 336 x 240 CRAM-format pixels. */
    const uint32_t *screen32;     /* MD_Screen32, 336 x 240 after palette lookup. */
    const int *audio_left, *audio_right;
    int audio_samples;            /* Seg_Length. */
    int visible_lines, width, cpu_mode, game_mode;
} GensMachineView;

/* Loads a cartridge image and performs Gens' Init_Genesis. The image is copied. */
GensMachineStatus gens_machine_load(const uint8_t *image, size_t size, const char *name,
                                    const GensMachineSettings *settings,
                                    GensRomStatus *rom_status);
/* Init_Genesis again from the loaded image, as a power cycle. */
GensMachineStatus gens_machine_power(void);
/* Gens' Reset_Genesis. */
GensMachineStatus gens_machine_reset(void);
/* Applies the packed input levels, then runs Do_Genesis_Frame(false). */
GensMachineStatus gens_machine_frame(uint64_t packed_input);
/* Names the first operation that failed during the last frame, or null. */
const char *gens_machine_error_site(void);
/* True when the game read a controller during the last frame. */
int gens_machine_input_read(void);
void gens_machine_view(GensMachineView *view);

/* Internal state needed by tests that compare CPU registers. */
struct GensM68k;
struct GensZ80;
const struct GensM68k *gens_machine_m68k(void);
const struct GensZ80 *gens_machine_z80(void);
void gens_machine_vdp_state(uint32_t out[16]);

#ifdef __cplusplus
}
#endif

#endif
