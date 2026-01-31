#include "bh_dbg_prims.h"

#include "bh_gpu.h"
#include "bh_material.h"
#include "bh_renderer.h"
#include "bh_shader_program.h"
#include "../debug/bh_debug_draw_internal.h"

#include <SDL3/SDL.h>
#include <stddef.h>
#include <string.h>

typedef struct BH_DbgPrims
{
    BH_DebugDraw *dd;
    BH_GPUDevice *device;

    BH_ShaderProgram line_depth;
    BH_ShaderProgram line_always;
    BH_ShaderProgram tri_depth;
    BH_ShaderProgram tri_always;

    BH_GPUBuffer *vb_line_depth;
    BH_GPUBuffer *vb_line_always;
    BH_GPUBuffer *vb_tri_depth;
    BH_GPUBuffer *vb_tri_always;

    uint32_t vb_line_depth_bytes;
    uint32_t vb_line_always_bytes;
    uint32_t vb_tri_depth_bytes;
    uint32_t vb_tri_always_bytes;

    BH_GPUTransferBuffer *tb_line_depth;
    BH_GPUTransferBuffer *tb_line_always;
    BH_GPUTransferBuffer *tb_tri_depth;
    BH_GPUTransferBuffer *tb_tri_always;

    uint32_t tb_line_depth_bytes;
    uint32_t tb_line_always_bytes;
    uint32_t tb_tri_depth_bytes;
    uint32_t tb_tri_always_bytes;

    bool hooks_registered;
    bool enabled;
} BH_DbgPrims;

static BH_GPUBuffer *bh_dbg_ensure_vb(BH_DbgPrims *p, BH_GPUBuffer *vb, uint32_t *vb_bytes, uint32_t needed_bytes)
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
        BH_GPU_ReleaseBuffer(p->device, vb);
        vb = NULL;
    }

    uint32_t alloc_bytes = 4096;
    while (alloc_bytes < needed_bytes)
    {
        alloc_bytes *= 2;
    }

    BH_GPUBufferCreateInfo bci = {0};
    bci.usage = BH_GPU_BUFFERUSAGE_VERTEX;
    bci.size = alloc_bytes;

    vb = BH_GPU_CreateBuffer(p->device, &bci);
    if (!vb)
    {
        SDL_Log("[bh][dbg] BH_GPU_CreateBuffer failed: %s", BH_GPU_GetLastError());
        *vb_bytes = 0;
        return NULL;
    }

    *vb_bytes = alloc_bytes;
    return vb;
}

static BH_GPUTransferBuffer *bh_dbg_ensure_tb(BH_DbgPrims *p, BH_GPUTransferBuffer *tb, uint32_t *tb_bytes,
                                              uint32_t needed_bytes)
{
    if (needed_bytes == 0)
    {
        return tb;
    }

    if (tb && *tb_bytes >= needed_bytes)
    {
        return tb;
    }

    if (tb)
    {
        BH_GPU_ReleaseTransferBuffer(p->device, tb);
        tb = NULL;
    }

    uint32_t alloc_bytes = 4096;
    while (alloc_bytes < needed_bytes)
    {
        alloc_bytes *= 2;
    }

    BH_GPUTransferBufferCreateInfo tci = {0};
    tci.usage = BH_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = alloc_bytes;

    tb = BH_GPU_CreateTransferBuffer(p->device, &tci);
    if (!tb)
    {
        SDL_Log("[bh][dbg] BH_GPU_CreateTransferBuffer failed: %s", BH_GPU_GetLastError());
        *tb_bytes = 0;
        return NULL;
    }

    *tb_bytes = alloc_bytes;
    return tb;
}

static void bh_dbg_upload_vertices(BH_DbgPrims *p, BH_GPUCopyPass *copy_pass, BH_GPUBuffer *dst_buffer,
                                   BH_GPUTransferBuffer *tb, const BH_DbgVertex *src, uint32_t vertex_count)
{
    if (!copy_pass || !dst_buffer || !tb || !src || vertex_count == 0)
    {
        return;
    }

    const uint32_t size = vertex_count * (uint32_t)sizeof(BH_DbgVertex);

    void *mapped = BH_GPU_MapTransferBuffer(p->device, tb, true);
    if (!mapped)
    {
        SDL_Log("[bh][dbg] BH_GPU_MapTransferBuffer failed: %s", BH_GPU_GetLastError());
        return;
    }

    memcpy(mapped, src, size);
    BH_GPU_UnmapTransferBuffer(p->device, tb);

    BH_GPUTransferBufferLocation src_loc = {.transfer_buffer = tb, .offset = 0};
    BH_GPUBufferRegion dst = {.buffer = dst_buffer, .offset = 0, .size = size};
    BH_GPU_UploadToBuffer(copy_pass, &src_loc, &dst, true);
}

static void bh_dbg_draw_one(BH_DbgPrims *p, BH_ShaderProgram *prog, BH_GPUCommandBuffer *cmd, BH_GPURenderPass *pass,
                            BH_GPUBuffer *vb, uint32_t vert_count, const mat4 *view_proj)
{
    if (vert_count == 0 || !prog || !prog->pipeline || !vb)
    {
        return;
    }

    BH_GPU_BindGraphicsPipeline(pass, prog->pipeline);
    const BH_GPUBufferBinding binding = {.buffer = vb, .offset = 0};
    BH_GPU_BindVertexBuffers(pass, 0, &binding, 1);

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
    if (!BH_ParamBlock_InitSingleSlot(&per_draw, &prog->vs_refl, per_draw_slot, per_draw_storage, per_draw_size))
    {
        return;
    }

    if (!BH_ParamBlock_SetMat4(&per_draw, "u_MVP", view_proj))
    {
        SDL_Log("[bh][dbg] Failed to set u_MVP. Reflection JSON may be out of date.");
    }

    BH_ParamBlock_PushUniforms(&per_draw, cmd);
    BH_GPU_DrawPrimitives(pass, vert_count, 1, 0, 0);
}

static void bh_dbg_prepare_cb(void *user, BH_GPUCommandBuffer *cmd, BH_GPUCopyPass *copy_pass, const mat4 *view_proj,
                              uint32_t fb_width, uint32_t fb_height, float alpha)
{
    (void)cmd;
    (void)view_proj;
    (void)fb_width;
    (void)fb_height;
    (void)alpha;

    BH_DbgPrims *p = (BH_DbgPrims *)user;
    if (!p || !p->enabled || !p->dd || !p->dd->dirty)
    {
        return;
    }

    BH_DbgBatches b = {0};
    BH_DebugDraw_GetBatches(p->dd, &b);

    const uint32_t line_depth_bytes = b.line_depth_count * (uint32_t)sizeof(BH_DbgVertex);
    const uint32_t line_always_bytes = b.line_always_count * (uint32_t)sizeof(BH_DbgVertex);
    const uint32_t tri_depth_bytes = b.tri_depth_count * (uint32_t)sizeof(BH_DbgVertex);
    const uint32_t tri_always_bytes = b.tri_always_count * (uint32_t)sizeof(BH_DbgVertex);

    p->vb_line_depth = bh_dbg_ensure_vb(p, p->vb_line_depth, &p->vb_line_depth_bytes, line_depth_bytes);
    p->vb_line_always = bh_dbg_ensure_vb(p, p->vb_line_always, &p->vb_line_always_bytes, line_always_bytes);
    p->vb_tri_depth = bh_dbg_ensure_vb(p, p->vb_tri_depth, &p->vb_tri_depth_bytes, tri_depth_bytes);
    p->vb_tri_always = bh_dbg_ensure_vb(p, p->vb_tri_always, &p->vb_tri_always_bytes, tri_always_bytes);

    p->tb_line_depth = bh_dbg_ensure_tb(p, p->tb_line_depth, &p->tb_line_depth_bytes, line_depth_bytes);
    p->tb_line_always = bh_dbg_ensure_tb(p, p->tb_line_always, &p->tb_line_always_bytes, line_always_bytes);
    p->tb_tri_depth = bh_dbg_ensure_tb(p, p->tb_tri_depth, &p->tb_tri_depth_bytes, tri_depth_bytes);
    p->tb_tri_always = bh_dbg_ensure_tb(p, p->tb_tri_always, &p->tb_tri_always_bytes, tri_always_bytes);

    bh_dbg_upload_vertices(p, copy_pass, p->vb_line_depth, p->tb_line_depth, b.line_depth, b.line_depth_count);
    bh_dbg_upload_vertices(p, copy_pass, p->vb_line_always, p->tb_line_always, b.line_always, b.line_always_count);
    bh_dbg_upload_vertices(p, copy_pass, p->vb_tri_depth, p->tb_tri_depth, b.tri_depth, b.tri_depth_count);
    bh_dbg_upload_vertices(p, copy_pass, p->vb_tri_always, p->tb_tri_always, b.tri_always, b.tri_always_count);
}

static void bh_dbg_draw_cb(void *user, BH_GPUCommandBuffer *cmd, BH_GPURenderPass *render_pass, const mat4 *view_proj,
                           uint32_t fb_width, uint32_t fb_height, float alpha)
{
    (void)fb_width;
    (void)fb_height;
    (void)alpha;

    BH_DbgPrims *p = (BH_DbgPrims *)user;
    if (!p || !p->enabled || !p->dd || p->dd->dirty)
    {
        return;
    }

    bh_dbg_draw_one(p, &p->tri_depth, cmd, render_pass, p->vb_tri_depth, p->dd->tri_depth_vert_count, view_proj);
    bh_dbg_draw_one(p, &p->tri_always, cmd, render_pass, p->vb_tri_always, p->dd->tri_always_vert_count, view_proj);
    bh_dbg_draw_one(p, &p->line_depth, cmd, render_pass, p->vb_line_depth, p->dd->line_depth_vert_count, view_proj);
    bh_dbg_draw_one(p, &p->line_always, cmd, render_pass, p->vb_line_always, p->dd->line_always_vert_count, view_proj);
}

static void bh_dbg_end_frame_cb(void *user, bool submit_ok)
{
    (void)user;
    (void)submit_ok;
}

void BH_DbgPrims_Detach(BH_DebugDraw *dd)
{
    BH_DbgPrims *p = dd ? (BH_DbgPrims *)dd->render : NULL;
    if (!p)
    {
        return;
    }

    p->enabled = false;

    if (p->device)
    {
        if (p->vb_line_depth)
            BH_GPU_ReleaseBuffer(p->device, p->vb_line_depth);
        if (p->vb_line_always)
            BH_GPU_ReleaseBuffer(p->device, p->vb_line_always);
        if (p->vb_tri_depth)
            BH_GPU_ReleaseBuffer(p->device, p->vb_tri_depth);
        if (p->vb_tri_always)
            BH_GPU_ReleaseBuffer(p->device, p->vb_tri_always);

        if (p->tb_line_depth)
            BH_GPU_ReleaseTransferBuffer(p->device, p->tb_line_depth);
        if (p->tb_line_always)
            BH_GPU_ReleaseTransferBuffer(p->device, p->tb_line_always);
        if (p->tb_tri_depth)
            BH_GPU_ReleaseTransferBuffer(p->device, p->tb_tri_depth);
        if (p->tb_tri_always)
            BH_GPU_ReleaseTransferBuffer(p->device, p->tb_tri_always);

        BH_ShaderProgram_Release(&p->line_depth, p->device);
        BH_ShaderProgram_Release(&p->line_always, p->device);
        BH_ShaderProgram_Release(&p->tri_depth, p->device);
        BH_ShaderProgram_Release(&p->tri_always, p->device);
    }

    p->vb_line_depth = NULL;
    p->vb_line_always = NULL;
    p->vb_tri_depth = NULL;
    p->vb_tri_always = NULL;

    p->vb_line_depth_bytes = 0;
    p->vb_line_always_bytes = 0;
    p->vb_tri_depth_bytes = 0;
    p->vb_tri_always_bytes = 0;

    p->tb_line_depth = NULL;
    p->tb_line_always = NULL;
    p->tb_tri_depth = NULL;
    p->tb_tri_always = NULL;

    p->tb_line_depth_bytes = 0;
    p->tb_line_always_bytes = 0;
    p->tb_tri_depth_bytes = 0;
    p->tb_tri_always_bytes = 0;

    p->device = NULL;
    p->dd = NULL;
}

bool BH_DbgPrims_Attach(BH_DebugDraw *dd, BH_Renderer *renderer, const char *asset_root, BH_Arena *permanent_arena)
{
    if (!dd || !renderer || !renderer->device || !asset_root || !permanent_arena)
    {
        return false;
    }

    BH_DbgPrims *p = (BH_DbgPrims *)dd->render;
    if (!p)
    {
        p = (BH_DbgPrims *)BH_Arena_Alloc(permanent_arena, sizeof(BH_DbgPrims), _Alignof(BH_DbgPrims));
        if (!p)
        {
            return false;
        }
        *p = (BH_DbgPrims){0};
        dd->render = p;
    }

    p->dd = dd;
    p->device = renderer->device;
    p->enabled = true;

    BH_GPUVertexAttribute attrs[2] = {
        {.location = 0,
         .buffer_slot = 0,
         .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT3,
         .offset = (uint32_t)offsetof(BH_DbgVertex, position)},
        {.location = 1,
         .buffer_slot = 0,
         .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset = (uint32_t)offsetof(BH_DbgVertex, color)},
    };

    BH_GPUVertexBufferDescription vb_desc = {
        .slot = 0,
        .pitch = (uint32_t)sizeof(BH_DbgVertex),
        .input_rate = BH_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };

    BH_GPUVertexInputState vi = {
        .vertex_buffer_descriptions = &vb_desc,
        .num_vertex_buffers = 1,
        .vertex_attributes = attrs,
        .num_vertex_attributes = 2,
    };

    const bool use_gl = (SDL_strcasecmp(BH_GPU_GetBackend()->name, "OpenGL") == 0);

    char vs_spv[1024], vs_json[1024], fs_spv[1024], fs_json[1024];
    SDL_snprintf(vs_spv, sizeof(vs_spv), "%s/shaders/%s/debug_prim.vert.%s", asset_root, use_gl ? "gl" : "compiled",
                 use_gl ? "glsl" : "spv");
    SDL_snprintf(vs_json, sizeof(vs_json), "%s/shaders/compiled/debug_prim.vert.json", asset_root);
    SDL_snprintf(fs_spv, sizeof(fs_spv), "%s/shaders/%s/debug_prim.frag.%s", asset_root, use_gl ? "gl" : "compiled",
                 use_gl ? "glsl" : "spv");
    SDL_snprintf(fs_json, sizeof(fs_json), "%s/shaders/compiled/debug_prim.frag.json", asset_root);

    BH_GPUColorTargetBlendState blend = {.enable_blend = true,
                                         .color_blend_op = BH_GPU_BLENDOP_ADD,
                                         .alpha_blend_op = BH_GPU_BLENDOP_ADD,
                                         .src_color_blendfactor = BH_GPU_BLENDFACTOR_SRC_ALPHA,
                                         .dst_color_blendfactor = BH_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                         .src_alpha_blendfactor = BH_GPU_BLENDFACTOR_ONE,
                                         .dst_alpha_blendfactor = BH_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                         .enable_color_write_mask = false};

    BH_ShaderProgramPipelineConfig pcfg = {
        .vertex_input = vi,
        .rasterizer_state = {.fill_mode = BH_GPU_FILLMODE_FILL,
                             .cull_mode = BH_GPU_CULLMODE_NONE,
                             .front_face = BH_GPU_FRONTFACE_COUNTER_CLOCKWISE},
        .multisample_state.sample_count = BH_GPU_SAMPLECOUNT_1,
        .depth_stencil_state = {.enable_depth_write = false, .compare_op = BH_GPU_COMPAREOP_LESS_OR_EQUAL},
        .blend_state = blend,
        .has_depth_stencil_target = true,
    };

    pcfg.primitive_type = BH_GPU_PRIMITIVETYPE_LINELIST;
    pcfg.depth_stencil_state.enable_depth_test = true;
    if (!BH_ShaderProgram_LoadEx(&p->line_depth, renderer->device, vs_spv, vs_json, fs_spv, fs_json,
                                 renderer->swapchain_format, renderer->depth_format, &pcfg, permanent_arena))
    {
        SDL_Log("[bh][dbg] Failed to load debug line_depth program");
        BH_DbgPrims_Detach(dd);
        return false;
    }

    pcfg.primitive_type = BH_GPU_PRIMITIVETYPE_LINELIST;
    pcfg.depth_stencil_state.enable_depth_test = false;
    if (!BH_ShaderProgram_LoadEx(&p->line_always, renderer->device, vs_spv, vs_json, fs_spv, fs_json,
                                 renderer->swapchain_format, renderer->depth_format, &pcfg, permanent_arena))
    {
        SDL_Log("[bh][dbg] Failed to load debug line_always program");
        BH_DbgPrims_Detach(dd);
        return false;
    }

    pcfg.primitive_type = BH_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pcfg.depth_stencil_state.enable_depth_test = true;
    if (!BH_ShaderProgram_LoadEx(&p->tri_depth, renderer->device, vs_spv, vs_json, fs_spv, fs_json,
                                 renderer->swapchain_format, renderer->depth_format, &pcfg, permanent_arena))
    {
        SDL_Log("[bh][dbg] Failed to load debug tri_depth program");
        BH_DbgPrims_Detach(dd);
        return false;
    }

    pcfg.primitive_type = BH_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pcfg.depth_stencil_state.enable_depth_test = false;
    if (!BH_ShaderProgram_LoadEx(&p->tri_always, renderer->device, vs_spv, vs_json, fs_spv, fs_json,
                                 renderer->swapchain_format, renderer->depth_format, &pcfg, permanent_arena))
    {
        SDL_Log("[bh][dbg] Failed to load debug tri_always program");
        BH_DbgPrims_Detach(dd);
        return false;
    }

    if (!p->hooks_registered)
    {
        BH_RenderPassHooks hk = {
            .prepare = bh_dbg_prepare_cb,
            .draw = bh_dbg_draw_cb,
            .end_frame = bh_dbg_end_frame_cb,
            .user = p,
        };

        if (!BH_Renderer_AddHooks(renderer, hk))
        {
            SDL_Log("[bh][dbg] Failed to register renderer hook; debug drawing will be disabled");
        }
        else
        {
            p->hooks_registered = true;
        }
    }

    return true;
}
