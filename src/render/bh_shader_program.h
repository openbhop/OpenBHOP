/* -----------------------------------------------------------------------------
   bh_shader_program.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../core/bh_arena.h"
#include "bh_mesh.h"
#include "bh_shader_reflection.h"
#include "bh_gpu.h"

typedef struct BH_ShaderProgram
{
    BH_GPUShader *vs;
    BH_GPUShader *fs;
    BH_GPUGraphicsPipeline *pipeline;

    BH_ShaderStageReflection vs_refl;
    BH_ShaderStageReflection fs_refl;

    BH_GPUTextureFormat color_format;
    BH_GPUTextureFormat depth_format;
} BH_ShaderProgram;

/*
   Pipeline configuration.
   Allows subsystems to share loading logic while defining specific vertex layouts,
   topologies, and depth settings.
*/
typedef struct BH_ShaderProgramPipelineConfig
{
    BH_GPUVertexInputState vertex_input;
    BH_GPUPrimitiveType primitive_type;

    BH_GPURasterizerState rasterizer_state;
    BH_GPUMultisampleState multisample_state;
    BH_GPUDepthStencilState depth_stencil_state;

    BH_GPUColorTargetBlendState blend_state;
    bool has_depth_stencil_target;
} BH_ShaderProgramPipelineConfig;

/*
   Loads shader stages and creates a graphics pipeline with custom configuration.
*/
bool BH_ShaderProgram_LoadEx(BH_ShaderProgram *out_prog, BH_GPUDevice *device, const char *vs_spv_path,
                             const char *vs_json_path, const char *fs_spv_path, const char *fs_json_path,
                             BH_GPUTextureFormat color_format, BH_GPUTextureFormat depth_format,
                             const BH_ShaderProgramPipelineConfig *pipeline_cfg, BH_Arena *permanent_arena);

/*
   Loads shader stages and creates a default pipeline for BH_Vertex meshes.
*/
bool BH_ShaderProgram_Load(BH_ShaderProgram *out_prog, BH_GPUDevice *device, const char *vs_spv_path,
                           const char *vs_json_path, const char *fs_spv_path, const char *fs_json_path,
                           BH_GPUTextureFormat color_format, BH_GPUTextureFormat depth_format,
                           BH_Arena *permanent_arena);

void BH_ShaderProgram_Release(BH_ShaderProgram *prog, BH_GPUDevice *device);
