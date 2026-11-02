#ifndef CHIMERA_GENS_M68K_FETCH_H
#define CHIMERA_GENS_M68K_FETCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { GENS_M68K_FETCH_MAX_REGIONS = 64 };

typedef enum GensM68kFetchStatus {
    GENS_M68K_FETCH_OK = 0,
    GENS_M68K_FETCH_INVALID_ARGUMENT,
    GENS_M68K_FETCH_INVALID_REGION,
    GENS_M68K_FETCH_OUT_OF_RANGE
} GensM68kFetchStatus;

typedef struct GensM68kFetchRegion {
    uint32_t first;
    uint32_t last;
    const uint8_t *backing;
    size_t backing_size;
} GensM68kFetchRegion;

typedef struct GensM68kFetchResult {
    uintptr_t host_pc;
    uintptr_t fetch_base;
    uint32_t cache_first;
    uint32_t cache_last;
} GensM68kFetchResult;

GensM68kFetchStatus gens_m68k_resolve_fetch(uint32_t pc,
                                            const GensM68kFetchRegion *regions,
                                            size_t region_count,
                                            GensM68kFetchResult *result);

#ifdef __cplusplus
}
#endif

#endif
