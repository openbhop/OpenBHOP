/* -----------------------------------------------------------------------------
   bh_node_brushes.c
   ----------------------------------------------------------------------------- */
#include "bh_node_brushes.h"
#include "bh_scene.h"
#include <math.h>

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static BH_FORCEINLINE vec3 bh_vec3_min3(vec3 a, vec3 b)
{
    return (vec3){.x = (float)fmin((double)a.x, (double)b.x),
                  .y = (float)fmin((double)a.y, (double)b.y),
                  .z = (float)fmin((double)a.z, (double)b.z)};
}

static BH_FORCEINLINE vec3 bh_vec3_max3(vec3 a, vec3 b)
{
    return (vec3){.x = (float)fmax((double)a.x, (double)b.x),
                  .y = (float)fmax((double)a.y, (double)b.y),
                  .z = (float)fmax((double)a.z, (double)b.z)};
}

static BH_NodeBrushes *bh_node_brushes_ensure(BH_SceneNode *node, BH_Arena *arena)
{
    if (node->brushes)
    {
        return node->brushes;
    }

    BH_NodeBrushes *nb = (BH_NodeBrushes *)BH_Arena_Alloc(arena, sizeof(BH_NodeBrushes), 8);
    if (nb)
    {
        *nb = (BH_NodeBrushes){0};
        node->brushes = nb;
    }
    return nb;
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_NodeBrushes_AttachBrush(BH_SceneNode *node, BH_BrushGeom *brush, BH_Arena *arena)
{
    if (!node || !brush || !arena)
    {
        return false;
    }

    BH_NodeBrushes *nb = bh_node_brushes_ensure(node, arena);
    if (!nb)
    {
        return false;
    }

    BH_NodeBrushLink *link = (BH_NodeBrushLink *)BH_Arena_Alloc(arena, sizeof(BH_NodeBrushLink), 8);
    if (!link)
    {
        return false;
    }

    link->brush = brush;
    link->next = nb->first;
    nb->first = link;
    nb->count++;

    if (!nb->has_bounds)
    {
        nb->has_bounds = true;
        nb->mins = brush->mins;
        nb->maxs = brush->maxs;
    }
    else
    {
        nb->mins = bh_vec3_min3(nb->mins, brush->mins);
        nb->maxs = bh_vec3_max3(nb->maxs, brush->maxs);
    }

    if (!node->has_world_bounds)
    {
        node->has_world_bounds = true;
        node->world_mins = brush->mins;
        node->world_maxs = brush->maxs;
    }
    else
    {
        node->world_mins = bh_vec3_min3(node->world_mins, brush->mins);
        node->world_maxs = bh_vec3_max3(node->world_maxs, brush->maxs);
    }

    return true;
}

const BH_BrushGeom *BH_NodeBrushes_FindByUID(const BH_NodeBrushes *nb, int32_t brush_uid)
{
    if (!nb)
    {
        return NULL;
    }

    for (const BH_NodeBrushLink *l = nb->first; l; l = l->next)
    {
        if (l->brush && l->brush->brush_uid == brush_uid)
        {
            return l->brush;
        }
    }
    return NULL;
}

int BH_BrushGeom_AddToPhysics(BH_BrushGeom *b, BH_PhysicsWorld *world, BH_Contents contents, bool add_bevels_optional)
{
    if (!b || !world || !b->planes || b->plane_count == 0)
    {
        return -1;
    }

    if (b->physics_registered && b->physics_brush_index >= 0)
    {
        return b->physics_brush_index;
    }

    const int idx =
        BH_PhysicsAddBrushConvex(world, b->planes, b->plane_count, contents, b->brush_uid, add_bevels_optional);

    if (idx >= 0)
    {
        b->physics_brush_index = idx;
        b->physics_contents = contents;
        b->physics_registered = true;
    }

    return idx;
}