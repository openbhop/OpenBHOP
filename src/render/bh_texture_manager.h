/* -----------------------------------------------------------------------------
   bh_texture_manager.h
   ----------------------------------------------------------------------------- */

#pragma once

#include "../core/bh_arena.h"
#include "../core/bh_core.h"

#include <SDL3/SDL_gpu.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* -----------------------------------------------------------------------------
       Types
       ----------------------------------------------------------------------------- */

    typedef uint32_t BH_TextureHandle;

    typedef enum BH_TextureSemantic
    {
        BH_TEXTURE_SEMANTIC_ALBEDO = 0,
        BH_TEXTURE_SEMANTIC_DATA = 1, /* normal/roughness/metallic/ao */
        BH_TEXTURE_SEMANTIC_HDR_LIGHTMAP = 2
    } BH_TextureSemantic;

    typedef struct BH_TextureManager
    {
        SDL_GPUDevice *device;
        BH_Arena *arena;
        SDL_GPUSampler *sampler_linear_repeat;

        struct BH_TextureSlot
        {
            const char *path; /* arena-owned */
            SDL_GPUTexture *tex;
            uint32_t w;
            uint32_t h;
            uint32_t refcount;
            bool srgb;
            bool owned;
            bool in_use;
        } slots[256];

        /* Default textures (valid after init) */
        BH_TextureHandle default_white;
        BH_TextureHandle default_black;
        BH_TextureHandle default_normal;
    } BH_TextureManager;

    /* -----------------------------------------------------------------------------
       Public API
       ----------------------------------------------------------------------------- */

    bool BH_TextureManager_Init(BH_TextureManager *tm, SDL_GPUDevice *device, BH_Arena *permanent_arena);
    void BH_TextureManager_Shutdown(BH_TextureManager *tm);

    SDL_GPUSampler *BH_TextureManager_GetSampler(const BH_TextureManager *tm);
    SDL_GPUTexture *BH_TextureManager_GetGPUTexture(const BH_TextureManager *tm, BH_TextureHandle handle);

    /* Loads PNG from disk. Returns cached handle if path matches. */
    BH_TextureHandle BH_TextureManager_LoadTexture(BH_TextureManager *tm, const char *asset_root, const char *rel_path,
                                                   BH_TextureSemantic semantic);

    /* Loads PNG from memory buffer (virtual_path used as cache key). */
    BH_TextureHandle BH_TextureManager_LoadTextureFromMemory(BH_TextureManager *tm, const char *virtual_path,
                                                             const void *bytes, size_t byte_len,
                                                             BH_TextureSemantic semantic);

    /* Registers external texture. Optional ownership transfer. */
    BH_TextureHandle BH_TextureManager_RegisterExternalTexture(BH_TextureManager *tm, const char *debug_name,
                                                               SDL_GPUTexture *tex, uint32_t w, uint32_t h, bool srgb,
                                                               bool take_ownership);

    /* Decrements refcount; releases slot/texture at 0. */
    void BH_TextureManager_ReleaseHandle(BH_TextureManager *tm, BH_TextureHandle handle);

    BH_TextureHandle BH_TextureManager_GetDefaultWhite(const BH_TextureManager *tm);
    BH_TextureHandle BH_TextureManager_GetDefaultBlack(const BH_TextureManager *tm);
    BH_TextureHandle BH_TextureManager_GetDefaultNormal(const BH_TextureManager *tm);

#ifdef __cplusplus
}
#endif