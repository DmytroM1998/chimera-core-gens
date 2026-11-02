/* Shared trace format for the headless original reference and the port runner.
 * Each frame prints one line per state block with a 64-bit FNV-1a digest. */
#ifndef CHIMERA_GENS_HEADLESS_TRACE_H
#define CHIMERA_GENS_HEADLESS_TRACE_H

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Set by the ABI runner, whose FrameAdvance already converted and cleared the mixer planes. */
static int gens_trace_no_audio;

static uint64_t gens_trace_fnv(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void gens_trace_block(FILE *out, unsigned int frame, const char *name,
                             const void *data, size_t size)
{
    fprintf(out, "%u %s %016" PRIx64 "\n", frame, name, gens_trace_fnv(data, size));
}

static void gens_trace_words(FILE *out, unsigned int frame, const char *name,
                             const uint32_t *words, size_t count)
{
    gens_trace_block(out, frame, name, words, count * sizeof(words[0]));
}

/* Input scripts hold one line per frame: "P1 P2" as hexadecimal 12-bit masks in the
 * order Up, Down, Left, Right, A, B, C, Start, X, Y, Z, Mode (bit 0 first).
 * A line may also be "R" for a soft reset before that frame. Missing lines are neutral. */
typedef struct GensTraceInput {
    uint16_t *p1, *p2;
    uint8_t *reset;
    unsigned int frames;
} GensTraceInput;

static int gens_trace_read_input(const char *path, unsigned int frames, GensTraceInput *input)
{
    FILE *file;
    char line[256];
    unsigned int frame = 0;
    input->frames = frames;
    input->p1 = (uint16_t *)calloc(frames + 1u, sizeof(uint16_t));
    input->p2 = (uint16_t *)calloc(frames + 1u, sizeof(uint16_t));
    input->reset = (uint8_t *)calloc(frames + 1u, 1u);
    if (input->p1 == 0 || input->p2 == 0 || input->reset == 0)
        return 0;
    if (path == 0 || strcmp(path, "-") == 0)
        return 1;
    file = fopen(path, "r");
    if (file == 0)
        return 0;
    while (frame < frames && fgets(line, sizeof(line), file) != 0)
    {
        unsigned int a = 0, b = 0;
        if (line[0] == '#')
            continue;
        if (line[0] == 'R')
            input->reset[frame] = 1;
        else if (sscanf(line, "%x %x", &a, &b) < 1)
            a = b = 0;
        input->p1[frame] = (uint16_t)(a & 0xfffu);
        input->p2[frame] = (uint16_t)(b & 0xfffu);
        ++frame;
    }
    fclose(file);
    return 1;
}

/* Packs the masks into the provisional Chimera wire used by gens_input_apply. */
static uint64_t gens_trace_packed(const GensTraceInput *input, unsigned int frame)
{
    return (uint64_t)input->p1[frame] << 2 | (uint64_t)input->p2[frame] << 14;
}

/* With GENS_HEADLESS_AUDIO_FRAME=n, prints frame n's samples to stderr. */
static void gens_trace_audio_dump(unsigned int frame, const int *left, const int *right, int count)
{
    const char *selected = getenv("GENS_HEADLESS_AUDIO_FRAME");
    int i;
    if (selected == 0 || (unsigned int)strtoul(selected, 0, 0) != frame)
        return;
    for (i = 0; i < count; ++i)
        fprintf(stderr, "%d %d %d\n", i, left[i], right[i]);
}

/* Writes the visible area of Screen_16X as PPM. Pixels are CRAM colors (0ahsbbb0ggg0rrr0)
 * before Gens' palette lookup. Shadow and highlight bits are ignored. */
static int gens_trace_ppm(const char *path, const uint16_t *screen16, unsigned int width,
                          unsigned int lines, unsigned int begin)
{
    FILE *file = fopen(path, "wb");
    unsigned int x, y;
    if (file == 0)
        return 0;
    fprintf(file, "P6\n%u %u\n255\n", width, lines);
    for (y = 0; y < lines; ++y)
        for (x = 0; x < width; ++x)
        {
            uint16_t pixel = screen16[y * 336u + 8u + begin + x]; /* begin is 0: both widths start at column 8 */
            uint8_t rgb[3];
            rgb[0] = (uint8_t)((pixel >> 1 & 7u) * 255u / 7u);
            rgb[1] = (uint8_t)((pixel >> 5 & 7u) * 255u / 7u);
            rgb[2] = (uint8_t)((pixel >> 9 & 7u) * 255u / 7u);
            fwrite(rgb, 1, 3, file);
        }
    fclose(file);
    return 1;
}

/* YM2612 register writes of the current frame, hashed into the trace. */
static uint64_t gens_trace_ym_hash = UINT64_C(14695981039346656037);
static unsigned int gens_trace_ym_count, gens_trace_frame_now;

static void gens_trace_ym_write(unsigned int port, unsigned int data)
{
    const char *selected = getenv("GENS_HEADLESS_YM_FRAME");
    gens_trace_ym_hash = (gens_trace_ym_hash ^ (port & 3u)) * UINT64_C(1099511628211);
    gens_trace_ym_hash = (gens_trace_ym_hash ^ (data & 255u)) * UINT64_C(1099511628211);
    ++gens_trace_ym_count;
    if (selected != 0 && (unsigned int)strtoul(selected, 0, 0) == gens_trace_frame_now)
        fprintf(stderr, "ym %u %02x\n", port & 3u, data & 255u);
}

static void gens_trace_ym_flush(FILE *out, unsigned int frame)
{
    if (!gens_trace_no_audio)
        fprintf(out, "%u ym %016" PRIx64 "-%u\n", frame, gens_trace_ym_hash, gens_trace_ym_count);
    gens_trace_ym_hash = UINT64_C(14695981039346656037);
    gens_trace_ym_count = 0;
    gens_trace_frame_now = frame + 1u;
}

/* The 16-bit stereo output of the frame, as Gens' sound output step produces it. */
static void gens_trace_pcm(FILE *out, unsigned int frame, const int16_t *samples, size_t frames)
{
    gens_trace_block(out, frame, "pcm", samples, frames * 4u);
}

#endif
