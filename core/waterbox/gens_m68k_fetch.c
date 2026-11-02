/* Based on the pinned Gens-Rerecording Starscream 24-bit fetch rebase.
 * The upstream component terms apply to derived portions.
 */
#include "gens_m68k_fetch.h"

_Static_assert(sizeof(uintptr_t) == 8, "the ported fetch resolver requires a 64-bit host");

GensM68kFetchStatus gens_m68k_resolve_fetch(uint32_t pc,
                                            const GensM68kFetchRegion *regions,
                                            size_t region_count,
                                            GensM68kFetchResult *result)
{
    const uint32_t physical = pc & UINT32_C(0x00ffffff);
    const uint32_t upper = pc & UINT32_C(0xff000000);
    size_t i;
    if (result == 0 || (region_count != 0 && regions == 0) ||
        region_count > GENS_M68K_FETCH_MAX_REGIONS)
        return GENS_M68K_FETCH_INVALID_ARGUMENT;

    for (i = 0; i < region_count; ++i)
    {
        const GensM68kFetchRegion *region = &regions[i];
        size_t length;
        uintptr_t address;
        GensM68kFetchResult found;
        if (region->first > region->last || region->last > UINT32_C(0x00ffffff) ||
            region->backing == 0)
            return GENS_M68K_FETCH_INVALID_REGION;
        length = (size_t)(region->last - region->first) + 1u;
        address = (uintptr_t)region->backing;
        if (region->backing_size < length || (uintptr_t)(length - 1u) > UINTPTR_MAX - address)
            return GENS_M68K_FETCH_INVALID_REGION;
        if (physical < region->first || physical > region->last)
            continue;

        found.host_pc = address + (uintptr_t)(physical - region->first);
        found.fetch_base = found.host_pc - (uintptr_t)pc;
        found.cache_first = upper | region->first;
        found.cache_last = upper | region->last;
        *result = found;
        return GENS_M68K_FETCH_OK;
    }
    return GENS_M68K_FETCH_OUT_OF_RANGE;
}
