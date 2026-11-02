#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "gens_m68k_fetch.h"

#ifndef MAP_FIXED_NOREPLACE
#error MAP_FIXED_NOREPLACE is required for the fixed-address test
#endif

_Static_assert(sizeof(void *) == 8, "the fetch test requires x86-64");

static void check_unchanged(const GensM68kFetchResult *result,
                            const GensM68kFetchResult *sentinel)
{
    assert(result->host_pc == sentinel->host_pc);
    assert(result->fetch_base == sentinel->fetch_base);
    assert(result->cache_first == sentinel->cache_first);
    assert(result->cache_last == sentinel->cache_last);
}

int main(void)
{
    const uintptr_t guest_base = (uintptr_t)UINT64_C(0x36f00000000);
    const size_t mapping_size = 0x12000;
    uint8_t *memory;
    GensM68kFetchRegion regions[3];
    GensM68kFetchResult result, sentinel, first_alias;
    size_t i;

    memory = mmap((void *)guest_base, mapping_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (memory == MAP_FAILED)
    {
        perror("fixed-address fetch test mmap");
        return 1;
    }
    if ((uintptr_t)memory != guest_base)
    {
        fprintf(stderr, "fixed-address fetch test mapped an unexpected address\n");
        if (munmap(memory, mapping_size) != 0)
            perror("fixed-address fetch test munmap");
        return 1;
    }
    for (i = 0; i < mapping_size; ++i)
        memory[i] = (uint8_t)(i ^ (i >> 8));

    regions[0].first = 0x100; regions[0].last = 0x1ff;
    regions[0].backing = memory; regions[0].backing_size = 0x100;
    regions[1].first = 0xff0000; regions[1].last = 0xffffff;
    regions[1].backing = memory + 0x1000; regions[1].backing_size = 0x10000;
    regions[2].first = 0x100; regions[2].last = 0x1ff;
    regions[2].backing = memory + 0x100; regions[2].backing_size = 0x100;

    assert(gens_m68k_resolve_fetch(0x100, regions, 3, &result) == GENS_M68K_FETCH_OK);
    assert(result.host_pc == guest_base && result.fetch_base + 0x100 == result.host_pc);
    assert(result.cache_first == 0x100 && result.cache_last == 0x1ff);
    assert(*(const uint8_t *)result.host_pc == memory[0]);

    regions[0].backing = (const uint8_t *)(uintptr_t)0x1000;
    assert(gens_m68k_resolve_fetch(0xff000100u, regions, 3, &result)
           == GENS_M68K_FETCH_OK);
    assert(result.host_pc == 0x1000u);
    assert(result.fetch_base + (uintptr_t)0xff000100u == result.host_pc);
    regions[0].backing = memory;

    assert(gens_m68k_resolve_fetch(0x00ff0123u, regions, 3, &first_alias)
           == GENS_M68K_FETCH_OK);
    assert(first_alias.host_pc == guest_base + 0x1000 + 0x123);
    assert(gens_m68k_resolve_fetch(0xabff0123u, regions, 3, &result)
           == GENS_M68K_FETCH_OK);
    assert(result.host_pc == first_alias.host_pc);
    assert(result.fetch_base + (uintptr_t)0xabff0123u == result.host_pc);
    assert(result.cache_first == 0xabff0000u && result.cache_last == 0xabffffffu);
    assert(*(const uint8_t *)result.host_pc == memory[0x1123]);
    assert(gens_m68k_resolve_fetch(0xffffffffu, regions, 3, &result)
           == GENS_M68K_FETCH_OK);
    assert(result.host_pc == guest_base + 0x10fff);
    assert(result.cache_first == 0xffff0000u && result.cache_last == 0xffffffffu);
    assert(result.fetch_base + (uintptr_t)0xffffffffu == result.host_pc);

    sentinel.host_pc = 0x1234; sentinel.fetch_base = 0x5678;
    sentinel.cache_first = 0x9abc; sentinel.cache_last = 0xdef0;
    result = sentinel;
    assert(gens_m68k_resolve_fetch(0x800000ffu, regions, 3, &result)
           == GENS_M68K_FETCH_OUT_OF_RANGE);
    check_unchanged(&result, &sentinel);
    assert(gens_m68k_resolve_fetch(0, 0, 0, &result) == GENS_M68K_FETCH_OUT_OF_RANGE);
    check_unchanged(&result, &sentinel);
    assert(gens_m68k_resolve_fetch(0, 0, 1, &result) == GENS_M68K_FETCH_INVALID_ARGUMENT);
    assert(gens_m68k_resolve_fetch(0, regions, GENS_M68K_FETCH_MAX_REGIONS + 1, &result)
           == GENS_M68K_FETCH_INVALID_ARGUMENT);
    assert(gens_m68k_resolve_fetch(0, regions, 3, 0) == GENS_M68K_FETCH_INVALID_ARGUMENT);
    check_unchanged(&result, &sentinel);

    regions[0].backing_size = 0xff;
    assert(gens_m68k_resolve_fetch(0x100, regions, 3, &result)
           == GENS_M68K_FETCH_INVALID_REGION);
    regions[0].backing_size = 0x100;
    regions[0].last = 0x1000000u;
    assert(gens_m68k_resolve_fetch(0x100, regions, 3, &result)
           == GENS_M68K_FETCH_INVALID_REGION);
    regions[0].last = 0xff;
    assert(gens_m68k_resolve_fetch(0x100, regions, 3, &result)
           == GENS_M68K_FETCH_INVALID_REGION);
    regions[0].last = 0x1ff;
    regions[0].backing = 0;
    assert(gens_m68k_resolve_fetch(0x100, regions, 3, &result)
           == GENS_M68K_FETCH_INVALID_REGION);
    regions[0].backing = (const uint8_t *)(UINTPTR_MAX - 127u);
    assert(gens_m68k_resolve_fetch(0x100, regions, 3, &result)
           == GENS_M68K_FETCH_INVALID_REGION);
    check_unchanged(&result, &sentinel);
    assert(munmap(memory, mapping_size) == 0);
    return 0;
}
