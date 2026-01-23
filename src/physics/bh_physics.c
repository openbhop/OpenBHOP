/* -----------------------------------------------------------------------------
   bh_physics.c
   ----------------------------------------------------------------------------- */
#include "bh_physics.h"
#include "../debug/bh_debug_draw.h"
#include <SDL3/SDL.h>
#include <float.h>
#include <math.h>
#include <string.h>

/* -----------------------------------------------------------------------------
   Constants & Tuning
   ----------------------------------------------------------------------------- */

#define BH_DIST_EPSILON (0.03125f)  /* 1/32 epsilon */
#define BH_NEVER_UPDATED (-9999.0f) /* Negative to ensure initial tests pass */
#define BH_RENDER_NORMAL_EPSILON (0.00001f)
#define BH_BRUSH_CLIP_EPSILON (0.01f)

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static BH_FORCEINLINE float bh_absf(float x)
{
    return (x < 0.0f) ? -x : x;
}

static BH_FORCEINLINE vec3 bh_vec3_min(vec3 a, vec3 b)
{
    return (vec3){(a.x < b.x) ? a.x : b.x, (a.y < b.y) ? a.y : b.y, (a.z < b.z) ? a.z : b.z};
}

static BH_FORCEINLINE vec3 bh_vec3_max(vec3 a, vec3 b)
{
    return (vec3){(a.x > b.x) ? a.x : b.x, (a.y > b.y) ? a.y : b.y, (a.z > b.z) ? a.z : b.z};
}

static BH_FORCEINLINE bool bh_vec3_is_zero(vec3 v)
{
    return (bh_absf(v.x) < 1e-8f) && (bh_absf(v.y) < 1e-8f) && (bh_absf(v.z) < 1e-8f);
}

static BH_FORCEINLINE bool bh_aabb_overlaps(vec3 a_mins, vec3 a_maxs, vec3 b_mins, vec3 b_maxs)
{
    if (a_maxs.x < b_mins.x || a_mins.x > b_maxs.x)
        return false;
    if (a_maxs.y < b_mins.y || a_mins.y > b_maxs.y)
        return false;
    if (a_maxs.z < b_mins.z || a_mins.z > b_maxs.z)
        return false;
    return true;
}

static BH_FORCEINLINE int bh_plane_type_from_normal(vec3 n)
{
    if (bh_absf(n.x) == 1.0f && n.y == 0.0f && n.z == 0.0f)
        return 0;
    if (bh_absf(n.y) == 1.0f && n.x == 0.0f && n.z == 0.0f)
        return 1;
    if (bh_absf(n.z) == 1.0f && n.x == 0.0f && n.y == 0.0f)
        return 2;
    return 3;
}

/* -----------------------------------------------------------------------------
   Winding Logic
   ----------------------------------------------------------------------------- */

static BH_Winding *bh_winding_alloc(void)
{
    return (BH_Winding *)SDL_calloc(1, sizeof(BH_Winding));
}

static void bh_winding_free(BH_Winding *w)
{
    SDL_free(w);
}

static BH_Winding *bh_base_winding_for_plane(vec3 normal, float dist)
{
    const float max_size = 65536.0f;

    float ax = bh_absf(normal.x);
    float ay = bh_absf(normal.y);
    float az = bh_absf(normal.z);

    int x = (ay > ax) ? 1 : 0;
    if ((x == 0 && az > ax) || (x == 1 && az > ay))
    {
        x = 2;
    }

    vec3 vup = {0};
    if (x == 0)
        vup = (vec3){0, 1, 0};
    else if (x == 1)
        vup = (vec3){0, 0, 1};
    else
        vup = (vec3){1, 0, 0};

    float d = vec3_dot(vup, normal);
    vup = vec3_sub(vup, vec3_scale(normal, d));
    vec3_norm_inplace(&vup);

    vec3 vright = vec3_cross(vup, normal);
    vec3_norm_inplace(&vright);

    vup = vec3_scale(vup, max_size);
    vright = vec3_scale(vright, max_size);
    vec3 org = vec3_scale(normal, dist);

    BH_Winding *w = bh_winding_alloc();
    if (!w)
        return NULL;

    w->numpoints = 4;
    w->p[0] = vec3_add(vec3_sub(org, vright), vup);
    w->p[1] = vec3_add(vec3_add(org, vright), vup);
    w->p[2] = vec3_sub(vec3_add(org, vright), vup);
    w->p[3] = vec3_sub(vec3_sub(org, vright), vup);

    return w;
}

static void bh_chop_winding_in_place(BH_Winding **inout_w, vec3 normal, float dist, float epsilon)
{
    if (!inout_w || !*inout_w)
        return;

    BH_Winding *in = *inout_w;
    float dists[64];
    int sides[64];
    int counts[3] = {0, 0, 0};
    const int SIDE_FRONT = 0, SIDE_BACK = 1, SIDE_ON = 2;

    for (int i = 0; i < in->numpoints; ++i)
    {
        float d = vec3_dot(in->p[i], normal) - dist;
        dists[i] = d;
        if (d > epsilon)
        {
            sides[i] = SIDE_FRONT;
            counts[SIDE_FRONT]++;
        }
        else if (d < -epsilon)
        {
            sides[i] = SIDE_BACK;
            counts[SIDE_BACK]++;
        }
        else
        {
            sides[i] = SIDE_ON;
            counts[SIDE_ON]++;
        }
    }

    /* Keep FRONT side (matching Quake/Source plane^1 usage) */
    if (counts[SIDE_FRONT] == 0)
    {
        if (counts[SIDE_ON] == 0 || counts[SIDE_BACK] > 0)
        {
            bh_winding_free(in);
            *inout_w = NULL;
        }
        return;
    }

    if (counts[SIDE_BACK] == 0)
        return;

    BH_Winding *out = bh_winding_alloc();
    if (!out)
        return;
    out->numpoints = 0;

    for (int i = 0; i < in->numpoints; ++i)
    {
        int j = (i + 1) % in->numpoints;
        vec3 p1 = in->p[i];
        vec3 p2 = in->p[j];
        int s1 = sides[i];
        int s2 = sides[j];

        if (s1 != SIDE_BACK)
        {
            if (out->numpoints < (int)BH_ARRAY_COUNT(out->p))
            {
                out->p[out->numpoints++] = p1;
            }
        }

        if ((s1 == SIDE_FRONT && s2 == SIDE_BACK) || (s1 == SIDE_BACK && s2 == SIDE_FRONT))
        {
            float d1 = dists[i];
            float d2 = dists[j];
            float denom = (d1 - d2);
            if (bh_absf(denom) < 1e-8f)
                continue;

            float frac = d1 / denom;
            vec3 mid = vec3_add(p1, vec3_scale(vec3_sub(p2, p1), frac));
            if (out->numpoints < (int)BH_ARRAY_COUNT(out->p))
            {
                out->p[out->numpoints++] = mid;
            }
        }
    }

    if (out->numpoints < 3)
    {
        bh_winding_free(out);
        bh_winding_free(in);
        *inout_w = NULL;
        return;
    }

    bh_winding_free(in);
    *inout_w = out;
}

static void bh_clear_bounds(vec3 *mins, vec3 *maxs)
{
    *mins = (vec3){FLT_MAX, FLT_MAX, FLT_MAX};
    *maxs = (vec3){-FLT_MAX, -FLT_MAX, -FLT_MAX};
}

static void bh_add_point_to_bounds(vec3 p, vec3 *mins, vec3 *maxs)
{
    mins->x = (p.x < mins->x) ? p.x : mins->x;
    mins->y = (p.y < mins->y) ? p.y : mins->y;
    mins->z = (p.z < mins->z) ? p.z : mins->z;

    maxs->x = (p.x > maxs->x) ? p.x : maxs->x;
    maxs->y = (p.y > maxs->y) ? p.y : maxs->y;
    maxs->z = (p.z > maxs->z) ? p.z : maxs->z;
}

/* -----------------------------------------------------------------------------
   Plane Snapping (map.cpp reference)
   ----------------------------------------------------------------------------- */

static bool bh_snap_vector(vec3 *v)
{
    if (bh_absf(v->x - 1.0f) < BH_RENDER_NORMAL_EPSILON)
    {
        *v = (vec3){1, 0, 0};
        return true;
    }
    if (bh_absf(v->x + 1.0f) < BH_RENDER_NORMAL_EPSILON)
    {
        *v = (vec3){-1, 0, 0};
        return true;
    }
    if (bh_absf(v->y - 1.0f) < BH_RENDER_NORMAL_EPSILON)
    {
        *v = (vec3){0, 1, 0};
        return true;
    }
    if (bh_absf(v->y + 1.0f) < BH_RENDER_NORMAL_EPSILON)
    {
        *v = (vec3){0, -1, 0};
        return true;
    }
    if (bh_absf(v->z - 1.0f) < BH_RENDER_NORMAL_EPSILON)
    {
        *v = (vec3){0, 0, 1};
        return true;
    }
    if (bh_absf(v->z + 1.0f) < BH_RENDER_NORMAL_EPSILON)
    {
        *v = (vec3){0, 0, -1};
        return true;
    }
    return false;
}

static bool bh_plane_equal(const BH_TracePlane *p, vec3 normal, float dist, float normal_eps, float dist_eps)
{
    if (bh_absf(p->normal.x - normal.x) > normal_eps)
        return false;
    if (bh_absf(p->normal.y - normal.y) > normal_eps)
        return false;
    if (bh_absf(p->normal.z - normal.z) > normal_eps)
        return false;
    if (bh_absf(p->dist - dist) > dist_eps)
        return false;
    return true;
}

/* -----------------------------------------------------------------------------
   Brush Construction
   ----------------------------------------------------------------------------- */

static bool bh_make_brush_windings(BH_PhysicsBrush *b)
{
    bh_clear_bounds(&b->mins, &b->maxs);

    for (int i = 0; i < b->numsides; ++i)
    {
        BH_PhysicsBrushSide *side = &b->sides[i];
        BH_Winding *w = bh_base_winding_for_plane(side->plane.normal, side->plane.dist);
        if (!w)
        {
            side->winding = NULL;
            continue;
        }

        for (int j = 0; j < b->numsides && w; ++j)
        {
            if (i == j || b->sides[j].bevel)
                continue;

            /* Clip by inverse plane (planenum^1) */
            vec3 n = vec3_scale(b->sides[j].plane.normal, -1.0f);
            float d = -b->sides[j].plane.dist;
            bh_chop_winding_in_place(&w, n, d, BH_BRUSH_CLIP_EPSILON);
        }

        side->winding = w;
        if (w)
        {
            for (int k = 0; k < w->numpoints; ++k)
            {
                bh_add_point_to_bounds(w->p[k], &b->mins, &b->maxs);
            }
        }
    }
    return true;
}

static void bh_free_brush_windings(BH_PhysicsBrush *b)
{
    for (int i = 0; i < b->numsides; ++i)
    {
        if (b->sides[i].winding)
        {
            bh_winding_free(b->sides[i].winding);
            b->sides[i].winding = NULL;
        }
    }
}

/* Matches AddBrushBevels from reference/map.cpp.
   Generates axial and edge bevels for collision sweeping.
*/
static void bh_add_brush_bevels(BH_PhysicsBrush *b)
{
    int order = 0;

    /* Axial Planes */
    for (int axis = 0; axis < 3; ++axis)
    {
        for (int dir = -1; dir <= 1; dir += 2, ++order)
        {
            int i;
            for (i = 0; i < b->numsides; ++i)
            {
                const BH_TracePlane *p = &b->sides[i].plane;
                const float comp = (axis == 0) ? p->normal.x : (axis == 1) ? p->normal.y : p->normal.z;
                if (comp == (float)dir)
                    break;
            }

            if (i == b->numsides)
            {
                if (b->numsides >= BH_PHYS_MAX_SIDES)
                {
                    SDL_Log("[bh_physics] AddBrushBevels: BH_PHYS_MAX_SIDES exceeded");
                    return;
                }

                vec3 normal = {0};
                if (axis == 0)
                    normal.x = (float)dir;
                if (axis == 1)
                    normal.y = (float)dir;
                if (axis == 2)
                    normal.z = (float)dir;

                float dist;
                if (dir == 1)
                    dist = (axis == 0) ? b->maxs.x : (axis == 1) ? b->maxs.y : b->maxs.z;
                else
                    dist = (axis == 0) ? -b->mins.x : (axis == 1) ? -b->mins.y : -b->mins.z;

                b->sides[b->numsides] = (BH_PhysicsBrushSide){
                    .plane = {.normal = normal, .dist = dist, .type = bh_plane_type_from_normal(normal)},
                    .surface_id = b->sides[0].surface_id,
                    .bevel = true,
                    .winding = NULL};
                b->numsides++;
            }

            /* Maintain canonical order */
            if (i != order && order < b->numsides)
            {
                BH_PhysicsBrushSide tmp = b->sides[order];
                b->sides[order] = b->sides[i];
                b->sides[i] = tmp;
            }
        }
    }

    /* Edge Bevels */
    if (b->numsides == 6)
        return;

    for (int i = 6; i < b->numsides; ++i)
    {
        BH_PhysicsBrushSide *s = &b->sides[i];
        BH_Winding *w = s->winding;
        if (!w)
            continue;

        for (int j = 0; j < w->numpoints; ++j)
        {
            int k = (j + 1) % w->numpoints;
            vec3 vec = vec3_sub(w->p[j], w->p[k]);
            if (vec3_norm_inplace(&vec) < 0.5f)
                continue;

            bh_snap_vector(&vec);

            int a;
            for (a = 0; a < 3; ++a)
            {
                float comp = (a == 0) ? vec.x : (a == 1) ? vec.y : vec.z;
                if (comp == -1.0f || comp == 1.0f)
                    break;
            }
            if (a != 3)
                continue; /* Axial */

            for (int axis = 0; axis < 3; ++axis)
            {
                for (int dir = -1; dir <= 1; dir += 2)
                {
                    vec3 vec2 = {0};
                    if (axis == 0)
                        vec2.x = (float)dir;
                    if (axis == 1)
                        vec2.y = (float)dir;
                    if (axis == 2)
                        vec2.z = (float)dir;

                    vec3 normal = vec3_cross(vec, vec2);
                    if (vec3_norm_inplace(&normal) < 0.5f)
                        continue;

                    float dist = vec3_dot(w->p[j], normal);

                    /* Verify all points are behind this plane */
                    int kside;
                    for (kside = 0; kside < b->numsides; ++kside)
                    {
                        if (bh_plane_equal(&b->sides[kside].plane, normal, dist, 0.01f, 0.01f))
                            break;

                        BH_Winding *w2 = b->sides[kside].winding;
                        if (!w2)
                            continue;

                        int l;
                        for (l = 0; l < w2->numpoints; ++l)
                        {
                            if ((vec3_dot(w2->p[l], normal) - dist) > 0.1f)
                                break;
                        }
                        if (l != w2->numpoints)
                            break;
                    }

                    if (kside != b->numsides)
                        continue;

                    if (b->numsides >= BH_PHYS_MAX_SIDES)
                    {
                        SDL_Log("[bh_physics] AddBrushBevels: MAX_SIDES exceeded (edge)");
                        return;
                    }

                    b->sides[b->numsides] = (BH_PhysicsBrushSide){
                        .plane = {.normal = normal, .dist = dist, .type = bh_plane_type_from_normal(normal)},
                        .surface_id = b->sides[0].surface_id,
                        .bevel = true,
                        .winding = NULL};
                    b->numsides++;
                }
            }
        }
    }
}

/* -----------------------------------------------------------------------------
   Trace Logic
   ----------------------------------------------------------------------------- */

/* Matches DM_ClipBoxToBrush from reference/trace.cpp */
static void bh_clip_box_to_brush(BH_TraceResult *trace, bool ispoint, const vec3 mins, const vec3 maxs, const vec3 p1,
                                 const vec3 p2, const BH_PhysicsBrush *brush, int brush_index)
{
    if (!brush->numsides)
        return;

    float enterfrac = BH_NEVER_UPDATED;
    float leavefrac = 1.0f;
    const BH_TracePlane *clipplane = NULL;
    const BH_PhysicsBrushSide *leadside = NULL;
    bool getout = false;
    bool startout = false;

    for (int i = 0; i < brush->numsides; ++i)
    {
        const BH_PhysicsBrushSide *side = &brush->sides[i];
        const BH_TracePlane *plane = &side->plane;

        if (ispoint && side->bevel)
            continue;

        float dist = plane->dist;
        if (!ispoint)
        {
            vec3 ofs;
            ofs.x = (plane->normal.x < 0.0f) ? maxs.x : mins.x;
            ofs.y = (plane->normal.y < 0.0f) ? maxs.y : mins.y;
            ofs.z = (plane->normal.z < 0.0f) ? maxs.z : mins.z;
            dist -= vec3_dot(ofs, plane->normal);
        }

        float d1 = vec3_dot(p1, plane->normal) - dist;
        float d2 = vec3_dot(p2, plane->normal) - dist;

        if (d1 > 0.0f && d2 > 0.0f)
            return;
        if (d2 > 0.0f)
            getout = true;
        if (d1 > 0.0f)
            startout = true;

        if (d1 <= 0.0f && d2 <= 0.0f)
            continue;

        if (d1 > d2)
        { /* Enter */
            float f = (d1 - BH_DIST_EPSILON) / (d1 - d2);
            if (f > enterfrac)
            {
                enterfrac = f;
                clipplane = plane;
                leadside = side;
            }
        }
        else
        { /* Leave */
            float f = (d1 + BH_DIST_EPSILON) / (d1 - d2);
            if (f < leavefrac)
            {
                leavefrac = f;
            }
        }
    }

    if (!startout)
    {
        trace->startsolid = true;
        if (!getout)
            trace->allsolid = true;
        trace->contents = brush->contents;
        trace->brush_index = brush_index;
        return;
    }

    if (enterfrac < leavefrac)
    {
        if (enterfrac > BH_NEVER_UPDATED && enterfrac < trace->fraction)
        {
            if (enterfrac < 0.0f)
                enterfrac = 0.0f;
            trace->fraction = enterfrac;
            if (clipplane)
                trace->plane = *clipplane;
            if (leadside)
                trace->surface_id = leadside->surface_id;
            trace->contents = brush->contents;
            trace->brush_index = brush_index;
        }
    }
}

/* -----------------------------------------------------------------------------
   Public API Implementation
   ----------------------------------------------------------------------------- */

bool BH_Physics_Init(BH_PhysicsWorld *world, BH_Arena *arena, uint32_t initial_brush_capacity)
{
    if (!world || !arena)
        return false;

    *world = (BH_PhysicsWorld){0};
    world->arena = arena;
    world->brush_cap = (initial_brush_capacity > 0) ? initial_brush_capacity : BH_PHYS_DEFAULT_BRUSH_CAP;

    size_t sz = (size_t)world->brush_cap * sizeof(BH_PhysicsBrush);
    world->brushes = (BH_PhysicsBrush *)BH_Arena_Alloc(arena, sz, 8);

    if (!world->brushes)
        return false;
    memset(world->brushes, 0, sz);
    return true;
}

void BH_Physics_Reset(BH_PhysicsWorld *world)
{
    if (world)
        world->brush_count = 0;
}

static bool bh_physics_grow(BH_PhysicsWorld *world)
{
    uint32_t new_cap = (world->brush_cap > 0) ? world->brush_cap * 2u : BH_PHYS_DEFAULT_BRUSH_CAP;
    size_t sz = (size_t)new_cap * sizeof(BH_PhysicsBrush);
    BH_PhysicsBrush *new_mem = (BH_PhysicsBrush *)BH_Arena_Alloc(world->arena, sz, 8);

    if (!new_mem)
        return false;

    memset(new_mem, 0, sz);
    if (world->brushes && world->brush_count > 0)
    {
        memcpy(new_mem, world->brushes, (size_t)world->brush_count * sizeof(BH_PhysicsBrush));
    }

    world->brushes = new_mem;
    world->brush_cap = new_cap;
    return true;
}

int BH_Physics_AddBrushAABB(BH_PhysicsWorld *world, vec3 mins, vec3 maxs, BH_Contents contents, int surface_id,
                            bool add_bevels_optional)
{
    if (!world)
        return -1;
    if (world->brush_count >= world->brush_cap && !bh_physics_grow(world))
        return -1;

    int idx = (int)world->brush_count;
    BH_PhysicsBrush *b = &world->brushes[world->brush_count++];
    *b = (BH_PhysicsBrush){.contents = contents, .mins = mins, .maxs = maxs, .numsides = 6};

    b->sides[0] =
        (BH_PhysicsBrushSide){.plane = {.normal = {-1, 0, 0}, .dist = -mins.x, .type = 0}, .surface_id = surface_id};
    b->sides[1] =
        (BH_PhysicsBrushSide){.plane = {.normal = {1, 0, 0}, .dist = maxs.x, .type = 0}, .surface_id = surface_id};
    b->sides[2] =
        (BH_PhysicsBrushSide){.plane = {.normal = {0, -1, 0}, .dist = -mins.y, .type = 1}, .surface_id = surface_id};
    b->sides[3] =
        (BH_PhysicsBrushSide){.plane = {.normal = {0, 1, 0}, .dist = maxs.y, .type = 1}, .surface_id = surface_id};
    b->sides[4] =
        (BH_PhysicsBrushSide){.plane = {.normal = {0, 0, -1}, .dist = -mins.z, .type = 2}, .surface_id = surface_id};
    b->sides[5] =
        (BH_PhysicsBrushSide){.plane = {.normal = {0, 0, 1}, .dist = maxs.z, .type = 2}, .surface_id = surface_id};

    if (add_bevels_optional)
    {
        bh_make_brush_windings(b);
        bh_add_brush_bevels(b);
        bh_free_brush_windings(b);
    }

    return idx;
}

int BH_PhysicsAddBrushConvex(BH_PhysicsWorld *world, const BH_TracePlane *planes, uint32_t plane_count,
                             BH_Contents contents, int surface_id, bool add_bevels_optional)
{
    if (!world || !planes || plane_count == 0)
        return -1;
    if (plane_count >= (uint32_t)(BH_PHYS_MAX_SIDES - 8))
    {
        SDL_Log("[bh_physics] AddBrushConvex: too many planes (%u)", (unsigned)plane_count);
        return -1;
    }
    if (world->brush_count >= world->brush_cap && !bh_physics_grow(world))
        return -1;

    int idx = (int)world->brush_count;
    BH_PhysicsBrush *b = &world->brushes[world->brush_count++];
    *b = (BH_PhysicsBrush){.contents = contents, .numsides = (int)plane_count};

    for (uint32_t i = 0; i < plane_count; ++i)
    {
        b->sides[i] = (BH_PhysicsBrushSide){
            .plane = planes[i],
            .surface_id = surface_id,
        };
        b->sides[i].plane.type = bh_plane_type_from_normal(planes[i].normal);
    }

    bh_make_brush_windings(b);

    if (b->mins.x == FLT_MAX)
    {
        b->mins = (vec3){0};
        b->maxs = (vec3){0};
    }

    if (add_bevels_optional)
    {
        bh_add_brush_bevels(b);
    }

    bh_free_brush_windings(b);
    return idx;
}

void BH_Physics_Trace(const BH_PhysicsWorld *world, vec3 start, vec3 end, vec3 mins, vec3 maxs, BH_Contents mask,
                      int ignore_brush_index, BH_TraceResult *out)
{
    if (!out)
        return;
    *out = (BH_TraceResult){.fraction = 1.0f, .startpos = start, .endpos = end, .surface_id = -1, .brush_index = -1};

    if (!world || !world->brushes || world->brush_count == 0)
        return;

    bool ispoint = bh_vec3_is_zero(mins) && bh_vec3_is_zero(maxs);

    vec3 s_mins = vec3_add(start, mins);
    vec3 s_maxs = vec3_add(start, maxs);
    vec3 e_mins = vec3_add(end, mins);
    vec3 e_maxs = vec3_add(end, maxs);
    vec3 sweep_mins = bh_vec3_min(s_mins, e_mins);
    vec3 sweep_maxs = bh_vec3_max(s_maxs, e_maxs);

    for (uint32_t i = 0; i < world->brush_count; ++i)
    {
        if ((int)i == ignore_brush_index)
            continue;

        const BH_PhysicsBrush *b = &world->brushes[i];
        if ((b->contents & mask) == 0)
            continue;
        if (!bh_aabb_overlaps(sweep_mins, sweep_maxs, b->mins, b->maxs))
            continue;

        bh_clip_box_to_brush(out, ispoint, mins, maxs, start, end, b, (int)i);

        if (out->allsolid)
        {
            out->fraction = 0.0f;
            out->endpos = start;
            return;
        }
    }

    if (out->startsolid)
    {
        out->fraction = 0.0f;
        out->endpos = start;
        return;
    }

    vec3 delta = vec3_sub(end, start);
    out->endpos = vec3_add(start, vec3_scale(delta, out->fraction));
}

void BH_Physics_DebugDraw(const BH_PhysicsWorld *world, float duration_s)
{
    if (!world || !world->brushes)
        return;

    for (uint32_t i = 0; i < world->brush_count; ++i)
    {
        const BH_PhysicsBrush *b = &world->brushes[i];
        color4f c = BH_COLOR_RGBA(0.2f, 0.9f, 0.35f, 0.35f);

        if (b->sides[0].surface_id == 1)
            c = BH_COLOR_RGBA(0.75f, 0.75f, 0.75f, 0.25f);
        else if (b->sides[0].surface_id == 2)
            c = BH_COLOR_RGBA(1.0f, 0.25f, 0.25f, 0.35f);

        BH_DebugDraw_DrawBounds(b->mins, b->maxs, c, duration_s);
    }
}