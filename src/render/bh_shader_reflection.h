/* -----------------------------------------------------------------------------
   bh_shader_reflection.h
   ----------------------------------------------------------------------------- */
#pragma once

#include "../core/bh_arena.h"
#include "../core/bh_core.h"

#define BH_GPU_MAX_UNIFORM_SLOTS 4
#define BH_GPU_MAX_PARAMS 64

typedef enum BH_ShaderStage
{
    BH_SHADERSTAGE_VERTEX = 0,
    BH_SHADERSTAGE_FRAGMENT = 1,
} BH_ShaderStage;

typedef enum BH_ParamType
{
    BH_PARAMTYPE_UNKNOWN = 0,
    BH_PARAMTYPE_FLOAT,
    BH_PARAMTYPE_FLOAT2,
    BH_PARAMTYPE_FLOAT3,
    BH_PARAMTYPE_FLOAT4,
    BH_PARAMTYPE_MAT4,
} BH_ParamType;

typedef struct BH_UniformBufferReflection
{
    const char *name;
    uint32_t slot;
    uint32_t size_bytes;
} BH_UniformBufferReflection;

typedef struct BH_ParamReflection
{
    const char *name;
    uint32_t name_hash;
    BH_ParamType type;
    uint32_t slot;
    uint32_t offset_bytes;
    uint32_t size_bytes;
} BH_ParamReflection;

typedef struct BH_ShaderStageReflection
{
    BH_ShaderStage entry_stage; /* Renamed slightly internally? No, must preserve API. keeping 'stage' */
    BH_ShaderStage stage;
    const char *entry;

    uint32_t num_samplers;
    uint32_t num_storage_textures;
    uint32_t num_storage_buffers;
    uint32_t num_uniform_buffers;

    BH_UniformBufferReflection uniform_buffers[BH_GPU_MAX_UNIFORM_SLOTS];
    uint32_t uniform_buffer_count;

    BH_ParamReflection params[BH_GPU_MAX_PARAMS];
    uint32_t param_count;
} BH_ShaderStageReflection;

/*
   Parses JSON reflection data into the output structure.
   Allocates strings/arrays from the permanent_arena.
*/
bool BH_ShaderReflection_Load(BH_ShaderStageReflection *out_refl, const char *json_path, BH_Arena *permanent_arena);

/*
   Linear lookup of a parameter by name. Returns NULL if not found.
*/
const BH_ParamReflection *BH_ShaderReflection_FindParam(const BH_ShaderStageReflection *refl, const char *name);

/*
   Linear lookup of a uniform buffer by slot index. Returns NULL if not found.
*/
const BH_UniformBufferReflection *BH_ShaderReflection_FindUniformBuffer(const BH_ShaderStageReflection *refl,
                                                                        uint32_t slot);
