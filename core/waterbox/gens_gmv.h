#ifndef CHIMERA_GENS_GMV_H
#define CHIMERA_GENS_GMV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GensGmvStatus {
    GENS_GMV_OK = 0,
    GENS_GMV_NOT_GMV,
    GENS_GMV_OLD_VERSION,
    GENS_GMV_STATE_REQUIRED,
    GENS_GMV_UNKNOWN_PADS,
    GENS_GMV_EMPTY
} GensGmvStatus;

/* A Gens movie as movie.cpp reads it (GetMovieInfo and the playback branch). */
typedef struct GensGmv {
    unsigned int version;      /* 9 for TEST9, 17 for TESTA (the character minus '0'). */
    unsigned int p1_buttons;   /* 3 or 6, header byte 20. */
    unsigned int p2_buttons;   /* 3 or 6, header byte 21. */
    int pal;                   /* TESTA flag 0x80: recorded at 50 Hz. Information only. */
    int triple;                /* TESTA flag 0x20: Team Player with pads 1, 1B and 1C. */
    uint32_t rerecords;
    char note[41];
    const uint8_t *frames;     /* Three bytes per frame. */
    uint32_t frame_count;      /* (size - 64) / 3, as Gens computes LastFrame. */
} GensGmv;

GensGmvStatus gens_gmv_parse(const uint8_t *data, size_t size, GensGmv *movie);

/* The pressed 12-bit masks of one frame, in the order Up, Down, Left, Right, A, B, C, Start,
 * X, Y, Z, Mode (bit 0 first). In Team Player movies p1, p2 and p3 are pads 1, 1B and 1C and
 * carry no X, Y, Z or Mode. Otherwise p3 is zero. */
void gens_gmv_frame(const GensGmv *movie, uint32_t frame, uint16_t *p1, uint16_t *p2,
                    uint16_t *p3);

#ifdef __cplusplus
}
#endif

#endif