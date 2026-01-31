/* -----------------------------------------------------------------------------
   bh_debug_draw.c
   ----------------------------------------------------------------------------- */

#include "bh_debug_draw.h"
#include "bh_debug_draw_internal.h"

#include "../render/bh_dbg_prims.h"

#include <SDL3/SDL.h>
#include <float.h>
#include <stddef.h>
#include <string.h>

static BH_DebugDraw *g_dbg_active = NULL;

static inline BH_DbgState *bh_dbg_state(BH_DebugDraw *dd)
{
    return &dd->state_stack[dd->state_top ? (dd->state_top - 1) : 0];
}

static double bh_dbg_end_time(BH_DebugDraw *dd, float duration_s)
{
    if (duration_s < 0.0f)
    {
        return DBL_MAX;
    }
    return dd->time_s + (double)duration_s;
}

static bool bh_dbg_reserve_lines(BH_DebugDraw *dd, uint32_t additional)
{
    const uint32_t needed = dd->line_count + additional;
    if (needed <= dd->line_cap)
    {
        return true;
    }

    uint32_t new_cap = dd->line_cap ? dd->line_cap : 256;
    while (new_cap < needed)
    {
        new_cap *= 2;
    }

    BH_DbgLine *new_mem = (BH_DbgLine *)SDL_realloc(dd->lines, (size_t)new_cap * sizeof(BH_DbgLine));
    if (!new_mem)
    {
        return false;
    }

    dd->lines = new_mem;
    dd->line_cap = new_cap;
    return true;
}

static bool bh_dbg_reserve_tris(BH_DebugDraw *dd, uint32_t additional)
{
    const uint32_t needed = dd->tri_count + additional;
    if (needed <= dd->tri_cap)
    {
        return true;
    }

    uint32_t new_cap = dd->tri_cap ? dd->tri_cap : 256;
    while (new_cap < needed)
    {
        new_cap *= 2;
    }

    BH_DbgTri *new_mem = (BH_DbgTri *)SDL_realloc(dd->tris, (size_t)new_cap * sizeof(BH_DbgTri));
    if (!new_mem)
    {
        return false;
    }

    dd->tris = new_mem;
    dd->tri_cap = new_cap;
    return true;
}

static bool bh_dbg_reserve_vertices(BH_DbgVertex **ptr, uint32_t *cap, uint32_t needed)
{
    if (needed <= *cap)
    {
        return true;
    }

    uint32_t new_cap = (*cap) ? (*cap) : 512;
    while (new_cap < needed)
    {
        new_cap *= 2;
    }

    BH_DbgVertex *new_mem = (BH_DbgVertex *)SDL_realloc(*ptr, (size_t)new_cap * sizeof(BH_DbgVertex));
    if (!new_mem)
    {
        return false;
    }

    *ptr = new_mem;
    *cap = new_cap;
    return true;
}

static void bh_dbg_add_line_internal(BH_DebugDraw *dd, vec3 a, vec3 b, color4f color, float duration_s)
{
    if (!bh_dbg_reserve_lines(dd, 1))
    {
        return;
    }

    BH_DbgState *st = bh_dbg_state(dd);
    dd->lines[dd->line_count++] = (BH_DbgLine){
        .a = a,
        .b = b,
        .color = color,
        .end_time_s = bh_dbg_end_time(dd, duration_s),
        .depth = st ? st->depth : BH_DBG_DEPTH_TEST,
    };

    dd->dirty = true;
}

static void bh_dbg_add_tri_internal(BH_DebugDraw *dd, vec3 a, vec3 b, vec3 c, color4f color, float duration_s)
{
    if (!bh_dbg_reserve_tris(dd, 1))
    {
        return;
    }

    BH_DbgState *st = bh_dbg_state(dd);
    dd->tris[dd->tri_count++] = (BH_DbgTri){
        .a = a,
        .b = b,
        .c = c,
        .color = color,
        .end_time_s = bh_dbg_end_time(dd, duration_s),
        .depth = st ? st->depth : BH_DBG_DEPTH_TEST,
    };

    dd->dirty = true;
}

static void bh_dbg_rebuild_vertices(BH_DebugDraw *dd)
{
    uint32_t line_depth = 0;
    uint32_t line_always = 0;

    for (uint32_t i = 0; i < dd->line_count; ++i)
    {
        if (dd->lines[i].depth == BH_DBG_DEPTH_ALWAYS)
        {
            line_always += 2;
        }
        else
        {
            line_depth += 2;
        }
    }

    uint32_t tri_depth = 0;
    uint32_t tri_always = 0;

    for (uint32_t i = 0; i < dd->tri_count; ++i)
    {
        if (dd->tris[i].depth == BH_DBG_DEPTH_ALWAYS)
        {
            tri_always += 3;
        }
        else
        {
            tri_depth += 3;
        }
    }

    (void)bh_dbg_reserve_vertices(&dd->cpu_line_depth, &dd->cpu_line_depth_cap, line_depth);
    (void)bh_dbg_reserve_vertices(&dd->cpu_line_always, &dd->cpu_line_always_cap, line_always);
    (void)bh_dbg_reserve_vertices(&dd->cpu_tri_depth, &dd->cpu_tri_depth_cap, tri_depth);
    (void)bh_dbg_reserve_vertices(&dd->cpu_tri_always, &dd->cpu_tri_always_cap, tri_always);

    dd->line_depth_vert_count = 0;
    dd->line_always_vert_count = 0;
    dd->tri_depth_vert_count = 0;
    dd->tri_always_vert_count = 0;

    for (uint32_t i = 0; i < dd->line_count; ++i)
    {
        const BH_DbgLine *ln = &dd->lines[i];
        const bool always = (ln->depth == BH_DBG_DEPTH_ALWAYS);

        BH_DbgVertex *dst = always ? &dd->cpu_line_always[dd->line_always_vert_count]
                                   : &dd->cpu_line_depth[dd->line_depth_vert_count];

        dst[0] = (BH_DbgVertex){.position = ln->a, .color = ln->color};
        dst[1] = (BH_DbgVertex){.position = ln->b, .color = ln->color};

        if (always)
        {
            dd->line_always_vert_count += 2;
        }
        else
        {
            dd->line_depth_vert_count += 2;
        }
    }

    for (uint32_t i = 0; i < dd->tri_count; ++i)
    {
        const BH_DbgTri *tr = &dd->tris[i];
        const bool always = (tr->depth == BH_DBG_DEPTH_ALWAYS);

        BH_DbgVertex *dst = always ? &dd->cpu_tri_always[dd->tri_always_vert_count]
                                   : &dd->cpu_tri_depth[dd->tri_depth_vert_count];

        dst[0] = (BH_DbgVertex){.position = tr->a, .color = tr->color};
        dst[1] = (BH_DbgVertex){.position = tr->b, .color = tr->color};
        dst[2] = (BH_DbgVertex){.position = tr->c, .color = tr->color};

        if (always)
        {
            dd->tri_always_vert_count += 3;
        }
        else
        {
            dd->tri_depth_vert_count += 3;
        }
    }
}

void BH_DebugDraw_GetBatches(BH_DebugDraw *dd, BH_DbgBatches *out)
{
    if (!out)
    {
        return;
    }

    *out = (BH_DbgBatches){0};

    if (!dd)
    {
        return;
    }

    if (dd->dirty)
    {
        bh_dbg_rebuild_vertices(dd);
        dd->dirty = false;
    }

    out->line_depth = dd->cpu_line_depth;
    out->line_depth_count = dd->line_depth_vert_count;

    out->line_always = dd->cpu_line_always;
    out->line_always_count = dd->line_always_vert_count;

    out->tri_depth = dd->cpu_tri_depth;
    out->tri_depth_count = dd->tri_depth_vert_count;

    out->tri_always = dd->cpu_tri_always;
    out->tri_always_count = dd->tri_always_vert_count;
}

bool BH_DebugDraw_Init(BH_DebugDraw *dd, BH_Renderer *renderer, const char *asset_root, BH_Arena *permanent_arena)
{
    if (!dd || !renderer || !asset_root || !permanent_arena)
    {
        return false;
    }

    *dd = (BH_DebugDraw){0};
    dd->dirty = true;
    dd->state_top = 1;
    dd->state_stack[0].depth = BH_DBG_DEPTH_TEST;
    dd->state_stack[0].default_color = BH_COLOR_WHITE;

    if (!BH_DbgPrims_Attach(dd, renderer, asset_root, permanent_arena))
    {
        BH_DebugDraw_Shutdown(dd);
        return false;
    }

    return true;
}

void BH_DebugDraw_Shutdown(BH_DebugDraw *dd)
{
    if (!dd)
    {
        return;
    }

    if (g_dbg_active == dd)
    {
        g_dbg_active = NULL;
    }

    BH_DbgPrims_Detach(dd);

    SDL_free(dd->lines);
    SDL_free(dd->tris);

    SDL_free(dd->cpu_line_depth);
    SDL_free(dd->cpu_line_always);
    SDL_free(dd->cpu_tri_depth);
    SDL_free(dd->cpu_tri_always);

    *dd = (BH_DebugDraw){0};
}

void BH_DebugDraw_Tick(BH_DebugDraw *dd, float dt_s)
{
    if (!dd || dt_s <= 0.0f)
    {
        return;
    }

    dd->time_s += (double)dt_s;
    bool removed_any = false;

    uint32_t line_out = 0;
    for (uint32_t i = 0; i < dd->line_count; ++i)
    {
        if (dd->lines[i].end_time_s == DBL_MAX || dd->lines[i].end_time_s > dd->time_s)
        {
            if (line_out != i)
            {
                dd->lines[line_out] = dd->lines[i];
            }
            ++line_out;
        }
        else
        {
            removed_any = true;
        }
    }
    dd->line_count = line_out;

    uint32_t tri_out = 0;
    for (uint32_t i = 0; i < dd->tri_count; ++i)
    {
        if (dd->tris[i].end_time_s == DBL_MAX || dd->tris[i].end_time_s > dd->time_s)
        {
            if (tri_out != i)
            {
                dd->tris[tri_out] = dd->tris[i];
            }
            ++tri_out;
        }
        else
        {
            removed_any = true;
        }
    }
    dd->tri_count = tri_out;

    if (removed_any)
    {
        dd->dirty = true;
    }
}

void BH_DebugDraw_SetActive(BH_DebugDraw *dd)
{
    g_dbg_active = dd;
}

BH_DebugDraw *BH_DebugDraw_GetActive(void)
{
    return g_dbg_active;
}

void BH_DebugDraw_Push(void)
{
    BH_DebugDraw *dd = BH_DebugDraw_GetActive();
    if (!dd)
    {
        return;
    }

    if (dd->state_top < (uint32_t)(sizeof(dd->state_stack) / sizeof(dd->state_stack[0])))
    {
        dd->state_stack[dd->state_top] = dd->state_stack[dd->state_top - 1];
        dd->state_top++;
    }
}

void BH_DebugDraw_Pop(void)
{
    BH_DebugDraw *dd = BH_DebugDraw_GetActive();
    if (dd && dd->state_top > 1)
    {
        dd->state_top--;
    }
}

void BH_DebugDraw_SetDepthMode(BH_DBG_DepthMode mode)
{
    BH_DebugDraw *dd = BH_DebugDraw_GetActive();
    if (dd)
    {
        bh_dbg_state(dd)->depth = mode;
    }
}

BH_DBG_DepthMode BH_DebugDraw_GetDepthMode(void)
{
    BH_DebugDraw *dd = BH_DebugDraw_GetActive();
    return dd ? bh_dbg_state(dd)->depth : BH_DBG_DEPTH_TEST;
}

void BH_DebugDraw_SetDefaultColor(color4f color)
{
    BH_DebugDraw *dd = BH_DebugDraw_GetActive();
    if (dd)
    {
        bh_dbg_state(dd)->default_color = color;
    }
}

color4f BH_DebugDraw_GetDefaultColor(void)
{
    BH_DebugDraw *dd = BH_DebugDraw_GetActive();
    return dd ? bh_dbg_state(dd)->default_color : BH_COLOR_WHITE;
}

void BH_DebugDraw_DrawLine(vec3 a, vec3 b, color4f color, float duration_s)
{
    BH_DebugDraw *dd = BH_DebugDraw_GetActive();
    if (dd)
    {
        bh_dbg_add_line_internal(dd, a, b, color, duration_s);
    }
}

void BH_DebugDraw_DrawTriangle(vec3 a, vec3 b, vec3 c, color4f color, float duration_s)
{
    BH_DebugDraw *dd = BH_DebugDraw_GetActive();
    if (dd)
    {
        bh_dbg_add_tri_internal(dd, a, b, c, color, duration_s);
    }
}

void BH_DebugDraw_DrawGrid(vec3 origin, float size_x, float size_y, float cell_size, color4f color, float duration_s)
{
    BH_DebugDraw *dd = BH_DebugDraw_GetActive();
    if (!dd || cell_size <= 0.0f)
    {
        return;
    }

    if (color.a == 0.0f)
    {
        color = BH_DebugDraw_GetDefaultColor();
    }

    const int nx = (int)(size_x / cell_size);
    const int ny = (int)(size_y / cell_size);
    const float half_x = 0.5f * size_x;
    const float half_y = 0.5f * size_y;

    for (int ix = 0; ix <= nx; ++ix)
    {
        const float x = origin.x - half_x + (float)ix * cell_size;
        const vec3 a = {x, origin.y - half_y, origin.z};
        const vec3 b = {x, origin.y + half_y, origin.z};
        bh_dbg_add_line_internal(dd, a, b, color, duration_s);
    }

    for (int iy = 0; iy <= ny; ++iy)
    {
        const float y = origin.y - half_y + (float)iy * cell_size;
        const vec3 a = {origin.x - half_x, y, origin.z};
        const vec3 b = {origin.x + half_x, y, origin.z};
        bh_dbg_add_line_internal(dd, a, b, color, duration_s);
    }
}

void BH_DebugDraw_DrawBounds(vec3 mins, vec3 maxs, color4f color, float duration_s)
{
    vec3 center = vec3_scale(vec3_add(mins, maxs), 0.5f);
    vec3 half_ext = vec3_scale(vec3_sub(maxs, mins), 0.5f);
    BH_DebugDraw_DrawWireCuboid(center, half_ext, color, duration_s);
}

void BH_DebugDraw_DrawWireCube(vec3 center, float half_extent, color4f color, float duration_s)
{
    BH_DebugDraw_DrawWireCuboid(center, (vec3){half_extent, half_extent, half_extent}, color, duration_s);
}

void BH_DebugDraw_DrawCube(vec3 center, float half_extent, color4f color, float duration_s)
{
    BH_DebugDraw_DrawCuboid(center, (vec3){half_extent, half_extent, half_extent}, color, duration_s);
}

void BH_DebugDraw_DrawWireCuboid(vec3 center, vec3 half_extents, color4f color, float duration_s)
{
    BH_DebugDraw *dd = BH_DebugDraw_GetActive();
    if (!dd)
    {
        return;
    }

    if (color.a == 0.0f)
    {
        color = BH_DebugDraw_GetDefaultColor();
    }

    const vec3 mn = vec3_sub(center, half_extents);
    const vec3 mx = vec3_add(center, half_extents);

    const vec3 p000 = {mn.x, mn.y, mn.z};
    const vec3 p100 = {mx.x, mn.y, mn.z};
    const vec3 p010 = {mn.x, mx.y, mn.z};
    const vec3 p110 = {mx.x, mx.y, mn.z};

    const vec3 p001 = {mn.x, mn.y, mx.z};
    const vec3 p101 = {mx.x, mn.y, mx.z};
    const vec3 p011 = {mn.x, mx.y, mx.z};
    const vec3 p111 = {mx.x, mx.y, mx.z};

    bh_dbg_add_line_internal(dd, p000, p100, color, duration_s);
    bh_dbg_add_line_internal(dd, p100, p110, color, duration_s);
    bh_dbg_add_line_internal(dd, p110, p010, color, duration_s);
    bh_dbg_add_line_internal(dd, p010, p000, color, duration_s);

    bh_dbg_add_line_internal(dd, p001, p101, color, duration_s);
    bh_dbg_add_line_internal(dd, p101, p111, color, duration_s);
    bh_dbg_add_line_internal(dd, p111, p011, color, duration_s);
    bh_dbg_add_line_internal(dd, p011, p001, color, duration_s);

    bh_dbg_add_line_internal(dd, p000, p001, color, duration_s);
    bh_dbg_add_line_internal(dd, p100, p101, color, duration_s);
    bh_dbg_add_line_internal(dd, p110, p111, color, duration_s);
    bh_dbg_add_line_internal(dd, p010, p011, color, duration_s);
}

void BH_DebugDraw_DrawCuboid(vec3 center, vec3 half_extents, color4f color, float duration_s)
{
    BH_DebugDraw *dd = BH_DebugDraw_GetActive();
    if (!dd)
    {
        return;
    }

    if (color.a == 0.0f)
    {
        color = BH_DebugDraw_GetDefaultColor();
    }

    const vec3 mn = vec3_sub(center, half_extents);
    const vec3 mx = vec3_add(center, half_extents);

    const vec3 p000 = {mn.x, mn.y, mn.z};
    const vec3 p100 = {mx.x, mn.y, mn.z};
    const vec3 p010 = {mn.x, mx.y, mn.z};
    const vec3 p110 = {mx.x, mx.y, mn.z};

    const vec3 p001 = {mn.x, mn.y, mx.z};
    const vec3 p101 = {mx.x, mn.y, mx.z};
    const vec3 p011 = {mn.x, mx.y, mx.z};
    const vec3 p111 = {mx.x, mx.y, mx.z};

    bh_dbg_add_tri_internal(dd, p000, p010, p110, color, duration_s);
    bh_dbg_add_tri_internal(dd, p000, p110, p100, color, duration_s);

    bh_dbg_add_tri_internal(dd, p001, p101, p111, color, duration_s);
    bh_dbg_add_tri_internal(dd, p001, p111, p011, color, duration_s);

    bh_dbg_add_tri_internal(dd, p000, p100, p101, color, duration_s);
    bh_dbg_add_tri_internal(dd, p000, p101, p001, color, duration_s);

    bh_dbg_add_tri_internal(dd, p010, p011, p111, color, duration_s);
    bh_dbg_add_tri_internal(dd, p010, p111, p110, color, duration_s);

    bh_dbg_add_tri_internal(dd, p000, p001, p011, color, duration_s);
    bh_dbg_add_tri_internal(dd, p000, p011, p010, color, duration_s);

    bh_dbg_add_tri_internal(dd, p100, p110, p111, color, duration_s);
    bh_dbg_add_tri_internal(dd, p100, p111, p101, color, duration_s);
}
