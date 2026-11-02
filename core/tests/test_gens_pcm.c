#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gens_pcm.h"

static int32_t left[882], right[882];
static GensPcmOutput output;
static uint64_t hash;

#ifdef GENS_PCM_REFERENCE
int Seg_L[882], Seg_R[882], Seg_Length, Sound_Soften;
unsigned short MastVol;
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#include "pcm_reference.inc"
#undef min
#undef max
void Write_Sound_Stereo_MMX(int *, int *, short *, int);
void Write_Sound_Mono_MMX(int *, int *, short *, int);

static void render(size_t count, const GensPcmOptions *options)
{
    short samples[1764];
    size_t i;
    Seg_Length = (int)count;
    Sound_Soften = options->soften;
    MastVol = options->master_volume;
    memcpy(Seg_L, left, sizeof(left));
    memcpy(Seg_R, right, sizeof(right));
    if (!options->dump)
    {
        for (i = 0; i < count; ++i)
        {
            Seg_L[i] = (Seg_L[i] * MastVol) >> 8;
            Seg_R[i] = (Seg_R[i] * MastVol) >> 8;
        }
    }
    if (count != 0u)
    {
        if (options->dump)
        {
            if (options->stereo) Dump_Sound_Stereo(samples, (int)count);
            else Dump_Sound_Mono(samples, (int)count);
        }
        else if (options->use_mmx && !options->soften)
        {
            if (options->stereo) Write_Sound_Stereo_MMX(Seg_L, Seg_R, samples, (int)count);
            else Write_Sound_Mono_MMX(Seg_L, Seg_R, samples, (int)count);
            __asm__ volatile ("emms");
        }
        else
        {
            if (options->stereo) Write_Sound_Stereo(samples, (int)count);
            else Write_Sound_Mono(samples, (int)count);
        }
    }
    for (i = 0; i < count; ++i)
    {
        output.samples[i * 2u] = samples[options->stereo ? i * 2u : i];
        output.samples[i * 2u + 1u] = samples[options->stereo ? i * 2u + 1u : i];
    }
    output.frames = count;
    memcpy(left, Seg_L, sizeof(left));
    memcpy(right, Seg_R, sizeof(right));
}
#else
static void render(size_t count, const GensPcmOptions *options)
{
    assert(gens_pcm_render(&output, left, right, count, options));
#ifdef GENS_PCM_NEGATIVE_CONTROL
    if (count != 0u)
        output.samples[0] ^= 1;
#endif
}

static void unit_cases(void)
{
    GensPcmOptions options = {256, 1, 0, 0, 0};
    output.frames = 123;
    left[0] = 45;
    assert(!gens_pcm_render(&output, left, right, 883, &options));
    assert(!gens_pcm_render(&output, left, left, 1, &options));
    assert(!gens_pcm_render(&output, left, left + 1, 2, &options));
    assert(!gens_pcm_render(&output, (int32_t *)(void *)output.samples, right, 1, &options));
    options.stereo = 2;
    assert(!gens_pcm_render(&output, left, right, 1, &options));
    assert(output.frames == 123 && left[0] == 45);
    options.stereo = 1;
    left[0] = -32769; right[0] = 32768;
    assert(gens_pcm_render(&output, left, right, 1, &options));
    assert(output.samples[0] == -32768 && output.samples[1] == 32767 && left[0] == 0 && right[0] == 0);
    options.use_mmx = 1;
    left[0] = -32769; right[0] = 32768;
    assert(gens_pcm_render(&output, left, right, 1, &options));
    assert(output.samples[0] == -32767 && output.samples[1] == 32767);
    options.stereo = 0;
    left[0] = -65536; right[0] = 0;
    assert(gens_pcm_render(&output, left, right, 1, &options));
    assert(output.samples[0] == -32767 && output.samples[1] == -32767);
    left[0] = -65535; right[0] = 0;
    assert(gens_pcm_render(&output, left, right, 1, &options));
    assert(output.samples[0] == -32768);
    left[0] = right[0] = left[1] = right[1] = 1;
    assert(gens_pcm_render(&output, left, right, 2, &options));
    assert(output.samples[0] == 0 && output.samples[2] == 0);
    options.use_mmx = 0;
    left[0] = right[0] = left[1] = right[1] = 1;
    assert(gens_pcm_render(&output, left, right, 2, &options));
    assert(output.samples[0] == 1 && output.samples[2] == 1);
    options.stereo = 1;
    options.soften = 1;
    left[0] = 0; left[1] = 90; left[2] = 0;
    assert(gens_pcm_render(&output, left, right, 3, &options));
    assert(output.samples[0] == 20 && output.samples[2] == 30 && output.samples[4] == 20);
    options.dump = 1;
    options.master_volume = 0;
    left[0] = 0; left[1] = 90; left[2] = 0;
    assert(gens_pcm_render(&output, left, right, 3, &options));
    assert(output.samples[0] == 0 && output.samples[2] == 90 && left[1] == 90);
    options.dump = options.soften = 0;
    options.master_volume = 256;
    left[0] = INT32_MAX; right[0] = INT32_MIN;
    assert(gens_pcm_render(&output, left, right, 1, &options));
    assert(output.samples[0] == -1 && output.samples[1] == 0);
    options.dump = 1;
    left[881] = 321; right[881] = -123;
    assert(gens_pcm_render(&output, left, right, 882, &options));
    assert(output.frames == 882 && output.samples[1762] == 321 &&
           output.samples[1763] == -123 && left[881] == 321 && right[881] == -123);
    left[0] = 456;
    assert(gens_pcm_render(&output, left, right, 0, &options));
    assert(output.frames == 0 && left[0] == 456);
}
#endif

static void record(uint32_t value)
{
    unsigned int shift;
    for (shift = 0; shift < 32; shift += 8)
    {
        hash ^= (value >> shift) & 0xffu;
        hash *= UINT64_C(1099511628211);
    }
}

int main(void)
{
    static const size_t counts[] = {0, 1, 2, 3, 7, 184, 220, 368, 441, 735, 882};
    static const uint16_t volumes[] = {0, 1, 128, 255, 256, 257, 65535};
    static const int32_t edges[] = {
        INT32_MIN, -65536, -65535, -32769, -32768, -32767, -3, -2, -1,
        0, 1, 2, 3, 32766, 32767, 32768, 65535, 65536, INT32_MAX
    };
    unsigned int flags, c, v, i;
#ifndef GENS_PCM_REFERENCE
    unit_cases();
#endif
    for (flags = 0; flags < 16; ++flags)
    {
        GensPcmOptions options = {0, (int)(flags & 1u), (int)(flags >> 1 & 1u),
                                 (int)(flags >> 2 & 1u), (int)(flags >> 3 & 1u)};
        uint32_t rng = UINT32_C(0xa0d10);
        hash = UINT64_C(14695981039346656037);
        for (v = 0; v < sizeof(volumes) / sizeof(volumes[0]); ++v)
        {
            options.master_volume = volumes[v];
            for (c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c)
            {
                for (i = 0; i < 882; ++i)
                {
                    if (i < sizeof(edges) / sizeof(edges[0]))
                    {
                        left[i] = edges[i];
                        right[i] = edges[sizeof(edges) / sizeof(edges[0]) - i - 1u];
                    }
                    else
                    {
                        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                        memcpy(&left[i], &rng, sizeof(rng));
                        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                        memcpy(&right[i], &rng, sizeof(rng));
                    }
                }
                render(counts[c], &options);
                assert(output.frames == counts[c]);
                record(options.master_volume);
                record((uint32_t)output.frames);
                for (i = 0; i < output.frames * 2u; ++i)
                    record((uint16_t)output.samples[i]);
                for (i = 0; i < 882; ++i)
                {
                    record((uint32_t)left[i]);
                    record((uint32_t)right[i]);
                }
            }
        }
        printf("pcm %u %016" PRIx64 "\n", flags, hash);
    }
    return 0;
}
