/* Sprite preparation follows the pinned Genesis renderer macros.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_sprites.h"

#include <string.h>

_Static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4, "Gens sprite global widths");

int gens_vdp_sprites_valid(const GensVdpSprites *sprites)
{
    return sprites != 0 && sprites->vram != 0 && sprites->vram_size >= 65536u &&
        sprites->cram != 0 && sprites->cram_size >= 256u &&
        sprites->vsram_over != 0 && sprites->vsram_over_size >= 32u &&
        sprites->vsram != 0 && sprites->vsram_size >= 256u &&
        sprites->h_counter != 0 && sprites->h_counter_size >= 1024u &&
        sprites->sprite_address != 0 && sprites->set2 != 0 && sprites->set4 != 0 &&
        sprites->dirty != 0 && sprites->table != 0 && sprites->end != 0 &&
        sprites->table_dwords >= 2048u;
}

static uint8_t source_byte(const GensVdpSprites *sprites, uint32_t offset)
{
    if (offset < 65536u)
        return sprites->vram[offset];
    offset -= 65536u;
    if (offset < 256u)
        return sprites->cram[offset];
    offset -= 256u;
    if (offset < 32u)
        return sprites->vsram_over[offset];
    offset -= 32u;
    if (offset < 256u)
        return sprites->vsram[offset];
    return sprites->h_counter[offset - 256u];
}

static uint16_t source_word(const GensVdpSprites *sprites, uint32_t offset)
{
    return (uint16_t)(source_byte(sprites, offset) |
                      (uint16_t)source_byte(sprites, offset + 1u) << 8);
}

int gens_vdp_sprites_read_word(const GensVdpSprites *sprites, uint32_t offset,
                               uint16_t *word)
{
    if (!gens_vdp_sprites_valid(sprites) || word == 0 || offset > 67104u - 2u)
        return 0;
    *word = source_word(sprites, offset);
    return 1;
}

int gens_vdp_sprites_read_pattern(const GensVdpSprites *sprites, uint32_t offset,
                                  uint32_t *pattern)
{
    if (!gens_vdp_sprites_valid(sprites) || pattern == 0 || offset > 67104u - 4u)
        return 0;
    *pattern = (uint32_t)source_byte(sprites, offset) |
        (uint32_t)source_byte(sprites, offset + 1u) << 8 |
        (uint32_t)source_byte(sprites, offset + 2u) << 16 |
        (uint32_t)source_byte(sprites, offset + 3u) << 24;
    return 1;
}
int gens_vdp_sprites_prepare(GensVdpSprites *sprites)
{
    uintptr_t base, vram;
    uint32_t dirty, cleared, offset, row, x, y, width, size, next, i, index;
    int partial, interlace;

    if (!gens_vdp_sprites_valid(sprites))
        return 0;
    if ((*sprites->set2 & 0x40u) == 0u)
        return 1;
    memcpy(&dirty, sprites->dirty, sizeof(dirty));
    cleared = dirty & UINT32_C(0xffffff00);
    memcpy(sprites->dirty, &cleared, sizeof(cleared));
    dirty &= 3u;
    if (dirty == 0u)
        return 1;
    base = (uintptr_t)*sprites->sprite_address;
    vram = (uintptr_t)sprites->vram;
    if (base < vram || base - vram >= 65536u)
        return 0;
    offset = (uint32_t)(base - vram);
    partial = dirty == 2u;
    interlace = (*sprites->set4 & 4u) != 0u;
    next = 0;

    for (i = 0; i < 80u; ++i)
    {
        row = offset + next * 8u;
        index = i * 8u;
        size = source_byte(sprites, row + 3u);
        next = source_byte(sprites, row + 2u) & 0x7fu;
        x = (source_word(sprites, row + 6u) & 0x1ffu) - 128u;
        sprites->table[index] = x;
        sprites->table[index + 6u] = (sprites->table[index + 6u] & UINT32_C(0xffff0000)) |
            source_word(sprites, row + 4u);
        if (partial)
            sprites->table[index + 4u] = x + (size & 0xcu) * 2u + 7u;
        else
        {
            y = source_word(sprites, row);
            if (interlace)
                y >>= 1;
            y = (y & 0x1ffu) - 128u;
            width = ((size >> 2) & 3u) + 1u;
            sprites->table[index + 1u] = y;
            sprites->table[index + 2u] =
                (sprites->table[index + 2u] & UINT32_C(0xffffff00)) | width;
            sprites->table[index + 3u] =
                (sprites->table[index + 3u] & UINT32_C(0xffffff00)) | (size & 3u);
            sprites->table[index + 4u] = x + width * 8u - 1u;
            sprites->table[index + 5u] = y + (size & 3u) * 8u + 7u;
        }
        if (next == 0u)
            break;
    }
    if (!partial)
        *sprites->end = (i < 80u ? i : 79u) * 32u;
    return 1;
}

static int32_t signed_bits(uint32_t value)
{
    int32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

int gens_vdp_sprite_line_valid(const GensVdpSpriteLine *line)
{
    return line != 0 && line->table != 0 && line->end != 0 &&
        line->h_cells != 0 && line->h_pixels != 0 && line->table_dwords >= 2048u &&
        line->visible != 0 && line->bound != 0 && line->visible_dwords >= 256u &&
        line->current_line != 0 && line->limits != 0 && line->status != 0;
}

static int on_line(const uint32_t *sprite, int32_t current)
{
    return signed_bits(sprite[1]) <= current && signed_bits(sprite[5]) >= current;
}

int gens_vdp_sprites_select(GensVdpSpriteLine *line)
{
    uint32_t end, count, budget, used = 0, i, j, status;
    int32_t current, pixels;
    int first = 1, limited;
    const uint32_t *sprite;

    if (!gens_vdp_sprite_line_valid(line))
        return 0;
    end = *line->end;
    if (signed_bits(end) >= 0 && end >= 8192u)
        return 0;
    count = signed_bits(end) < 0 ? 1u : end / 32u + 1u;
    budget = *line->h_cells;
    current = *line->current_line;
    pixels = signed_bits(*line->h_pixels);
    limited = (*line->limits & 1) != 0;

    for (i = 0; i < count; ++i)
    {
        sprite = line->table + i * 8u;
        if (first)
        {
            if (!on_line(sprite, current))
                continue;
            first = 0;
            if (limited)
                budget -= sprite[2];
            if (signed_bits(sprite[0]) < pixels && signed_bits(sprite[4]) >= 0)
                line->visible[used++] = i * 32u;
            continue;
        }
        if (on_line(sprite, current))
        {
            if (sprite[0] == UINT32_C(0xffffff80))
                break;
            if (limited)
                budget -= sprite[2];
            if (signed_bits(sprite[0]) < pixels && signed_bits(sprite[4]) >= 0)
                line->visible[used++] = i * 32u;
        }
        if (limited && signed_bits(budget) <= 0)
        {
            for (j = i + 1u; j < count; ++j)
                if (on_line(line->table + j * 8u, current))
                {
                    memcpy(&status, line->status, sizeof(status));
                    status |= 0x40u;
                    memcpy(line->status, &status, sizeof(status));
                    break;
                }
            break;
        }
    }
    *line->bound = used * 4u;
    return 1;
}

int gens_vdp_sprites_prepare_line(GensVdpSprites *sprites, GensVdpSpriteLine *line)
{
    if (!gens_vdp_sprites_valid(sprites) || !gens_vdp_sprite_line_valid(line) ||
        line->table != sprites->table || line->end != sprites->end)
        return 0;
    if (!gens_vdp_sprites_prepare(sprites))
        return 0;
    if ((*sprites->set2 & 0x40u) == 0u)
        return 1;
    return gens_vdp_sprites_select(line);
}
