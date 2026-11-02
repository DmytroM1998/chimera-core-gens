#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "gens_rom.h"

static uint8_t image[GENS_ROM_MAX_BYTES];
static uint8_t expected[GENS_ROM_SMD_BLOCK_BYTES * 2];
static uint8_t trailing[GENS_ROM_HEADER_BYTES];

static void build_smd(void)
{
    size_t i, block, j;
    for (i = 0; i < sizeof(expected); ++i)
        expected[i] = (uint8_t)(i * 13 + 19);
    memcpy(expected + 0x100, "SEGA GENESIS", 12);
    memset(image, 0, sizeof(image));
    image[8] = 0xaa;
    image[9] = 0xbb;
    image[10] = 0x06;
    for (block = 0; block < 2; ++block)
    {
        size_t in = block * GENS_ROM_SMD_BLOCK_BYTES;
        size_t out = GENS_ROM_HEADER_BYTES + in;
        for (j = 0; j < GENS_ROM_SMD_BLOCK_BYTES / 2; ++j)
        {
            image[out + j] = expected[in + j * 2 + 1];
            image[out + GENS_ROM_SMD_BLOCK_BYTES / 2 + j] = expected[in + j * 2];
        }
    }
}

static GensRomStatus normalize(size_t length, const char *name,
                               size_t *size, GensRomEncoding *encoding)
{
    return gens_rom_normalize(image, length, sizeof(image), name, size, encoding);
}

static void put_be32(size_t at, uint32_t value)
{
    image[at] = (uint8_t)(value >> 24);
    image[at + 1] = (uint8_t)(value >> 16);
    image[at + 2] = (uint8_t)(value >> 8);
    image[at + 3] = (uint8_t)value;
}

static void test_setup(void)
{
    uint8_t order[3] = {0, 1, 2};
    GensRomSetup setup;
    int country;
    memset(image, 0, sizeof(image));

    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_OK);
    assert(setup.game_mode == 1 && setup.cpu_mode == 0);
    assert(setup.sram_start == 0x200000 && setup.sram_end == 0x20ffff);
    assert(setup.sram_enabled && setup.sram_writable && !setup.sram_custom);

    memcpy(image + 0x1f0, "eUj ", 4);
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_OK);
    assert(setup.game_mode == 1 && setup.cpu_mode == 0);
    order[0] = 0; order[1] = 2; order[2] = 1;
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_OK);
    assert(setup.game_mode == 1 && setup.cpu_mode == 0);
    order[0] = 1; order[1] = 0; order[2] = 2;
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_OK);
    assert(setup.game_mode == 0 && setup.cpu_mode == 0);
    order[0] = 1; order[1] = 2; order[2] = 0;
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_OK);
    assert(setup.game_mode == 0 && setup.cpu_mode == 0);
    order[0] = 2; order[1] = 0; order[2] = 1;
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_OK);
    assert(setup.game_mode == 1 && setup.cpu_mode == 1);
    order[0] = 2; order[1] = 1; order[2] = 0;
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_OK);
    assert(setup.game_mode == 1 && setup.cpu_mode == 1);
    memcpy(image + 0x1f0, "JaP ", 4);
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_OK);
    assert(setup.game_mode == 0 && setup.cpu_mode == 0);
    memcpy(image + 0x1f0, "2   ", 4);
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_OK);
    assert(setup.game_mode == 0 && setup.cpu_mode == 1);
    image[0x1f0] = 2;
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_OK);
    assert(setup.game_mode == 0 && setup.cpu_mode == 1);
    memcpy(image + 0x1f0, "eurU", 4);
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_OK);
    assert(setup.game_mode == 1 && setup.cpu_mode == 1);

    for (country = GENS_COUNTRY_JAPAN_NTSC; country <= GENS_COUNTRY_JAPAN_PAL; ++country)
    {
        assert(gens_rom_setup(image, 512, country, order, &setup) == GENS_ROM_OK);
        assert(setup.game_mode == (country == GENS_COUNTRY_USA_NTSC ||
                                   country == GENS_COUNTRY_EUROPE_PAL));
        assert(setup.cpu_mode == (country == GENS_COUNTRY_EUROPE_PAL ||
                                  country == GENS_COUNTRY_JAPAN_PAL));
    }

    image[0x1f0] = 0xff;
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, order, &setup)
           == GENS_ROM_UNSUPPORTED_COUNTRY_HEADER);
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_USA_NTSC, order, &setup) == GENS_ROM_OK);
    order[1] = 2;
    setup.sram_start = 123;
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_USA_NTSC, order, &setup)
           == GENS_ROM_INVALID_ARGUMENT);
    assert(setup.sram_start == 123);
    order[1] = 1;
    order[2] = 3;
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_USA_NTSC, order, &setup)
           == GENS_ROM_INVALID_ARGUMENT);
    order[2] = 0;

    image[0x1b0] = 'R'; image[0x1b1] = 'A'; image[0x1b2] = 0x40;
    put_be32(0x1b4, 0x200001);
    put_be32(0x1b8, 0x200001);
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_USA_NTSC, order, &setup) == GENS_ROM_OK);
    assert(setup.sram_start == 0x200000 && setup.sram_end == 0x200001 && setup.sram_custom);
    put_be32(0x1b8, 0x200003);
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_USA_NTSC, order, &setup) == GENS_ROM_OK);
    assert(setup.sram_start == 0x200000 && setup.sram_end == 0x200003 && !setup.sram_custom);
    put_be32(0x1b4, 0x1a0000);
    put_be32(0x1b8, 0x180100);
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_USA_NTSC, order, &setup) == GENS_ROM_OK);
    assert(setup.sram_start == 0x180000 && setup.sram_end == 0x180101 && !setup.sram_custom);
    put_be32(0x1b4, 0x400000);
    put_be32(0x1b8, 0x100);
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_USA_NTSC, order, &setup) == GENS_ROM_OK);
    assert(setup.sram_start == 0x400000 && setup.sram_end == 0x40ffff);
    put_be32(0x1b4, 0x200000);
    put_be32(0x1b8, 0x210000);
    assert(gens_rom_setup(image, 2 * 1024 * 1024, GENS_COUNTRY_USA_NTSC, order, &setup)
           == GENS_ROM_OK);
    assert(setup.sram_end == 0x20ffff && setup.sram_enabled && setup.sram_writable);
    assert(gens_rom_setup(image, 2 * 1024 * 1024 + 1, GENS_COUNTRY_USA_NTSC, order, &setup)
           == GENS_ROM_OK);
    assert(!setup.sram_enabled && !setup.sram_writable);
    image[0x1b2] = 0;
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_USA_NTSC, order, &setup) == GENS_ROM_OK);
    assert(setup.sram_start == 0x200000 && setup.sram_end == 0x20ffff);

    assert(gens_rom_setup(image, 511, GENS_COUNTRY_AUTO, order, &setup) == GENS_ROM_TOO_SMALL);
    assert(gens_rom_setup(image, GENS_ROM_MAX_BYTES + 1, GENS_COUNTRY_AUTO, order, &setup)
           == GENS_ROM_TOO_LARGE);
    assert(gens_rom_setup(image, 512, -2, order, &setup) == GENS_ROM_INVALID_ARGUMENT);
    assert(gens_rom_setup(image, 512, GENS_COUNTRY_AUTO, 0, &setup) == GENS_ROM_INVALID_ARGUMENT);
}

int main(void)
{
    size_t size = 123;
    GensRomEncoding encoding = GENS_ROM_RAW;
    size_t smd_size = GENS_ROM_HEADER_BYTES + sizeof(expected);
    size_t i;

    build_smd();
    memcpy(trailing, image + sizeof(expected), sizeof(trailing));
    assert(normalize(smd_size, "test.smd", &size, &encoding) == GENS_ROM_OK);
    assert(size == sizeof(expected) && encoding == GENS_ROM_SMD);
    assert(memcmp(image, expected, size) == 0);
    assert(memcmp(image + size, trailing, sizeof(trailing)) == 0);
    assert(gens_rom_swap_words(image, size) == GENS_ROM_OK);
    for (i = 0; i < size; i += 2)
    {
        assert(image[i] == expected[i + 1]);
        assert(image[i + 1] == expected[i]);
    }
    assert(gens_rom_swap_words(image, size) == GENS_ROM_OK);
    assert(memcmp(image, expected, size) == 0);

    memset(image, 0, sizeof(image));
    memcpy(image + 0x100, "sEgA", 4);
    image[8] = 0xaa;
    image[9] = 0xbb;
    image[10] = 0x06;
    image[512] = 0x9b;
    assert(normalize(513, "test.bin", &size, &encoding) == GENS_ROM_OK);
    assert(size == 513 && encoding == GENS_ROM_RAW);
    assert(gens_rom_swap_words(image, size) == GENS_ROM_OK && image[512] == 0x9b);
    assert(gens_rom_swap_words(0, 0) == GENS_ROM_OK);
    assert(gens_rom_swap_words(0, 1) == GENS_ROM_INVALID_ARGUMENT);

    build_smd();
    image[8] = image[9] = image[10] = 0;
    assert(normalize(smd_size, "test.bin", &size, &encoding) == GENS_ROM_OK);
    assert(size == sizeof(expected) && encoding == GENS_ROM_SMD);
    assert(memcmp(image, expected, size) == 0);

    memset(image, 0, sizeof(image));
    memcpy(image, "segadiscsystem", 14);
    size = 123;
    encoding = GENS_ROM_RAW;
    assert(normalize(512, "disc.bin", &size, &encoding) == GENS_ROM_UNSUPPORTED_SEGA_CD);
    assert(size == 123 && encoding == GENS_ROM_RAW && image[0] == 's');
    memset(image, 0, sizeof(image));
    memcpy(image + 0x10, "SEGADISCSYSTEM", 14);
    assert(normalize(512, "disc.bin", &size, &encoding) == GENS_ROM_UNSUPPORTED_SEGA_CD);
    memset(image, 0, sizeof(image));
    memcpy(image + 0x105, "32x", 3);
    assert(normalize(512, "test.bin", &size, &encoding) == GENS_ROM_UNSUPPORTED_32X);
    assert(normalize(512, "test.32X", &size, &encoding) == GENS_ROM_UNSUPPORTED_32X);
    memset(image, 0, sizeof(image));
    assert(normalize(512, "disc.CUE", &size, &encoding) == GENS_ROM_UNSUPPORTED_SEGA_CD);
    assert(normalize(512, "test.ZIP", &size, &encoding) == GENS_ROM_UNSUPPORTED_ARCHIVE);

    build_smd();
    image[0x282] = '3';
    image[0x283] = 'X';
    assert(normalize(smd_size, "test.smd", &size, &encoding) == GENS_ROM_UNSUPPORTED_32X);
    build_smd();
    assert(normalize(smd_size - 1, "test.smd", &size, &encoding) == GENS_ROM_INCOMPLETE_SMD);
    assert(size == 123 && image[8] == 0xaa);
    assert(normalize(511, "test.bin", &size, &encoding) == GENS_ROM_TOO_SMALL);
    assert(gens_rom_normalize(image, 513, 512, "test.bin", &size, &encoding)
           == GENS_ROM_BUFFER_TOO_SMALL);
    assert(gens_rom_normalize(image, GENS_ROM_MAX_BYTES + 1,
                              GENS_ROM_MAX_BYTES + 1, "test.bin", &size, &encoding)
           == GENS_ROM_TOO_LARGE);
    assert(gens_rom_normalize(image, 512, sizeof(image), "", &size, &encoding)
           == GENS_ROM_INVALID_ARGUMENT);
    assert(gens_rom_normalize(image, 512, sizeof(image), "test.bin", 0, &encoding)
           == GENS_ROM_INVALID_ARGUMENT);
    assert(gens_rom_normalize(0, 512, 512, "test.bin", &size, &encoding)
           == GENS_ROM_INVALID_ARGUMENT);

    memset(image, 0, sizeof(image));
    assert(normalize(sizeof(image), "test.bin", &size, &encoding) == GENS_ROM_OK);
    assert(size == GENS_ROM_MAX_BYTES && encoding == GENS_ROM_RAW);
    test_setup();
    return 0;
}
