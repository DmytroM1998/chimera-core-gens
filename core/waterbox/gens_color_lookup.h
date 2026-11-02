#ifndef CHIMERA_GENS_COLOR_LOOKUP_H
#define CHIMERA_GENS_COLOR_LOOKUP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensColorProfile {
    int red_max, green_max, blue_max;
    int brightness, contrast;
    int mode555, greyscale, invert, pink;
} GensColorProfile;

typedef struct GensColorLookup {
    uint16_t *colors16;
    uint32_t *colors32;
    size_t count16, count32;
} GensColorLookup;

int gens_color_profile_valid(const GensColorProfile *);
int gens_color_lookup_generate(GensColorLookup *, const GensColorProfile *);

#ifdef __cplusplus
}
#endif

#endif
