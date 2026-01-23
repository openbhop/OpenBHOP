/* -----------------------------------------------------------------------------
   bh_shader_program.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../core/bh_arena.h"
#include "bh_mesh.h"
#include "bh_shader_reflection.h"
#include <SDL3/SDL_gpu.h>

typedef struct BH_ShaderProgram
{
    SDL_GPUShader *vs;
    SDL_GPUShader *fs;
    SDL_GPUGraphicsPipeline *pipeline;

    BH_ShaderStageReflection vs_refl;
    BH_ShaderStageReflection fs_refl;

    SDL_GPUTextureFormat color_format;
    SDL_GPUTextureFormat depth_format;
} BH_ShaderProgram;

/*
   Pipeline configuration.
   Allows subsystems to share loading logic while defining specific vertex layouts,
   topologies, and depth settings.
*/
typedef struct BH_ShaderProgramPipelineConfig
{
    SDL_GPUVertexInputState vertex_input;
    SDL_GPUPrimitiveType primitive_type;

    SDL_GPURasterizerState rasterizer_state;
    SDL_GPUMultisampleState multisample_state;
    SDL_GPUDepthStencilState depth_stencil_state;

    SDL_GPUColorTargetBlendState blend_state;
    bool has_depth_stencil_target;
} BH_ShaderProgramPipelineConfig;

/*
   Loads shader stages and creates a graphics pipeline with custom configuration.
*/
bool BH_ShaderProgram_LoadEx(BH_ShaderProgram *out_prog, SDL_GPUDevice *device, const char *vs_spv_path,
                             const char *vs_json_path, const char *fs_spv_path, const char *fs_json_path,
                             SDL_GPUTextureFormat color_format, SDL_GPUTextureFormat depth_format,
                             const BH_ShaderProgramPipelineConfig *pipeline_cfg, BH_Arena *permanent_arena);

/*
   Loads shader stages and creates a default pipeline for BH_Vertex meshes.
*/
bool BH_ShaderProgram_Load(BH_ShaderProgram *out_prog, SDL_GPUDevice *device, const char *vs_spv_path,
                           const char *vs_json_path, const char *fs_spv_path, const char *fs_json_path,
                           SDL_GPUTextureFormat color_format, SDL_GPUTextureFormat depth_format,
                           BH_Arena *permanent_arena);

void BH_ShaderProgram_Release(BH_ShaderProgram *prog, SDL_GPUDevice *device);
