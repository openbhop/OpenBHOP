/* -----------------------------------------------------------------------------
   bh_entity.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../core/bh_arena.h"
#include "../core/bh_core.h"
#include "../core/bh_parse.h"
#include "../input/bh_input.h"
#include "../physics/bh_physics.h"
#include "../render/bh_gpu.h"

struct BH_Scene;
struct BH_SceneNode;
struct BH_Material;
struct BH_MaterialManager;
struct BH_TextureManager;
struct BH_PhysicsWorld;
struct BH_UI;

/* -----------------------------------------------------------------------------
   Entity Context & Services
   ----------------------------------------------------------------------------- */

typedef struct BH_EntityKV
{
    const char *key;
    const char *value;
} BH_EntityKV;

typedef struct BH_EntityServices
{
    BH_Arena *permanent_arena;          /* Persistent entity memory */
    BH_GPUDevice *gpu_device;           /* Mesh creation context */
    struct BH_TextureManager *textures; /* Runtime resources */
    struct BH_MaterialManager *materials;
    struct BH_Scene *scene;          /* Global scene state */
    struct BH_UI *ui;                /* Optional UI system */
    void *user;                      /* User-data pass-through */
    struct BH_PhysicsWorld *physics; /* Collision world */
    BH_InputState *input;            /* Raw input state */
} BH_EntityServices;

/* -----------------------------------------------------------------------------
   Solidity Configuration (Source-Compatible)
   ----------------------------------------------------------------------------- */

typedef enum SolidType_t
{
    SOLID_NONE = 0,
    SOLID_BSP = 1,      /* Convex brush set */
    SOLID_BBOX = 2,     /* Axis-aligned box */
    SOLID_OBB = 3,      /* Oriented box (Unimplemented) */
    SOLID_OBB_YAW = 4,  /* Yaw-only OBB (Unimplemented) */
    SOLID_CUSTOM = 5,   /* Callback-based (Unimplemented) */
    SOLID_VPHYSICS = 6, /* Physics engine (Unimplemented) */
    SOLID_LAST,
} SolidType_t;

typedef enum SolidFlags_t
{
    FSOLID_CUSTOMRAYTEST = 0x0001,
    FSOLID_CUSTOMBOXTEST = 0x0002,
    FSOLID_NOT_SOLID = 0x0004,
    FSOLID_TRIGGER = 0x0008,
    FSOLID_NOT_STANDABLE = 0x0010,
    FSOLID_VOLUME_CONTENTS = 0x0020,
    FSOLID_FORCE_WORLD_ALIGNED = 0x0040,
    FSOLID_USE_TRIGGER_BOUNDS = 0x0080,
    FSOLID_ROOT_PARENT_ALIGNED = 0x0100,
    FSOLID_TRIGGER_TOUCH_DEBRIS = 0x0200,

    FSOLID_MAX_BITS = 10,
} SolidFlags_t;

/* -----------------------------------------------------------------------------
   Trigger State
   ----------------------------------------------------------------------------- */

typedef struct BH_TouchLink
{
    struct BH_Entity *other;
    uint32_t touch_stamp;
} BH_TouchLink;

typedef struct BH_TouchList
{
    BH_TouchLink *links;
    uint32_t count;
    uint32_t cap;
} BH_TouchList;

/* -----------------------------------------------------------------------------
   Entity Definition
   ----------------------------------------------------------------------------- */

typedef struct BH_Entity BH_Entity;

typedef struct BH_EntityVTable
{
    const char *type_name;

    void (*awake)(BH_Entity *e, const BH_EntityServices *sv);
    void (*fixed_update)(BH_Entity *e, const BH_EntityServices *sv, const BH_Intent *intent, float dt);
    void (*destroy)(BH_Entity *e, const BH_EntityServices *sv);
    void (*update)(BH_Entity *e, const BH_EntityServices *sv, float dt);

    vec3 (*get_view_offset)(const BH_Entity *e, const BH_EntityServices *sv);
    bool (*get_world_aabb)(const BH_Entity *e, const BH_EntityServices *sv, vec3 *out_mins, vec3 *out_maxs);

    /* Trigger callbacks */
    bool (*passes_trigger_filters)(BH_Entity *trigger, const BH_EntityServices *sv, BH_Entity *other);
    void (*start_touch)(BH_Entity *trigger, const BH_EntityServices *sv, BH_Entity *other);
    void (*touch)(BH_Entity *trigger, const BH_EntityServices *sv, BH_Entity *other);
    void (*end_touch)(BH_Entity *trigger, const BH_EntityServices *sv, BH_Entity *other);
} BH_EntityVTable;

struct BH_Entity
{
    const BH_EntityVTable *vt;
    struct BH_SceneNode *node;

    BH_EntityKV *kvs;
    uint32_t kv_count;

    SolidType_t solid_type;
    uint32_t solid_flags;
    BH_Contents solid_contents;

    BH_TouchList touching;
    bool awoken;

    BH_Entity *next; /* Intrusive scene list */
};

/* -----------------------------------------------------------------------------
   Public API: Lifecycle
   ----------------------------------------------------------------------------- */

void BH_Entity_Attach(struct BH_Scene *scene, BH_Entity *entity, struct BH_SceneNode *node);
BH_Entity *BH_Entity_Alloc(BH_Arena *arena, size_t size, const BH_EntityVTable *vt);
vec3 BH_Entity_GetViewOffset(const BH_Entity *e, const BH_EntityServices *sv);

void BH_Entity_AwakeAll(struct BH_Scene *scene, const BH_EntityServices *sv);
void BH_Entity_FixedUpdateAll(struct BH_Scene *scene, const BH_EntityServices *sv, const BH_Intent *intent, float dt);
void BH_Entity_UpdateAll(struct BH_Scene *scene, const BH_EntityServices *sv, float dt);
void BH_Entity_DestroyAll(struct BH_Scene *scene, const BH_EntityServices *sv);

/* -----------------------------------------------------------------------------
   Public API: Solidity
   ----------------------------------------------------------------------------- */

void BH_Entity_SetSolid(BH_Entity *e, SolidType_t solid);
SolidType_t BH_Entity_GetSolid(const BH_Entity *e);

void BH_Entity_SetSolidFlags(BH_Entity *e, uint32_t solid_flags);
void BH_Entity_AddSolidFlags(BH_Entity *e, uint32_t solid_flags);
void BH_Entity_RemoveSolidFlags(BH_Entity *e, uint32_t solid_flags);
uint32_t BH_Entity_GetSolidFlags(const BH_Entity *e);

void BH_Entity_SetSolidContents(BH_Entity *e, BH_Contents contents);
BH_Contents BH_Entity_GetSolidContents(const BH_Entity *e);

bool BH_Entity_GetWorldAABB(const BH_Entity *e, const BH_EntityServices *sv, vec3 *out_mins, vec3 *out_maxs);

/* -----------------------------------------------------------------------------
   Public API: Key/Value Access
   ----------------------------------------------------------------------------- */

const char *BH_Entity_KVGetString(const BH_Entity *e, const char *key, const char *fallback);
float BH_Entity_KVGetFloat32(const BH_Entity *e, const char *key, float fallback);
int32_t BH_Entity_KVGetInt32(const BH_Entity *e, const char *key, int32_t fallback);
vec3 BH_Entity_KVGetVec3(const BH_Entity *e, const char *key, vec3 fallback);
color4f BH_Entity_KVGetColor(const BH_Entity *e, const char *key, color4f fallback);