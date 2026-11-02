#ifndef CHIMERA_GENS_ROM_H
#define CHIMERA_GENS_ROM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GENS_ROM_MAX_BYTES = 6 * 1024 * 1024,
    GENS_ROM_HEADER_BYTES = 512,
    GENS_ROM_SMD_BLOCK_BYTES = 16384
};

typedef enum GensRomStatus {
    GENS_ROM_OK = 0,
    GENS_ROM_INVALID_ARGUMENT,
    GENS_ROM_BUFFER_TOO_SMALL,
    GENS_ROM_TOO_SMALL,
    GENS_ROM_TOO_LARGE,
    GENS_ROM_INCOMPLETE_SMD,
    GENS_ROM_UNSUPPORTED_SEGA_CD,
    GENS_ROM_UNSUPPORTED_32X,
    GENS_ROM_UNSUPPORTED_ARCHIVE,
    GENS_ROM_UNSUPPORTED_COUNTRY_HEADER
} GensRomStatus;

typedef enum GensRomEncoding {
    GENS_ROM_RAW = 0,
    GENS_ROM_SMD
} GensRomEncoding;

enum GensCountry {
    GENS_COUNTRY_AUTO = -1,
    GENS_COUNTRY_JAPAN_NTSC = 0,
    GENS_COUNTRY_USA_NTSC = 1,
    GENS_COUNTRY_EUROPE_PAL = 2,
    GENS_COUNTRY_JAPAN_PAL = 3
};

typedef struct GensRomSetup {
    uint32_t sram_start;
    uint32_t sram_end;
    int sram_enabled;
    int sram_writable;
    int sram_custom;
    int game_mode;
    int cpu_mode;
} GensRomSetup;

GensRomStatus gens_rom_normalize(uint8_t *data, size_t length, size_t capacity,
                                 const char *logical_name, size_t *rom_size,
                                 GensRomEncoding *encoding);
GensRomStatus gens_rom_swap_words(uint8_t *data, size_t length);
GensRomStatus gens_rom_setup(const uint8_t *data, size_t length, int country,
                             const uint8_t country_order[3], GensRomSetup *setup);
const char *gens_rom_status_string(GensRomStatus status);

#ifdef __cplusplus
}
#endif

#endif
