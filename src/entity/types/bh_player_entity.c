/* -----------------------------------------------------------------------------
   bh_player_entity.c
   ----------------------------------------------------------------------------- */
#include "bh_player_entity.h"

#include "../../debug/bh_console.h"
#include "../../debug/bh_debug_draw.h"
#include "../../scene/bh_scene.h"

/* -----------------------------------------------------------------------------
   Internal Globals
   ----------------------------------------------------------------------------- */

static BH_ConsoleVarId g_cvar_draw_player_bbox = BH_CONSOLE_VAR_INVALID;

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static void bh_player_awake(BH_Entity *e, const BH_EntityServices *sv)
{
    (void)sv;
    BH_PlayerEntity *p = (BH_PlayerEntity *)e;

    if (g_cvar_draw_player_bbox == BH_CONSOLE_VAR_INVALID)
    {
        g_cvar_draw_player_bbox = BH_Console_RegisterVarBool("debug_drawplayerbbox", false,
                                                             "Draw the player collision hull (AABB) each frame");
    }

    /* Source-like: players are SOLID_BBOX (used for trigger overlap tests). */
    BH_Entity_SetSolid(e, SOLID_BBOX);
    BH_Entity_RemoveSolidFlags(e, FSOLID_NOT_SOLID);

    BH_PlayerController_InitState(&p->state);
    p->cfg = BH_PlayerController_GetDefaultConfig();
    p->state.origin = e->node->local_curr.position;
}

static void bh_player_update(BH_Entity *e, const BH_EntityServices *sv, float dt)
{
    (void)dt;
    BH_PlayerEntity *p = (BH_PlayerEntity *)e;

    if (sv->input->pressed[BH_KEY_N])
    {
        if (p->state.movement_mode == BH_MOVEMENT_NOCLIP)
            p->state.movement_mode = BH_MOVEMENT_WALK;
        else
            p->state.movement_mode = BH_MOVEMENT_NOCLIP;
    }

    const bool ducked = (p->state.flags & BH_FL_DUCKING) != 0;
    const vec3 hull_mins = ducked ? p->cfg.duck_hull_mins : p->cfg.hull_mins;
    const vec3 hull_maxs = ducked ? p->cfg.duck_hull_maxs : p->cfg.hull_maxs;

    if (BH_Console_VarGetBool(g_cvar_draw_player_bbox))
    {
        const vec3 mins = vec3_add(p->state.origin, hull_mins);
        const vec3 maxs = vec3_add(p->state.origin, hull_maxs);
        BH_DebugDraw_DrawBounds(mins, maxs, BH_COLOR_RGBA(1.0f, 1.0f, 0.25f, 1.0f), 0.0f);
    }

    const vec3 vel_tip = vec3_add(p->state.origin, vec3_scale(p->state.velocity, 0.05f));
    BH_DebugDraw_DrawLine(p->state.origin, vel_tip, BH_COLOR_RGBA(1.0f, 1.0f, 0.25f, 1.0f), 0.0f);
}

static void bh_player_fixed_update(BH_Entity *e, const BH_EntityServices *sv, const BH_Intent *intent, float dt)
{
    BH_PlayerEntity *p = (BH_PlayerEntity *)e;

    BH_PlayerController_Simulate(&p->state, &p->cfg, sv ? sv->physics : NULL, intent, dt);

    e->node->local_curr.position = p->state.origin;
}

static vec3 bh_player_get_view_offset(const BH_Entity *e, const BH_EntityServices *sv)
{
    (void)sv;
    const BH_PlayerEntity *p = (const BH_PlayerEntity *)e;
    return p->state.view_offset;
}

static bool bh_player_get_world_aabb(const BH_Entity *e, const BH_EntityServices *sv, vec3 *out_mins, vec3 *out_maxs)
{
    (void)sv;
    const BH_PlayerEntity *p = (const BH_PlayerEntity *)e;

    const bool ducked = (p->state.flags & BH_FL_DUCKING) != 0;
    const vec3 hull_mins = ducked ? p->cfg.duck_hull_mins : p->cfg.hull_mins;
    const vec3 hull_maxs = ducked ? p->cfg.duck_hull_maxs : p->cfg.hull_maxs;

    /* Prefer simulated origin (authoritative for player). */
    const vec3 origin = p->state.origin;

    *out_mins = vec3_add(origin, hull_mins);
    *out_maxs = vec3_add(origin, hull_maxs);
    return true;
}

static const BH_EntityVTable g_vt = {
    .type_name = "bh_player",
    .awake = bh_player_awake,
    .fixed_update = bh_player_fixed_update,
    .update = bh_player_update,
    .destroy = NULL,
    .get_view_offset = bh_player_get_view_offset,
    .get_world_aabb = bh_player_get_world_aabb,
};

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

const BH_EntityVTable *bh_player_entity_vtable(void)
{
    return &g_vt;
}

BH_PlayerEntity *bh_player_entity_create(BH_Arena *arena)
{
    return (BH_PlayerEntity *)BH_Entity_Alloc(arena, sizeof(BH_PlayerEntity), bh_player_entity_vtable());
}