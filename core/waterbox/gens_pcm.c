/* Conversion follows the pinned G_dsound.cpp and Misc.asm output paths.
 * The upstream component terms apply to derived portions.
 */
#include "gens_pcm.h"

#include <string.h>

static uint32_t bits(int32_t value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static int32_t signed_bits(uint32_t value)
{
    int32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static int32_t shift_right(int32_t value, unsigned int shift)
{
    uint32_t result = bits(value) >> shift;
    if (value < 0)
        result |= UINT32_MAX << (32u - shift);
    return signed_bits(result);
}

static int16_t clip(int32_t value, int32_t lower)
{
    if (value < lower)
        return (int16_t)lower;
    if (value > 32767)
        return 32767;
    return (int16_t)value;
}

static int32_t lowpass(const int32_t *buffer, size_t index, size_t count)
{
    size_t next = index + 1u < count ? index + 1u : count - 1u;
    size_t next2 = index + 2u < count ? index + 2u : count - 1u;
    size_t previous = index > 0u ? index - 1u : 0u;
    size_t previous2 = index > 1u ? index - 2u : 0u;
    uint32_t sum = bits(buffer[index]) * 3u + bits(buffer[next]) * 2u +
        bits(buffer[previous]) * 2u + bits(buffer[next2]) + bits(buffer[previous2]);
    return signed_bits(sum) / 9;
}

static int overlaps(const void *a, size_t a_size, const void *b, size_t b_size)
{
    uintptr_t first = (uintptr_t)a, second = (uintptr_t)b;
    if (a_size == 0u || b_size == 0u)
        return 0;
    return first <= second ? second - first < a_size : first - second < b_size;
}

int gens_pcm_render(GensPcmOutput *output, int32_t *left, int32_t *right,
                     size_t frames, const GensPcmOptions *options)
{
    GensPcmOptions selected;
    size_t i, bytes;
    if (output == 0 || left == 0 || right == 0 || options == 0 || frames > GENS_PCM_MAX_FRAMES)
        return 0;
    selected = *options;
    if ((selected.stereo != 0 && selected.stereo != 1) ||
        (selected.soften != 0 && selected.soften != 1) ||
        (selected.use_mmx != 0 && selected.use_mmx != 1) ||
        (selected.dump != 0 && selected.dump != 1))
        return 0;
    bytes = frames * sizeof(left[0]);
    if (overlaps(left, bytes, right, bytes) ||
        overlaps(output, sizeof(*output), left, bytes) ||
        overlaps(output, sizeof(*output), right, bytes))
        return 0;
    if (!selected.dump)
    {
        for (i = 0; i < frames; ++i)
        {
            left[i] = shift_right(signed_bits(bits(left[i]) * selected.master_volume), 8);
            right[i] = shift_right(signed_bits(bits(right[i]) * selected.master_volume), 8);
        }
    }
    for (i = 0; i < frames; ++i)
    {
        int filtered = !selected.dump && selected.soften;
        int mmx = !selected.dump && !selected.soften && selected.use_mmx;
        int odd_first = mmx && (frames & 1u) != 0u && i == 0u;
        int32_t l = filtered ? lowpass(left, i, frames) : left[i];
        int32_t r = filtered ? lowpass(right, i, frames) : right[i];
        if (selected.stereo)
        {
            int32_t lower = odd_first ? -32767 : -32768;
            output->samples[i * 2u] = clip(l, lower);
            output->samples[i * 2u + 1u] = clip(r, lower);
        }
        else
        {
            int16_t mono;
            if (mmx && !odd_first)
                mono = (int16_t)(shift_right(clip(l, -32768), 1) +
                                  shift_right(clip(r, -32768), 1));
            else
            {
                int32_t sum = signed_bits(bits(l) + bits(r));
                if (sum < (odd_first ? -65535 : -65536))
                    mono = odd_first ? -32767 : -32768;
                else if (sum > 65535)
                    mono = 32767;
                else
                    mono = (int16_t)shift_right(sum, 1);
            }
            output->samples[i * 2u] = output->samples[i * 2u + 1u] = mono;
        }
    }
    if (!selected.dump)
    {
        memset(left, 0, bytes);
        memset(right, 0, bytes);
    }
    output->frames = frames;
    return 1;
}
