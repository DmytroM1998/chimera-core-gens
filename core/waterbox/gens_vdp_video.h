#ifndef CHIMERA_GENS_VDP_VIDEO_H
#define CHIMERA_GENS_VDP_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GensVdpVideo {
    const uint16_t *source;
    size_t source_count;
    uint16_t *screen16;
    uint32_t *screen32;
    size_t screen16_count, screen32_count;
    const uint16_t *lookup16;
    const uint32_t *lookup32;
    size_t lookup16_count, lookup32_count;
    const uint32_t *row_offsets;
    size_t row_count;
    const int *visible_lines;
    int *current_line;
    const unsigned int *set4;
} GensVdpVideo;

int gens_vdp_video_rows_init(uint32_t *, size_t);
int gens_vdp_video_valid(const GensVdpVideo *);
int gens_vdp_video_convert(GensVdpVideo *);
int gens_vdp_video_render_screen(GensVdpVideo *, uint8_t *);

#ifdef __cplusplus
}
#endif

#endif
