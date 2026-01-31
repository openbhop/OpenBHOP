/* -----------------------------------------------------------------------------
   bh_bsp_loader.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../core/bh_core.h"

struct BH_Renderer;
struct BH_Scene;
struct BH_SceneNode;
struct BH_PhysicsWorld;
struct BH_EntityServices;
struct BH_Arena;

typedef struct BH_BspLoadResult
{
    struct BH_SceneNode *player_node;
} BH_BspLoadResult;

/*
    Parses a Source-engine .bsp (VBSP) and populates engine subsystems.
*/
bool BH_Bsp_LoadIntoScene(const char *bsp_abs_path, struct BH_Renderer *renderer, struct BH_Scene *scene,
                          struct BH_PhysicsWorld *physics, struct BH_EntityServices *sv, struct BH_Arena *level_arena,
                          BH_BspLoadResult *out_result);
