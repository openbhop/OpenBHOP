/* -----------------------------------------------------------------------------
   bh_cmap_loader.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../core/bh_core.h"

struct BH_Renderer;
struct BH_Scene;
struct BH_SceneNode;
struct BH_PhysicsWorld;
struct BH_EntityServices;
struct BH_Arena;

typedef struct BH_CmapLoadResult
{
    struct BH_SceneNode *player_node;
} BH_CmapLoadResult;

/*
    Parses compiled .cmap binary and populates engine subsystems.
    Handles static geometry, collision brushes, baked lighting, and entity stubs.
*/
bool BH_Cmap_LoadIntoScene(const char *cmap_abs_path, struct BH_Renderer *renderer, struct BH_Scene *scene,
                           struct BH_PhysicsWorld *physics, struct BH_EntityServices *sv, struct BH_Arena *level_arena,
                           BH_CmapLoadResult *out_result);