/* -----------------------------------------------------------------------------
   bh_debug_draw.c
   ----------------------------------------------------------------------------- */

#include "bh_debug_draw.h"

#include "../render/bh_material.h"
#include "../render/bh_renderer.h"

#include <SDL3/SDL.h>
#include <float.h>
#include <stddef.h>
#include <string.h>

static BH_DebugDraw *g_dbg_active = NULL;

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

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

static bool bh_dbg_pending_push_tbuf(BH_DebugDraw *dd, SDL_GPUTransferBuffer *tbuf)
{
    if (dd->pending_tbuf_count >= dd->pending_tbuf_cap)
    {
        uint32_t new_cap = dd->pending_tbuf_cap ? dd->pending_tbuf_cap * 2 : 16;
        SDL_GPUTransferBuffer **new_mem =
            (SDL_GPUTransferBuffer **)SDL_realloc(dd->pending_tbufs, (size_t)new_cap * sizeof(SDL_GPUTransferBuffer *));
        if (!new_mem)
        {
            return false;
        }
        dd->pending_tbufs = new_mem;
        dd->pending_tbuf_cap = new_cap;
    }

    dd->pending_tbufs[dd->pending_tbuf_count++] = tbuf;
    return true;
}

static SDL_GPUColorTargetBlendState bh_dbg_alpha_blend_state(void)
{
    return (SDL_GPUColorTargetBlendState){
        .enable_blend = true,
        .color_blend_op = SDL_GPU_BLENDOP_ADD,
        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .enable_color_write_mask = false,
    };
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

        BH_DbgVertex *dst =
            always ? &dd->cpu_line_always[dd->line_always_vert_count] : &dd->cpu_line_depth[dd->line_depth_vert_count];

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

        BH_DbgVertex *dst =
            always ? &dd->cpu_tri_always[dd->tri_always_vert_count] : &dd->cpu_tri_depth[dd->tri_depth_vert_count];

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

static SDL_GPUBuffer *bh_dbg_ensure_vb(BH_DebugDraw *dd, SDL_GPUBuffer *vb, uint32_t *vb_bytes, uint32_t needed_bytes)
{
    if (needed_bytes == 0)
    {
        return vb;
    }

    if (vb && *vb_bytes >= needed_bytes)
    {
        return vb;
    }

    if (vb)
    {
        SDL_ReleaseGPUBuffer(dd->device, vb);
        vb = NULL;
    }

    uint32_t alloc_bytes = 4096;
    while (alloc_bytes < needed_bytes)
    {
        alloc_bytes *= 2;
    }

    SDL_GPUBufferCreateInfo bci = {0};
    bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bci.size = alloc_bytes;

    vb = SDL_CreateGPUBuffer(dd->device, &bci);
    if (!vb)
    {
        SDL_Log("[bh][dbg] SDL_CreateGPUBuffer failed: %s", SDL_GetError());
        *vb_bytes = 0;
        return NULL;
    }

    *vb_bytes = alloc_bytes;
    return vb;
}

static bool bh_dbg_upload_vertices(BH_DebugDraw *dd, SDL_GPUCopyPass *copy_pass, SDL_GPUBuffer *dst_buffer,
                                   const BH_DbgVertex *src, uint32_t vertex_count)
{
    if (!dst_buffer || !src || vertex_count == 0)
    {
        return true;
    }

    const uint32_t size = vertex_count * (uint32_t)sizeof(BH_DbgVertex);

    SDL_GPUTransferBufferCreateInfo tci = {0};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = size;

    SDL_GPUTransferBuffer *tbuf = SDL_CreateGPUTransferBuffer(dd->device, &tci);
    if (!tbuf)
    {
        SDL_Log("[bh][dbg] SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        return false;
    }

    void *mapped = SDL_MapGPUTransferBuffer(dd->device, tbuf, false);
    if (!mapped)
    {
        SDL_Log("[bh][dbg] SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(dd->device, tbuf);
        return false;
    }

    memcpy(mapped, src, size);
    SDL_UnmapGPUTransferBuffer(dd->device, tbuf);

    SDL_UploadToGPUBuffer(copy_pass, &(SDL_GPUTransferBufferLocation){.transfer_buffer = tbuf},
                          &(SDL_GPUBufferRegion){.buffer = dst_buffer, .size = size}, false);

    (void)bh_dbg_pending_push_tbuf(dd, tbuf);
    return true;
}

static void bh_dbg_prepare_cb(void *user, SDL_GPUCommandBuffer *cmd, SDL_GPUCopyPass *copy_pass, const mat4 *view_proj,
                              uint32_t fb_width, uint32_t fb_height, float alpha)
{
    (void)cmd;
    (void)view_proj;
    (void)fb_width;
    (void)fb_height;
    (void)alpha;

    BH_DebugDraw *dd = (BH_DebugDraw *)user;
    if (!dd || !dd->dirty)
    {
        return;
    }

    bh_dbg_rebuild_vertices(dd);

    const uint32_t line_depth_bytes = dd->line_depth_vert_count * (uint32_t)sizeof(BH_DbgVertex);
    const uint32_t line_always_bytes = dd->line_always_vert_count * (uint32_t)sizeof(BH_DbgVertex);
    const uint32_t tri_depth_bytes = dd->tri_depth_vert_count * (uint32_t)sizeof(BH_DbgVertex);
    const uint32_t tri_always_bytes = dd->tri_always_vert_count * (uint32_t)sizeof(BH_DbgVertex);

    dd->vb_line_depth = bh_dbg_ensure_vb(dd, dd->vb_line_depth, &dd->vb_line_depth_bytes, line_depth_bytes);
    dd->vb_line_always = bh_dbg_ensure_vb(dd, dd->vb_line_always, &dd->vb_line_always_bytes, line_always_bytes);
    dd->vb_tri_depth = bh_dbg_ensure_vb(dd, dd->vb_tri_depth, &dd->vb_tri_depth_bytes, tri_depth_bytes);
    dd->vb_tri_always = bh_dbg_ensure_vb(dd, dd->vb_tri_always, &dd->vb_tri_always_bytes, tri_always_bytes);

    (void)bh_dbg_upload_vertices(dd, copy_pass, dd->vb_line_depth, dd->cpu_line_depth, dd->line_depth_vert_count);
    (void)bh_dbg_upload_vertices(dd, copy_pass, dd->vb_line_always, dd->cpu_line_always, dd->line_always_vert_count);
    (void)bh_dbg_upload_vertices(dd, copy_pass, dd->vb_tri_depth, dd->cpu_tri_depth, dd->tri_depth_vert_count);
    (void)bh_dbg_upload_vertices(dd, copy_pass, dd->vb_tri_always, dd->cpu_tri_always, dd->tri_always_vert_count);

    dd->dirty = false;
}

static void bh_dbg_draw_one(BH_DebugDraw *dd, BH_ShaderProgram *prog, SDL_GPUCommandBuffer *cmd,
                            SDL_GPURenderPass *pass, SDL_GPUBuffer *vb, uint32_t vert_count, const mat4 *view_proj)
{
    if (vert_count == 0)
    {
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, prog->pipeline);
    SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){.buffer = vb}, 1);

    const BH_UniformBufferReflection *per_draw_ub =
        (prog->vs_refl.uniform_buffer_count > 0) ? &prog->vs_refl.uniform_buffers[0] : NULL;
    const uint32_t per_draw_slot = per_draw_ub ? per_draw_ub->slot : 0;
    const uint32_t per_draw_size = per_draw_ub ? per_draw_ub->size_bytes : 0;

    uint8_t per_draw_storage[256] = {0};
    if (per_draw_size == 0 || per_draw_size > (uint32_t)sizeof(per_draw_storage))
    {
        return;
    }

    BH_ParamBlock per_draw = {0};
    (void)BH_ParamBlock_InitSingleSlot(&per_draw, &prog->vs_refl, per_draw_slot, per_draw_storage, per_draw_size);

    if (!BH_ParamBlock_SetMat4(&per_draw, "u_MVP", view_proj) && !dd->logged_missing_mvp)
    {
        SDL_Log("[bh][dbg] Failed to set u_MVP. Reflection JSON may be out of date.");
        dd->logged_missing_mvp = true;
    }

    BH_ParamBlock_PushUniforms(&per_draw, cmd);
    SDL_DrawGPUPrimitives(pass, vert_count, 1, 0, 0);
}

static void bh_dbg_draw_cb(void *user, SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *render_pass, const mat4 *view_proj,
                           uint32_t fb_width, uint32_t fb_height, float alpha)
{
    (void)fb_width;
    (void)fb_height;
    (void)alpha;

    BH_DebugDraw *dd = (BH_DebugDraw *)user;
    if (!dd)
    {
        return;
    }

    /* Triangles first (opaque-ish), then lines (wireframe) */
    if (dd->tri_depth_vert_count > 0 && dd->vb_tri_depth)
    {
        bh_dbg_draw_one(dd, &dd->tri_depth, cmd, render_pass, dd->vb_tri_depth, dd->tri_depth_vert_count, view_proj);
    }
    if (dd->tri_always_vert_count > 0 && dd->vb_tri_always)
    {
        bh_dbg_draw_one(dd, &dd->tri_always, cmd, render_pass, dd->vb_tri_always, dd->tri_always_vert_count, view_proj);
    }

    if (dd->line_depth_vert_count > 0 && dd->vb_line_depth)
    {
        bh_dbg_draw_one(dd, &dd->line_depth, cmd, render_pass, dd->vb_line_depth, dd->line_depth_vert_count, view_proj);
    }
    if (dd->line_always_vert_count > 0 && dd->vb_line_always)
    {
        bh_dbg_draw_one(dd, &dd->line_always, cmd, render_pass, dd->vb_line_always, dd->line_always_vert_count,
                        view_proj);
    }
}

static void bh_dbg_end_frame_cb(void *user, bool submit_ok)
{
    (void)submit_ok;

    BH_DebugDraw *dd = (BH_DebugDraw *)user;
    if (!dd)
    {
        return;
    }

    for (uint32_t i = 0; i < dd->pending_tbuf_count; ++i)
    {
        if (dd->pending_tbufs[i])
        {
            SDL_ReleaseGPUTransferBuffer(dd->device, dd->pending_tbufs[i]);
        }
    }
    dd->pending_tbuf_count = 0;
}

/* -----------------------------------------------------------------------------
   Init / Shutdown
   ----------------------------------------------------------------------------- */

bool BH_DebugDraw_Init(BH_DebugDraw *dd, BH_Renderer *renderer, const char *asset_root, BH_Arena *permanent_arena)
{
    if (!dd || !renderer || !renderer->device || !asset_root || !permanent_arena)
    {
        return false;
    }

    *dd = (BH_DebugDraw){0};
    dd->device = renderer->device;
    dd->dirty = true;
    dd->state_top = 1;
    dd->state_stack[0].depth = BH_DBG_DEPTH_TEST;
    dd->state_stack[0].default_color = BH_COLOR_WHITE;

    SDL_GPUVertexAttribute attrs[2] = {
        {.location = 0,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
         .offset = (uint32_t)offsetof(BH_DbgVertex, position)},
        {.location = 1,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset = (uint32_t)offsetof(BH_DbgVertex, color)},
    };

    SDL_GPUVertexBufferDescription vb_desc = {
        .slot = 0,
        .pitch = (uint32_t)sizeof(BH_DbgVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };

    SDL_GPUVertexInputState vi = {
        .vertex_buffer_descriptions = &vb_desc,
        .num_vertex_buffers = 1,
        .vertex_attributes = attrs,
        .num_vertex_attributes = 2,
    };

    char vs_spv[1024], vs_json[1024], fs_spv[1024], fs_json[1024];
    SDL_snprintf(vs_spv, sizeof(vs_spv), "%s/shaders/compiled/debug_prim.vert.spv", asset_root);
    SDL_snprintf(vs_json, sizeof(vs_json), "%s/shaders/compiled/debug_prim.vert.json", asset_root);
    SDL_snprintf(fs_spv, sizeof(fs_spv), "%s/shaders/compiled/debug_prim.frag.spv", asset_root);
    SDL_snprintf(fs_json, sizeof(fs_json), "%s/shaders/compiled/debug_prim.frag.json", asset_root);

    BH_ShaderProgramPipelineConfig pcfg = {
        .vertex_input = vi,
        .rasterizer_state = {.fill_mode = SDL_GPU_FILLMODE_FILL,
                             .cull_mode = SDL_GPU_CULLMODE_NONE,
                             .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE},
        .multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1,
        .depth_stencil_state = {.enable_depth_write = false, .compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL},
        .blend_state = bh_dbg_alpha_blend_state(),
        .has_depth_stencil_target = true,
    };

    /* Depth-tested lines */
    pcfg.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
    pcfg.depth_stencil_state.enable_depth_test = true;
    if (!BH_ShaderProgram_LoadEx(&dd->line_depth, renderer->device, vs_spv, vs_json, fs_spv, fs_json,
                                 renderer->swapchain_format, renderer->depth_format, &pcfg, permanent_arena))
    {
        SDL_Log("[bh][dbg] Failed to load debug line_depth program");
        BH_DebugDraw_Shutdown(dd);
        return false;
    }

    /* Always-draw lines */
    pcfg.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
    pcfg.depth_stencil_state.enable_depth_test = false;
    if (!BH_ShaderProgram_LoadEx(&dd->line_always, renderer->device, vs_spv, vs_json, fs_spv, fs_json,
                                 renderer->swapchain_format, renderer->depth_format, &pcfg, permanent_arena))
    {
        SDL_Log("[bh][dbg] Failed to load debug line_always program");
        BH_DebugDraw_Shutdown(dd);
        return false;
    }

    /* Depth-tested triangles */
    pcfg.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pcfg.depth_stencil_state.enable_depth_test = true;
    if (!BH_ShaderProgram_LoadEx(&dd->tri_depth, renderer->device, vs_spv, vs_json, fs_spv, fs_json,
                                 renderer->swapchain_format, renderer->depth_format, &pcfg, permanent_arena))
    {
        SDL_Log("[bh][dbg] Failed to load debug tri_depth program");
        BH_DebugDraw_Shutdown(dd);
        return false;
    }

    /* Always-draw triangles */
    pcfg.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pcfg.depth_stencil_state.enable_depth_test = false;
    if (!BH_ShaderProgram_LoadEx(&dd->tri_always, renderer->device, vs_spv, vs_json, fs_spv, fs_json,
                                 renderer->swapchain_format, renderer->depth_format, &pcfg, permanent_arena))
    {
        SDL_Log("[bh][dbg] Failed to load debug tri_always program");
        BH_DebugDraw_Shutdown(dd);
        return false;
    }

    BH_RenderPassHooks hk = {
        .prepare = bh_dbg_prepare_cb,
        .draw = bh_dbg_draw_cb,
        .end_frame = bh_dbg_end_frame_cb,
        .user = dd,
    };

    if (!BH_Renderer_AddHooks(renderer, hk))
    {
        SDL_Log("[bh][dbg] Failed to register renderer hook; debug drawing will be disabled");
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

    if (dd->device)
    {
        if (dd->vb_line_depth)
            SDL_ReleaseGPUBuffer(dd->device, dd->vb_line_depth);
        if (dd->vb_line_always)
            SDL_ReleaseGPUBuffer(dd->device, dd->vb_line_always);
        if (dd->vb_tri_depth)
            SDL_ReleaseGPUBuffer(dd->device, dd->vb_tri_depth);
        if (dd->vb_tri_always)
            SDL_ReleaseGPUBuffer(dd->device, dd->vb_tri_always);

        BH_ShaderProgram_Release(&dd->line_depth, dd->device);
        BH_ShaderProgram_Release(&dd->line_always, dd->device);
        BH_ShaderProgram_Release(&dd->tri_depth, dd->device);
        BH_ShaderProgram_Release(&dd->tri_always, dd->device);

        for (uint32_t i = 0; i < dd->pending_tbuf_count; ++i)
        {
            if (dd->pending_tbufs[i])
            {
                SDL_ReleaseGPUTransferBuffer(dd->device, dd->pending_tbufs[i]);
            }
        }
    }

    SDL_free(dd->pending_tbufs);
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

/* -----------------------------------------------------------------------------
   State Management
   ----------------------------------------------------------------------------- */

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

/* -----------------------------------------------------------------------------
   Public Drawing API
   ----------------------------------------------------------------------------- */

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

    const float half_x = size_x * 0.5f;
    const float half_y = size_y * 0.5f;
    const uint32_t cells_x = (uint32_t)(size_x / cell_size);
    const uint32_t cells_y = (uint32_t)(size_y / cell_size);

    /* X-parallel lines */
    for (uint32_t y = 0; y <= cells_y; ++y)
    {
        const float fy = -half_y + (float)y * cell_size;
        bh_dbg_add_line_internal(dd, (vec3){origin.x - half_x, origin.y + fy, origin.z},
                                 (vec3){origin.x + half_x, origin.y + fy, origin.z}, color, duration_s);
    }

    /* Y-parallel lines */
    for (uint32_t x = 0; x <= cells_x; ++x)
    {
        const float fx = -half_x + (float)x * cell_size;
        bh_dbg_add_line_internal(dd, (vec3){origin.x + fx, origin.y - half_y, origin.z},
                                 (vec3){origin.x + fx, origin.y + half_y, origin.z}, color, duration_s);
    }
}

void BH_DebugDraw_DrawBounds(vec3 mins, vec3 maxs, color4f color, float duration_s)
{
    const vec3 center = {(mins.x + maxs.x) * 0.5f, (mins.y + maxs.y) * 0.5f, (mins.z + maxs.z) * 0.5f};
    const vec3 half_ext = {(maxs.x - mins.x) * 0.5f, (maxs.y - mins.y) * 0.5f, (maxs.z - mins.z) * 0.5f};
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

    const vec3 c = center;
    const vec3 e = half_extents;

    const vec3 p000 = {c.x - e.x, c.y - e.y, c.z - e.z};
    const vec3 p001 = {c.x - e.x, c.y - e.y, c.z + e.z};
    const vec3 p010 = {c.x - e.x, c.y + e.y, c.z - e.z};
    const vec3 p011 = {c.x - e.x, c.y + e.y, c.z + e.z};
    const vec3 p100 = {c.x + e.x, c.y - e.y, c.z - e.z};
    const vec3 p101 = {c.x + e.x, c.y - e.y, c.z + e.z};
    const vec3 p110 = {c.x + e.x, c.y + e.y, c.z - e.z};
    const vec3 p111 = {c.x + e.x, c.y + e.y, c.z + e.z};

    /* Bottom Ring */
    bh_dbg_add_line_internal(dd, p000, p100, color, duration_s);
    bh_dbg_add_line_internal(dd, p100, p110, color, duration_s);
    bh_dbg_add_line_internal(dd, p110, p010, color, duration_s);
    bh_dbg_add_line_internal(dd, p010, p000, color, duration_s);

    /* Top Ring */
    bh_dbg_add_line_internal(dd, p001, p101, color, duration_s);
    bh_dbg_add_line_internal(dd, p101, p111, color, duration_s);
    bh_dbg_add_line_internal(dd, p111, p011, color, duration_s);
    bh_dbg_add_line_internal(dd, p011, p001, color, duration_s);

    /* Verticals */
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

    const vec3 c = center;
    const vec3 e = half_extents;

    const vec3 p000 = {c.x - e.x, c.y - e.y, c.z - e.z};
    const vec3 p001 = {c.x - e.x, c.y - e.y, c.z + e.z};
    const vec3 p010 = {c.x - e.x, c.y + e.y, c.z - e.z};
    const vec3 p011 = {c.x - e.x, c.y + e.y, c.z + e.z};
    const vec3 p100 = {c.x + e.x, c.y - e.y, c.z - e.z};
    const vec3 p101 = {c.x + e.x, c.y - e.y, c.z + e.z};
    const vec3 p110 = {c.x + e.x, c.y + e.y, c.z - e.z};
    const vec3 p111 = {c.x + e.x, c.y + e.y, c.z + e.z};

    /* -X */
    bh_dbg_add_tri_internal(dd, p000, p001, p011, color, duration_s);
    bh_dbg_add_tri_internal(dd, p000, p011, p010, color, duration_s);
    /* +X */
    bh_dbg_add_tri_internal(dd, p100, p110, p111, color, duration_s);
    bh_dbg_add_tri_internal(dd, p100, p111, p101, color, duration_s);
    /* -Y */
    bh_dbg_add_tri_internal(dd, p000, p100, p101, color, duration_s);
    bh_dbg_add_tri_internal(dd, p000, p101, p001, color, duration_s);
    /* +Y */
    bh_dbg_add_tri_internal(dd, p010, p011, p111, color, duration_s);
    bh_dbg_add_tri_internal(dd, p010, p111, p110, color, duration_s);
    /* -Z */
    bh_dbg_add_tri_internal(dd, p000, p010, p110, color, duration_s);
    bh_dbg_add_tri_internal(dd, p000, p110, p100, color, duration_s);
    /* +Z */
    bh_dbg_add_tri_internal(dd, p001, p101, p111, color, duration_s);
    bh_dbg_add_tri_internal(dd, p001, p111, p011, color, duration_s);
}