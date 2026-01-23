/* -----------------------------------------------------------------------------
   bh_arena.c
----------------------------------------------------------------------------- */
#include "bh_arena.h"

#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------------
   Init / Shutdown
----------------------------------------------------------------------------- */

bool BH_Arena_Init(BH_Arena *arena, size_t capacity_bytes)
{
    if (!arena || capacity_bytes == 0)
    {
        return false;
    }

    arena->base = (uint8_t *)malloc(capacity_bytes);
    if (!arena->base)
    {
        arena->capacity = 0;
        arena->offset = 0;
        return false;
    }

    arena->capacity = capacity_bytes;
    arena->offset = 0;

    return true;
}

void BH_Arena_Shutdown(BH_Arena *arena)
{
    if (!arena)
    {
        return;
    }

    free(arena->base);
    arena->base = NULL;
    arena->capacity = 0;
    arena->offset = 0;
}

/* -----------------------------------------------------------------------------
   Allocation
----------------------------------------------------------------------------- */

void *BH_Arena_Alloc(BH_Arena *arena, size_t size_bytes, size_t align_bytes)
{
    if (!arena || size_bytes == 0)
    {
        return NULL;
    }

    if (align_bytes == 0)
    {
        align_bytes = sizeof(void *);
    }

    const size_t aligned_off = bh_align_up_size(arena->offset, align_bytes);
    const size_t end = aligned_off + size_bytes;

    if (end > arena->capacity)
    {
        return NULL;
    }

    void *ptr = arena->base + aligned_off;
    arena->offset = end;

    memset(ptr, 0, size_bytes);

    return ptr;
}