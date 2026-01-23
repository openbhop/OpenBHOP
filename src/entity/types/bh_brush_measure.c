#include "bh_brush_measure.h"

#include "../../scene/bh_scene.h"

#include "../../scene/bh_node_brushes.h"

#include "../../debug/bh_debug_draw.h"

#include <SDL3/SDL.h>

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

static void bh_brush_measure_awake(BH_Entity *e, const BH_EntityServices *sv)
{
    if (!e || !e->node || !sv)
    {
        return;
    }

    BH_BrushMeasure *bm = (BH_BrushMeasure *)e;
    (void)bm;

    /*
      Example Source-style trigger setup:

        SetSolid( SOLID_BSP );
        AddSolidFlags( FSOLID_NOT_SOLID );
        AddSolidFlags( FSOLID_TRIGGER );

      In WR, SOLID_BSP means "use attached convex brushes".
    */
    BH_Entity_SetSolid(e, SOLID_BSP);
    BH_Entity_AddSolidFlags(e, FSOLID_NOT_SOLID | FSOLID_TRIGGER);

    if (!e->node->brushes || e->node->brushes->count == 0)
    {
        SDL_Log("[bh] brush_measure: node '%s' has no attached brushes", e->node->name ? e->node->name : "(null)");
    }
    else
    {
        SDL_Log("[bh] brush_measure: node '%s' has %u brush(es)", e->node->name ? e->node->name : "(null)",
                (unsigned)e->node->brushes->count);
    }
}

static bool bh_brush_measure_passes_filters(BH_Entity *trigger, const BH_EntityServices *sv, BH_Entity *other)
{
    (void)trigger;
    (void)sv;
    (void)other;

    /* Stub: always allow. (Hook up keyvalues, team filters, etc. later.) */
    return true;
}

static void bh_brush_measure_start_touch(BH_Entity *trigger, const BH_EntityServices *sv, BH_Entity *other)
{
    (void)sv;

    const char *tname = (trigger && trigger->node && trigger->node->name) ? trigger->node->name : "(trigger)";
    const char *oname = (other && other->node && other->node->name)
                            ? other->node->name
                            : (other && other->vt ? other->vt->type_name : "(other)");
    SDL_Log("[bh] %s StartTouch( %s )", tname, oname);
}

static void bh_brush_measure_end_touch(BH_Entity *trigger, const BH_EntityServices *sv, BH_Entity *other)
{
    (void)sv;

    const char *tname = (trigger && trigger->node && trigger->node->name) ? trigger->node->name : "(trigger)";
    const char *oname = (other && other->node && other->node->name)
                            ? other->node->name
                            : (other && other->vt ? other->vt->type_name : "(other)");
    SDL_Log("[bh] %s EndTouch( %s )", tname, oname);
}

static void bh_brush_measure_update(BH_Entity *e, const BH_EntityServices *sv, float dt)
{
    (void)sv;
    (void)dt;

    if (!e || !e->node)
    {
        return;
    }

    const BH_NodeBrushes *nb = e->node->brushes;
    if (!nb || nb->count == 0)
    {
        return;
    }

    /* Draw each brush AABB as a translucent cuboid every frame. */
    color4f c = BH_COLOR_RGBA(0.25f, 0.95f, 0.35f, 0.15f);

    if (e->touching.count > 0)
        c = BH_COLOR_RGBA(0.95f, 0.25f, 0.35f, 0.25f);

    for (const BH_NodeBrushLink *l = nb->first; l; l = l->next)
    {
        if (!l->brush)
        {
            continue;
        }

        const vec3 center = bh_aabb_center(l->brush->mins, l->brush->maxs);
        const vec3 he = bh_aabb_half_extents(l->brush->mins, l->brush->maxs);
        BH_DebugDraw_DrawCuboid(center, he, c, 0.0f);
    }
}

static void bh_brush_measure_destroy(BH_Entity *e, const BH_EntityServices *sv)
{
    if (!e || !sv)
    {
        return;
    }

    BH_BrushMeasure *bm = (BH_BrushMeasure *)e;
    (void)bm;
    SDL_Log("[bh] brush_measure destroyed");
}

static const BH_EntityVTable g_vt = {
    .type_name = "brush_measure",
    .awake = bh_brush_measure_awake,
    .fixed_update = NULL,
    .update = bh_brush_measure_update,
    .destroy = bh_brush_measure_destroy,
    .passes_trigger_filters = bh_brush_measure_passes_filters,
    .start_touch = bh_brush_measure_start_touch,
    .end_touch = bh_brush_measure_end_touch,
};

const BH_EntityVTable *bh_brush_measure_vtable(void)
{
    return &g_vt;
}

BH_BrushMeasure *bh_brush_measure_create(BH_Arena *arena)
{
    return (BH_BrushMeasure *)BH_Entity_Alloc(arena, sizeof(BH_BrushMeasure), bh_brush_measure_vtable());
}
