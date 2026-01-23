/* -----------------------------------------------------------------------------
   bh_shader_program.c
   ----------------------------------------------------------------------------- */

#include "bh_shader_program.h"
#include "bh_gpu.h"
#include "../core/bh_file.h"

#include <SDL3/SDL.h>
#include <stddef.h>
#include <string.h>

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static BH_GPUShader *bh_create_shader_from_spirv(BH_GPUDevice *device, BH_GPUShaderStage stage, const char *spv_path,
                                                 const BH_ShaderStageReflection *refl)
{
    BH_FileData fd = {0};
    if (!BH_File_ReadAll(spv_path, &fd))
    {
        SDL_Log("[bh] Failed to read shader: %s", spv_path);
        return NULL;
    }

    BH_GPUShaderCreateInfo ci = {0};
    ci.code = fd.data;
    ci.code_size = fd.size;
    ci.entrypoint = (refl && refl->entry && refl->entry[0]) ? refl->entry : "main";
    ci.format = BH_GPU_SHADERFORMAT_SPIRV;
    ci.stage = stage;

    ci.num_samplers = refl ? refl->num_samplers : 0;
    ci.num_storage_textures = refl ? refl->num_storage_textures : 0;
    ci.num_storage_buffers = refl ? refl->num_storage_buffers : 0;
    ci.num_uniform_buffers = refl ? refl->num_uniform_buffers : 0;

    BH_GPUShader *shader = BH_GPU_CreateShader(device, &ci);
    if (!shader)
    {
        SDL_Log("[bh] BH_GPU_CreateShader failed for %s: %s", spv_path, BH_GPU_GetLastError());
    }

    BH_File_Free(&fd);
    return shader;
}

static bool bh_path_has_ext(const char *path, const char *ext)
{
    if (!path || !ext)
    {
        return false;
    }

    const size_t path_len = strlen(path);
    const size_t ext_len = strlen(ext);
    if (path_len < ext_len)
    {
        return false;
    }

    return SDL_strcasecmp(path + path_len - ext_len, ext) == 0;
}

static BH_GPUShader *bh_create_shader_from_glsl(BH_GPUDevice *device, BH_GPUShaderStage stage, const char *glsl_path,
                                                const BH_ShaderStageReflection *refl)
{
    BH_FileData fd = {0};
    if (!BH_File_ReadAll(glsl_path, &fd))
    {
        SDL_Log("[bh] Failed to read shader: %s", glsl_path);
        return NULL;
    }

    BH_GPUShaderCreateInfo ci = {0};
    ci.code = fd.data;
    ci.code_size = fd.size;
    // GLSL always uses 'main' in this project.
    ci.entrypoint = "main";
    ci.format = BH_GPU_SHADERFORMAT_GLSL;
    ci.stage = stage;

    ci.num_samplers = refl ? refl->num_samplers : 0;
    ci.num_storage_textures = refl ? refl->num_storage_textures : 0;
    ci.num_storage_buffers = refl ? refl->num_storage_buffers : 0;
    ci.num_uniform_buffers = refl ? refl->num_uniform_buffers : 0;

    BH_GPUShader *shader = BH_GPU_CreateShader(device, &ci);
    if (!shader)
    {
        SDL_Log("[bh] BH_GPU_CreateShader failed for %s: %s", glsl_path, BH_GPU_GetLastError());
    }

    BH_File_Free(&fd);
    return shader;
}

static BH_GPUShader *bh_create_shader_from_file(BH_GPUDevice *device, BH_GPUShaderStage stage, const char *path,
                                                const BH_ShaderStageReflection *refl)
{
    if (bh_path_has_ext(path, ".glsl"))
    {
        return bh_create_shader_from_glsl(device, stage, path, refl);
    }

    // Default to SPIR-V (compiled pipeline).
    return bh_create_shader_from_spirv(device, stage, path, refl);
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_ShaderProgram_Load(BH_ShaderProgram *out_prog, BH_GPUDevice *device, const char *vs_spv_path,
                           const char *vs_json_path, const char *fs_spv_path, const char *fs_json_path,
                           BH_GPUTextureFormat color_format, BH_GPUTextureFormat depth_format,
                           BH_Arena *permanent_arena)
{
    if (!out_prog || !device || !vs_spv_path || !vs_json_path || !fs_spv_path || !fs_json_path || !permanent_arena)
    {
        return false;
    }

    BH_GPUVertexBufferDescription vb_desc = {.slot = 0,
                                             .pitch = (uint32_t)sizeof(BH_Vertex),
                                             .input_rate = BH_GPU_VERTEXINPUTRATE_VERTEX,
                                             .instance_step_rate = 0};

    BH_GPUVertexAttribute attrs[] = {{.location = 0,
                                      .buffer_slot = 0,
                                      .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                      .offset = (uint32_t)offsetof(BH_Vertex, position)},
                                     {.location = 1,
                                      .buffer_slot = 0,
                                      .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                      .offset = (uint32_t)offsetof(BH_Vertex, normal)},
                                     {.location = 2,
                                      .buffer_slot = 0,
                                      .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                                      .offset = (uint32_t)offsetof(BH_Vertex, uv)},
                                     {.location = 3,
                                      .buffer_slot = 0,
                                      .format = BH_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                                      .offset = (uint32_t)offsetof(BH_Vertex, uv2)}};

    BH_ShaderProgramPipelineConfig pcfg = {.vertex_input = {.vertex_buffer_descriptions = &vb_desc,
                                                            .num_vertex_buffers = 1,
                                                            .vertex_attributes = attrs,
                                                            .num_vertex_attributes = 4},
                                           .primitive_type = BH_GPU_PRIMITIVETYPE_TRIANGLELIST,
                                           .rasterizer_state = {.fill_mode = BH_GPU_FILLMODE_FILL,
                                                                .cull_mode = BH_GPU_CULLMODE_NONE,
                                                                .front_face = BH_GPU_FRONTFACE_COUNTER_CLOCKWISE},
                                           .multisample_state = {.sample_count = BH_GPU_SAMPLECOUNT_1},
                                           .depth_stencil_state = {.enable_depth_test = true,
                                                                   .enable_depth_write = true,
                                                                   .compare_op = BH_GPU_COMPAREOP_LESS_OR_EQUAL},
                                           .blend_state = {.enable_blend = false, .enable_color_write_mask = false},
                                           .has_depth_stencil_target = true};

    return BH_ShaderProgram_LoadEx(out_prog, device, vs_spv_path, vs_json_path, fs_spv_path, fs_json_path, color_format,
                                   depth_format, &pcfg, permanent_arena);
}

bool BH_ShaderProgram_LoadEx(BH_ShaderProgram *out_prog, BH_GPUDevice *device, const char *vs_spv_path,
                             const char *vs_json_path, const char *fs_spv_path, const char *fs_json_path,
                             BH_GPUTextureFormat color_format, BH_GPUTextureFormat depth_format,
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

    out_prog->vs = bh_create_shader_from_file(device, BH_GPU_SHADERSTAGE_VERTEX, vs_spv_path, &out_prog->vs_refl);
    out_prog->fs = bh_create_shader_from_file(device, BH_GPU_SHADERSTAGE_FRAGMENT, fs_spv_path, &out_prog->fs_refl);

    if (!out_prog->vs || !out_prog->fs)
    {
        BH_ShaderProgram_Release(out_prog, device);
        return false;
    }

    BH_GPUColorTargetDescription color_desc = {.format = color_format, .blend_state = pipeline_cfg->blend_state};

    BH_GPUGraphicsPipelineCreateInfo pci = {0};
    pci.vertex_shader = out_prog->vs;
    pci.fragment_shader = out_prog->fs;
    pci.vertex_input_state = pipeline_cfg->vertex_input;
    pci.primitive_type = pipeline_cfg->primitive_type;
    pci.rasterizer_state = pipeline_cfg->rasterizer_state;
    pci.multisample_state = pipeline_cfg->multisample_state;
    pci.depth_stencil_state = pipeline_cfg->depth_stencil_state;
    pci.target_info.color_target_descriptions = &color_desc;
    pci.target_info.num_color_targets = 1;
    pci.target_info.depth_stencil_format = depth_format;
    pci.target_info.has_depth_stencil_target = pipeline_cfg->has_depth_stencil_target;

    out_prog->pipeline = BH_GPU_CreateGraphicsPipeline(device, &pci);
    if (!out_prog->pipeline)
    {
        SDL_Log("[bh] BH_GPU_CreateGraphicsPipeline failed: %s", BH_GPU_GetLastError());
        BH_ShaderProgram_Release(out_prog, device);
        return false;
    }

    return true;
}

void BH_ShaderProgram_Release(BH_ShaderProgram *prog, BH_GPUDevice *device)
{
    if (!prog || !device)
    {
        return;
    }

    if (prog->pipeline)
    {
        BH_GPU_ReleaseGraphicsPipeline(device, prog->pipeline);
    }
    if (prog->vs)
    {
        BH_GPU_ReleaseShader(device, prog->vs);
    }
    if (prog->fs)
    {
        BH_GPU_ReleaseShader(device, prog->fs);
    }

    *prog = (BH_ShaderProgram){0};
}
