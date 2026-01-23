/* -----------------------------------------------------------------------------
   bh_material_manager.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../core/bh_arena.h"
#include "bh_material.h"
#include "bh_texture_manager.h"

/* -----------------------------------------------------------------------------
   Types
   ----------------------------------------------------------------------------- */

/*
   Manages runtime loading and caching of materials from .mat text files.
   Resources are allocated from the provided permanent arena.
*/
typedef struct BH_MaterialManager
{
    const char *asset_root;
    BH_TextureManager *textures;
    const BH_ShaderStageReflection *fs_refl;
    BH_Arena *arena;

    struct BH_MatEntry
    {
        const char *rel_path;  /* Arena-owned */
        BH_Material *material; /* Arena-owned */
        uint32_t refcount;
        bool in_use;
    } entries[128];

    BH_Material *fallback_material;
} BH_MaterialManager;

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_MaterialManager_Init(BH_MaterialManager *mm, const char *asset_root, BH_TextureManager *textures,
                             const BH_ShaderStageReflection *fragment_refl, BH_Arena *permanent_arena);

void BH_MaterialManager_Shutdown(BH_MaterialManager *mm);

/* Loads material from path relative to asset_root, or returns cached instance. */
const BH_Material *BH_MaterialManager_Load(BH_MaterialManager *mm, const char *mat_rel_path);

/* Heuristic loader for material IDs (e.g. from compiled maps).
   Resolves "id", "materials/id", etc., to the canonical .mat path.
*/
const BH_Material *BH_MaterialManager_LoadById(BH_MaterialManager *mm, const char *material_id);

/* Returns a valid neutral material (default textures, identity params). */
const BH_Material *BH_MaterialManager_GetFallback(BH_MaterialManager *mm);