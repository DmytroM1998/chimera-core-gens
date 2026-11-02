/* Pixel lookup and marker writes follow the pinned Render_MD_Screen.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_video.h"

_Static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4, "Gens video global widths");

int gens_vdp_video_rows_init(uint32_t *rows, size_t count)
{
    unsigned int i;
    if (rows == 0 || count < 240u)
        return 0;
    for (i = 0; i < 240u; ++i)
        rows[i] = i * 336u;
    return 1;
}

int gens_vdp_video_valid(const GensVdpVideo *video)
{
    return video != 0 && video->source != 0 && video->source_count >= 80640u &&
        video->screen16 != 0 && video->screen16_count >= 80640u &&
        video->screen32 != 0 && video->screen32_count >= 80640u &&
        video->lookup16 != 0 && video->lookup16_count >= 32768u &&
        video->lookup32 != 0 && video->lookup32_count >= 32768u &&
        video->row_offsets != 0 && video->row_count >= 240u &&
        video->visible_lines != 0 && video->current_line != 0 &&
        video->visible_lines != video->current_line && *video->visible_lines <= 240 &&
        video->set4 != 0;
}

int gens_vdp_video_convert(GensVdpVideo *video)
{
    unsigned int lines, row, column, marker;
    uint32_t offset, pixel;
    uint16_t index;
    if (!gens_vdp_video_valid(video))
        return 0;
    lines = *video->visible_lines > 0 ? (unsigned int)*video->visible_lines : 0u;
    for (row = 0; row < lines; ++row)
        if (video->row_offsets[row] > 80640u - 336u)
            return 0;

    *video->current_line = 0;
    for (row = 0; row < lines; ++row)
    {
        *video->current_line = (int)row;
        offset = video->row_offsets[row];
        if (offset > 80640u - 336u)
            return 0;
        for (column = 8; column < 336u; ++column)
        {
            pixel = offset + column;
            index = video->source[pixel];
            /* Columns Render_Line did not resolve (past H32's 256 pixels) can hold raw values of
             * 0x8000 and above. The original then reads past Palette and Palette32. Those pixels
             * are outside the active display, so the index is masked instead. */
            index &= 0x7fffu;
            video->screen32[pixel] = video->lookup32[index];
            video->screen16[pixel] = video->lookup16[index];
        }
    }

    marker = (*video->set4 & 1u) != 0u ? 328u : 264u;
    *video->current_line = 0;
    for (row = 0; row < lines; ++row)
    {
        *video->current_line = (int)row;
        offset = video->row_offsets[row];
        if (offset > 80640u - 336u)
            return 0;
        video->screen32[offset + marker] = 0;
        video->screen16[offset + marker] = 0;
    }
    *video->current_line = (int)lines;
    return 1;
}

int gens_vdp_video_render_screen(GensVdpVideo *video, uint8_t *fake_screen)
{
    if (!gens_vdp_video_valid(video) || fake_screen == 0)
        return 0;
    *fake_screen = 0;
    return gens_vdp_video_convert(video);
}
