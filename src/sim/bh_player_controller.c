/* -----------------------------------------------------------------------------
   bh_player_controller_cleanroom.c

   A C-based, standalone re-implementation of the Counter-Strike: Source
   movement physics. Written from scratch to replicate the behavior 1:1,
   but decoupled from the Source SDK architecture.

   DISCLAIMER:
   This software is an independent creation and is not affiliated with,
   endorsed by, or connected to Valve Corporation. "Counter-Strike",
   "Source", and "Source SDK" are trademarks of Valve Corporation.
   ----------------------------------------------------------------------------- */

#include "bh_player_controller.h"

#include <math.h>

/* -----------------------------------------------------------------------------
   Tunables
   ----------------------------------------------------------------------------- */

static const float kDuckSeconds = 0.4f;
static const float kUnDuckSeconds = 0.2f;

static const float kClipEpsilon = 0.03125f;
static const float kDuckMoveScale = 0.34f;

static const float kStaminaMax = 100.0f;
static const float kStaminaRecoverRate = 19.0f;

enum
{
    BH_MAX_PLANES = 5,
    BH_SPEEDCROP_NONE = 0,
    BH_SPEEDCROP_DUCK = 1,
};

static const float kSafeFallSpeed = 580.0f;

/* -----------------------------------------------------------------------------
   Small math helpers
   ----------------------------------------------------------------------------- */

static inline float bh_maxf(float a, float b)
{
    return (a > b) ? a : b;
}

static inline float bh_len2_xy(vec3 v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

/* Normalize in-place; returns original magnitude. (Does not zero the vector.) */
static inline float bh_norm3_retlen(vec3 *v)
{
    const float l = vec3_len(*v);
    if (l > 0.0f)
    {
        const float inv = 1.0f / l;
        v->x *= inv;
        v->y *= inv;
        v->z *= inv;
    }
    return l;
}

static inline vec3 bh_mad(vec3 start, float t, vec3 dir)
{
    return (vec3){start.x + dir.x * t, start.y + dir.y * t, start.z + dir.z * t};
}

static inline float bh_spline(float t)
{
    /* Smoothstep-like curve used by the authoritative implementation. */
    return t * t * (3.0f - 2.0f * t);
}

/* -----------------------------------------------------------------------------
   Work buffers
   ----------------------------------------------------------------------------- */

typedef struct BH_MoveWork
{
    vec3 view_angles_deg;

    float cmd_forward;
    float cmd_side;
    float cmd_up;

    float max_speed;

    uint32_t buttons;
    uint32_t old_buttons;

    vec3 pos;
    vec3 vel;

    vec3 out_wish;
    vec3 out_jump;
    float out_step;
} BH_MoveWork;

typedef struct BH_MoveCtx
{
    BH_PlayerState *ps;
    BH_MoveWork *wk;
    const BH_PlayerControllerConfig *cfg;
    const BH_PhysicsWorld *world;

    float dt;
    int speed_crop_state;
} BH_MoveCtx;

/* -----------------------------------------------------------------------------
   World interaction
   ----------------------------------------------------------------------------- */

static inline bool bh_is_grounded(const BH_PlayerState *ps)
{
    return ps->ground_brush_index >= 0;
}

static inline float bh_world_gravity(const BH_MoveCtx *ctx)
{
    return ctx->cfg->sv.sv_gravity;
}

static inline float bh_entity_gravity_scale(const BH_PlayerState *ps)
{
    return (ps->gravity_scale > 0.0f) ? ps->gravity_scale : 1.0f;
}

static inline BH_Contents bh_solid_mask(const BH_MoveCtx *ctx)
{
    return ctx->cfg->player_solid_mask;
}

static inline vec3 bh_hull_mins(const BH_MoveCtx *ctx)
{
    return (ctx->ps->flags & BH_FL_DUCKING) ? ctx->cfg->duck_hull_mins : ctx->cfg->hull_mins;
}

static inline vec3 bh_hull_maxs(const BH_MoveCtx *ctx)
{
    return (ctx->ps->flags & BH_FL_DUCKING) ? ctx->cfg->duck_hull_maxs : ctx->cfg->hull_maxs;
}

static inline vec3 bh_view_offset_for(const BH_MoveCtx *ctx, bool ducked)
{
    return ducked ? ctx->cfg->duck_view_offset : ctx->cfg->view_offset;
}

static void bh_trace_bbox(const BH_MoveCtx *ctx, vec3 start, vec3 end, BH_Contents mask, BH_TraceResult *out)
{
    BH_Physics_Trace(ctx->world, start, end, bh_hull_mins(ctx), bh_hull_maxs(ctx), mask, -1, out);
}

static void bh_trace_bbox_hull(const BH_MoveCtx *ctx, vec3 start, vec3 end, vec3 mins, vec3 maxs, BH_Contents mask,
                               BH_TraceResult *out)
{
    BH_Physics_Trace(ctx->world, start, end, mins, maxs, mask, -1, out);
}

static void bh_set_ground(BH_MoveCtx *ctx, const BH_TraceResult *tr)
{
    BH_PlayerState *ps = ctx->ps;

    if (!tr || tr->fraction >= 1.0f || tr->startsolid || tr->brush_index < 0)
    {
        ps->ground_brush_index = -1;
        ps->ground_normal = (vec3){0, 0, 1};
        ps->flags &= ~BH_FL_ONGROUND;
        return;
    }

    ps->ground_brush_index = tr->brush_index;
    ps->ground_normal = tr->plane.normal;
    ps->flags |= BH_FL_ONGROUND;
}

/* -----------------------------------------------------------------------------
   Time-based state decay
   ----------------------------------------------------------------------------- */

static void bh_tick_timers(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    const float frame_ms = ctx->dt * 1000.0f;

    if (ps->water_jump_time)
    {
        ps->water_jump_time -= ctx->dt;
        if (ps->water_jump_time < 0.0f)
            ps->water_jump_time = 0.0f;
    }

    if (ps->local.m_flDucktime)
    {
        ps->local.m_flDucktime -= frame_ms;
        if (ps->local.m_flDucktime < 0.0f)
            ps->local.m_flDucktime = 0.0f;
    }

    if (ps->local.m_flDuckJumpTime > 0.0f)
    {
        ps->local.m_flDuckJumpTime -= ctx->dt;
        if (ps->local.m_flDuckJumpTime < 0.0f)
            ps->local.m_flDuckJumpTime = 0.0f;
    }

    if (ps->stamina > 0.0f)
    {
        ps->stamina -= frame_ms;
        if (ps->stamina < 0.0f)
            ps->stamina = 0.0f;
    }
}

/* -----------------------------------------------------------------------------
   Gravity (integrated as two half-steps)
   ----------------------------------------------------------------------------- */

static void bh_gravity_start(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    const float gscale = bh_entity_gravity_scale(ps);

    wk->vel.z += ps->base_velocity.z;
    ps->base_velocity.z = 0.0f;

    wk->vel.z -= gscale * bh_world_gravity(ctx) * 0.5f * ctx->dt;
    wk->vel.z += ps->base_velocity.z * ctx->dt;
    ps->base_velocity.z = 0.0f;

    wk->out_wish.z += gscale * bh_world_gravity(ctx) * 0.5f * ctx->dt;
}

static void bh_gravity_finish(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    const float gscale = bh_entity_gravity_scale(ps);

    wk->vel.z -= gscale * bh_world_gravity(ctx) * 0.5f * ctx->dt;

    wk->vel.z += ps->base_velocity.z * ctx->dt;
    ps->base_velocity.z = 0.0f;

    wk->out_wish.z += gscale * bh_world_gravity(ctx) * 0.5f * ctx->dt;
}

/* -----------------------------------------------------------------------------
   Sanitization / clamping
   ----------------------------------------------------------------------------- */

static void bh_limit_vel_and_origin(BH_MoveCtx *ctx)
{
    BH_MoveWork *wk = ctx->wk;

    for (int axis = 0; axis < 3; ++axis)
    {
        float *v = (&wk->vel.x) + axis;
        float *p = (&wk->pos.x) + axis;

        if (!isfinite(*v))
            *v = 0.0f;
        if (!isfinite(*p))
            *p = 0.0f;

        const float vmax = ctx->cfg->sv.sv_maxvelocity;
        if (*v > vmax)
            *v = vmax;
        else if (*v < -vmax)
            *v = -vmax;
    }
}

/* -----------------------------------------------------------------------------
   Friction
   ----------------------------------------------------------------------------- */

static void bh_apply_ground_friction(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    const float speed = bh_len2_xy(wk->vel);
    if (speed < 0.1f)
        return;

    const float friction = ctx->cfg->sv.sv_friction * ps->m_surfaceFriction;
    const float control = (speed < ctx->cfg->sv.sv_stopspeed) ? ctx->cfg->sv.sv_stopspeed : speed;
    const float drop = control * friction * ctx->dt;

    float new_speed = speed - drop;
    if (new_speed < 0.0f)
        new_speed = 0.0f;

    if (new_speed != speed)
    {
        const float frac = new_speed / speed;
        wk->vel.x *= frac;
        wk->vel.y *= frac;
        wk->out_wish.x *= frac;
        wk->out_wish.y *= frac;
    }
}

/* -----------------------------------------------------------------------------
   Acceleration
   ----------------------------------------------------------------------------- */

static void bh_accel(BH_MoveCtx *ctx, vec3 wish_dir, float wish_speed, float accel)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    const float current = vec3_dot(wk->vel, wish_dir);
    const float add = wish_speed - current;
    if (add <= 0.0f)
        return;

    float push = accel * ctx->dt * wish_speed * ps->m_surfaceFriction;
    if (push > add)
        push = add;

    wk->vel.x += push * wish_dir.x;
    wk->vel.y += push * wish_dir.y;
    wk->vel.z += push * wish_dir.z;
}

static float bh_air_speed_cap(const BH_MoveCtx *ctx)
{
    (void)ctx;
    return 30.0f;
}

static void bh_accel_air(BH_MoveCtx *ctx, vec3 wish_dir, float wish_speed, float accel)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    float capped = wish_speed;
    if (capped > bh_air_speed_cap(ctx))
        capped = bh_air_speed_cap(ctx);

    const float current = vec3_dot(wk->vel, wish_dir);
    const float add = capped - current;
    if (add <= 0.0f)
        return;

    float push = accel * wish_speed * ctx->dt * ps->m_surfaceFriction;
    if (push > add)
        push = add;

    wk->vel.x += push * wish_dir.x;
    wk->vel.y += push * wish_dir.y;
    wk->vel.z += push * wish_dir.z;
}

/* -----------------------------------------------------------------------------
   Collision response
   ----------------------------------------------------------------------------- */

static int bh_clip_velocity(vec3 in, vec3 normal, vec3 *out, float overbounce)
{
    int blocked = 0;
    const float backoff = vec3_dot(in, normal) * overbounce;

    /* axis-wise write to match the authoritative behaviour exactly */
    for (int i = 0; i < 3; ++i)
    {
        const float n = (&normal.x)[i];
        const float x = (&in.x)[i];
        float *o = (&out->x) + i;
        *o = x - n * backoff;

        if (*o > -kClipEpsilon && *o < kClipEpsilon)
            *o = 0.0f;
    }

    if (normal.z > 0.0f)
        blocked |= 1;
    if (normal.z == 0.0f)
        blocked |= 2;

    return blocked;
}

/* Slide move. Returns blocked flags (floor/wall). */
static int bh_slide_move(BH_MoveCtx *ctx, vec3 *first_end, BH_TraceResult *first_trace)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    const int max_bumps = 4;
    vec3 planes[BH_MAX_PLANES];
    int plane_count = 0;

    const vec3 primal = wk->vel;
    vec3 original = wk->vel;
    vec3 new_vel = (vec3){0};

    float all_frac = 0.0f;
    float time_left = ctx->dt;
    int blocked = 0;

    for (int bump = 0; bump < max_bumps; ++bump)
    {
        if (wk->vel.x == 0.0f && wk->vel.y == 0.0f && wk->vel.z == 0.0f)
            break;

        const vec3 end = bh_mad(wk->pos, time_left, wk->vel);
        BH_TraceResult tr;

        if (first_end && first_trace)
        {
            if (first_end->x == end.x && first_end->y == end.y && first_end->z == end.z)
            {
                tr = *first_trace;
            }
            else
            {
                bh_trace_bbox(ctx, wk->pos, end, bh_solid_mask(ctx), &tr);
            }
        }
        else
        {
            bh_trace_bbox(ctx, wk->pos, end, bh_solid_mask(ctx), &tr);
        }

        all_frac += tr.fraction;

        if (tr.allsolid)
        {
            wk->vel = (vec3){0, 0, 0};
            return 4;
        }

        if (tr.fraction > 0.0f)
        {
            /* Stuck safety check (mirrors authoritative corner-case). */
            if (max_bumps > 0 && tr.fraction == 1.0f)
            {
                BH_TraceResult stuck;
                bh_trace_bbox(ctx, tr.endpos, tr.endpos, bh_solid_mask(ctx), &stuck);
                if (stuck.startsolid || stuck.fraction != 1.0f)
                {
                    wk->vel = (vec3){0, 0, 0};
                    break;
                }
            }

            wk->pos = tr.endpos;
            original = wk->vel;
            plane_count = 0;
        }

        if (tr.fraction == 1.0f)
            break;

        if (tr.plane.normal.z > 0.7f)
            blocked |= 1;
        if (tr.plane.normal.z == 0.0f)
            blocked |= 2;

        time_left -= time_left * tr.fraction;

        if (plane_count >= BH_MAX_PLANES)
        {
            wk->vel = (vec3){0, 0, 0};
            break;
        }

        planes[plane_count++] = tr.plane.normal;

        /* Special-case for walk mode when not grounded. */
        if (plane_count == 1 && ps->movement_mode == BH_MOVEMENT_WALK && !bh_is_grounded(ps))
        {
            for (int i = 0; i < plane_count; ++i)
            {
                if (planes[i].z > 0.7f)
                {
                    bh_clip_velocity(original, planes[i], &new_vel, 1.0f);
                    original = new_vel;
                }
                else
                {
                    const float bounce = 1.0f + ctx->cfg->sv.sv_bounce * (1.0f - ps->m_surfaceFriction);
                    bh_clip_velocity(original, planes[i], &new_vel, bounce);
                }
            }

            wk->vel = new_vel;
            original = new_vel;
        }
        else
        {
            int i = 0;
            for (; i < plane_count; ++i)
            {
                bh_clip_velocity(original, planes[i], &wk->vel, 1.0f);

                int j = 0;
                for (; j < plane_count; ++j)
                {
                    if (j == i)
                        continue;
                    if (vec3_dot(wk->vel, planes[j]) < 0.0f)
                        break;
                }

                if (j == plane_count)
                    break;
            }

            if (i == plane_count)
            {
                if (plane_count != 2)
                {
                    wk->vel = (vec3){0, 0, 0};
                    break;
                }

                vec3 dir = vec3_cross(planes[0], planes[1]);
                (void)bh_norm3_retlen(&dir);
                const float d = vec3_dot(dir, wk->vel);
                wk->vel = (vec3){dir.x * d, dir.y * d, dir.z * d};
            }

            if (vec3_dot(wk->vel, primal) <= 0.0f)
            {
                wk->vel = (vec3){0, 0, 0};
                break;
            }
        }
    }

    if (all_frac == 0.0f)
        wk->vel = (vec3){0, 0, 0};

    /* Volume computation preserved for behaviour parity (unused). */
    float slam_vol = 0.0f;
    const float stop_amt = bh_len2_xy(primal) - bh_len2_xy(wk->vel);
    if (stop_amt > kSafeFallSpeed * 2.0f)
        slam_vol = 1.0f;
    else if (stop_amt > kSafeFallSpeed)
        slam_vol = 0.85f;
    (void)slam_vol;

    return blocked;
}

static void bh_step_move(BH_MoveCtx *ctx, vec3 dest, const BH_TraceResult *tr_in)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    vec3 end_pos = dest;
    BH_TraceResult tr = *tr_in;

    if (!bh_is_grounded(ps) && ps->water_level == 0)
        return;
    if (ps->water_jump_time)
        return;

    const vec3 start_pos = wk->pos;
    const vec3 start_vel = wk->vel;

    bh_slide_move(ctx, &end_pos, &tr);

    const vec3 down_pos = wk->pos;
    const vec3 down_vel = wk->vel;

    wk->pos = start_pos;
    wk->vel = start_vel;

    /* Try stepping up. */
    end_pos = wk->pos;
    end_pos.z += ps->local.m_flStepSize;
    bh_trace_bbox(ctx, wk->pos, end_pos, bh_solid_mask(ctx), &tr);
    if (!tr.startsolid && !tr.allsolid)
        wk->pos = tr.endpos;

    bh_slide_move(ctx, NULL, NULL);

    /* Step back down. */
    end_pos = wk->pos;
    end_pos.z -= ps->local.m_flStepSize;
    bh_trace_bbox(ctx, wk->pos, end_pos, bh_solid_mask(ctx), &tr);
    if (!tr.startsolid && !tr.allsolid)
        wk->pos = tr.endpos;

    if (tr.fraction != 1.0f && tr.plane.normal.z < ctx->cfg->ground_normal_min_z)
    {
        wk->pos = down_pos;
        wk->vel = down_vel;
        return;
    }

    const float step_z = wk->pos.z - start_pos.z;
    if (step_z > 0.0f)
        wk->out_step += step_z;

    const float down_dist = (down_pos.x - start_pos.x) * (down_pos.x - start_pos.x) +
                            (down_pos.y - start_pos.y) * (down_pos.y - start_pos.y);
    const float up_dist = (wk->pos.x - start_pos.x) * (wk->pos.x - start_pos.x) +
                          (wk->pos.y - start_pos.y) * (wk->pos.y - start_pos.y);

    if (down_dist > up_dist)
    {
        wk->pos = down_pos;
        wk->vel = down_vel;
    }
    else
    {
        wk->vel.z = down_vel.z;
    }
}

static void bh_snap_to_floor(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    vec3 start = wk->pos;
    vec3 end = start;
    start.z += 2.0f;
    end.z -= ps->local.m_flStepSize;

    BH_TraceResult tr;
    bh_trace_bbox(ctx, start, end, bh_solid_mask(ctx), &tr);

    if (tr.fraction > 0.0f && tr.fraction < 1.0f && !tr.startsolid && tr.plane.normal.z >= ctx->cfg->ground_normal_min_z)
        wk->pos = tr.endpos;
}

/* -----------------------------------------------------------------------------
   Move composition (air / ground)
   ----------------------------------------------------------------------------- */

static void bh_air_move(BH_MoveCtx *ctx)
{
    BH_MoveWork *wk = ctx->wk;
    BH_PlayerState *ps = ctx->ps;

    vec3 f, r, u;
    AngleVectors(wk->view_angles_deg, &f, &r, &u);

    const float fmove = wk->cmd_forward;
    const float smove = wk->cmd_side;

    f.z = 0.0f;
    r.z = 0.0f;
    (void)bh_norm3_retlen(&f);
    (void)bh_norm3_retlen(&r);

    vec3 wish_vel = (vec3){f.x * fmove + r.x * smove, f.y * fmove + r.y * smove, 0.0f};
    vec3 wish_dir = wish_vel;
    float wish_speed = bh_norm3_retlen(&wish_dir);

    if (wish_speed != 0.0f && wish_speed > wk->max_speed)
    {
        const float s = wk->max_speed / wish_speed;
        wish_vel.x *= s;
        wish_vel.y *= s;
        wish_speed = wk->max_speed;
    }

    bh_accel_air(ctx, wish_dir, wish_speed, ctx->cfg->sv.sv_airaccelerate);

    wk->vel.x += ps->base_velocity.x;
    wk->vel.y += ps->base_velocity.y;
    wk->vel.z += ps->base_velocity.z;

    bh_slide_move(ctx, NULL, NULL);

    wk->vel.x -= ps->base_velocity.x;
    wk->vel.y -= ps->base_velocity.y;
    wk->vel.z -= ps->base_velocity.z;
}

static void bh_ground_move_raw(BH_MoveCtx *ctx)
{
    BH_MoveWork *wk = ctx->wk;
    BH_PlayerState *ps = ctx->ps;

    vec3 f, r, u;
    AngleVectors(wk->view_angles_deg, &f, &r, &u);

    const float fmove = wk->cmd_forward;
    const float smove = wk->cmd_side;

    f.z = 0.0f;
    r.z = 0.0f;
    (void)bh_norm3_retlen(&f);
    (void)bh_norm3_retlen(&r);

    vec3 wish_vel = (vec3){f.x * fmove + r.x * smove, f.y * fmove + r.y * smove, 0.0f};
    vec3 wish_dir = wish_vel;
    float wish_speed = bh_norm3_retlen(&wish_dir);

    if (wish_speed != 0.0f && wish_speed > wk->max_speed)
    {
        const float s = wk->max_speed / wish_speed;
        wish_vel.x *= s;
        wish_vel.y *= s;
        wish_speed = wk->max_speed;
    }

    wk->vel.z = 0.0f;
    bh_accel(ctx, wish_dir, wish_speed, ctx->cfg->sv.sv_accelerate);

    wk->vel.x += ps->base_velocity.x;
    wk->vel.y += ps->base_velocity.y;
    wk->vel.z += ps->base_velocity.z;

    const float spd = vec3_len(wk->vel);
    if (spd < 1.0f)
    {
        wk->vel = (vec3){0, 0, 0};
        wk->vel.x -= ps->base_velocity.x;
        wk->vel.y -= ps->base_velocity.y;
        wk->vel.z -= ps->base_velocity.z;
        return;
    }

    const vec3 dest = bh_mad(wk->pos, ctx->dt, wk->vel);
    BH_TraceResult tr;
    bh_trace_bbox(ctx, wk->pos, dest, bh_solid_mask(ctx), &tr);

    if (tr.fraction == 1.0f)
    {
        wk->pos = tr.endpos;
        wk->vel.x -= ps->base_velocity.x;
        wk->vel.y -= ps->base_velocity.y;
        wk->vel.z -= ps->base_velocity.z;
        bh_snap_to_floor(ctx);
        return;
    }

    bh_step_move(ctx, dest, &tr);

    wk->vel.x -= ps->base_velocity.x;
    wk->vel.y -= ps->base_velocity.y;
    wk->vel.z -= ps->base_velocity.z;
    bh_snap_to_floor(ctx);
}

static void bh_ground_move(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    if (ps->stamina > 0.0f)
    {
        float ratio = (kStaminaMax - ((ps->stamina / 1000.0f) * kStaminaRecoverRate)) / kStaminaMax;

        const float ref_dt = 1.0f / 70.0f;
        const float dt_ratio = ctx->dt / ref_dt;
        ratio = powf(ratio, dt_ratio);

        wk->vel.x *= ratio;
        wk->vel.y *= ratio;
    }

    bh_ground_move_raw(ctx);
}

/* -----------------------------------------------------------------------------
   Ground detection
   ----------------------------------------------------------------------------- */

static void bh_check_grounded(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    ps->m_surfaceFriction = 1.0f;

    if (wk->vel.z > 140.0f)
    {
        bh_set_ground(ctx, NULL);
        return;
    }

    vec3 probe = wk->pos;
    probe.z -= 2.0f;

    BH_TraceResult tr;
    bh_trace_bbox(ctx, wk->pos, probe, bh_solid_mask(ctx), &tr);

    if (tr.fraction == 1.0f || tr.allsolid || tr.plane.normal.z < ctx->cfg->ground_normal_min_z)
    {
        bh_set_ground(ctx, NULL);
        return;
    }

    bh_set_ground(ctx, &tr);
}

/* -----------------------------------------------------------------------------
   Ducking
   ----------------------------------------------------------------------------- */

static void bh_set_eye_for_duck_fraction(BH_MoveCtx *ctx, float duck_fraction)
{
    BH_PlayerState *ps = ctx->ps;
    const vec3 crouch = bh_view_offset_for(ctx, true);
    const vec3 stand = bh_view_offset_for(ctx, false);

    ps->view_offset.x = crouch.x * duck_fraction + stand.x * (1.0f - duck_fraction);
    ps->view_offset.y = crouch.y * duck_fraction + stand.y * (1.0f - duck_fraction);
    ps->view_offset.z = crouch.z * duck_fraction + stand.z * (1.0f - duck_fraction);
}

static bool bh_test_position(const BH_MoveCtx *ctx, vec3 pos)
{
    BH_TraceResult tr;
    bh_trace_bbox(ctx, pos, pos, bh_solid_mask(ctx), &tr);
    return tr.startsolid || tr.allsolid;
}

static void bh_fix_crouch_stuck(BH_MoveCtx *ctx, bool upward)
{
    BH_MoveWork *wk = ctx->wk;

    if (!bh_test_position(ctx, wk->pos))
        return;

    vec3 test = wk->pos;
    const int dir = upward ? 1 : -1;
    for (int i = 0; i < 36; ++i)
    {
        test.z += (float)dir;
        if (!bh_test_position(ctx, test))
        {
            wk->pos = test;
            break;
        }
    }
}

static bool bh_can_stand(BH_MoveCtx *ctx)
{
    BH_TraceResult tr;
    vec3 new_origin = ctx->wk->pos;

    if (bh_is_grounded(ctx->ps))
    {
        const vec3 diff = vec3_sub(ctx->cfg->duck_hull_mins, ctx->cfg->hull_mins);
        new_origin = vec3_add(new_origin, diff);
    }
    else
    {
        const vec3 size_n = vec3_sub(ctx->cfg->hull_maxs, ctx->cfg->hull_mins);
        const vec3 size_c = vec3_sub(ctx->cfg->duck_hull_maxs, ctx->cfg->duck_hull_mins);
        const vec3 delta = vec3_scale(vec3_sub(size_n, size_c), -0.5f);
        new_origin = vec3_add(new_origin, delta);
    }

    bh_trace_bbox_hull(ctx, ctx->wk->pos, new_origin, ctx->cfg->hull_mins, ctx->cfg->hull_maxs, bh_solid_mask(ctx),
                       &tr);

    if (tr.startsolid || tr.fraction != 1.0f)
        return false;

    return true;
}

static void bh_finish_duck(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;
    const BH_PlayerControllerConfig *cfg = ctx->cfg;

    const vec3 size_n = vec3_sub(cfg->hull_maxs, cfg->hull_mins);
    const vec3 size_c = vec3_sub(cfg->duck_hull_maxs, cfg->duck_hull_mins);
    const vec3 view_delta = vec3_scale(vec3_sub(size_n, size_c), 0.5f);

    ps->view_offset = bh_view_offset_for(ctx, true);
    ps->flags |= BH_FL_DUCKING;
    ps->local.m_bDucking = false;

    if (!ps->local.m_bDucked)
    {
        vec3 org = wk->pos;
        if (bh_is_grounded(ps))
        {
            const vec3 diff = vec3_sub(cfg->duck_hull_mins, cfg->hull_mins);
            org = vec3_sub(org, diff);
        }
        else
        {
            org = vec3_add(org, view_delta);
        }
        wk->pos = org;
        ps->local.m_bDucked = true;
    }

    bh_fix_crouch_stuck(ctx, true);
    bh_check_grounded(ctx);
}

static void bh_finish_unduck(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;
    const BH_PlayerControllerConfig *cfg = ctx->cfg;

    vec3 new_origin = wk->pos;

    if (bh_is_grounded(ps))
    {
        const vec3 diff = vec3_sub(cfg->duck_hull_mins, cfg->hull_mins);
        new_origin = vec3_add(new_origin, diff);
    }
    else
    {
        const vec3 size_n = vec3_sub(cfg->hull_maxs, cfg->hull_mins);
        const vec3 size_c = vec3_sub(cfg->duck_hull_maxs, cfg->duck_hull_mins);
        const vec3 view_delta = vec3_scale(vec3_sub(size_n, size_c), -0.5f);
        new_origin = vec3_add(new_origin, view_delta);
    }

    ps->local.m_bDucked = false;
    ps->flags &= ~BH_FL_DUCKING;
    ps->local.m_bDucking = false;
    ps->view_offset = bh_view_offset_for(ctx, false);
    ps->local.m_flDucktime = 0.0f;

    wk->pos = new_origin;
    bh_check_grounded(ctx);
}

static void bh_crop_move_for_duck(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    if (ctx->speed_crop_state)
        return;

    if ((wk->buttons & BH_IN_DUCK) || ps->local.m_bDucking || (ps->flags & BH_FL_DUCKING))
    {
        wk->cmd_forward *= kDuckMoveScale;
        wk->cmd_side *= kDuckMoveScale;
        wk->cmd_up *= kDuckMoveScale;
        ctx->speed_crop_state = BH_SPEEDCROP_DUCK;
    }
}

static void bh_update_duck(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;
    const BH_PlayerControllerConfig *cfg = ctx->cfg;

    const int changed = (int)(wk->old_buttons ^ wk->buttons);
    const int pressed = changed & (int)wk->buttons;
    const int released = changed & (int)wk->old_buttons;

    const bool in_air = !bh_is_grounded(ps);

    /* Mirror old-button bookkeeping used by the authoritative controller. */
    if (wk->buttons & BH_IN_DUCK)
        wk->old_buttons |= BH_IN_DUCK;
    else
        wk->old_buttons &= ~BH_IN_DUCK;

    if (ps->deadflag)
    {
        if (ps->flags & BH_FL_DUCKING)
            bh_finish_unduck(ctx);
        return;
    }

    bh_crop_move_for_duck(ctx);

    /* Forced duck-until-ground logic. */
    if (ps->duck_until_on_ground)
    {
        if (!in_air)
        {
            ps->duck_until_on_ground = false;
            if (bh_can_stand(ctx))
                bh_finish_unduck(ctx);
            return;
        }

        if (wk->vel.z > 0.0f)
            return;

        /* Predictively check if standing would collide when landing. */
        vec3 new_origin = wk->pos;

        const vec3 size_n = (vec3){cfg->hull_maxs.x - cfg->hull_mins.x, cfg->hull_maxs.y - cfg->hull_mins.y,
                                   cfg->hull_maxs.z - cfg->hull_mins.z};
        const vec3 size_c = (vec3){cfg->duck_hull_maxs.x - cfg->duck_hull_mins.x, cfg->duck_hull_maxs.y - cfg->duck_hull_mins.y,
                                   cfg->duck_hull_maxs.z - cfg->duck_hull_mins.z};

        new_origin.x -= (size_n.x - size_c.x);
        new_origin.y -= (size_n.y - size_c.y);
        new_origin.z -= (size_n.z - size_c.z);

        vec3 ground_check = new_origin;
        ground_check.z -= ps->local.m_flStepSize;

        BH_TraceResult tr;
        bh_trace_bbox_hull(ctx, new_origin, ground_check, cfg->hull_mins, cfg->hull_maxs, bh_solid_mask(ctx), &tr);

        if (tr.startsolid || tr.fraction == 1.0f)
            return;

        ps->duck_until_on_ground = false;
        if (bh_can_stand(ctx))
            bh_finish_unduck(ctx);
        return;
    }

    if ((wk->buttons & BH_IN_DUCK) || ps->local.m_bDucking || (ps->flags & BH_FL_DUCKING))
    {
        if (wk->buttons & BH_IN_DUCK)
        {
            const bool already_ducked = (ps->flags & BH_FL_DUCKING) ? true : false;

            if ((pressed & (int)BH_IN_DUCK) && !(ps->flags & BH_FL_DUCKING))
            {
                ps->local.m_flDucktime = 1000.0f;
                ps->local.m_bDucking = true;
            }

            const float duck_ms = bh_maxf(0.0f, 1000.0f - ps->local.m_flDucktime);
            const float duck_s = duck_ms / 1000.0f;

            if (ps->local.m_bDucking)
            {
                if ((duck_s > kDuckSeconds) || (!bh_is_grounded(ps)) || already_ducked)
                {
                    bh_finish_duck(ctx);
                }
                else
                {
                    const float frac = bh_spline(duck_s / kDuckSeconds);
                    bh_set_eye_for_duck_fraction(ctx, frac);
                }
            }
        }
        else
        {
            if (ps->local.m_bAllowAutoMovement || !bh_is_grounded(ps))
            {
                if ((released & (int)BH_IN_DUCK) && (ps->flags & BH_FL_DUCKING))
                {
                    ps->local.m_flDucktime = 1000.0f;
                    ps->local.m_bDucking = true;
                }

                const float duck_ms = bh_maxf(0.0f, 1000.0f - ps->local.m_flDucktime);
                const float duck_s = duck_ms / 1000.0f;

                if (bh_can_stand(ctx))
                {
                    if (ps->local.m_bDucking || ps->local.m_bDucked)
                    {
                        if ((duck_s > kUnDuckSeconds) || (!bh_is_grounded(ps)))
                        {
                            bh_finish_unduck(ctx);
                        }
                        else
                        {
                            const float frac = bh_spline(1.0f - (duck_s / kUnDuckSeconds));
                            bh_set_eye_for_duck_fraction(ctx, frac);
                        }
                    }
                }
                else
                {
                    ps->local.m_flDucktime = 1000.0f;
                }
            }
        }
    }
}

/* -----------------------------------------------------------------------------
   Jump
   ----------------------------------------------------------------------------- */

static bool bh_try_jump(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    if (ps->deadflag)
    {
        wk->old_buttons |= BH_IN_JUMP;
        return false;
    }

    if (ps->water_jump_time)
    {
        ps->water_jump_time -= ctx->dt;
        if (ps->water_jump_time < 0.0f)
            ps->water_jump_time = 0.0f;
        return false;
    }

    if (ps->water_level >= 2)
    {
        bh_set_ground(ctx, NULL);

        if (ps->water_type == CONTENTS_WATER)
            wk->vel.z = 100.0f;
        else if (ps->water_type == CONTENTS_SLIME)
            wk->vel.z = 80.0f;

        return false;
    }

    if (!bh_is_grounded(ps))
    {
        wk->old_buttons |= BH_IN_JUMP;
        return false;
    }

    if (ps->local.m_bDucking && (ps->flags & BH_FL_DUCKING))
        return false;

    if (ps->local.m_flDuckJumpTime > 0.0f)
        return false;

    bh_set_ground(ctx, NULL);

    const float ground_factor = 1.0f;
    float mul = sqrtf(2.0f * 800.0f * 57.0f);

    const float stamina = ps->stamina;
    float ratio = (kStaminaMax - ((stamina / 1000.0f) * kStaminaRecoverRate)) / kStaminaMax;
    ratio = clampf(ratio, 0.0f, 1.0f);
    mul *= ratio;

    const float startz = wk->vel.z;

    if (ps->local.m_bDucking || (ps->flags & BH_FL_DUCKING))
        wk->vel.z = ground_factor * mul;
    else
        wk->vel.z += ground_factor * mul;

    bh_gravity_finish(ctx);

    wk->out_jump.z += wk->vel.z - startz;
    wk->out_step += 0.1f;

    wk->old_buttons |= BH_IN_JUMP;
    return true;
}

/* -----------------------------------------------------------------------------
   Noclip
   ----------------------------------------------------------------------------- */

static void bh_movement_noclip(BH_MoveCtx *ctx, float speed_factor, float accel_cap)
{
    BH_MoveWork *wk = ctx->wk;
    BH_PlayerState *ps = ctx->ps;
    const BH_PlayerControllerConfig *cfg = ctx->cfg;

    vec3 wish_vel = (vec3){0, 0, 0};
    vec3 f, r, u;
    vec3 wish_dir;
    float wish_speed;

    const float max_speed = cfg->sv.sv_maxspeed * speed_factor;

    AngleVectors(wk->view_angles_deg, &f, &r, &u);

    if (wk->buttons & BH_IN_SPEED)
        speed_factor /= 2.0f;

    const float fmove = wk->cmd_forward * speed_factor;
    const float smove = wk->cmd_side * speed_factor;

    (void)bh_norm3_retlen(&f);
    (void)bh_norm3_retlen(&r);

    for (int i = 0; i < 3; ++i)
    {
        const float fc = (&f.x)[i];
        const float rc = (&r.x)[i];
        (&wish_vel.x)[i] = fc * fmove + rc * smove;
    }

    wish_vel.z += wk->cmd_up * speed_factor;

    wish_dir = wish_vel;
    wish_speed = bh_norm3_retlen(&wish_dir);

    if (wish_speed > max_speed)
    {
        const float scale = max_speed / wish_speed;
        wish_vel.x *= scale;
        wish_vel.y *= scale;
        wish_vel.z *= scale;
        wish_speed = max_speed;
    }

    if (accel_cap > 0.0f)
    {
        bh_accel(ctx, wish_dir, wish_speed, accel_cap);

        const float spd = vec3_len(wk->vel);
        if (spd < 1.0f)
        {
            wk->vel = (vec3){0, 0, 0};
            return;
        }

        const float control = (spd < max_speed / 4.0f) ? max_speed / 4.0f : spd;
        const float friction = cfg->noclip_friction * ps->m_surfaceFriction;
        const float drop = control * friction * ctx->dt;

        float new_speed = spd - drop;
        if (new_speed < 0.0f)
            new_speed = 0.0f;
        new_speed /= spd;

        wk->vel.x *= new_speed;
        wk->vel.y *= new_speed;
        wk->vel.z *= new_speed;
    }
    else
    {
        wk->vel = wish_vel;
    }

    wk->pos = bh_mad(wk->pos, ctx->dt, wk->vel);

    if (accel_cap < 0.0f)
        wk->vel = (vec3){0, 0, 0};
}

/* -----------------------------------------------------------------------------
   Walk pipeline
   ----------------------------------------------------------------------------- */

static void bh_movement_walk(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    bh_gravity_start(ctx);

    if (wk->buttons & BH_IN_JUMP)
        (void)bh_try_jump(ctx);
    else
        wk->old_buttons &= ~BH_IN_JUMP;

    if (bh_is_grounded(ps))
    {
        wk->vel.z = 0.0f;
        bh_apply_ground_friction(ctx);
    }

    bh_limit_vel_and_origin(ctx);

    if (bh_is_grounded(ps))
        bh_ground_move(ctx);
    else
        bh_air_move(ctx);

    bh_check_grounded(ctx);
    bh_limit_vel_and_origin(ctx);
    bh_gravity_finish(ctx);

    if (bh_is_grounded(ps))
        wk->vel.z = 0.0f;
}

static void bh_run_controller(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;

    bh_tick_timers(ctx);

    if (ps->movement_mode != BH_MOVEMENT_NOCLIP)
    {
        bh_check_grounded(ctx);
    }
    else
    {
        if (wk->vel.z > 250.0f)
            bh_set_ground(ctx, NULL);
    }

    bh_update_duck(ctx);

    switch (ps->movement_mode)
    {
    case BH_MOVEMENT_NOCLIP:
        bh_movement_noclip(ctx, ctx->cfg->noclip_speed, ctx->cfg->noclip_accelerate);
        break;
    case BH_MOVEMENT_WALK:
        bh_movement_walk(ctx);
        break;
    default:
        break;
    }
}

/* -----------------------------------------------------------------------------
   Build per-tick work packet
   ----------------------------------------------------------------------------- */

static BH_MoveWork bh_make_work(const BH_PlayerState *ps, const BH_PlayerControllerConfig *cfg, const BH_Intent *in)
{
    BH_MoveWork wk;
    wk = (BH_MoveWork){0};

    wk.view_angles_deg = in->view_angles_deg;

    wk.cmd_forward = in->move_forward * cfg->sv.sv_maxspeed;
    wk.cmd_side = (-in->move_left) * cfg->sv.sv_maxspeed;
    wk.cmd_up = in->move_up * cfg->sv.sv_maxspeed;

    wk.max_speed = 260.0f;

    wk.buttons = ps->buttons;
    wk.old_buttons = ps->old_buttons;

    wk.pos = ps->origin;
    wk.vel = ps->velocity;

    wk.out_wish = (vec3){0, 0, 0};
    wk.out_jump = (vec3){0, 0, 0};
    wk.out_step = 0.0f;

    return wk;
}

/* -----------------------------------------------------------------------------
   Eye offset clearance correction
   ----------------------------------------------------------------------------- */

static void bh_fixup_view_offset_after_move(BH_MoveCtx *ctx)
{
    BH_PlayerState *ps = ctx->ps;
    BH_MoveWork *wk = ctx->wk;
    const BH_PlayerControllerConfig *cfg = ctx->cfg;

    if (ps->deadflag)
        return;

    const float eye_clearance = 12.0f;

    vec3 offset = ps->view_offset;

    vec3 mins = bh_hull_mins(ctx);
    mins.z = 0.0f;
    vec3 maxs = bh_hull_maxs(ctx);

    vec3 start = wk->pos;
    start.z += maxs.z;

    vec3 end = start;
    end.z += eye_clearance - maxs.z;

    const float duck_z = cfg->duck_view_offset.z;
    const float stand_z = cfg->view_offset.z;
    end.z += (ps->local.m_bDucked) ? duck_z : stand_z;

    maxs.z = 0.0f;

    const vec3 fudge = (vec3){1.0f, 1.0f, 0.0f};
    mins = vec3_add(mins, fudge);
    maxs = vec3_sub(maxs, fudge);

    BH_TraceResult tr;
    bh_trace_bbox_hull(ctx, start, end, mins, maxs, bh_solid_mask(ctx), &tr);

    if (tr.fraction < 1.0f)
    {
        const float est = start.z + tr.fraction * (end.z - start.z) - wk->pos.z - eye_clearance;

        if (!(ps->flags & BH_FL_DUCKING) && !ps->local.m_bDucking && !ps->local.m_bDucked)
        {
            offset.z = est;
        }
        else
        {
            offset.z = (est < offset.z) ? est : offset.z;
        }
        ps->view_offset = offset;
    }
    else
    {
        if (!(ps->flags & BH_FL_DUCKING) && !ps->local.m_bDucking && !ps->local.m_bDucked)
        {
            ps->view_offset = cfg->view_offset;
        }
        else if (ps->duck_until_on_ground)
        {
            const vec3 size_n = vec3_sub(cfg->hull_maxs, cfg->hull_mins);
            const vec3 size_c = vec3_sub(cfg->duck_hull_maxs, cfg->duck_hull_mins);
            const vec3 lower = vec3_sub(size_n, size_c);
            const vec3 duck_eye = vec3_sub(cfg->view_offset, lower);
            ps->view_offset = duck_eye;
        }
        else if (ps->local.m_bDucked && !ps->local.m_bDucking)
        {
            ps->view_offset = cfg->duck_view_offset;
        }
    }
}

/* -----------------------------------------------------------------------------
   Public entry points
   ----------------------------------------------------------------------------- */

void BH_PlayerController_InitState(BH_PlayerState *ps)
{
    if (!ps)
        return;

    *ps = (BH_PlayerState){0};

    ps->movement_mode = BH_MOVEMENT_WALK;
    ps->flags = 0;
    ps->buttons = 0;
    ps->old_buttons = 0;

    ps->ground_brush_index = -1;
    ps->ground_normal = (vec3){0, 0, 1};

    ps->m_surfaceFriction = 1.0f;
    ps->base_velocity = (vec3){0, 0, 0};
    ps->gravity_scale = 1.0f;

    ps->water_level = 0;
    ps->water_type = CONTENTS_EMPTY;
    ps->water_jump_time = 0.0f;

    ps->stamina = 0.0f;
    ps->deadflag = 0;
    ps->duck_until_on_ground = false;

    ps->local.m_bDucked = false;
    ps->local.m_bDucking = false;
    ps->local.m_flDucktime = 0.0f;
    ps->local.m_flDuckJumpTime = 0.0f;
    ps->local.m_bInDuckJump = false;
    ps->local.m_bAllowAutoMovement = true;
    ps->local.m_flStepSize = 18.0f;

    ps->view_offset = (vec3){0.0f, 0.0f, 64.0f};
}

BH_PlayerControllerConfig BH_PlayerController_GetDefaultConfig(void)
{
    BH_PlayerControllerConfig cfg;
    cfg = (BH_PlayerControllerConfig){0};

    cfg.sv.sv_accelerate = 5;
    cfg.sv.sv_airaccelerate = 1000;
    cfg.sv.sv_friction = 4;
    cfg.sv.sv_stopspeed = 75.0f;
    cfg.sv.sv_gravity = 800.0f;
    cfg.sv.sv_maxspeed = 320.0f;
    cfg.sv.sv_maxvelocity = 9999999;
    cfg.sv.sv_bounce = 0.0f;

    cfg.noclip_accelerate = 4.0f;
    cfg.noclip_friction = 6.0f;
    cfg.noclip_speed = 8;

    cfg.ground_normal_min_z = 0.7f;
    cfg.step_size = 18.0f;

    cfg.player_solid_mask = MASK_PLAYERSOLID;

    cfg.hull_mins = (vec3){-16, -16, 0};
    cfg.hull_maxs = (vec3){16, 16, 62};

    cfg.duck_hull_mins = (vec3){-16, -16, 0};
    cfg.duck_hull_maxs = (vec3){16, 16, 45};

    cfg.view_offset = (vec3){0, 0, 64};
    cfg.duck_view_offset = (vec3){0, 0, 47};

    return cfg;
}

void BH_PlayerController_Simulate(BH_PlayerState *player, const BH_PlayerControllerConfig *cfg,
                                    const BH_PhysicsWorld *world, const BH_Intent *intent, float dt)
{
    if (!player || !cfg || !world || !intent)
        return;

    uint32_t btn = 0;
    if (intent->jump)
        btn |= BH_IN_JUMP;
    if (intent->duck)
        btn |= BH_IN_DUCK;

    player->buttons = btn;
    player->view_angles_deg = intent->view_angles_deg;

    BH_MoveWork wk = bh_make_work(player, cfg, intent);

    BH_MoveCtx ctx;
    ctx = (BH_MoveCtx){0};
    ctx.ps = player;
    ctx.wk = &wk;
    ctx.cfg = cfg;
    ctx.world = world;
    ctx.dt = dt;
    ctx.speed_crop_state = BH_SPEEDCROP_NONE;

    if (player->m_surfaceFriction <= 0.0f)
        player->m_surfaceFriction = 1.0f;
    player->local.m_flStepSize = cfg->step_size;

    if (player->deadflag)
    {
        wk.cmd_forward = 0.0f;
        wk.cmd_side = 0.0f;
        wk.cmd_up = 0.0f;
        wk.buttons &= ~BH_IN_JUMP;
    }

    bh_run_controller(&ctx);

    /* View offset clearance adjustment. */
    bh_fixup_view_offset_after_move(&ctx);

    player->origin = wk.pos;
    player->velocity = wk.vel;
    player->old_buttons = wk.buttons;
}
