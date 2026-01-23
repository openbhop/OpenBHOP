/* -----------------------------------------------------------------------------
   bh_entity.c
   ----------------------------------------------------------------------------- */

#include "bh_entity.h"

#include "../scene/bh_node_brushes.h"
#include "../scene/bh_scene.h"

#include <math.h>
#include <string.h>

/* -----------------------------------------------------------------------------
    Public API: Configuration
    ----------------------------------------------------------------------------- */

void BH_Entity_SetSolid(BH_Entity *e, SolidType_t solid)
{
    if (e)
    {
        e->solid_type = solid;
    }
}

SolidType_t BH_Entity_GetSolid(const BH_Entity *e)
{
    return e ? e->solid_type : SOLID_NONE;
}

void BH_Entity_SetSolidFlags(BH_Entity *e, uint32_t solid_flags)
{
    if (e)
    {
        e->solid_flags = solid_flags;
    }
}

void BH_Entity_AddSolidFlags(BH_Entity *e, uint32_t solid_flags)
{
    if (e)
    {
        e->solid_flags |= solid_flags;
    }
}

void BH_Entity_RemoveSolidFlags(BH_Entity *e, uint32_t solid_flags)
{
    if (e)
    {
        e->solid_flags &= ~solid_flags;
    }
}

uint32_t BH_Entity_GetSolidFlags(const BH_Entity *e)
{
    return e ? e->solid_flags : 0u;
}

void BH_Entity_SetSolidContents(BH_Entity *e, BH_Contents contents)
{
    if (e)
    {
        e->solid_contents = contents;
    }
}

BH_Contents BH_Entity_GetSolidContents(const BH_Entity *e)
{
    return e ? e->solid_contents : (BH_Contents)0;
}

bool BH_Entity_GetWorldAABB(const BH_Entity *e, const BH_EntityServices *sv, vec3 *out_mins, vec3 *out_maxs)
{
    const vec3 zero = {0};
    if (out_mins)
        *out_mins = zero;
    if (out_maxs)
        *out_maxs = zero;

    if (!e || !out_mins || !out_maxs)
    {
        return false;
    }

    if (e->vt && e->vt->get_world_aabb)
    {
        if (e->vt->get_world_aabb(e, sv, out_mins, out_maxs))
        {
            return true;
        }
    }

    if (e->node && e->node->has_world_bounds)
    {
        *out_mins = e->node->world_mins;
        *out_maxs = e->node->world_maxs;
        return true;
    }

    /* Fallback: Point AABB at node origin */
    if (e->node)
    {
        const vec3 p = e->node->local_curr.position;
        *out_mins = p;
        *out_maxs = p;
        return true;
    }

    return false;
}

/* -----------------------------------------------------------------------------
   Internal: Intersection Math
   ----------------------------------------------------------------------------- */

static BH_FORCEINLINE bool bh_aabb_intersects(vec3 a_mins, vec3 a_maxs, vec3 b_mins, vec3 b_maxs)
{
    return (a_mins.x <= b_maxs.x && a_maxs.x >= b_mins.x) && (a_mins.y <= b_maxs.y && a_maxs.y >= b_mins.y) &&
           (a_mins.z <= b_maxs.z && a_maxs.z >= b_mins.z);
}

static BH_FORCEINLINE vec3 bh_aabb_center(vec3 mins, vec3 maxs)
{
    return (vec3){
        (mins.x + maxs.x) * 0.5f,
        (mins.y + maxs.y) * 0.5f,
        (mins.z + maxs.z) * 0.5f,
    };
}

static BH_FORCEINLINE vec3 bh_aabb_half_extents(vec3 mins, vec3 maxs)
{
    return (vec3){
        (maxs.x - mins.x) * 0.5f,
        (maxs.y - mins.y) * 0.5f,
        (maxs.z - mins.z) * 0.5f,
    };
}

/*
   Conservative AABB-vs-convex-brush overlap test (half-space form).
   Brush planes define the inside as: dot(n, X) <= dist.
   An AABB intersects the brush iff it is not fully outside any plane.
*/
static bool bh_aabb_intersects_convex_brush(vec3 aabb_mins, vec3 aabb_maxs, const BH_BrushGeom *brush)
{
    if (!brush || !brush->planes || brush->plane_count == 0)
    {
        return false;
    }

    const vec3 c = bh_aabb_center(aabb_mins, aabb_maxs);
    const vec3 he = bh_aabb_half_extents(aabb_mins, aabb_maxs);

    for (uint32_t i = 0; i < brush->plane_count; ++i)
    {
        const BH_TracePlane p = brush->planes[i];
        const vec3 n = p.normal;

        const float r = fabsf(n.x) * he.x + fabsf(n.y) * he.y + fabsf(n.z) * he.z;
        const float s = n.x * c.x + n.y * c.y + n.z * c.z;

        /* Minimum dot(n, X) over the AABB. If > dist, AABB is fully outside. */
        if ((s - r) > p.dist)
        {
            return false;
        }
    }

    return true;
}

/* -----------------------------------------------------------------------------
   Internal: Trigger Logic
   ----------------------------------------------------------------------------- */

static bool bh_trigger_passes_filters(BH_Entity *trigger, const BH_EntityServices *sv, BH_Entity *other)
{
    if (trigger->vt && trigger->vt->passes_trigger_filters)
    {
        return trigger->vt->passes_trigger_filters(trigger, sv, other);
    }
    return true;
}

static int bh_touchlist_find(const BH_TouchList *tl, const BH_Entity *other)
{
    if (!tl->links)
    {
        return -1;
    }

    for (uint32_t i = 0; i < tl->count; ++i)
    {
        if (tl->links[i].other == other)
        {
            return (int)i;
        }
    }
    return -1;
}

static bool bh_touchlist_reserve(BH_TouchList *tl, BH_Arena *arena, uint32_t want_cap)
{
    if (want_cap <= tl->cap)
    {
        return true;
    }

    uint32_t new_cap = tl->cap ? tl->cap : 8u;
    while (new_cap < want_cap)
    {
        new_cap *= 2u;
    }

    BH_TouchLink *new_links = (BH_TouchLink *)BH_Arena_Alloc(arena, (size_t)new_cap * sizeof(BH_TouchLink), 8);
    if (!new_links)
    {
        return false;
    }

    if (tl->links && tl->count > 0)
    {
        memcpy(new_links, tl->links, (size_t)tl->count * sizeof(BH_TouchLink));
    }

    tl->links = new_links;
    tl->cap = new_cap;
    return true;
}

static void bh_trigger_call_touch_cb(BH_Entity *trigger, const BH_EntityServices *sv, BH_Entity *other, int type)
{
    if (!trigger || !trigger->vt)
        return;

    /* 0: Start, 1: Touch, 2: End */
    if (type == 0 && trigger->vt->start_touch)
        trigger->vt->start_touch(trigger, sv, other);
    else if (type == 1 && trigger->vt->touch)
        trigger->vt->touch(trigger, sv, other);
    else if (type == 2 && trigger->vt->end_touch)
        trigger->vt->end_touch(trigger, sv, other);
}

static void bh_trigger_mark_touch(BH_Entity *trigger, const BH_EntityServices *sv, BH_Entity *other, uint32_t stamp)
{
    BH_TouchList *tl = &trigger->touching;
    const int existing_idx = bh_touchlist_find(tl, other);

    if (existing_idx >= 0)
    {
        tl->links[existing_idx].touch_stamp = stamp;
        bh_trigger_call_touch_cb(trigger, sv, other, 1);
        return;
    }

    /* New overlap: Filter check required before acceptance */
    if (!bh_trigger_passes_filters(trigger, sv, other))
    {
        return;
    }

    if (!sv || !sv->permanent_arena)
    {
        return;
    }

    if (!bh_touchlist_reserve(tl, sv->permanent_arena, tl->count + 1u))
    {
        return;
    }

    tl->links[tl->count++] = (BH_TouchLink){.other = other, .touch_stamp = stamp};
    bh_trigger_call_touch_cb(trigger, sv, other, 0);
}

static void bh_trigger_prune_untouched(BH_Entity *trigger, const BH_EntityServices *sv, uint32_t stamp)
{
    BH_TouchList *tl = &trigger->touching;
    uint32_t i = 0;

    while (i < tl->count)
    {
        BH_TouchLink *link = &tl->links[i];
        if (link->touch_stamp == stamp)
        {
            ++i;
            continue;
        }

        /* Stale: EndTouch and swap-remove */
        BH_Entity *other = link->other;
        bh_trigger_call_touch_cb(trigger, sv, other, 2);

        tl->links[i] = tl->links[tl->count - 1u];
        tl->count--;
    }
}

static bool bh_trigger_overlaps_entity(const BH_Entity *trigger, const BH_EntityServices *sv, const BH_Entity *other)
{
    if (!trigger->node)
    {
        return false;
    }

    vec3 other_mins, other_maxs;
    if (!BH_Entity_GetWorldAABB(other, sv, &other_mins, &other_maxs))
    {
        return false;
    }

    /* Fast reject using trigger union bounds */
    if (trigger->node->has_world_bounds)
    {
        if (!bh_aabb_intersects(other_mins, other_maxs, trigger->node->world_mins, trigger->node->world_maxs))
        {
            return false;
        }
    }

    if (trigger->solid_type == SOLID_BBOX)
    {
        vec3 t_mins, t_maxs;
        if (!BH_Entity_GetWorldAABB(trigger, sv, &t_mins, &t_maxs))
        {
            return false;
        }
        return bh_aabb_intersects(other_mins, other_maxs, t_mins, t_maxs);
    }

    if (trigger->solid_type == SOLID_BSP)
    {
        const BH_NodeBrushes *nb = trigger->node->brushes;
        if (!nb || nb->count == 0)
        {
            return false;
        }

        /* Treat SOLID_BSP as union of convex brushes */
        for (const BH_NodeBrushLink *l = nb->first; l; l = l->next)
        {
            const BH_BrushGeom *b = l->brush;
            if (!b)
                continue;

            if (!bh_aabb_intersects(other_mins, other_maxs, b->mins, b->maxs))
            {
                continue;
            }

            if (bh_aabb_intersects_convex_brush(other_mins, other_maxs, b))
            {
                return true;
            }
        }
    }

    return false;
}

static void bh_entity_touch_triggers(BH_Scene *scene, const BH_EntityServices *sv, uint32_t stamp)
{
    for (BH_Entity *trigger = scene->entities; trigger; trigger = trigger->next)
    {
        if (!trigger->node || (trigger->solid_flags & FSOLID_TRIGGER) == 0u)
        {
            continue;
        }

        if (trigger->solid_type != SOLID_BSP && trigger->solid_type != SOLID_BBOX)
        {
            continue;
        }

        for (BH_Entity *other = scene->entities; other; other = other->next)
        {
            if (!other || other == trigger)
            {
                continue;
            }

            /* Rules: Ignore non-solids and other triggers to reduce noise */
            if ((other->solid_flags & FSOLID_NOT_SOLID) != 0u)
                continue;
            if ((other->solid_flags & FSOLID_TRIGGER) != 0u)
                continue;

            if (bh_trigger_overlaps_entity(trigger, sv, other))
            {
                bh_trigger_mark_touch(trigger, sv, other, stamp);
            }
        }

        bh_trigger_prune_untouched(trigger, sv, stamp);
    }
}

/* -----------------------------------------------------------------------------
   Public API: Lifecycle
   ----------------------------------------------------------------------------- */

void BH_Entity_Attach(BH_Scene *scene, BH_Entity *entity, BH_SceneNode *node)
{
    if (!scene || !entity || !node || node->entity)
    {
        return;
    }

    entity->node = node;
    node->entity = entity;

    /* Push front */
    entity->next = scene->entities;
    scene->entities = entity;
}

BH_Entity *BH_Entity_Alloc(BH_Arena *arena, size_t size, const BH_EntityVTable *vt)
{
    if (!arena || !vt || size < sizeof(BH_Entity))
    {
        return NULL;
    }

    BH_Entity *e = (BH_Entity *)BH_Arena_Alloc(arena, size, 8);
    if (e)
    {
        memset(e, 0, size);
        e->vt = vt;
    }
    return e;
}

vec3 BH_Entity_GetViewOffset(const BH_Entity *e, const BH_EntityServices *sv)
{
    if (e && e->vt && e->vt->get_view_offset)
    {
        return e->vt->get_view_offset(e, sv);
    }
    return (vec3){0, 0, 0};
}

static void bh_entity_try_awake(BH_Entity *e, const BH_EntityServices *sv)
{
    if (!e || e->awoken)
    {
        return;
    }

    e->awoken = true;

    /* Map KV overrides: Translate 'contents' KV to solidity defaults */
    if (e->solid_type == SOLID_NONE && e->node && e->node->brushes)
    {
        const int32_t contents_i = BH_Entity_KVGetInt32(e, "contents", 0);
        if (contents_i != 0)
        {
            e->solid_type = SOLID_BSP;
            e->solid_contents = (BH_Contents)contents_i;
        }
    }

    if (e->vt && e->vt->awake)
    {
        e->vt->awake(e, sv);
    }

    /* Finalize Physics: Register brushes if entity confirmed as SOLID_BSP */
    if (sv && sv->physics && e->node && e->node->brushes)
    {
        if (e->solid_type == SOLID_BSP && (e->solid_flags & FSOLID_NOT_SOLID) == 0u && e->solid_contents != 0u)
        {
            for (BH_NodeBrushLink *l = e->node->brushes->first; l; l = l->next)
            {
                if (l->brush)
                {
                    (void)BH_BrushGeom_AddToPhysics(l->brush, sv->physics, e->solid_contents, true);
                }
            }
        }
    }
}

void BH_Entity_AwakeAll(BH_Scene *scene, const BH_EntityServices *sv)
{
    if (!scene)
        return;
    for (BH_Entity *e = scene->entities; e; e = e->next)
    {
        bh_entity_try_awake(e, sv);
    }
}

void BH_Entity_FixedUpdateAll(BH_Scene *scene, const BH_EntityServices *sv, const BH_Intent *intent, float dt)
{
    if (!scene || !intent)
        return;
    if (dt < 0.0f)
        dt = 0.0f;

    scene->fixed_tick++;

    for (BH_Entity *e = scene->entities; e; e = e->next)
    {
        bh_entity_try_awake(e, sv);

        /* Interpolation Contract: prev = last fixed state */
        if (e->node)
        {
            e->node->local_prev = e->node->local_curr;
        }

        if (e->vt && e->vt->fixed_update)
        {
            e->vt->fixed_update(e, sv, intent, dt);
        }
    }

    bh_entity_touch_triggers(scene, sv, scene->fixed_tick);
}

void BH_Entity_UpdateAll(BH_Scene *scene, const BH_EntityServices *sv, float dt)
{
    if (!scene)
        return;
    if (dt < 0.0f)
        dt = 0.0f;

    for (BH_Entity *e = scene->entities; e; e = e->next)
    {
        bh_entity_try_awake(e, sv);
        if (e->vt && e->vt->update)
        {
            e->vt->update(e, sv, dt);
        }
    }
}

void BH_Entity_DestroyAll(BH_Scene *scene, const BH_EntityServices *sv)
{
    if (!scene)
        return;

    for (BH_Entity *e = scene->entities; e; e = e->next)
    {
        if (e->vt && e->vt->destroy)
        {
            e->vt->destroy(e, sv);
        }
        if (e->node && e->node->entity == e)
        {
            e->node->entity = NULL;
        }
    }
    scene->entities = NULL;
}

/* -----------------------------------------------------------------------------
   Public API: KV Access
   ----------------------------------------------------------------------------- */

static const char *bh_entity_kv_find(const BH_Entity *e, const char *key)
{
    if (!e || !key || !key[0] || !e->kvs)
    {
        return NULL;
    }

    for (uint32_t i = 0; i < e->kv_count; ++i)
    {
        const BH_EntityKV *kv = &e->kvs[i];
        if (kv->key && strcmp(kv->key, key) == 0)
        {
            return kv->value;
        }
    }
    return NULL;
}

const char *BH_Entity_KVGetString(const BH_Entity *e, const char *key, const char *fallback)
{
    const char *v = bh_entity_kv_find(e, key);
    return (v && v[0]) ? v : fallback;
}

float BH_Entity_KVGetFloat32(const BH_Entity *e, const char *key, float fallback)
{
    const char *s = bh_entity_kv_find(e, key);
    return BH_Parse_Float32(s, fallback);
}

int32_t BH_Entity_KVGetInt32(const BH_Entity *e, const char *key, int32_t fallback)
{
    const char *s = bh_entity_kv_find(e, key);
    return BH_Parse_Int32(s, fallback);
}

vec3 BH_Entity_KVGetVec3(const BH_Entity *e, const char *key, vec3 fallback)
{
    const char *s = bh_entity_kv_find(e, key);
    return BH_Parse_Vec3(s, fallback);
}

color4f BH_Entity_KVGetColor(const BH_Entity *e, const char *key, color4f fallback)
{
    const char *s = bh_entity_kv_find(e, key);
    return BH_Parse_Color(s, fallback);
}