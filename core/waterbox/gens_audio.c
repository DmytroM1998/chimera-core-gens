/* Timing follows the pinned Gens-Rerecording Gens.cpp, psg.c, and ym2612.c.
 * The upstream component terms apply to derived portions.
 */
#include "gens_audio.h"

static int valid_sample_count(int line_count, int sample_count)
{
    if (line_count == 262)
        return sample_count == 184 || sample_count == 368 || sample_count == 735;
    if (line_count == 312)
        return sample_count == 220 || sample_count == 441 || sample_count == 882;
    return 0;
}

GensAudioStatus gens_audio_next_offset(int current_line, int line_count,
                                       int sample_count, unsigned int table[][2],
                                       size_t table_count, unsigned int *offset)
{
    unsigned int index, expected_start, expected_end;
    if (table == 0 || offset == 0)
        return GENS_AUDIO_INVALID_ARGUMENT;
    if (!valid_sample_count(line_count, sample_count) ||
        table_count < (size_t)line_count || current_line < -1 || current_line > line_count)
        return GENS_AUDIO_INVALID_TIMING;

    index = (unsigned int)(current_line + 1);
    if (index >= (unsigned int)line_count)
        index = (unsigned int)line_count - 1u;
    expected_start = (unsigned int)(sample_count * (int)index / line_count);
    expected_end = (unsigned int)(sample_count * ((int)index + 1) / line_count);
    if (table[index][0] != expected_start ||
        table[index][1] != expected_end - expected_start)
        return GENS_AUDIO_INVALID_TIMING;

    *offset = current_line + 1 >= line_count ?
              (unsigned int)sample_count : expected_start;
    return GENS_AUDIO_OK;
}
