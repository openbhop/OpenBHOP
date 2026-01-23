/* -----------------------------------------------------------------------------
   bh_shader_program.c
   ----------------------------------------------------------------------------- */

#include "bh_shader_program.h"
#include "../core/bh_file.h"
#include <SDL3/SDL.h>
#include <stddef.h>

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static SDL_GPUShader *bh_create_shader_from_spirv(SDL_GPUDevice *device, SDL_GPUShaderStage stage, const char *spv_path,
                                                  const BH_ShaderStageReflection *refl)
{
    BH_FileData fd = {0};
    if (!BH_File_ReadAll(spv_path, &fd))
    {
        SDL_Log("[bh] Failed to read shader: %s", spv_path);
        return NULL;
    }

    SDL_GPUShaderCreateInfo ci = {.code = fd.data,
                                  .code_size = fd.size,
                                  .entrypoint = (refl && refl->entry) ? refl->entry : NULL,
                                  .format = SDL_GPU_SHADERFORMAT_SPIRV,
                                  .stage = stage,
                                  .num_samplers = refl ? refl->num_samplers : 0,
                                  .num_storage_textures = refl ? refl->num_storage_textures : 0,
                                  .num_storage_buffers = refl ? refl->num_storage_buffers : 0,
                                  .num_uniform_buffers = refl ? refl->num_uniform_buffers : 0};

    SDL_GPUShader *shader = SDL_CreateGPUShader(device, &ci);
    if (!shader)
    {
        SDL_Log("[bh] SDL_CreateGPUShader failed for %s: %s", spv_path, SDL_GetError());
    }

    BH_File_Free(&fd);
    return shader;
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_ShaderProgram_Load(BH_ShaderProgram *out_prog, SDL_GPUDevice *device, const char *vs_spv_path,
                           const char *vs_json_path, const char *fs_spv_path, const char *fs_json_path,
                           SDL_GPUTextureFormat color_format, SDL_GPUTextureFormat depth_format,
                           BH_Arena *permanent_arena)
{
    if (!out_prog || !device || !vs_spv_path || !vs_json_path || !fs_spv_path || !fs_json_path || !permanent_arena)
    {
        return false;
    }

    SDL_GPUVertexBufferDescription vb_desc = {.slot = 0,
                                              .pitch = (uint32_t)sizeof(BH_Vertex),
                                              .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
                                              .instance_step_rate = 0};

    SDL_GPUVertexAttribute attrs[] = {{.location = 0,
                                       .buffer_slot = 0,
                                       .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                       .offset = (uint32_t)offsetof(BH_Vertex, position)},
                                      {.location = 1,
                                       .buffer_slot = 0,
                                       .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                       .offset = (uint32_t)offsetof(BH_Vertex, normal)},
                                      {.location = 2,
                                       .buffer_slot = 0,
                                       .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                                       .offset = (uint32_t)offsetof(BH_Vertex, uv)},
                                      {.location = 3,
                                       .buffer_slot = 0,
                                       .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                                       .offset = (uint32_t)offsetof(BH_Vertex, uv2)}};

    BH_ShaderProgramPipelineConfig pcfg = {.vertex_input = {.vertex_buffer_descriptions = &vb_desc,
                                                            .num_vertex_buffers = 1,
                                                            .vertex_attributes = attrs,
                                                            .num_vertex_attributes = 4},
                                           .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                                           .rasterizer_state = {.fill_mode = SDL_GPU_FILLMODE_FILL,
                                                                .cull_mode = SDL_GPU_CULLMODE_NONE,
                                                                .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE},
                                           .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1},
                                           .depth_stencil_state = {.enable_depth_test = true,
                                                                   .enable_depth_write = true,
                                                                   .compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL},
                                           .blend_state = {.enable_blend = false, .enable_color_write_mask = false},
                                           .has_depth_stencil_target = true};

    return BH_ShaderProgram_LoadEx(out_prog, device, vs_spv_path, vs_json_path, fs_spv_path, fs_json_path, color_format,
                                   depth_format, &pcfg, permanent_arena);
}

bool BH_ShaderProgram_LoadEx(BH_ShaderProgram *out_prog, SDL_GPUDevice *device, const char *vs_spv_path,
                             const char *vs_json_path, const char *fs_spv_path, const char *fs_json_path,
                             SDL_GPUTextureFormat color_format, SDL_GPUTextureFormat depth_format,
                             const BH_ShaderProgramPipelineConfig *pipeline_cfg, BH_Arena *permanent_arena)
{
    if (!out_prog || !device || !vs_spv_path || !vs_json_path || !fs_spv_path || !fs_json_path || !pipeline_cfg ||
        !permanent_arena)
    {
        return false;
    }

    *out_prog = (BH_ShaderProgram){.color_format = color_format, .depth_format = depth_format};

    if (!BH_ShaderReflection_Load(&out_prog->vs_refl, vs_json_path, permanent_arena))
    {
        return false;
    }
    if (!BH_ShaderReflection_Load(&out_prog->fs_refl, fs_json_path, permanent_arena))
    {
        return false;
    }

    out_prog->vs = bh_create_shader_from_spirv(device, SDL_GPU_SHADERSTAGE_VERTEX, vs_spv_path, &out_prog->vs_refl);
    out_prog->fs = bh_create_shader_from_spirv(device, SDL_GPU_SHADERSTAGE_FRAGMENT, fs_spv_path, &out_prog->fs_refl);

    if (!out_prog->vs || !out_prog->fs)
    {
        BH_ShaderProgram_Release(out_prog, device);
        return false;
    }

    SDL_GPUColorTargetDescription color_desc = {.format = color_format, .blend_state = pipeline_cfg->blend_state};

    SDL_GPUGraphicsPipelineCreateInfo pci = {
        .vertex_shader = out_prog->vs,
        .fragment_shader = out_prog->fs,
        .vertex_input_state = pipeline_cfg->vertex_input,
        .primitive_type = pipeline_cfg->primitive_type,
        .rasterizer_state = pipeline_cfg->rasterizer_state,
        .multisample_state = pipeline_cfg->multisample_state,
        .depth_stencil_state = pipeline_cfg->depth_stencil_state,
        .target_info = {.color_target_descriptions = &color_desc,
                        .num_color_targets = 1,
                        .depth_stencil_format = depth_format,
                        .has_depth_stencil_target = pipeline_cfg->has_depth_stencil_target}};

    out_prog->pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    if (!out_prog->pipeline)
    {
        SDL_Log("[bh] SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        BH_ShaderProgram_Release(out_prog, device);
        return false;
    }

    return true;
}

void BH_ShaderProgram_Release(BH_ShaderProgram *prog, SDL_GPUDevice *device)
{
    if (!prog || !device)
    {
        return;
    }

    if (prog->pipeline)
    {
        SDL_ReleaseGPUGraphicsPipeline(device, prog->pipeline);
    }
    if (prog->vs)
    {
        SDL_ReleaseGPUShader(device, prog->vs);
    }
    if (prog->fs)
    {
        SDL_ReleaseGPUShader(device, prog->fs);
    }

    *prog = (BH_ShaderProgram){0};
}