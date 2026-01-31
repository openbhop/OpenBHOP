/* -----------------------------------------------------------------------------
   bh_renderer.h
   ----------------------------------------------------------------------------- */
#pragma once

#include "bh_material.h"
#include "bh_mesh.h"
#include "bh_shader_program.h"

#include "bh_material_manager.h"
#include "bh_texture_manager.h"

#include "../core/bh_arena.h"
#include "../math/bh_math.h"

/* Backend-agnostic GPU handles. */
#include "bh_gpu.h"

/* Note: This header intentionally avoids including any backend GPU headers.
   Subsystems that still directly use SDL_gpu should include it themselves. */

struct BH_Scene;

typedef struct BH_RendererConfig
{
    bool debug_gpu;
    bool enable_vsync;
} BH_RendererConfig;

/* Render Pass Injection Hooks
   Renderer-agnostic callbacks for overlays, gizmos, and post-scene logic.
   Callbacks execute on the render thread.
*/
typedef struct BH_RenderPassHooks
{
    /* Pre-pass: GPU copy/upload operations. */
    void (*prepare)(void *user, BH_GPUCommandBuffer *cmd, BH_GPUCopyPass *copy_pass, const mat4 *view_proj,
                    uint32_t fb_width, uint32_t fb_height, float alpha);

    /* Main pass: Post-scene draw commands. */
    void (*draw)(void *user, BH_GPUCommandBuffer *cmd, BH_GPURenderPass *render_pass, const mat4 *view_proj,
                 uint32_t fb_width, uint32_t fb_height, float alpha);

    /* Post-submission: Resource cleanup (transfer buffers). */
    void (*end_frame)(void *user, bool submit_ok);

    void *user;
} BH_RenderPassHooks;

typedef struct BH_Renderer
{
    BH_GPUDevice *device;
    BH_Window *window;

    BH_GPUTextureFormat swapchain_format;
    BH_GPUTextureFormat depth_format;

    BH_GPUTexture *depth_texture;
    uint32_t depth_width;
    uint32_t depth_height;

    /* Programs */
    BH_ShaderProgram program;             /* Opaque */
    BH_ShaderProgram program_transparent; /* Alpha Blended */
    BH_ShaderProgram program_skybox;      /* Procedural Sky */

    /* Resources */
    BH_Mesh skybox_mesh;
    bool skybox_mesh_created;

    BH_TextureManager textures;
    BH_MaterialManager materials;

    void *transparent_items;
    uint32_t transparent_count;
    uint32_t transparent_cap;

    BH_RenderPassHooks hooks[16];
    uint32_t hook_count;
} BH_Renderer;

bool BH_Renderer_AddHooks(BH_Renderer *r, BH_RenderPassHooks hooks);

bool BH_Renderer_Init(BH_Renderer *r, BH_Window *window, const char *asset_root, const BH_RendererConfig *cfg,
                      BH_Arena *permanent_arena);

void BH_Renderer_Shutdown(BH_Renderer *r);

void BH_Renderer_RenderScene(BH_Renderer *r, const struct BH_Scene *scene, const mat4 *view_proj, vec3 camera_pos,
                             float alpha);
