/* run-native.c - the native build of the core, driven through the same exports Chimera calls.
 *
 * The work directory holds what the sandbox would mount: the cartridge (named in "slots", or
 * "rom") and "settings"; for the tool, the request "tool", "movie" and "cartridge".
 *
 * usage:
 *   run-native <dir> tool
 *       runs RunTool, which writes "project", and prints its answer
 *   run-native <dir> play <frames> <input-log|-> <trace> [last-frame.ppm]
 *       plays a Chimera input log ([Input] ... [/Input], or bare "|...|" lines) through
 *       FrameAdvance and writes the machine trace of tests/gens_headless_trace.h
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif

#include "gens_headless_machine_trace.h"

extern int Init(void);
extern const char *GetLoadError(void);
extern void FrameAdvance(uint64_t packed);
extern int16_t *GetAudio(void);
extern int GetAudioSampleCount(void);
extern int IsButtonActive(int32_t index);
extern int InputWasRead(void);
extern const char *RunTool(void);
extern uint32_t *GetVideoBgra(void);
extern int GetVideoWidth(void);
extern int GetVideoHeight(void);

/* The entry's columns in order: Power and Reset, then 12 per active player. */
static uint64_t parse_entry(const char *line)
{
    uint64_t packed = 0;
    int bit = 0;
    const char *c;
    for (c = line; *c && *c != '\n' && *c != '\r'; ++c)
    {
        if (*c == '|')
            continue;
        while (bit < 38 && !IsButtonActive(bit))
            ++bit;
        if (bit >= 38)
            break;
        if (*c != '.' && *c != ' ')
            packed |= UINT64_C(1) << bit;
        ++bit;
    }
    return packed;
}

static int write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    const uint32_t *video = GetVideoBgra();
    int w = GetVideoWidth(), h = GetVideoHeight(), i;
    if (f == 0)
        return 0;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (i = 0; i < w * h; ++i)
    {
        uint8_t rgb[3] = {(uint8_t)(video[i] >> 16), (uint8_t)(video[i] >> 8), (uint8_t)video[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    FILE *log = 0, *out;
    char line[512];
    unsigned int frames, frame, lag_frames = 0;

    if (argc < 3 || chdir(argv[1]) != 0)
    {
        fprintf(stderr, "usage: run-native <dir> tool | play <frames> <log|-> <trace> [ppm]\n");
        return 2;
    }
    if (strcmp(argv[2], "tool") == 0)
    {
        fputs(RunTool(), stdout);
        fputc('\n', stdout);
        return 0;
    }
    if (strcmp(argv[2], "play") != 0 || argc < 6)
        return 2;
    frames = (unsigned int)strtoul(argv[3], 0, 0);
    if (!Init())
    {
        fprintf(stderr, "Init failed: %s\n", GetLoadError());
        return 3;
    }
    if (strcmp(argv[4], "-") != 0 && (log = fopen(argv[4], "r")) == 0)
        return 2;
    out = fopen(argv[5], "w");
    if (out == 0)
        return 2;
    gens_trace_no_audio = 1;
    for (frame = 0; frame < frames; ++frame)
    {
        uint64_t packed = 0;
        while (log != 0 && fgets(line, sizeof(line), log) != 0)
        {
            if (line[0] == '|')
            {
                packed = parse_entry(line);
                break;
            }
        }
        FrameAdvance(packed);
        if (!InputWasRead())
            ++lag_frames;
        trace_frame(out, frame);
        gens_trace_pcm(out, frame, GetAudio(), (size_t)GetAudioSampleCount());
    }
    fclose(out);
    fprintf(stderr, "lag frames: %u of %u\n", lag_frames, frames);
    if (argc > 6)
        write_ppm(argv[6]);
    return 0;
}
