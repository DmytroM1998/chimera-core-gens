/* Derived from Gens-Rerecording Rom.cpp, Gens.cpp, and Misc.asm at the pinned revision.
 * The upstream component terms apply to the derived portions.
 */
#include "gens_rom.h"

#include <string.h>

static unsigned char lower_ascii(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return (unsigned char)(c + ('a' - 'A'));
    return c;
}

static int matches_ascii(const uint8_t *data, const char *text, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i)
        if (lower_ascii(data[i]) != lower_ascii((unsigned char)text[i]))
            return 0;
    return 1;
}

static int ends_with(const char *name, const char *suffix)
{
    size_t n = strlen(name), suffix_length = strlen(suffix), i;
    if (n < suffix_length)
        return 0;
    for (i = 0; i < suffix_length; ++i)
        if (lower_ascii((unsigned char)name[n - suffix_length + i]) !=
            lower_ascii((unsigned char)suffix[i]))
            return 0;
    return 1;
}

GensRomStatus gens_rom_normalize(uint8_t *data, size_t length, size_t capacity,
                                 const char *logical_name, size_t *rom_size,
                                 GensRomEncoding *encoding)
{
    int interleaved = 0;
    size_t offset, j;
    uint8_t block[GENS_ROM_SMD_BLOCK_BYTES];

    if (data == 0 || logical_name == 0 || logical_name[0] == '\0' ||
        rom_size == 0 || encoding == 0)
        return GENS_ROM_INVALID_ARGUMENT;
    if (length > capacity)
        return GENS_ROM_BUFFER_TOO_SMALL;
    if (length > GENS_ROM_MAX_BYTES)
        return GENS_ROM_TOO_LARGE;
    if (length < GENS_ROM_HEADER_BYTES)
        return GENS_ROM_TOO_SMALL;
    if (matches_ascii(data, "SEGADISCSYSTEM", 14) ||
        matches_ascii(data + 0x10, "SEGADISCSYSTEM", 14) ||
        ends_with(logical_name, ".cue") || ends_with(logical_name, ".iso") ||
        ends_with(logical_name, ".raw"))
        return GENS_ROM_UNSUPPORTED_SEGA_CD;
    if (ends_with(logical_name, ".32x"))
        return GENS_ROM_UNSUPPORTED_32X;
    if (ends_with(logical_name, ".zip") || ends_with(logical_name, ".rar") ||
        ends_with(logical_name, ".7z"))
        return GENS_ROM_UNSUPPORTED_ARCHIVE;

    if (!matches_ascii(data + 0x100, "SEGA", 4))
    {
        if (length >= 0x282 && matches_ascii(data + 0x280, "EA", 2))
            interleaved = 1;
        if (data[0x08] == 0xaa && data[0x09] == 0xbb && data[0x0a] == 0x06)
            interleaved = 1;
    }

    if (interleaved)
    {
        if (length < GENS_ROM_HEADER_BYTES + GENS_ROM_SMD_BLOCK_BYTES ||
            (length - GENS_ROM_HEADER_BYTES) % GENS_ROM_SMD_BLOCK_BYTES != 0)
            return GENS_ROM_INCOMPLETE_SMD;
        if (matches_ascii(data + 0x282, "3X", 2))
            return GENS_ROM_UNSUPPORTED_32X;
        for (offset = 0; offset < length - GENS_ROM_HEADER_BYTES;
             offset += GENS_ROM_SMD_BLOCK_BYTES)
        {
            memcpy(block, data + GENS_ROM_HEADER_BYTES + offset, sizeof(block));
            for (j = 0; j < GENS_ROM_SMD_BLOCK_BYTES / 2; ++j)
            {
                data[offset + j * 2] = block[j + GENS_ROM_SMD_BLOCK_BYTES / 2];
                data[offset + j * 2 + 1] = block[j];
            }
        }
        *rom_size = length - GENS_ROM_HEADER_BYTES;
        *encoding = GENS_ROM_SMD;
    }
    else
    {
        if (matches_ascii(data + 0x105, "32X", 3))
            return GENS_ROM_UNSUPPORTED_32X;
        *rom_size = length;
        *encoding = GENS_ROM_RAW;
    }
    return GENS_ROM_OK;
}

GensRomStatus gens_rom_swap_words(uint8_t *data, size_t length)
{
    size_t word;
    if (data == 0 && length != 0)
        return GENS_ROM_INVALID_ARGUMENT;
    for (word = 0; word < length / 2; ++word)
    {
        uint8_t byte = data[word * 2];
        data[word * 2] = data[word * 2 + 1];
        data[word * 2 + 1] = byte;
    }
    return GENS_ROM_OK;
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

GensRomStatus gens_rom_setup(const uint8_t *data, size_t length, int country,
                             const uint8_t country_order[3], GensRomSetup *setup)
{
    GensRomSetup result;
    unsigned int seen = 0, country_bits = 0;
    size_t i;

    if (data == 0 || country_order == 0 || setup == 0 ||
        country < GENS_COUNTRY_AUTO || country > GENS_COUNTRY_JAPAN_PAL)
        return GENS_ROM_INVALID_ARGUMENT;
    if (length < GENS_ROM_HEADER_BYTES)
        return GENS_ROM_TOO_SMALL;
    if (length > GENS_ROM_MAX_BYTES)
        return GENS_ROM_TOO_LARGE;
    for (i = 0; i < 3; ++i)
    {
        unsigned int bit;
        if (country_order[i] > 2)
            return GENS_ROM_INVALID_ARGUMENT;
        bit = 1u << country_order[i];
        if ((seen & bit) != 0u)
            return GENS_ROM_INVALID_ARGUMENT;
        seen |= bit;
    }

    if (country == GENS_COUNTRY_AUTO)
    {
        if (matches_ascii(data + 0x1f0, "eur", 3)) country_bits |= 8;
        else if (matches_ascii(data + 0x1f0, "usa", 3)) country_bits |= 4;
        else if (matches_ascii(data + 0x1f0, "jap", 3)) country_bits |= 1;
        else
        {
            for (i = 0; i < 4; ++i)
            {
                unsigned char c = data[0x1f0 + i];
                if (c >= 0x80)
                    return GENS_ROM_UNSUPPORTED_COUNTRY_HEADER;
                c = lower_ascii(c);
                if (c == 'u') country_bits |= 4;
                else if (c == 'j') country_bits |= 1;
                else if (c == 'e') country_bits |= 8;
                else if (c < 16) country_bits |= c;
                else if (c >= '0' && c <= '9') country_bits |= (unsigned int)(c - '0');
                else if (c >= 'a' && c <= 'f') country_bits |= (unsigned int)(c - 'a' + 10);
            }
        }

        country = GENS_COUNTRY_AUTO;
        for (i = 0; i < 3; ++i)
        {
            unsigned int candidate = country_order[i];
            unsigned int mask = candidate == 0 ? 4u : (candidate == 1 ? 1u : 8u);
            if ((country_bits & mask) != 0u)
            {
                country = candidate == 0 ? GENS_COUNTRY_USA_NTSC :
                          (candidate == 1 ? GENS_COUNTRY_JAPAN_NTSC : GENS_COUNTRY_EUROPE_PAL);
                break;
            }
        }
        if (country == GENS_COUNTRY_AUTO)
            country = (country_bits & 2u) ? GENS_COUNTRY_JAPAN_PAL : GENS_COUNTRY_USA_NTSC;
    }

    result.game_mode = country == GENS_COUNTRY_USA_NTSC ||
                       country == GENS_COUNTRY_EUROPE_PAL;
    result.cpu_mode = country == GENS_COUNTRY_EUROPE_PAL ||
                      country == GENS_COUNTRY_JAPAN_PAL;
    if (data[0x1b0] == 'R' && data[0x1b1] == 'A' && (data[0x1b2] & 0x40u))
    {
        result.sram_start = read_be32(data + 0x1b4) & UINT32_C(0x0f80000);
        result.sram_end = read_be32(data + 0x1b8);
    }
    else
    {
        result.sram_start = UINT32_C(0x200000);
        result.sram_end = UINT32_C(0x20ffff);
    }
    if (result.sram_start > result.sram_end ||
        result.sram_end - result.sram_start >= UINT32_C(65536))
        result.sram_end = result.sram_start + UINT32_C(65536) - 1u;
    result.sram_enabled = length <= 2u * 1024u * 1024u;
    result.sram_writable = result.sram_enabled;
    result.sram_start &= UINT32_C(0xfffffffe);
    result.sram_end |= UINT32_C(1);
    result.sram_custom = result.sram_end - result.sram_start <= UINT32_C(2);
    *setup = result;
    return GENS_ROM_OK;
}

const char *gens_rom_status_string(GensRomStatus status)
{
    switch (status)
    {
        case GENS_ROM_OK: return "ok";
        case GENS_ROM_INVALID_ARGUMENT: return "invalid cartridge input";
        case GENS_ROM_BUFFER_TOO_SMALL: return "cartridge buffer is too small";
        case GENS_ROM_TOO_SMALL: return "cartridge header is incomplete";
        case GENS_ROM_TOO_LARGE: return "cartridge exceeds the Gens size limit";
        case GENS_ROM_INCOMPLETE_SMD: return "interleaved cartridge has an incomplete block";
        case GENS_ROM_UNSUPPORTED_SEGA_CD: return "Sega CD images are not supported";
        case GENS_ROM_UNSUPPORTED_32X: return "32X cartridges are not supported";
        case GENS_ROM_UNSUPPORTED_ARCHIVE: return "compressed cartridges are not supported";
        case GENS_ROM_UNSUPPORTED_COUNTRY_HEADER: return "cartridge country header is not supported";
        default: return "unknown cartridge error";
    }
}
