/* Movie layout follows Gens-rr movie.cpp at the pinned revision.
 * The upstream component terms apply to derived portions.
 */
#include "gens_gmv.h"

#include <string.h>

GensGmvStatus gens_gmv_parse(const uint8_t *data, size_t size, GensGmv *movie)
{
    unsigned int flags = 0;
    if (data == 0 || movie == 0 || size < 64u || memcmp(data, "Gens Movie TEST", 15) != 0)
        return GENS_GMV_NOT_GMV;
    memset(movie, 0, sizeof(*movie));
    movie->version = (unsigned int)data[15] - '0';
    if (data[15] < '9')
        return GENS_GMV_OLD_VERSION;
    movie->rerecords = (uint32_t)data[16] | (uint32_t)data[17] << 8 |
        (uint32_t)data[18] << 16 | (uint32_t)data[19] << 24;
    movie->p1_buttons = (unsigned int)data[20] - '0';
    movie->p2_buttons = (unsigned int)data[21] - '0';
    if (data[15] >= 'A')
        flags = data[22];
    movie->pal = (flags & 0x80u) != 0u;
    movie->triple = (flags & 0x20u) != 0u;
    memcpy(movie->note, data + 24, 40);
    movie->note[40] = 0;
    movie->frames = data + 64;
    movie->frame_count = (uint32_t)((size - 64u) / 3u);
    if ((flags & 0x40u) != 0u)
        return GENS_GMV_STATE_REQUIRED;
    if ((movie->p1_buttons != 3u && movie->p1_buttons != 6u) ||
        (movie->p2_buttons != 3u && movie->p2_buttons != 6u))
        return GENS_GMV_UNKNOWN_PADS;
    if (movie->frame_count == 0u)
        return GENS_GMV_EMPTY;
    return GENS_GMV_OK;
}

void gens_gmv_frame(const GensGmv *movie, uint32_t frame, uint16_t *p1, uint16_t *p2,
                    uint16_t *p3)
{
    const uint8_t *pad = movie->frames + (size_t)frame * 3u;
    /* A zero bit is pressed. */
    unsigned int a = (unsigned int)~pad[0] & 0xffu;
    unsigned int b = (unsigned int)~pad[1] & 0xffu;
    unsigned int c = (unsigned int)~pad[2] & 0xffu;
    if (movie->triple)
    {
        *p1 = (uint16_t)a;
        *p2 = (uint16_t)b;
        *p3 = (uint16_t)c;
    }
    else
    {
        *p1 = (uint16_t)(a | (c & 0x0fu) << 8);
        *p2 = (uint16_t)(b | (c >> 4) << 8);
        *p3 = 0;
    }
}