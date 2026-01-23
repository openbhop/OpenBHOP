/* -----------------------------------------------------------------------------
   bh_scene.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../core/bh_arena.h"
#include "../core/bh_core.h"
#include "../math/bh_math.h"

/* -----------------------------------------------------------------------------
   Forward Declarations
   ----------------------------------------------------------------------------- */

struct BH_Mesh;
struct BH_Material;
struct BH_Entity;
struct BH_NodeBrushes;

/* -----------------------------------------------------------------------------
   Scene Structures
   ----------------------------------------------------------------------------- */

typedef struct BH_Transform
{
    vec3 position;
    quat rotation;
    vec3 scale;
} BH_Transform;

typedef struct BH_SceneNode
{
    const char *name;

    /* Hierarchy */
    struct BH_SceneNode *parent;
    struct BH_SceneNode *first_child;
    struct BH_SceneNode *next_sibling;

    /* Interpolation state */
    BH_Transform local_prev;
    BH_Transform local_curr;

    /* Visuals */
    struct BH_Mesh *mesh;
    const struct BH_Material *material_override;

    /* Gameplay / Logic */
    struct BH_Entity *entity;
    struct BH_NodeBrushes *brushes;

    /* Spatial Bounds */
    bool has_world_bounds;
    vec3 world_mins;
    vec3 world_maxs;

    /* Serialization */
    int32_t map_uid;
} BH_SceneNode;

typedef struct BH_Scene
{
    BH_SceneNode *root;
    struct BH_Entity *entities; /* Intrusive list */
    uint32_t fixed_tick;

    /* Global Lighting */
    struct BH_DirectionalLight
    {
        vec3 direction; /* Normalized direction TO light */
        vec3 color;     /* Linear RGB */
        float intensity;
    } directional_light;

    bool has_directional_light;
} BH_Scene;

/* -----------------------------------------------------------------------------
   Transform API
   ----------------------------------------------------------------------------- */

BH_Transform BH_Transform_GetIdentity(void);
BH_Transform BH_Transform_Lerp(const BH_Transform *a, const BH_Transform *b, float t);
mat4 BH_Transform_GetMat4(const BH_Transform *t);

/* -----------------------------------------------------------------------------
   Scene API
   ----------------------------------------------------------------------------- */

bool BH_Scene_Init(BH_Scene *scene, BH_Arena *permanent_arena);
BH_SceneNode *BH_Scene_CreateNode(BH_Arena *permanent_arena, const char *name);

void BH_Scene_AddChild(BH_SceneNode *parent, BH_SceneNode *child);
void BH_Scene_SetNodeLocalTransform(BH_SceneNode *node, BH_Transform prev, BH_Transform curr);

/* -----------------------------------------------------------------------------
   Traversal
   ----------------------------------------------------------------------------- */

typedef void (*BH_SceneVisitFn)(const BH_SceneNode *node, const mat4 *world_matrix, void *user);

void BH_Scene_Traverse(const BH_Scene *scene, float alpha, BH_SceneVisitFn fn, void *user);