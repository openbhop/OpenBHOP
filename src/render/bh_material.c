/* -----------------------------------------------------------------------------
   bh_material.c
   ----------------------------------------------------------------------------- */

#include "bh_material.h"
#include <SDL3/SDL.h>
#include <string.h>

/* -----------------------------------------------------------------------------
    ParamBlock API
    ----------------------------------------------------------------------------- */

bool BH_ParamBlock_Init(BH_ParamBlock *pb, const BH_ShaderStageReflection *refl, BH_Arena *arena)
{
    if (!pb || !refl || !arena)
    {
        return false;
    }

    *pb = (BH_ParamBlock){.refl = refl};

    for (uint32_t i = 0; i < refl->uniform_buffer_count; ++i)
    {
        const BH_UniformBufferReflection *ub = &refl->uniform_buffers[i];

        if (ub->slot >= BH_GPU_MAX_UNIFORM_SLOTS)
        {
            continue;
        }

        void *data = BH_Arena_Alloc(arena, ub->size_bytes, 16);
        if (!data)
        {
            return false;
        }

        memset(data, 0, ub->size_bytes);
        pb->slot_data[ub->slot] = data;
        pb->slot_size[ub->slot] = ub->size_bytes;
    }

    return true;
}

bool BH_ParamBlock_InitSingleSlot(BH_ParamBlock *pb, const BH_ShaderStageReflection *refl, uint32_t slot, void *storage,
                                  uint32_t storage_size)
{
    if (!pb || !refl || !storage || storage_size == 0 || slot >= BH_GPU_MAX_UNIFORM_SLOTS)
    {
        return false;
    }

    memset(storage, 0, storage_size);

    *pb = (BH_ParamBlock){0};
    pb->refl = refl;
    pb->slot_data[slot] = (uint8_t *)storage;
    pb->slot_size[slot] = storage_size;

    return true;
}

bool BH_ParamBlock_SetRaw(BH_ParamBlock *pb, const char *name, const void *data, uint32_t data_size)
{
    if (!pb || !pb->refl || !name || !data)
    {
        return false;
    }

    const BH_ParamReflection *p = BH_ShaderReflection_FindParam(pb->refl, name);
    if (!p || p->slot >= BH_GPU_MAX_UNIFORM_SLOTS)
    {
        return false;
    }

    uint8_t *dst = pb->slot_data[p->slot];
    const uint32_t cap = pb->slot_size[p->slot];

    if (!dst || cap == 0 || (p->offset_bytes + p->size_bytes > cap))
    {
        return false;
    }

    const uint32_t copy_size = (data_size < p->size_bytes) ? data_size : p->size_bytes;
    memcpy(dst + p->offset_bytes, data, copy_size);

    /* Deterministic padding for partial updates */
    if (copy_size < p->size_bytes)
    {
        memset(dst + p->offset_bytes + copy_size, 0, p->size_bytes - copy_size);
    }

    return true;
}

void BH_ParamBlock_PushUniforms(const BH_ParamBlock *pb, SDL_GPUCommandBuffer *cmd)
{
    if (!pb || !pb->refl || !cmd)
    {
        return;
    }

    for (uint32_t slot = 0; slot < BH_GPU_MAX_UNIFORM_SLOTS; ++slot)
    {
        const uint8_t *data = pb->slot_data[slot];
        const uint32_t size = pb->slot_size[slot];

        if (!data || size == 0)
        {
            continue;
        }

        if (pb->refl->stage == BH_SHADERSTAGE_VERTEX)
        {
            SDL_PushGPUVertexUniformData(cmd, slot, data, size);
        }
        else
        {
            SDL_PushGPUFragmentUniformData(cmd, slot, data, size);
        }
    }
}

/* -----------------------------------------------------------------------------
   Material API
   ----------------------------------------------------------------------------- */

bool BH_Material_Init(BH_Material *mat, const char *name, const BH_ShaderStageReflection *fragment_refl,
                      BH_Arena *arena)
{
    if (!mat || !fragment_refl || !arena)
    {
        return false;
    }

    if (fragment_refl->stage != BH_SHADERSTAGE_FRAGMENT)
    {
        SDL_Log("[bh] bh_material_init expects fragment reflection");
        return false;
    }

    *mat = (BH_Material){.name = name};

    /* Materials own slot 0. Scene/per-frame uniforms (slots 1+) are pushed
       separately by the renderer.
    */
    const BH_UniformBufferReflection *ub = BH_ShaderReflection_FindUniformBuffer(fragment_refl, 0);

    if (!ub || ub->size_bytes == 0 || ub->size_bytes > 256)
    {
        SDL_Log("[bh] bh_material_init: missing/invalid material uniform buffer (slot 0)");
        return false;
    }

    void *storage = BH_Arena_Alloc(arena, ub->size_bytes, 16);
    if (!storage)
    {
        return false;
    }

    if (!BH_ParamBlock_InitSingleSlot(&mat->fragment_params, fragment_refl, 0, storage, ub->size_bytes))
    {
        return false;
    }

    memset(mat->textures, 0, sizeof(mat->textures));

    return true;
}

bool BH_Material_SetRaw(BH_Material *mat, const char *name, const void *data, uint32_t data_size)
{
    if (!mat)
    {
        return false;
    }
    return BH_ParamBlock_SetRaw(&mat->fragment_params, name, data, data_size);
}