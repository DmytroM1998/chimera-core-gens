/* Chip initialization and dispatch follow the pinned Gens sound paths.
 * The upstream component terms apply to derived portions.
 */
#include "gens_sound.h"

#include "psg.h"
#include "ym2612.h"

extern int Seg_L[882], Seg_R[882], Seg_Length;

int gens_sound_init(int pal, int rate, int ym_interpolation, int psg_improvement)
{
    int clock;
    if ((pal != 0 && pal != 1) ||
        (rate != 11025 && rate != 22050 && rate != 44100) ||
        (ym_interpolation != 0 && ym_interpolation != 1) ||
        (psg_improvement != 0 && psg_improvement != 1))
        return 0;
    clock = pal ? 53203424 : 53693175;
    YM2612_Improv = ym_interpolation;
    PSG_Improv = psg_improvement;
    if (YM2612_Init(clock / 7, rate, ym_interpolation) != 0)
        return 0;
    PSG_Init(clock / 15, rate);
    return 1;
}

int gens_sound_z80_read(void *opaque, GensM68kBusState *state,
                        enum GensZ80Device device, uint32_t address, uint16_t *value)
{
    (void)opaque;
    if (state == 0 || value == 0 || device != GENS_Z80_YM2612 || address > 3u)
        return 0;
    *value = (uint16_t)YM2612_Read();
    return 1;
}

int gens_sound_z80_write(void *opaque, GensM68kBusState *state,
                         enum GensZ80Device device, uint32_t address, uint8_t value)
{
    (void)opaque;
    if (state == 0)
        return 0;
    if (device == GENS_Z80_YM2612 && address <= 3u)
    {
        /* Gens' return values 1 and 2 mean ignored or redundant writes, not errors. */
        YM2612_Write((unsigned char)address, value);
        return 1;
    }
    if (device == GENS_Z80_PSG && address == UINT32_C(0x7f11))
    {
        PSG_Write(value);
        return 1;
    }
    return 0;
}

int gens_sound_genesis_write(void *opaque, GensM68kBusState *state,
                             enum GensGenesisDevice device, uint32_t address,
                             unsigned int size, uint16_t value)
{
    (void)opaque;
    if (state == 0 || device != GENS_GENESIS_PSG ||
        address != UINT32_C(0xc00011) || size != 1u)
        return 0;
    PSG_Write((uint8_t)value);
    return 1;
}

int gens_sound_reset_ym2612(void *opaque, GensM68kBusState *state)
{
    (void)opaque;
    if (state == 0)
        return 0;
    return YM2612_Reset() == 0;
}

int gens_sound_output(GensPcmOutput *output, const GensPcmOptions *options)
{
    if (Seg_Length < 0 || Seg_Length > GENS_PCM_MAX_FRAMES)
        return 0;
    return gens_pcm_render(output, Seg_L, Seg_R, (size_t)Seg_Length, options);
}

void gens_frame_backend_psg_finish(void)
{
    PSG_Special_Update();
}

void gens_frame_backend_ym_finish(void)
{
    YM2612_Special_Update();
}
