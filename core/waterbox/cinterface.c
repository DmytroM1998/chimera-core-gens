/* Chimera guest ABI for the Gens-Rerecording machine.
 *
 * The emulation is gens_machine.c, a port of the pinned Gens revision that matches the
 * original i386 build frame for frame (docs/MACHINE.md). This file only connects it to
 * Chimera: the cartridge slot, settings, input levels, picture, sound, lag, memory
 * domains, registers, save data and the GMV movie importer.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "gens_gmv.h"
#include "gens_io.h"
#include "gens_machine.h"
#include "gens_m68k_cpu.h"
#include "gens_pcm.h"
#include "gens_sound.h"
#include "gens_z80_cpu.h"

#define VIDEO_MAX_W 320
#define VIDEO_MAX_H 240
#define BUTTON_COUNT 38

static char g_load_error[512];
static int g_inited;
static int g_render = 1;
static int g_team_player;
static uint32_t g_video[VIDEO_MAX_W * VIDEO_MAX_H];
static int g_width = 320, g_height = 224;
static GensPcmOutput g_audio;

/* Chimera's function for each controller read. A callback slot, so it is in no savestate. */
ECL_INVISIBLE static uintptr_t (*g_input_poll)(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                               uintptr_t, uintptr_t);

static void input_polled(void)
{
    if (g_input_poll != 0)
        g_input_poll(0, 0, 0, 0, 0, 0);
}

ECL_EXPORT void SetInputPollCallback(uint64_t fn)
{
    g_input_poll = (uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                  uintptr_t))(uintptr_t)fn;
    gens_io_poll_hook = g_input_poll != 0 ? input_polled : 0;
}

static void set_error(const char *key, const char *argument)
{
    if (argument != 0)
        snprintf(g_load_error, sizeof(g_load_error), "%s\x1f%s", key, argument);
    else
        snprintf(g_load_error, sizeof(g_load_error), "%s", key);
}

ECL_EXPORT const char *GetLoadError(void)
{
    return g_load_error;
}

static int pad_setting(const char *name, int fallback)
{
    char value[16];
    if (!wbx_setting_str(name, value, sizeof(value)))
        return fallback;
    return strcmp(value, "three") == 0 ? 3 : 6;
}

static int country_setting(void)
{
    char value[32];
    if (!wbx_setting_str("region", value, sizeof(value)))
        return GENS_COUNTRY_AUTO;
    if (strcmp(value, "japan_ntsc") == 0) return GENS_COUNTRY_JAPAN_NTSC;
    if (strcmp(value, "usa") == 0) return GENS_COUNTRY_USA_NTSC;
    if (strcmp(value, "europe") == 0) return GENS_COUNTRY_EUROPE_PAL;
    if (strcmp(value, "japan_pal") == 0) return GENS_COUNTRY_JAPAN_PAL;
    return GENS_COUNTRY_AUTO;
}

/* What Gens' configuration file holds, from the project's settings. */
static void read_settings(GensMachineSettings *s)
{
    memset(s, 0, sizeof(*s));
    s->country = country_setting();
    s->country_order[0] = 0;
    s->country_order[1] = 1;
    s->country_order[2] = 2;
    /* Gens' default rate. The YM2612 timers advance per sample, so it is part of the machine. */
    s->sound_rate = 44100;
    s->ym2612_improvement = wbx_setting_bool("ym2612Improvement", 0) ? 1 : 0;
    s->psg_improvement = 0;
    s->sprite_limit = wbx_setting_bool("spriteLimit", 1) ? 1 : 0;
    g_team_player = wbx_setting_bool("teamPlayer", 0) ? 1 : 0;
    if (g_team_player)
    {
        /* Players 1, 2 and 3 are Team Player pads 1, 1B and 1C. Port 2 keeps its own pad. */
        s->input.topology = GENS_INPUT_TEAM_PLAYER;
        s->input.player1_buttons = (unsigned int)pad_setting("p1Pad", 6);
        s->input.player2_buttons = (unsigned int)pad_setting("p1bPad", 3);
        s->input.player3_buttons = (unsigned int)pad_setting("p1cPad", 3);
        s->input.team_port2_buttons = (unsigned int)pad_setting("p2Pad", 6);
        s->input.team_pad4_buttons = 3;
    }
    else
    {
        s->input.topology = GENS_INPUT_TWO_PADS;
        s->input.player1_buttons = (unsigned int)pad_setting("p1Pad", 6);
        s->input.player2_buttons = (unsigned int)pad_setting("p2Pad", 6);
        s->input.player3_buttons = 3;
        s->input.team_port2_buttons = 6;
        s->input.team_pad4_buttons = 3;
    }
    /* Gens' default colour settings. They change the picture only. */
    s->colors = (GensColorProfile){255, 255, 255, 100, 100, 0, 0, 0, 0};
}

static uint8_t *read_file(const char *name, size_t limit, size_t *size)
{
    FILE *f = fopen(name, "rb");
    uint8_t *data;
    long length;
    if (f == 0)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 ||
        (size_t)length > limit)
    {
        fclose(f);
        return 0;
    }
    data = (uint8_t *)malloc(length > 0 ? (size_t)length : 1u);
    if (data == 0 || fread(data, 1, (size_t)length, f) != (size_t)length)
    {
        free(data);
        fclose(f);
        return 0;
    }
    fclose(f);
    *size = (size_t)length;
    return data;
}

static const char *rom_error_key(GensRomStatus status)
{
    switch (status)
    {
    case GENS_ROM_TOO_SMALL: return "error.rom_too_small";
    case GENS_ROM_TOO_LARGE: return "error.rom_too_large";
    case GENS_ROM_INCOMPLETE_SMD: return "error.rom_incomplete_smd";
    case GENS_ROM_UNSUPPORTED_SEGA_CD: return "error.rom_sega_cd";
    case GENS_ROM_UNSUPPORTED_32X: return "error.rom_32x";
    case GENS_ROM_UNSUPPORTED_ARCHIVE: return "error.rom_archive";
    case GENS_ROM_UNSUPPORTED_COUNTRY_HEADER: return "error.rom_country";
    default: return "error.rom_unreadable";
    }
}

ECL_EXPORT int Init(void)
{
    GensMachineSettings settings;
    GensRomStatus rom_status = GENS_ROM_OK;
    GensMachineStatus status;
    char name[256];
    uint8_t *data;
    size_t size = 0;

    g_inited = 0;
    g_load_error[0] = 0;
    read_settings(&settings);
    if (wbx_slot_name("rom", 0, name, sizeof(name)) == 0)
        strcpy(name, "rom");
    data = read_file(name, (size_t)GENS_ROM_MAX_BYTES, &size);
    if (data == 0)
    {
        set_error("error.no_rom", name);
        return 0;
    }
    status = gens_machine_load(data, size, name, &settings, &rom_status);
    free(data);
    if (status == GENS_MACHINE_BAD_ROM)
    {
        set_error(rom_error_key(rom_status), name);
        return 0;
    }
    if (status != GENS_MACHINE_OK)
    {
        set_error("error.not_loaded", name);
        return 0;
    }
    g_inited = 1;
    return 1;
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
    GensMachineView view;
    GensPcmOptions options = {128, 1, 0, 1, 0};
    int y;

    if (!g_inited)
        return;
    /* Held levels, as Chimera's other Genesis core treats them. */
    if (gens_input_power_requested(packed))
        gens_machine_power();
    else if (gens_input_reset_requested(packed))
        gens_machine_reset();
    if (gens_machine_frame(packed) != GENS_MACHINE_OK)
    {
        fprintf(stderr, "gens: the frame failed at %s\n",
                gens_machine_error_site() ? gens_machine_error_site() : "an unknown step");
        abort();
    }

    gens_machine_view(&view);
    g_width = view.width;
    g_height = view.visible_lines;
    if (g_render)
        for (y = 0; y < g_height; ++y)
        {
            const uint32_t *source = view.screen32 + (size_t)y * 336u + 8u;
            uint32_t *target = g_video + (size_t)y * (size_t)g_width;
            int x;
            for (x = 0; x < g_width; ++x)
                target[x] = source[x] | 0xff000000u;
        }
    /* Gens' sound output at its defaults: master volume 128, stereo, MMX conversion. It also
     * clears the mixing buffers for the next frame, as the original does. */
    if (!gens_sound_output(&g_audio, &options))
        g_audio.frames = 0;
}

/* Turbo skips the copy of the picture. Rendering itself sets VDP status bits, so it still
 * runs and the machine is the same either way. */
ECL_EXPORT void SetRenderingEnabled(int on) { g_render = on != 0; }
ECL_EXPORT uint32_t *GetVideoBgra(void) { return g_video; }
ECL_EXPORT int GetVideoWidth(void) { return g_width; }
ECL_EXPORT int GetVideoHeight(void) { return g_height; }
ECL_EXPORT int16_t *GetAudio(void) { return g_audio.samples; }
ECL_EXPORT int GetAudioSampleCount(void) { return (int)g_audio.frames; }

/* Gens runs one emulated frame per 1/60 s (NTSC) or 1/50 s (PAL). */
ECL_EXPORT int GetVsyncNumerator(void)
{
    GensMachineView view;
    gens_machine_view(&view);
    return view.cpu_mode ? 50 : 60;
}
ECL_EXPORT int GetVsyncDenominator(void) { return 1; }

ECL_EXPORT int InputWasRead(void) { return gens_machine_input_read(); }

/* Player 3 exists only with the Team Player. */
ECL_EXPORT int IsButtonActive(int32_t index)
{
    if (index < 0 || index >= BUTTON_COUNT)
        return 0;
    return index < 26 || g_team_player;
}

/* ---- memory domains ---- */
typedef struct Domain {
    const char *name;
    uint8_t *data;
    int64_t size;
    int writable;
} Domain;

static int domain(int index, Domain *out)
{
    GensMachineView view;
    gens_machine_view(&view);
    switch (index)
    {
    case 0: *out = (Domain){"68K RAM", view.ram68k, 0x10000, 1}; return 1;
    case 1: *out = (Domain){"Z80 RAM", view.ramz80, 0x2000, 1}; return 1;
    case 2: *out = (Domain){"MD CART", view.rom, (int64_t)view.rom_size, 0}; return 1;
    case 3: *out = (Domain){"SRAM", view.sram, 0x10000, 1}; return 1;
    case 4: *out = (Domain){"VRAM", view.vram, 0x10000, 0}; return 1;
    case 5: *out = (Domain){"CRAM", view.cram, 0x80, 0}; return 1;
    case 6: *out = (Domain){"VSRAM", view.vsram, 0x50, 0}; return 1;
    default: return 0;
    }
}

ECL_EXPORT int GetMemoryDomainCount(void) { return 7; }
ECL_EXPORT const char *GetMemoryDomainName(int i) { Domain d; return domain(i, &d) ? d.name : 0; }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { Domain d; return domain(i, &d) ? d.data : 0; }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { Domain d; return domain(i, &d) ? d.size : 0; }
ECL_EXPORT int GetMemoryDomainWritable(int i) { Domain d; return domain(i, &d) ? d.writable : 0; }

/* ---- 68000 registers ---- */
static const char *const register_names[20] = {
    "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "PC", "SR", "SP_other", "Z80 PC"
};

ECL_EXPORT int32_t GetRegisterCount(void) { return 20; }
ECL_EXPORT const char *GetRegisterName(int32_t i)
{
    return i >= 0 && i < 20 ? register_names[i] : 0;
}
ECL_EXPORT int64_t GetRegisterValue(int32_t i)
{
    const struct GensM68k *cpu = gens_machine_m68k();
    if (i >= 0 && i < 8) return cpu->dreg[i];
    if (i >= 8 && i < 16) return cpu->areg[i - 8];
    if (i == 16) return cpu->pc;
    if (i == 17) return cpu->sr;
    if (i == 18) return cpu->asp;
    if (i == 19) return gens_z80_get_pc(gens_machine_z80());
    return 0;
}
ECL_EXPORT int32_t GetRegisterBits(int32_t i)
{
    return i == 17 || i == 19 ? 16 : (i >= 0 && i < 20 ? 32 : 0);
}

/* ---- save data: the cartridge's SRAM ----
 * Save_SRAM in save.cpp: nothing when every byte is zero, otherwise the bytes up to the
 * last non-zero one, rounded up to a power of two. */
static int64_t sram_save_size(void)
{
    GensMachineView view;
    int64_t last = 0xffff, size = 1;
    if (!g_inited)
        return 0;
    gens_machine_view(&view);
    while (last >= 0 && view.sram[last] == 0)
        --last;
    if (last < 0)
        return 0;
    while (last + 1 > size)
        size <<= 1;
    return size;
}

ECL_EXPORT int32_t GetSaveDataFileCount(void) { return sram_save_size() > 0 ? 1 : 0; }
ECL_EXPORT const char *GetSaveDataFileName(int32_t i)
{
    return i == 0 && GetSaveDataFileCount() ? "SRAM.srm" : 0;
}
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i)
{
    return i == 0 ? sram_save_size() : 0;
}
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i)
{
    GensMachineView view;
    if (i != 0 || !GetSaveDataFileCount())
        return 0;
    gens_machine_view(&view);
    return view.sram;
}

/* ---- importing a Gens movie (.gmv) ---- */
static char *g_import;
static size_t g_import_size, g_import_used;

static int append(const char *text, size_t length)
{
    if (g_import_used + length + 1u > g_import_size)
    {
        size_t next = g_import_size ? g_import_size * 2u : 65536u;
        char *grown;
        while (next < g_import_used + length + 1u)
            next *= 2u;
        grown = (char *)realloc(g_import, next);
        if (grown == 0)
            return 0;
        g_import = grown;
        g_import_size = next;
    }
    memcpy(g_import + g_import_used, text, length);
    g_import_used += length;
    g_import[g_import_used] = 0;
    return 1;
}

static int append_text(const char *text) { return append(text, strlen(text)); }

static int append_json_string(const char *text)
{
    char escaped[8];
    if (!append_text("\""))
        return 0;
    for (; *text; ++text)
    {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\')
        {
            escaped[0] = '\\';
            escaped[1] = (char)c;
            if (!append(escaped, 2))
                return 0;
        }
        else if (c < 0x20u)
        {
            snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            if (!append_text(escaped))
                return 0;
        }
        else if (!append((const char *)&c, 1))
            return 0;
    }
    return append_text("\"");
}

static void pad_columns(char *out, uint16_t pressed)
{
    static const char mnemonics[12] = {'U', 'D', 'L', 'R', 'A', 'B', 'C', 'S', 'X', 'Y', 'Z', 'M'};
    int i;
    for (i = 0; i < 12; ++i)
        out[i] = (pressed >> i & 1u) ? mnemonics[i] : '.';
}

static const char *import_refusal(GensGmvStatus status)
{
    switch (status)
    {
    case GENS_GMV_OLD_VERSION: return "{\"error\": \"import.gmv_old\"}";
    case GENS_GMV_STATE_REQUIRED: return "{\"error\": \"import.gmv_savestate\"}";
    case GENS_GMV_UNKNOWN_PADS: return "{\"error\": \"import.gmv_pads\"}";
    case GENS_GMV_EMPTY: return "{\"error\": \"import.gmv_empty\"}";
    default: return "{\"error\": \"import.gmv_not_gmv\"}";
    }
}

ECL_EXPORT const char *ImportMovie(void)
{
    GensGmv movie;
    GensGmvStatus status;
    uint8_t *data;
    size_t size = 0;
    char rom[256] = "";
    char line[64], number[64];
    uint32_t frame;

    data = read_file("movie", (size_t)512u * 1024u * 1024u, &size);
    if (data == 0)
        return "{\"error\": \"import.no_movie\"}";
    status = gens_gmv_parse(data, size, &movie);
    if (status != GENS_GMV_OK)
    {
        free(data);
        return import_refusal(status);
    }
    wbx_setting_str("importRom", rom, sizeof(rom));
    if (rom[0] == 0)
    {
        free(data);
        return "{\"error\": \"import.which_game\"}";
    }

    g_import_used = 0;
    if (!append_text("{\"format\": \"Gens Movie (GMV)\", \"settings\": {"))
        goto fail;
    snprintf(number, sizeof(number), "\"p1Pad\": \"%s\", \"p2Pad\": \"%s\", \"teamPlayer\": %s",
             movie.p1_buttons == 3u ? "three" : "six", movie.p2_buttons == 3u ? "three" : "six",
             movie.triple ? "true" : "false");
    if (!append_text(number))
        goto fail;
    /* Gens' PlaySubMovie makes pads 1B and 1C three-button pads for a Team Player movie. */
    if (movie.triple && !append_text(", \"p1bPad\": \"three\", \"p1cPad\": \"three\""))
        goto fail;
    if (!append_text("}, \"firmware\": [], \"files\": [{\"name\": ") || !append_json_string(rom) ||
        !append_text(", \"slot\": \"rom\"}], \"notes\": [\"import.gmv_note\""))
        goto fail;
    if (movie.pal && !append_text(", \"import.gmv_pal\""))
        goto fail;
    if (movie.triple && !append_text(", \"import.gmv_team\""))
        goto fail;
    if (!append_text("], \"input\": \"[Input]\\n"))
        goto fail;
    for (frame = 0; frame < movie.frame_count; ++frame)
    {
        uint16_t p1, p2, p3;
        size_t n = 0;
        gens_gmv_frame(&movie, frame, &p1, &p2, &p3);
        line[n++] = '|';
        line[n++] = '.';
        line[n++] = '.';
        line[n++] = '|';
        pad_columns(line + n, p1);
        n += 12;
        line[n++] = '|';
        pad_columns(line + n, p2);
        n += 12;
        line[n++] = '|';
        if (movie.triple)
        {
            pad_columns(line + n, p3);
            n += 12;
            line[n++] = '|';
        }
        line[n++] = '\\';
        line[n++] = 'n';
        if (!append(line, n))
            goto fail;
    }
    snprintf(number, sizeof(number), "[/Input]\\n\", \"frames\": %u}", movie.frame_count);
    if (!append_text(number))
        goto fail;
    free(data);
    return g_import;
fail:
    free(data);
    return "{\"error\": \"import.too_large\"}";
}
