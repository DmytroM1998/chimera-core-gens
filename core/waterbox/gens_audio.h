#ifndef CHIMERA_GENS_AUDIO_H
#define CHIMERA_GENS_AUDIO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GensAudioStatus {
    GENS_AUDIO_OK = 0,
    GENS_AUDIO_INVALID_ARGUMENT,
    GENS_AUDIO_INVALID_TIMING
} GensAudioStatus;

GensAudioStatus gens_audio_next_offset(int current_line, int line_count,
                                       int sample_count, unsigned int table[][2],
                                       size_t table_count, unsigned int *offset);

#ifdef __cplusplus
}
#endif

#endif
