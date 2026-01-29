/* -----------------------------------------------------------------------------
   bh_renderer.c
   ----------------------------------------------------------------------------- */
#include "bh_renderer.h"
#include "../scene/bh_scene.h"

#include "bh_gpu.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct BH_RenderDrawContext
{
    BH_Renderer *renderer;
    BH_GPUCommandBuffer *cmd;
    BH_GPURenderPass *pass;
    const mat4 *view_proj;
    const BH_ShaderProgram *program;
} BH_RenderDrawContext;

typedef struct BH_TransparentDraw
{
    const BH_Mesh *mesh;
    const BH_Submesh *submesh;
    const BH_Material *material;
    mat4 world;
    float sort_key;
} BH_TransparentDraw;

typedef struct BH_TransparentQueue
{
    BH_TransparentDraw *items;
    uint32_t count;
    uint32_t cap;
} BH_TransparentQueue;

typedef struct BH_SceneSplitPassContext
{
    BH_RenderDrawContext draw;
    vec3 camera_pos;
    BH_TransparentQueue *transparent;
} BH_SceneSplitPassContext;

/* -----------------------------------------------------------------------------
   Internal Helpers: Uniforms & Binding
   ----------------------------------------------------------------------------- */

static void bh_push_scene_fragment_uniforms(BH_Renderer *r, BH_GPUCommandBuffer *cmd, const BH_Scene *scene,
                                            vec3 camera_pos)
{
    /* Slot 1: Scene/Frame Fragment Uniforms */
    const BH_UniformBufferReflection *ub = BH_ShaderReflection_FindUniformBuffer(&r->program.fs_refl, 1);
    if (!ub || ub->size_bytes == 0 || ub->size_bytes > 256)
    {
        return;
    }

    uint8_t storage[256] = {0};
    BH_ParamBlock pb = {0};
    if (!BH_ParamBlock_InitSingleSlot(&pb, &r->program.fs_refl, 1, storage, ub->size_bytes))
    {
        return;
    }

    vec3 light_dir = {0.35f, 0.25f, 1.0f};
    vec3 light_color = {1.0f, 1.0f, 1.0f};
    float light_intensity = 3.0f;

    if (scene->has_directional_light)
    {
        light_dir = scene->directional_light.direction;
        light_color = scene->directional_light.color;
        light_intensity = scene->directional_light.intensity;
    }

    (void)BH_ParamBlock_SetVec3(&pb, "u_CameraPos", &camera_pos);
    (void)BH_ParamBlock_SetVec3(&pb, "u_LightDirection", &light_dir);
    (void)BH_ParamBlock_SetVec3(&pb, "u_LightColor", &light_color);
    (void)BH_ParamBlock_SetFloat32(&pb, "u_LightIntensity", light_intensity);
    (void)BH_ParamBlock_SetFloat32(&pb, "u_AmbientStrength", 0.15f);

    BH_ParamBlock_PushUniforms(&pb, cmd);
}

static BH_GPUTexture *bh_default_tex_for_slot(BH_Renderer *r, uint32_t slot)
{
    switch (slot)
    {
    case BH_MATERIAL_TEX_ALBEDO:
        return BH_TextureManager_GetGPUTexture(&r->textures, BH_TextureManager_GetDefaultWhite(&r->textures));
    case BH_MATERIAL_TEX_NORMAL:
        return BH_TextureManager_GetGPUTexture(&r->textures, BH_TextureManager_GetDefaultNormal(&r->textures));
    case BH_MATERIAL_TEX_METALLIC:
        return BH_TextureManager_GetGPUTexture(&r->textures, BH_TextureManager_GetDefaultBlack(&r->textures));
    case BH_MATERIAL_TEX_ROUGHNESS:
        return BH_TextureManager_GetGPUTexture(&r->textures, BH_TextureManager_GetDefaultWhite(&r->textures));
    case BH_MATERIAL_TEX_AO:
        return BH_TextureManager_GetGPUTexture(&r->textures, BH_TextureManager_GetDefaultWhite(&r->textures));
    case BH_MATERIAL_TEX_LIGHTMAP:
        return BH_TextureManager_GetGPUTexture(&r->textures, BH_TextureManager_GetDefaultBlack(&r->textures));
    case BH_MATERIAL_TEX_LIGHTDIR:
        return BH_TextureManager_GetGPUTexture(&r->textures, BH_TextureManager_GetDefaultNormal(&r->textures));
    case BH_MATERIAL_TEX_SHADOWMASK:
        return BH_TextureManager_GetGPUTexture(&r->textures, BH_TextureManager_GetDefaultWhite(&r->textures));
    default:
        return BH_TextureManager_GetGPUTexture(&r->textures, BH_TextureManager_GetDefaultWhite(&r->textures));
    }
}

static void bh_bind_material(BH_RenderDrawContext *ctx, const BH_Material *mat)
{
    const BH_ShaderProgram *prog = ctx->program ? ctx->program : &ctx->renderer->program;
    const uint32_t shader_num = prog->fs_refl.num_samplers;
    const uint32_t num = (shader_num < BH_MATERIAL_TEX_COUNT) ? shader_num : BH_MATERIAL_TEX_COUNT;

    if (num > 0)
    {
        BH_GPUTextureSamplerBinding binds[BH_MATERIAL_TEX_COUNT] = {0};
        BH_GPUSampler *sam = BH_TextureManager_GetSampler(&ctx->renderer->textures);

        for (uint32_t i = 0; i < num; ++i)
        {
            BH_GPUTexture *tex = NULL;
            if (mat)
            {
                tex = BH_TextureManager_GetGPUTexture(&ctx->renderer->textures, mat->textures[i]);
            }
            if (!tex)
            {
                tex = bh_default_tex_for_slot(ctx->renderer, i);
            }
            binds[i].texture = tex;
            binds[i].sampler = sam;
        }
        BH_GPU_BindFragmentSamplers(ctx->pass, 0, binds, num);
    }

    if (mat)
    {
        BH_ParamBlock_PushUniforms(&mat->fragment_params, ctx->cmd);
    }
}

static bool bh_push_per_draw_uniforms(const BH_ShaderProgram *prog, BH_GPUCommandBuffer *cmd, const mat4 *view_proj,
                                      const mat4 *world)
{
    const BH_UniformBufferReflection *per_draw_ub =
        (prog->vs_refl.uniform_buffer_count > 0) ? &prog->vs_refl.uniform_buffers[0] : NULL;
    const uint32_t per_draw_slot = per_draw_ub ? per_draw_ub->slot : 0;
    const uint32_t per_draw_size = per_draw_ub ? per_draw_ub->size_bytes : 0;

    uint8_t per_draw_storage[256] = {0};
    if (per_draw_size == 0 || per_draw_size > (uint32_t)sizeof(per_draw_storage))
    {
        return false;
    }

    BH_ParamBlock per_draw = {0};
    if (!BH_ParamBlock_InitSingleSlot(&per_draw, &prog->vs_refl, per_draw_slot, per_draw_storage, per_draw_size))
    {
        return false;
    }

    mat4 mvp = mat4_mul(*view_proj, *world);

    (void)BH_ParamBlock_SetMat4(&per_draw, "u_MVP", &mvp);
    (void)BH_ParamBlock_SetMat4(&per_draw, "u_Model", world);

    BH_ParamBlock_PushUniforms(&per_draw, cmd);
    return true;
}

static bool bh_push_skybox_uniforms(const BH_ShaderProgram *prog, BH_GPUCommandBuffer *cmd, const mat4 *view_proj,
                                    const mat4 *world)
{
    const BH_UniformBufferReflection *ub =
        (prog->vs_refl.uniform_buffer_count > 0) ? &prog->vs_refl.uniform_buffers[0] : NULL;
    const uint32_t slot = ub ? ub->slot : 0;
    const uint32_t size = ub ? ub->size_bytes : 0;

    uint8_t storage[256] = {0};
    if (size == 0 || size > (uint32_t)sizeof(storage))
    {
        return false;
    }

    BH_ParamBlock pb = {0};
    if (!BH_ParamBlock_InitSingleSlot(&pb, &prog->vs_refl, slot, storage, size))
    {
        return false;
    }

    mat4 mvp = mat4_mul(*view_proj, *world);
    (void)BH_ParamBlock_SetMat4(&pb, "u_MVP", &mvp);

    BH_ParamBlock_PushUniforms(&pb, cmd);
    return true;
}

static bool bh_renderer_ensure_depth_texture(BH_Renderer *r, uint32_t w, uint32_t h)
{
    if (r->depth_texture && r->depth_width == w && r->depth_height == h)
    {
        return true;
    }

    if (r->depth_texture)
    {
        BH_GPU_ReleaseTexture(r->device, r->depth_texture);
        r->depth_texture = NULL;
    }

    BH_GPUTextureCreateInfo tci = {.type = BH_GPU_TEXTURETYPE_2D,
                                   .format = (BH_GPUTextureFormat)r->depth_format,
                                   .usage = BH_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                                   .width = w,
                                   .height = h,
                                   .layer_count_or_depth = 1,
                                   .num_levels = 1,
                                   .sample_count = BH_GPU_SAMPLECOUNT_1};

    r->depth_texture = BH_GPU_CreateTexture(r->device, &tci);
    if (!r->depth_texture)
    {
        SDL_Log("[bh] Depth texture creation failed: %s", BH_GPU_GetLastError());
        r->depth_width = 0;
        r->depth_height = 0;
        return false;
    }

    r->depth_width = w;
    r->depth_height = h;
    return true;
}

/* -----------------------------------------------------------------------------
   Internal Helpers: Transparency Queue
   ----------------------------------------------------------------------------- */

static void bh_transparent_queue_free(BH_TransparentQueue *q)
{
    if (q)
    {
        SDL_free(q->items);
        *q = (BH_TransparentQueue){0};
    }
}

static bool bh_transparent_queue_push(BH_TransparentQueue *q, const BH_TransparentDraw *d)
{
    if (q->count >= q->cap)
    {
        uint32_t new_cap = (q->cap == 0) ? 64u : (q->cap * 2u);
        BH_TransparentDraw *new_items =
            (BH_TransparentDraw *)SDL_realloc(q->items, (size_t)new_cap * sizeof(BH_TransparentDraw));
        if (!new_items)
            return false;
        q->items = new_items;
        q->cap = new_cap;
    }
    q->items[q->count++] = *d;
    return true;
}

static int bh_cmp_transparent_back_to_front(const void *a, const void *b)
{
    const BH_TransparentDraw *A = (const BH_TransparentDraw *)a;
    const BH_TransparentDraw *B = (const BH_TransparentDraw *)b;
    if (A->sort_key < B->sort_key)
        return 1;
    if (A->sort_key > B->sort_key)
        return -1;
    return 0;
}

/* -----------------------------------------------------------------------------
   Internal Helpers: Scene Traversal
   ----------------------------------------------------------------------------- */

static void bh_draw_scene_node_split(const BH_SceneNode *node, const mat4 *world, void *user)
{
    BH_SceneSplitPassContext *ctx = (BH_SceneSplitPassContext *)user;
    const BH_Mesh *mesh = node->mesh;

    if (!mesh || !mesh->vertex_buffer || !mesh->index_buffer)
    {
        return;
    }

    BH_GPUBufferBinding vb = {.buffer = mesh->vertex_buffer, .offset = 0};
    BH_GPU_BindVertexBuffers(ctx->draw.pass, 0, &vb, 1);

    BH_GPUBufferBinding ib = {.buffer = mesh->index_buffer, .offset = 0};
    BH_GPU_BindIndexBuffer(ctx->draw.pass, &ib, mesh->index_element_size);

    const BH_ShaderProgram *prog = ctx->draw.program ? ctx->draw.program : &ctx->draw.renderer->program;
    if (!bh_push_per_draw_uniforms(prog, ctx->draw.cmd, ctx->draw.view_proj, world))
    {
        return;
    }

    for (uint32_t i = 0; i < mesh->submesh_count; ++i)
    {
        const BH_Submesh *sm = &mesh->submeshes[i];
        const BH_Material *mat = node->material_override ? node->material_override : sm->material;
        const bool is_transparent = (mat && ((mat->flags & BH_MATERIAL_FLAG_TRANSPARENT) != 0));

        if (is_transparent && ctx->transparent)
        {
            const vec3 pos = {world->m[12], world->m[13], world->m[14]};
            const vec3 d = vec3_sub(pos, ctx->camera_pos);

            BH_TransparentDraw td = {
                .mesh = mesh, .submesh = sm, .material = mat, .world = *world, .sort_key = vec3_lensq(d)};
            (void)bh_transparent_queue_push(ctx->transparent, &td);
            continue;
        }

        bh_bind_material(&ctx->draw, mat);
        BH_GPU_DrawIndexedPrimitives(ctx->draw.pass, sm->index_count, 1, sm->first_index, 0, 0);
    }
}

static void bh_draw_transparent_queue(BH_RenderDrawContext *draw, const BH_TransparentQueue *q)
{
    const BH_ShaderProgram *prog = draw->program ? draw->program : &draw->renderer->program;

    for (uint32_t i = 0; i < q->count; ++i)
    {
        const BH_TransparentDraw *td = &q->items[i];
        if (!td->mesh || !td->submesh)
            continue;

        BH_GPUBufferBinding vb = {.buffer = td->mesh->vertex_buffer, .offset = 0};
        BH_GPU_BindVertexBuffers(draw->pass, 0, &vb, 1);

        BH_GPUBufferBinding ib = {.buffer = td->mesh->index_buffer, .offset = 0};
        BH_GPU_BindIndexBuffer(draw->pass, &ib, td->mesh->index_element_size);

        (void)bh_push_per_draw_uniforms(prog, draw->cmd, draw->view_proj, &td->world);

        bh_bind_material(draw, td->material);
        BH_GPU_DrawIndexedPrimitives(draw->pass, td->submesh->index_count, 1, td->submesh->first_index, 0, 0);
    }
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_Renderer_AddHooks(BH_Renderer *r, BH_RenderPassHooks hooks)
{
    if (!r)
        return false;

    const uint32_t cap = (uint32_t)(sizeof(r->hooks) / sizeof(r->hooks[0]));
    if (r->hook_count >= cap)
    {
        SDL_Log("[bh] Hook limit reached (cap=%u)", cap);
        return false;
    }

    r->hooks[r->hook_count++] = hooks;
    return true;
}

bool BH_Renderer_Init(BH_Renderer *r, BH_Window *window, const char *asset_root, const BH_RendererConfig *cfg,
                      BH_Arena *permanent_arena)
{
    if (!r || !window || !asset_root || !permanent_arena)
        return false;

    *r = (BH_Renderer){0};
    r->window = window;

    const bool debug = cfg ? cfg->debug_gpu : false;
    const bool enable_vsync = cfg ? cfg->enable_vsync : false;
    const bool use_gl = (SDL_strcasecmp(BH_GPU_GetBackend()->name, "OpenGL") == 0);
    const BH_GPUShaderFormat shader_format = use_gl ? BH_GPU_SHADERFORMAT_GLSL : BH_GPU_SHADERFORMAT_SPIRV;

    r->device = BH_GPU_CreateDevice(shader_format, debug, NULL);
    if (!r->device)
    {
        SDL_Log("[bh] BH_GPU_CreateDevice failed: %s", BH_GPU_GetLastError());
        return false;
    }

    if (!BH_GPU_ClaimWindowForDevice(r->device, window))
    {
        SDL_Log("[bh] Window claim failed: %s", BH_GPU_GetLastError());
        BH_GPU_DestroyDevice(r->device);
        r->device = NULL;
        return false;
    }

    const BH_GPUPresentMode present_mode = enable_vsync ? BH_GPU_PRESENTMODE_VSYNC : BH_GPU_PRESENTMODE_IMMEDIATE;
    BH_GPU_SetSwapchainParameters(r->device, window, BH_GPU_SWAPCHAINCOMPOSITION_SDR, present_mode);

    r->swapchain_format = BH_GPU_GetSwapchainTextureFormat(r->device, window);
    r->depth_format = BH_GPU_GetTextureFormat_D32_FLOAT();

    char paths[4][1024];
    SDL_snprintf(paths[0], 1024, "%s/shaders/%s/basic.vert.%s", asset_root, use_gl ? "gl" : "compiled", use_gl ? "glsl" : "spv");
    SDL_snprintf(paths[1], 1024, "%s/shaders/compiled/basic.vert.json", asset_root);
    SDL_snprintf(paths[2], 1024, "%s/shaders/%s/basic.frag.%s", asset_root, use_gl ? "gl" : "compiled", use_gl ? "glsl" : "spv");
    SDL_snprintf(paths[3], 1024, "%s/shaders/compiled/basic.frag.json", asset_root);

    if (!BH_ShaderProgram_Load(&r->program, r->device, paths[0], paths[1], paths[2], paths[3],
                               r->swapchain_format, r->depth_format,
                               permanent_arena))
    {
        BH_Renderer_Shutdown(r);
        return false;
    }

    /* Transparent Pipeline */
    {
        BH_GPUVertexAttribute attrs[] = {
            {.location = 0, .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0},
            {.location = 1, .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 12},
            {.location = 2, .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 24},
            {.location = 3, .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 32},
        };
        BH_GPUVertexBufferDescription vbs[] = {
            {.slot = 0, .input_rate = BH_GPU_VERTEXINPUTRATE_VERTEX, .pitch = sizeof(BH_Vertex)}};

        BH_ShaderProgramPipelineConfig pcfg = {
            .vertex_input = {.num_vertex_buffers = 1,
                             .vertex_buffer_descriptions = vbs,
                             .num_vertex_attributes = 4,
                             .vertex_attributes = attrs},
            .primitive_type = BH_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = {.fill_mode = BH_GPU_FILLMODE_FILL, .front_face = BH_GPU_FRONTFACE_COUNTER_CLOCKWISE},
            .multisample_state = {.sample_count = BH_GPU_SAMPLECOUNT_1},
            .depth_stencil_state = {.enable_depth_test = true,
                                    .enable_depth_write = false,
                                    .compare_op = BH_GPU_COMPAREOP_LESS_OR_EQUAL},
            .blend_state = {.enable_blend = true,
                            .src_color_blendfactor = BH_GPU_BLENDFACTOR_SRC_ALPHA,
                            .dst_color_blendfactor = BH_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                            .color_blend_op = BH_GPU_BLENDOP_ADD,
                            .src_alpha_blendfactor = BH_GPU_BLENDFACTOR_ONE,
                            .dst_alpha_blendfactor = BH_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                            .alpha_blend_op = BH_GPU_BLENDOP_ADD},
            .has_depth_stencil_target = true};

        if (!BH_ShaderProgram_LoadEx(&r->program_transparent, r->device, paths[0], paths[1], paths[2],
                                     paths[3], r->swapchain_format,
                                     r->depth_format, &pcfg, permanent_arena))
        {
            BH_Renderer_Shutdown(r);
            return false;
        }
    }

    /* Skybox Pipeline */
    {
        char sky_paths[4][1024];
        SDL_snprintf(sky_paths[0], 1024, "%s/shaders/%s/skybox.vert.%s", asset_root, use_gl ? "gl" : "compiled",
                     use_gl ? "glsl" : "spv");
        SDL_snprintf(sky_paths[1], 1024, "%s/shaders/compiled/skybox.vert.json", asset_root);
        SDL_snprintf(sky_paths[2], 1024, "%s/shaders/%s/skybox.frag.%s", asset_root, use_gl ? "gl" : "compiled",
                     use_gl ? "glsl" : "spv");
        SDL_snprintf(sky_paths[3], 1024, "%s/shaders/compiled/skybox.frag.json", asset_root);

        BH_GPUVertexAttribute attrs[] = {
            {.location = 0, .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0},
            {.location = 1, .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 12},
            {.location = 2, .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 24},
            {.location = 3, .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 32},
        };
        BH_GPUVertexBufferDescription vbs[] = {
            {.slot = 0, .input_rate = BH_GPU_VERTEXINPUTRATE_VERTEX, .pitch = sizeof(BH_Vertex)}};

        BH_ShaderProgramPipelineConfig pcfg = {
            .vertex_input = {.num_vertex_buffers = 1,
                             .vertex_buffer_descriptions = vbs,
                             .num_vertex_attributes = 4,
                             .vertex_attributes = attrs},
            .primitive_type = BH_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = {.fill_mode = BH_GPU_FILLMODE_FILL, .front_face = BH_GPU_FRONTFACE_COUNTER_CLOCKWISE},
            .multisample_state = {.sample_count = BH_GPU_SAMPLECOUNT_1},
            .depth_stencil_state = {.enable_depth_test = false, .compare_op = BH_GPU_COMPAREOP_ALWAYS},
            .has_depth_stencil_target = true};

        if (!BH_ShaderProgram_LoadEx(&r->program_skybox, r->device, sky_paths[0], sky_paths[1],
                                     sky_paths[2], sky_paths[3], r->swapchain_format,
                                     r->depth_format, &pcfg, permanent_arena))
        {
            BH_Renderer_Shutdown(r);
            return false;
        }

        if (!BH_Mesh_CreateCube(&r->skybox_mesh, r->device, permanent_arena, 64.0f, NULL, NULL))
        {
            BH_Renderer_Shutdown(r);
            return false;
        }
        r->skybox_mesh_created = true;
    }

    if (!BH_TextureManager_Init(&r->textures, r->device, permanent_arena))
    {
        BH_Renderer_Shutdown(r);
        return false;
    }

    if (!BH_MaterialManager_Init(&r->materials, asset_root, &r->textures, &r->program.fs_refl, permanent_arena))
    {
        BH_Renderer_Shutdown(r);
        return false;
    }

    return true;
}

void BH_Renderer_Shutdown(BH_Renderer *r)
{
    if (!r)
        return;

    if (r->device)
    {
        BH_GPU_WaitForIdle(r->device);

        BH_MaterialManager_Shutdown(&r->materials);
        BH_TextureManager_Shutdown(&r->textures);

        if (r->depth_texture)
        {
            BH_GPU_ReleaseTexture(r->device, r->depth_texture);
        }

        if (r->skybox_mesh_created)
        {
            BH_Mesh_Release(&r->skybox_mesh, r->device);
        }

        BH_ShaderProgram_Release(&r->program_skybox, r->device);
        BH_ShaderProgram_Release(&r->program_transparent, r->device);
        BH_ShaderProgram_Release(&r->program, r->device);

        if (r->window)
        {
            BH_GPU_ReleaseWindowFromDevice(r->device, r->window);
        }

        BH_GPU_DestroyDevice(r->device);
    }
    *r = (BH_Renderer){0};
}

void BH_Renderer_RenderScene(BH_Renderer *r, const BH_Scene *scene, const mat4 *view_proj, vec3 camera_pos, float alpha)
{
    if (!r || !r->device || !r->window || !scene || !view_proj)
        return;

    BH_GPUCommandBuffer *cmd = BH_GPU_AcquireCommandBuffer(r->device);
    if (!cmd)
        return;

    bool submit_ok = false;
    BH_GPUTexture *swap_tex = NULL;
    uint32_t w = 0, h = 0;

    BH_GPU_WaitAndAcquireSwapchainTexture(cmd, r->window, &swap_tex, &w, &h);

    if (!swap_tex || w == 0 || h == 0)
    {
        BH_GPU_CancelCommandBuffer(cmd);
        cmd = NULL;
        goto end_frame;
    }

    if (!bh_renderer_ensure_depth_texture(r, w, h))
    {
        BH_GPU_CancelCommandBuffer(cmd);
        cmd = NULL;
        goto end_frame;
    }

    /* Hook: Prepare (Uploads/Copies) */
    {
        BH_GPUCopyPass *copy_pass = BH_GPU_BeginCopyPass(cmd);
        if (copy_pass)
        {
            for (uint32_t i = 0; i < r->hook_count; ++i)
            {
                if (r->hooks[i].prepare)
                {
                    r->hooks[i].prepare(r->hooks[i].user, cmd, copy_pass, view_proj, w, h, alpha);
                }
            }
            BH_GPU_EndCopyPass(copy_pass);
        }
    }

    BH_GPUColorTargetInfo color = {.texture = swap_tex,
                                   .clear_color = {0.0f, 0.0f, 0.0f, 1.0f},
                                   .load_op = BH_GPU_LOADOP_CLEAR,
                                   .store_op = BH_GPU_STOREOP_STORE};

    BH_GPUDepthStencilTargetInfo depth = {.texture = r->depth_texture,
                                          .clear_depth = 1.0f,
                                          .load_op = BH_GPU_LOADOP_CLEAR,
                                          .store_op = BH_GPU_STOREOP_DONT_CARE};

    BH_GPURenderPass *pass = BH_GPU_BeginRenderPass(cmd, &color, 1, &depth);
    if (!pass)
    {
        BH_GPU_CancelCommandBuffer(cmd);
        cmd = NULL;
        goto end_frame;
    }

    BH_GPU_SetViewport(pass, &(BH_GPUViewport){0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f});
    BH_GPU_SetScissor(pass, &(BH_GPU_Rect){0, 0, (int32_t)w, (int32_t)h});

    /* Skybox Pass */
    if (r->program_skybox.pipeline && r->skybox_mesh_created)
    {
        BH_GPU_BindGraphicsPipeline(pass, r->program_skybox.pipeline);

        BH_GPUBufferBinding vb = {.buffer = r->skybox_mesh.vertex_buffer, .offset = 0};
        BH_GPU_BindVertexBuffers(pass, 0, &vb, 1);

        BH_GPUBufferBinding ib = {.buffer = r->skybox_mesh.index_buffer, .offset = 0};
        BH_GPU_BindIndexBuffer(pass, &ib, r->skybox_mesh.index_element_size);

        const mat4 sky_world = mat4_translate(camera_pos);
        (void)bh_push_skybox_uniforms(&r->program_skybox, cmd, view_proj, &sky_world);

        BH_GPU_DrawIndexedPrimitives(pass, r->skybox_mesh.index_count, 1, 0, 0, 0);
    }

    /* Opaque Pass */
    BH_GPU_BindGraphicsPipeline(pass, r->program.pipeline);
    bh_push_scene_fragment_uniforms(r, cmd, scene, camera_pos);

    BH_TransparentQueue transparent = {0};
    BH_SceneSplitPassContext split = {
        .draw = {.renderer = r, .cmd = cmd, .pass = pass, .view_proj = view_proj, .program = &r->program},
        .camera_pos = camera_pos,
        .transparent = &transparent};

    BH_Scene_Traverse(scene, alpha, bh_draw_scene_node_split, &split);

    /* Transparent Pass */
    if (transparent.count > 0)
    {
        qsort(transparent.items, transparent.count, sizeof(BH_TransparentDraw), bh_cmp_transparent_back_to_front);

        BH_GPU_BindGraphicsPipeline(pass, r->program_transparent.pipeline);
        bh_push_scene_fragment_uniforms(r, cmd, scene, camera_pos);

        BH_RenderDrawContext tctx = split.draw;
        bh_draw_transparent_queue(&tctx, &transparent);
    }
    bh_transparent_queue_free(&transparent);

    /* Hook: Draw (Overlays/UI) */
    for (uint32_t i = 0; i < r->hook_count; ++i)
    {
        if (r->hooks[i].draw)
        {
            r->hooks[i].draw(r->hooks[i].user, cmd, pass, view_proj, w, h, alpha);
        }
    }

    BH_GPU_EndRenderPass(pass);

    submit_ok = BH_GPU_SubmitCommandBuffer(cmd);
    cmd = NULL;

end_frame:
    /* Hook: End Frame (Cleanup) */
    for (uint32_t i = 0; i < r->hook_count; ++i)
    {
        if (r->hooks[i].end_frame)
        {
            r->hooks[i].end_frame(r->hooks[i].user, submit_ok);
        }
    }
}