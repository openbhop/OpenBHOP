#pragma once

/*
  bh_core.h

  Shared low-level utilities.
  - No globals/static state.
  - Keep this header tiny and dependency-light.
*/

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(_MSC_VER)
#define BH_FORCEINLINE __forceinline
#define BH_ALIGNAS(x) __declspec(align(x))
#else
#define BH_FORCEINLINE inline __attribute__((always_inline))
#define BH_ALIGNAS(x) __attribute__((aligned(x)))
#endif

#define BH_ARRAY_COUNT(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

static BH_FORCEINLINE uint32_t bh_min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}
static BH_FORCEINLINE uint32_t bh_max_u32(uint32_t a, uint32_t b)
{
    return (a > b) ? a : b;
}

static BH_FORCEINLINE size_t bh_align_up_size(size_t value, size_t align)
{
    assert(align && ((align & (align - 1u)) == 0u));
    return (value + (align - 1u)) & ~(align - 1u);
}

static BH_FORCEINLINE uint32_t bh_hash_fnv1a_u32(const char *str)
{
    /* 32-bit FNV-1a */
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)str; *p; ++p)
    {
        h ^= (uint32_t)(*p);
        h *= 16777619u;
    }
    return h;
}
