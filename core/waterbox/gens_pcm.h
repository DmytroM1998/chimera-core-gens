#ifndef CHIMERA_GENS_PCM_H
#define CHIMERA_GENS_PCM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { GENS_PCM_MAX_FRAMES = 882 };

typedef struct GensPcmOptions {
    uint16_t master_volume;
    int stereo, soften, use_mmx, dump;
} GensPcmOptions;

typedef struct GensPcmOutput {
    int16_t samples[GENS_PCM_MAX_FRAMES * 2];
    size_t frames;
} GensPcmOutput;

int gens_pcm_render(GensPcmOutput *output, int32_t *left, int32_t *right,
                     size_t frames, const GensPcmOptions *options);

#ifdef __cplusplus
}
#endif

#endif
