/* Line orchestration follows the pinned Render_Line routine.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_render.h"

#define GENS_VDP_RENDER_SPAN 320u

int gens_vdp_render_valid(const GensVdpRender *render)
{
    return render != 0 && render->sprites != 0 && render->scroll_b != 0 &&
        render->scroll_a != 0 && render->sprite_draw != 0 && render->palette != 0 &&
        render->resolve != 0 && render->pixels != 0 && render->row_table != 0 &&
        render->current_line != 0 && gens_vdp_sprites_valid(render->sprites) &&
        render->scroll_b->memory == render->sprites &&
        render->scroll_a->memory == render->sprites &&
        render->sprite_draw->sprites == render->sprites &&
        render->scroll_b->pixels == render->pixels &&
        render->scroll_a->pixels == render->pixels &&
        render->sprite_draw->pixels == render->pixels &&
        render->resolve->pixels == render->pixels &&
        render->scroll_b->pixel_count == render->pixel_count &&
        render->scroll_a->pixel_count == render->pixel_count &&
        render->sprite_draw->pixel_count == render->pixel_count &&
        render->resolve->count == render->pixel_count &&
        render->scroll_b->set3 == render->scroll_a->set3 &&
        render->scroll_b->current_line == render->current_line &&
        render->scroll_a->current_line == render->current_line &&
        render->sprite_draw->line->current_line == render->current_line &&
        *render->current_line >= 0 && (size_t)*render->current_line < render->row_table_count &&
        render->row_table[*render->current_line] <= render->pixel_count &&
        render->pixel_count - render->row_table[*render->current_line] >= 336u;
}

int gens_vdp_render_line(GensVdpRender *render)
{
    size_t row, i;
    unsigned int interlace, shadow, cell_scroll;
    uint16_t value;

    if (!gens_vdp_render_valid(render))
        return 0;
    row = render->row_table[*render->current_line];
    if ((*render->sprites->set2 & 0x40u) == 0u)
    {
        value = (*render->sprites->set4 & 8u) != 0u ? UINT16_C(0x4040) : 0u;
        for (i = 0; i < GENS_VDP_RENDER_SPAN; ++i)
            render->pixels[row + 8u + i] = value;
    }
    else
    {
        if (!gens_vdp_sprites_prepare(render->sprites))
            return 0;
        interlace = (*render->sprites->set4 & 4u) != 0u;
        shadow = (*render->sprites->set4 & 8u) != 0u;
        cell_scroll = (*render->scroll_b->set3 & 4u) != 0u;
        render->scroll_b->interlace = render->scroll_a->interlace = interlace;
        render->scroll_b->shadow = render->scroll_a->shadow = shadow;
        render->scroll_b->cell_scroll = render->scroll_a->cell_scroll = cell_scroll;
        render->scroll_b->row_offset = render->scroll_a->row_offset = row;
        render->sprite_draw->interlace = interlace;
        render->sprite_draw->shadow = shadow;
        render->sprite_draw->row_offset = row;
        if (!gens_vdp_scroll_b_draw(render->scroll_b) ||
            !gens_vdp_scroll_a_draw(render->scroll_a) ||
            !gens_vdp_sprites_draw(render->sprite_draw))
            return 0;
    }
    if (!gens_vdp_palette_update(render->palette))
        return 0;
    render->resolve->offset = row + 8u;
    return gens_vdp_pixel_resolve(render->resolve);
}
