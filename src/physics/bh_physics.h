/* -----------------------------------------------------------------------------
   bh_physics.h
   ----------------------------------------------------------------------------- */
#pragma once

#include "../core/bh_arena.h"
#include "../core/bh_core.h"
#include "../math/bh_math.h"

/*
  bh_physics

  Quake/Source-style collision system.
  - Convex brushes defined by planes.
  - Ray/AABB sweeping against brushes.
  - Valve-compatible bevel generation and clipping logic.
*/

typedef uint32_t BH_Contents;

/*
  Source Engine contents flags.
  Values preserved for movement code portability and world filtering parity.
*/
enum
{
    CONTENTS_EMPTY = 0,
    CONTENTS_SOLID = 0x1,
    CONTENTS_WINDOW = 0x2,
    CONTENTS_AUX = 0x4,
    CONTENTS_GRATE = 0x8,
    CONTENTS_SLIME = 0x10,
    CONTENTS_WATER = 0x20,
    CONTENTS_BLOCKLOS = 0x40,
    CONTENTS_OPAQUE = 0x80,
    LAST_VISIBLE_CONTENTS = CONTENTS_OPAQUE,
    ALL_VISIBLE_CONTENTS = (LAST_VISIBLE_CONTENTS | (LAST_VISIBLE_CONTENTS - 1)),
    CONTENTS_TESTFOGVOLUME = 0x100,
    CONTENTS_UNUSED = 0x200,
    CONTENTS_BLOCKLIGHT = 0x400,
    CONTENTS_TEAM1 = 0x800,
    CONTENTS_TEAM2 = 0x1000,
    CONTENTS_IGNORE_NODRAW_OPAQUE = 0x2000,
    CONTENTS_MOVEABLE = 0x4000,
    CONTENTS_AREAPORTAL = 0x8000,
    CONTENTS_PLAYERCLIP = 0x10000,
    CONTENTS_MONSTERCLIP = 0x20000,
    CONTENTS_CURRENT_0 = 0x40000,
    CONTENTS_CURRENT_90 = 0x80000,
    CONTENTS_CURRENT_180 = 0x100000,
    CONTENTS_CURRENT_270 = 0x200000,
    CONTENTS_CURRENT_UP = 0x400000,
    CONTENTS_CURRENT_DOWN = 0x800000,
    CONTENTS_ORIGIN = 0x1000000,
    CONTENTS_MONSTER = 0x2000000,
    CONTENTS_DEBRIS = 0x4000000,
    CONTENTS_DETAIL = 0x8000000,
    CONTENTS_TRANSLUCENT = 0x10000000,
    CONTENTS_LADDER = 0x20000000,
    CONTENTS_HITBOX = 0x40000000,
};

enum
{
    MASK_ALL = 0xFFFFFFFFu,
    MASK_SOLID = (CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_WINDOW | CONTENTS_MONSTER | CONTENTS_GRATE),
    MASK_PLAYERSOLID = (CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_PLAYERCLIP | CONTENTS_WINDOW | CONTENTS_MONSTER |
                        CONTENTS_GRATE),
    MASK_NPCSOLID = (CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_MONSTERCLIP | CONTENTS_WINDOW | CONTENTS_MONSTER |
                     CONTENTS_GRATE),
    MASK_WATER = (CONTENTS_WATER | CONTENTS_MOVEABLE | CONTENTS_SLIME),
    MASK_OPAQUE = (CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_OPAQUE),
    MASK_OPAQUE_AND_NPCS = (MASK_OPAQUE | CONTENTS_MONSTER),
    MASK_BLOCKLOS = (CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_BLOCKLOS),
    MASK_BLOCKLOS_AND_NPCS = (MASK_BLOCKLOS | CONTENTS_MONSTER),
    MASK_VISIBLE = (MASK_OPAQUE | CONTENTS_IGNORE_NODRAW_OPAQUE),
    MASK_VISIBLE_AND_NPCS = (MASK_OPAQUE_AND_NPCS | CONTENTS_IGNORE_NODRAW_OPAQUE),
    MASK_SHOT =
        (CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_MONSTER | CONTENTS_WINDOW | CONTENTS_DEBRIS | CONTENTS_HITBOX),
    MASK_SHOT_HULL =
        (CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_MONSTER | CONTENTS_WINDOW | CONTENTS_DEBRIS | CONTENTS_GRATE),
    MASK_SHOT_PORTAL = (CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_WINDOW),
    MASK_SOLID_BRUSHONLY = (CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_WINDOW | CONTENTS_GRATE),
    MASK_PLAYERSOLID_BRUSHONLY =
        (CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_WINDOW | CONTENTS_PLAYERCLIP | CONTENTS_GRATE),
    MASK_NPCSOLID_BRUSHONLY =
        (CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_WINDOW | CONTENTS_MONSTERCLIP | CONTENTS_GRATE),
    MASK_NPCWORLDSTATIC = (CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_MONSTERCLIP | CONTENTS_GRATE),
    MASK_SPLITAREAPORTAL = (CONTENTS_WATER | CONTENTS_SLIME),
};

/* Legacy aliases */
enum
{
    BH_CONTENTS_EMPTY = CONTENTS_EMPTY,
    BH_CONTENTS_SOLID = CONTENTS_SOLID,
    BH_CONTENTS_PLAYERCLIP = CONTENTS_PLAYERCLIP,
    BH_CONTENTS_LADDER = CONTENTS_LADDER,
    BH_MASK_SOLID = MASK_SOLID,
    BH_MASK_PLAYERSOLID = MASK_PLAYERSOLID,
};

typedef struct BH_TracePlane
{
    vec3 normal;
    float dist;
    int type; /* 0..2 axial, 3 non-axial */
} BH_TracePlane;

typedef struct BH_TraceResult
{
    float fraction; /* 0..1 */
    vec3 startpos;
    vec3 endpos;

    bool startsolid;
    bool allsolid;

    BH_TracePlane plane; /* Valid if fraction < 1 */

    BH_Contents contents;
    int surface_id;
    int brush_index; /* Index in world brush array, or -1 */
} BH_TraceResult;

#define BH_PHYS_MAX_SIDES 64
#define BH_PHYS_DEFAULT_BRUSH_CAP 64

typedef struct BH_Winding
{
    int numpoints;
    vec3 p[64];
} BH_Winding;

typedef struct BH_PhysicsBrushSide
{
    BH_TracePlane plane;
    int surface_id;
    bool bevel;
    BH_Winding *winding; /* Used only during bevel generation */
} BH_PhysicsBrushSide;

typedef struct BH_PhysicsBrush
{
    BH_PhysicsBrushSide sides[BH_PHYS_MAX_SIDES];
    int numsides;
    BH_Contents contents;
    vec3 mins; /* World space AABB */
    vec3 maxs;
} BH_PhysicsBrush;

typedef struct BH_PhysicsWorld
{
    BH_Arena *arena;
    BH_PhysicsBrush *brushes;
    uint32_t brush_count;
    uint32_t brush_cap;
} BH_PhysicsWorld;

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

/* Initializes world with arena-allocated storage. */
bool BH_Physics_Init(BH_PhysicsWorld *world, BH_Arena *arena, uint32_t initial_brush_capacity);

/* Clears brushes, retains storage capacity. */
void BH_Physics_Reset(BH_PhysicsWorld *world);

/* Adds AABB brush. Returns brush index or -1 on failure. */
int BH_Physics_AddBrushAABB(BH_PhysicsWorld *world, vec3 mins, vec3 maxs, BH_Contents contents, int surface_id,
                            bool add_bevels_optional);

/*
   Adds convex brush from planes.
   plane_convention: dot(normal, X) <= dist -> inside
   add_bevels_optional: Generates bevels for proper box-sweeping on non-axial geometry.
*/
int BH_PhysicsAddBrushConvex(BH_PhysicsWorld *world, const BH_TracePlane *planes, uint32_t plane_count,
                             BH_Contents contents, int surface_id, bool add_bevels_optional);

/*
   Sweeps AABB (mins/maxs) from start to end against world brushes.
   Pass mins=maxs=(0,0,0) for raycast.
*/
void BH_Physics_Trace(const BH_PhysicsWorld *world, vec3 start, vec3 end, vec3 mins, vec3 maxs, BH_Contents mask,
                      int ignore_brush_index, BH_TraceResult *out);

void BH_Physics_DebugDraw(const BH_PhysicsWorld *world, float duration_s);