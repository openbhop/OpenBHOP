/* -----------------------------------------------------------------------------
   bh_debug_draw.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../math/bh_color.h"
#include "../math/bh_math.h"
#include "../render/bh_shader_program.h"

typedef struct BH_Renderer BH_Renderer;
typedef struct BH_Arena BH_Arena;

#ifdef __cplusplus
extern "C"
{
#endif

    /* -----------------------------------------------------------------------------
       Debug Draw API
       Queue primitives for late-pass rendering.
       Duration: < 0 (infinite), 0 (one frame), > 0 (seconds).
       ----------------------------------------------------------------------------- */

    typedef enum BH_DBG_DepthMode
    {
        BH_DBG_DEPTH_TEST = 0,
        BH_DBG_DEPTH_ALWAYS = 1,
    } BH_DBG_DepthMode;

    typedef struct BH_DbgState
    {
        BH_DBG_DepthMode depth;
        color4f default_color;
    } BH_DbgState;

    typedef struct BH_DbgVertex
    {
        vec3 position;
        color4f color;
    } BH_DbgVertex;

    typedef struct BH_DbgLine
    {
        vec3 a;
        vec3 b;
        color4f color;
        double end_time_s;
        BH_DBG_DepthMode depth;
    } BH_DbgLine;

    typedef struct BH_DbgTri
    {
        vec3 a;
        vec3 b;
        vec3 c;
        color4f color;
        double end_time_s;
        BH_DBG_DepthMode depth;
    } BH_DbgTri;

    typedef struct BH_DebugDraw
    {
        SDL_GPUDevice *device;

        BH_ShaderProgram line_depth;
        BH_ShaderProgram line_always;
        BH_ShaderProgram tri_depth;
        BH_ShaderProgram tri_always;

        SDL_GPUBuffer *vb_line_depth;
        SDL_GPUBuffer *vb_line_always;
        SDL_GPUBuffer *vb_tri_depth;
        SDL_GPUBuffer *vb_tri_always;

        uint32_t vb_line_depth_bytes;
        uint32_t vb_line_always_bytes;
        uint32_t vb_tri_depth_bytes;
        uint32_t vb_tri_always_bytes;

        BH_DbgVertex *cpu_line_depth;
        BH_DbgVertex *cpu_line_always;
        BH_DbgVertex *cpu_tri_depth;
        BH_DbgVertex *cpu_tri_always;

        uint32_t cpu_line_depth_cap;
        uint32_t cpu_line_always_cap;
        uint32_t cpu_tri_depth_cap;
        uint32_t cpu_tri_always_cap;

        uint32_t line_depth_vert_count;
        uint32_t line_always_vert_count;
        uint32_t tri_depth_vert_count;
        uint32_t tri_always_vert_count;

        BH_DbgLine *lines;
        uint32_t line_count;
        uint32_t line_cap;

        BH_DbgTri *tris;
        uint32_t tri_count;
        uint32_t tri_cap;

        double time_s;
        bool dirty;

        BH_DbgState state_stack[16];
        uint32_t state_top;

        SDL_GPUTransferBuffer **pending_tbufs;
        uint32_t pending_tbuf_count;
        uint32_t pending_tbuf_cap;

        bool logged_missing_mvp;
    } BH_DebugDraw;

    /* -----------------------------------------------------------------------------
       System Lifecycle
       ----------------------------------------------------------------------------- */

    bool BH_DebugDraw_Init(BH_DebugDraw *dd, BH_Renderer *renderer, const char *asset_root, BH_Arena *permanent_arena);
    void BH_DebugDraw_Shutdown(BH_DebugDraw *dd);
    void BH_DebugDraw_Tick(BH_DebugDraw *dd, float dt_s);

    /* -----------------------------------------------------------------------------
       Context & State
       ----------------------------------------------------------------------------- */

    void BH_DebugDraw_SetActive(BH_DebugDraw *dd);
    BH_DebugDraw *BH_DebugDraw_GetActive(void);

    void BH_DebugDraw_Push(void);
    void BH_DebugDraw_Pop(void);
    void BH_DebugDraw_SetDepthMode(BH_DBG_DepthMode mode);
    BH_DBG_DepthMode BH_DebugDraw_GetDepthMode(void);
    void BH_DebugDraw_SetDefaultColor(color4f color);
    color4f BH_DebugDraw_GetDefaultColor(void);

    /* -----------------------------------------------------------------------------
       Primitives
       ----------------------------------------------------------------------------- */

    void BH_DebugDraw_DrawLine(vec3 a, vec3 b, color4f color, float duration_s);
    void BH_DebugDraw_DrawTriangle(vec3 a, vec3 b, vec3 c, color4f color, float duration_s);
    void BH_DebugDraw_DrawGrid(vec3 origin, float size_x, float size_y, float cell_size, color4f color,
                               float duration_s);
    void BH_DebugDraw_DrawBounds(vec3 mins, vec3 maxs, color4f color, float duration_s);
    void BH_DebugDraw_DrawWireCube(vec3 center, float half_extent, color4f color, float duration_s);
    void BH_DebugDraw_DrawCube(vec3 center, float half_extent, color4f color, float duration_s);
    void BH_DebugDraw_DrawWireCuboid(vec3 center, vec3 half_extents, color4f color, float duration_s);
    void BH_DebugDraw_DrawCuboid(vec3 center, vec3 half_extents, color4f color, float duration_s);

#ifdef __cplusplus
}
#endif
