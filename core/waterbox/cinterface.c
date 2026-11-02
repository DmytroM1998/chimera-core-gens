/* Chimera guest ABI for the Gens-Rerecording machine.
 *
 * The emulation is gens_machine.c, a port of the pinned Gens revision that matches the
 * original i386 build frame for frame (docs/MACHINE.md). This file only connects it to
 * Chimera: the cartridge slot, settings, input levels, picture, sound, lag, memory
 * domains, registers, save data, and the tool that makes a Gens movie (.gmv) into a project.
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

/* ---- the core's own tool: a Gens movie (.gmv) made into a project ---- */
/* Chimera offers it under Tools > Core-Specific Tools (the package's "tools") and calls
 * RunTool instead of Init, with no game. The request is the file "tool":
 *   {"tool":"import_gmv","package":{"sha1":...,"version":...},"options":{},
 *    "inputs":{"movie":"<name>","cartridge":"<name>"},"outputs":{"project":"<name>"}}
 * The movie is mounted as "movie" and the cartridge as "cartridge" (Chimera builds before
 * ToolAssisted-run/chimera d43bb214 mount an empty "rom" in a tool run). The project is written to
 * "project", which goes straight to the file the person chose. It pins this package by the
 * hash and version the request gives, and the cartridge by its SHA-1 and its file's name.
 * The answer is {"done": ...} or {"refused": ...}: a key of the text table, then its
 * arguments, each after 0x1F. */
static char g_tool_answer[1024];

/* {"<what>": "<text>"}, the text escaped for JSON */
static const char *tool_answer(const char *what, const char *text)
{
    char *p = g_tool_answer;
    char *const last = g_tool_answer + sizeof(g_tool_answer) - 8;
    p += snprintf(p, sizeof(g_tool_answer), "{\"%s\": \"", what);
    for (; *text && p < last; ++text)
    {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\')
            p += sprintf(p, "\\%c", c);
        else if (c < 0x20u)
            p += sprintf(p, "\\u%04x", c);
        else
            *p++ = (char)c;
    }
    strcpy(p, "\"}");
    return g_tool_answer;
}

/* One string of one of the request's objects ("inputs", "package"): 1 when it is there. */
static int tool_string(const char *request, const char *object, const char *name, char *out,
                       size_t capacity)
{
    char key[96];
    const char *at, *end;
    int depth = 1, quoted = 0;
    size_t length, n = 0;
    out[0] = 0;
    snprintf(key, sizeof(key), "\"%s\":{", object);
    at = strstr(request, key);
    if (at == 0)
        return 0;
    at += strlen(key);
    for (end = at; *end && depth; ++end)
    {
        if (quoted)
        {
            if (*end == '\\' && end[1])
                ++end;
            else if (*end == '"')
                quoted = 0;
        }
        else if (*end == '"')
            quoted = 1;
        else if (*end == '{')
            ++depth;
        else if (*end == '}')
            --depth;
    }
    snprintf(key, sizeof(key), "\"%s\":\"", name);
    length = strlen(key);
    for (; at + length <= end; ++at)
    {
        if (strncmp(at, key, length) != 0)
            continue;
        for (at += length; *at && *at != '"' && n + 1 < capacity; ++at)
        {
            if (*at == '\\' && at[1])
            {
                ++at;
                /* the escapes a JSON writer uses in a file name: \" \\ \/ and \u00XX */
                if (*at == 'u' && at[1] && at[2] && at[3] && at[4])
                {
                    unsigned int value = 0;
                    sscanf(at + 1, "%4x", &value);
                    out[n++] = (char)value;
                    at += 4;
                    continue;
                }
            }
            out[n++] = *at;
        }
        out[n] = 0;
        return 1;
    }
    return 0;
}

/* SHA-1 (FIPS 180-4) of a file, as upper-case hex: how a project names its files. */
static void sha1_block(uint32_t state[5], const uint8_t block[64])
{
    uint32_t w[80], a, b, c, d, e, f, k, t;
    int i;
    for (i = 0; i < 16; ++i)
        w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
               (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
    for (i = 16; i < 80; ++i)
    {
        t = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = t << 1 | t >> 31;
    }
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    for (i = 0; i < 80; ++i)
    {
        if (i < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        t = (a << 5 | a >> 27) + f + e + k + w[i];
        e = d;
        d = c;
        c = b << 30 | b >> 2;
        b = a;
        a = t;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static int sha1_file(const char *name, char out[41])
{
    uint32_t state[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    uint8_t block[64];
    uint64_t bits = 0;
    size_t got;
    int i;
    FILE *f = fopen(name, "rb");
    if (f == 0)
        return 0;
    while ((got = fread(block, 1, sizeof(block), f)) == sizeof(block))
    {
        sha1_block(state, block);
        bits += 512u;
    }
    fclose(f);
    bits += (uint64_t)got * 8u;
    block[got++] = 0x80u;
    if (got > 56u)
    {
        memset(block + got, 0, 64u - got);
        sha1_block(state, block);
        got = 0;
    }
    memset(block + got, 0, 56u - got);
    for (i = 0; i < 8; ++i)
        block[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_block(state, block);
    for (i = 0; i < 5; ++i)
        snprintf(out + i * 8, 9, "%08X", (unsigned int)state[i]);
    return 1;
}

static void write_json_string(FILE *f, const char *text)
{
    fputc('"', f);
    for (; *text; ++text)
    {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (c < 0x20u)
            fprintf(f, "\\u%04x", c);
        else
            fputc(c, f);
    }
    fputc('"', f);
}

static void pad_columns(char *out, uint16_t pressed)
{
    static const char mnemonics[12] = {'U', 'D', 'L', 'R', 'A', 'B', 'C', 'S', 'X', 'Y', 'Z', 'M'};
    int i;
    for (i = 0; i < 12; ++i)
        out[i] = (pressed >> i & 1u) ? mnemonics[i] : '.';
}

/* The project: the movie's pads as the settings, the cartridge, and one entry a frame. The
 * columns are named, so they read the same whatever controls the package has later. */
static int write_project(FILE *f, const GensGmv *movie, const char *title, const char *rom,
                         const char *rom_sha1, const char *version, const char *sha1)
{
    static const char *const buttons[12] = {"Up", "Down", "Left", "Right", "A", "B",
                                            "C",  "Start", "X", "Y", "Z", "Mode"};
    const int players = movie->triple ? 3 : 2;
    char line[64];
    uint32_t frame;
    int player, i;

    fputs("{\n  \"title\": ", f);
    write_json_string(f, title);
    fputs(",\n  \"core\": {\n    \"name\": \"Gens-Rerecording\",\n    \"version\": ", f);
    write_json_string(f, version);
    fputs(",\n    \"sha1\": ", f);
    write_json_string(f, sha1);
    fprintf(f, "\n  },\n  \"rerecords\": %u,\n  \"files\": [\n    {\n      \"name\": ",
            (unsigned int)movie->rerecords);
    write_json_string(f, rom);
    fprintf(f, ",\n      \"sha1\": \"%s\",\n      \"slot\": \"rom\"\n    }\n  ],\n", rom_sha1);
    fprintf(f, "  \"settings\": {\n    \"p1Pad\": \"%s\",\n    \"p2Pad\": \"%s\",\n"
               "    \"teamPlayer\": %s",
            movie->p1_buttons == 3u ? "three" : "six", movie->p2_buttons == 3u ? "three" : "six",
            movie->triple ? "true" : "false");
    /* Gens' PlaySubMovie makes pads 1B and 1C three-button pads for a Team Player movie. */
    if (movie->triple)
        fputs(",\n    \"p1bPad\": \"three\",\n    \"p1cPad\": \"three\"", f);
    fputs("\n  },\n  \"firmware\": [],\n  \"inputs\": {\n    \"columns\": [[\"Power\", \"Reset\"]", f);
    for (player = 1; player <= players; ++player)
    {
        fputs(", [", f);
        for (i = 0; i < 12; ++i)
            fprintf(f, "%s\"P%d %s\"", i ? ", " : "", player, buttons[i]);
        fputc(']', f);
    }
    fputs("],\n    \"entries\": [", f);
    for (frame = 0; frame < movie->frame_count; ++frame)
    {
        uint16_t pressed[3];
        size_t n = 0;
        gens_gmv_frame(movie, frame, &pressed[0], &pressed[1], &pressed[2]);
        memcpy(line, "|..|", 4);
        n = 4;
        for (player = 0; player < players; ++player)
        {
            pad_columns(line + n, pressed[player]);
            n += 12;
            line[n++] = '|';
        }
        line[n] = 0;
        fputs(frame ? ",\n      \"" : "\n      \"", f);
        fputs(line, f);
        fputc('"', f);
    }
    fputs(movie->frame_count ? "\n    ]\n  },\n" : "]\n  },\n", f);
    fputs("  \"markers\": [],\n  \"branches\": []\n}\n", f);
    return !ferror(f);
}

static const char *import_refusal(GensGmvStatus status)
{
    switch (status)
    {
    case GENS_GMV_OLD_VERSION: return tool_answer("refused", "import.gmv_old");
    case GENS_GMV_STATE_REQUIRED: return tool_answer("refused", "import.gmv_savestate");
    case GENS_GMV_UNKNOWN_PADS: return tool_answer("refused", "import.gmv_pads");
    case GENS_GMV_EMPTY: return tool_answer("refused", "import.gmv_empty");
    default: return tool_answer("refused", "import.gmv_not_gmv");
    }
}

ECL_EXPORT const char *RunTool(void)
{
    static char request[16384];
    GensGmv movie;
    GensGmvStatus status;
    uint8_t *data;
    size_t size = 0;
    char movie_name[256], rom[256], title[256], rom_sha1[41], version[128], sha1[64];
    char done[320];
    char *dot;
    FILE *f = fopen("tool", "rb");
    int wrote;

    size = f != 0 ? fread(request, 1, sizeof(request) - 1u, f) : 0u;
    if (f != 0)
        fclose(f);
    request[size] = 0;
    if (strstr(request, "\"tool\":\"import_gmv\"") == 0)
        return tool_answer("refused", "import.unknown_tool");

    data = read_file("movie", (size_t)512u * 1024u * 1024u, &size);
    if (data == 0)
        return tool_answer("refused", "import.no_movie");
    status = gens_gmv_parse(data, size, &movie);
    if (status != GENS_GMV_OK)
    {
        free(data);
        return import_refusal(status);
    }
    if (!tool_string(request, "inputs", "cartridge", rom, sizeof(rom)) || rom[0] == 0)
    {
        free(data);
        return tool_answer("refused", "import.which_game");
    }
    if (!sha1_file("cartridge", rom_sha1))
    {
        free(data);
        snprintf(done, sizeof(done), "error.no_rom\x1f%s", rom);
        return tool_answer("refused", done);
    }

    /* the project is named after the movie */
    tool_string(request, "inputs", "movie", movie_name, sizeof(movie_name));
    snprintf(title, sizeof(title), "%s", movie_name[0] ? movie_name : "movie");
    dot = strrchr(title, '.');
    if (dot != 0 && dot != title)
        *dot = 0;
    tool_string(request, "package", "version", version, sizeof(version));
    tool_string(request, "package", "sha1", sha1, sizeof(sha1));

    f = fopen("project", "wb");
    wrote = f != 0 && write_project(f, &movie, title, rom, rom_sha1, version, sha1);
    if (f != 0 && fclose(f) != 0)
        wrote = 0;
    free(data);
    if (!wrote)
        return tool_answer("refused", "import.not_written");
    snprintf(done, sizeof(done), "%s\x1f%u\x1f%u", movie.pal ? "import.done_pal" : "import.done",
             (unsigned int)movie.frame_count, (unsigned int)movie.rerecords);
    return tool_answer("done", done);
}
