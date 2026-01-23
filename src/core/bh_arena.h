/* -----------------------------------------------------------------------------
   bh_arena.h
----------------------------------------------------------------------------- */
#pragma once

#include "bh_core.h"

typedef struct BH_Arena
{
    uint8_t *base;
    size_t capacity;
    size_t offset;
} BH_Arena;

/* -----------------------------------------------------------------------------
   Public API
----------------------------------------------------------------------------- */

bool BH_Arena_Init(BH_Arena *arena, size_t capacity_bytes);
void BH_Arena_Shutdown(BH_Arena *arena);

void *BH_Arena_Alloc(BH_Arena *arena, size_t size_bytes, size_t align_bytes);

static BH_FORCEINLINE void *BH_Arena_AllocStruct(BH_Arena *arena, size_t struct_size, size_t align_bytes)
{
    return BH_Arena_Alloc(arena, struct_size, align_bytes);
}

static BH_FORCEINLINE void BH_Arena_Reset(BH_Arena *arena)
{
    arena->offset = 0;
}