/* -----------------------------------------------------------------------------
   bh_material.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../math/bh_math.h"
#include "bh_shader_reflection.h"
#include "bh_gpu.h"

/* -----------------------------------------------------------------------------
    Texture Bindings
    ----------------------------------------------------------------------------- */

/* Shader resource slots (matches basic.frag.hlsl t0-t7) */
#define BH_MATERIAL_TEX_ALBEDO 0
#define BH_MATERIAL_TEX_NORMAL 1
#define BH_MATERIAL_TEX_ROUGHNESS 2
#define BH_MATERIAL_TEX_METALLIC 3
#define BH_MATERIAL_TEX_AO 4

/* Baked lighting resources */
#define BH_MATERIAL_TEX_LIGHTMAP 5
#define BH_MATERIAL_TEX_LIGHTDIR 6
#define BH_MATERIAL_TEX_SHADOWMASK 7

#define BH_MATERIAL_TEX_COUNT 8

/* -----------------------------------------------------------------------------
   Parameter Block
   ----------------------------------------------------------------------------- */

/* Uniform buffer storage and reflection-based accessors */
typedef struct BH_ParamBlock
{
    const BH_ShaderStageReflection *refl;
    uint8_t *slot_data[BH_GPU_MAX_UNIFORM_SLOTS];
    uint32_t slot_size[BH_GPU_MAX_UNIFORM_SLOTS];
} BH_ParamBlock;

bool BH_ParamBlock_Init(BH_ParamBlock *pb, const BH_ShaderStageReflection *refl, BH_Arena *arena);
bool BH_ParamBlock_InitSingleSlot(BH_ParamBlock *pb, const BH_ShaderStageReflection *refl, uint32_t slot, void *storage,
                                  uint32_t storage_size);
bool BH_ParamBlock_SetRaw(BH_ParamBlock *pb, const char *name, const void *data, uint32_t data_size);
void BH_ParamBlock_PushUniforms(const BH_ParamBlock *pb, BH_GPUCommandBuffer *cmd);

static BH_FORCEINLINE bool BH_ParamBlock_SetMat4(BH_ParamBlock *pb, const char *name, const mat4 *m)
{
    return BH_ParamBlock_SetRaw(pb, name, m, (uint32_t)sizeof(*m));
}

static BH_FORCEINLINE bool BH_ParamBlock_SetVec4(BH_ParamBlock *pb, const char *name, const vec4 *v)
{
    return BH_ParamBlock_SetRaw(pb, name, v, (uint32_t)sizeof(*v));
}

static BH_FORCEINLINE bool BH_ParamBlock_SetVec3(BH_ParamBlock *pb, const char *name, const vec3 *v)
{
    return BH_ParamBlock_SetRaw(pb, name, v, (uint32_t)sizeof(*v));
}

static BH_FORCEINLINE bool BH_ParamBlock_SetFloat32(BH_ParamBlock *pb, const char *name, float v)
{
    return BH_ParamBlock_SetRaw(pb, name, &v, (uint32_t)sizeof(v));
}

/* -----------------------------------------------------------------------------
   Material System
   ----------------------------------------------------------------------------- */

typedef enum BH_MaterialFlags
{
    BH_MATERIAL_FLAG_NONE = 0,
    BH_MATERIAL_FLAG_TRANSPARENT = 1u << 0,
} BH_MaterialFlags;

typedef struct BH_Material
{
    const char *name;
    uint32_t flags; /* Bitmask of BH_MaterialFlags */
    BH_ParamBlock fragment_params;
    uint32_t textures[BH_MATERIAL_TEX_COUNT];
} BH_Material;

bool BH_Material_Init(BH_Material *mat, const char *name, const BH_ShaderStageReflection *fragment_refl,
                      BH_Arena *arena);
bool BH_Material_SetRaw(BH_Material *mat, const char *name, const void *data, uint32_t data_size);

static BH_FORCEINLINE bool BH_Material_SetVec4(BH_Material *mat, const char *name, vec4 v)
{
    return BH_Material_SetRaw(mat, name, &v, (uint32_t)sizeof(v));
}