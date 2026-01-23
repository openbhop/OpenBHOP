/* -----------------------------------------------------------------------------
   bh_texture_manager.c
   ----------------------------------------------------------------------------- */

#include "bh_texture_manager.h"

#include <SDL3/SDL.h>
#include <string.h>

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static const char *bh_arena_strdup0(BH_Arena *arena, const char *s)
{
    SDL_assert(arena && s);

    const size_t len = SDL_strlen(s);
    char *dst = (char *)BH_Arena_Alloc(arena, len + 1u, 1);

    if (dst)
    {
        SDL_memcpy(dst, s, len);
        dst[len] = '\0';
    }
    return dst;
}

static SDL_GPUTexture *bh_upload_rgba8_texture(SDL_GPUDevice *device, const void *pixels_rgba8, uint32_t w, uint32_t h,
                                               uint32_t pitch_bytes, SDL_GPUTextureFormat fmt)
{
    SDL_assert(device && pixels_rgba8 && w > 0 && h > 0 && pitch_bytes > 0);

    SDL_GPUTextureCreateInfo tci = {.type = SDL_GPU_TEXTURETYPE_2D,
                                    .format = fmt,
                                    .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                    .width = w,
                                    .height = h,
                                    .layer_count_or_depth = 1,
                                    .num_levels = 1,
                                    .sample_count = SDL_GPU_SAMPLECOUNT_1};

    SDL_GPUTexture *tex = SDL_CreateGPUTexture(device, &tci);
    if (!tex)
    {
        SDL_Log("[bh] SDL_CreateGPUTexture failed: %s", SDL_GetError());
        return NULL;
    }

    const uint32_t upload_bytes = pitch_bytes * h;
    SDL_GPUTransferBufferCreateInfo tbci = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = upload_bytes};

    SDL_GPUTransferBuffer *tbuf = SDL_CreateGPUTransferBuffer(device, &tbci);
    if (!tbuf)
    {
        SDL_Log("[bh] SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(device, tex);
        return NULL;
    }

    void *mapped = SDL_MapGPUTransferBuffer(device, tbuf, false);
    if (!mapped)
    {
        SDL_Log("[bh] SDL_MapGPUTransferBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, tbuf);
        SDL_ReleaseGPUTexture(device, tex);
        return NULL;
    }

    SDL_memcpy(mapped, pixels_rgba8, upload_bytes);
    SDL_UnmapGPUTransferBuffer(device, tbuf);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd)
    {
        SDL_Log("[bh] SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, tbuf);
        SDL_ReleaseGPUTexture(device, tex);
        return NULL;
    }

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    if (!copy)
    {
        SDL_Log("[bh] SDL_BeginGPUCopyPass failed: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device, tbuf);
        SDL_ReleaseGPUTexture(device, tex);
        return NULL;
    }

    SDL_GPUTextureTransferInfo src = {
        .transfer_buffer = tbuf, .offset = 0, .pixels_per_row = pitch_bytes / 4u, .rows_per_layer = h};

    SDL_GPUTextureRegion dst = {
        .texture = tex, .mip_level = 0, .layer = 0, .x = 0, .y = 0, .z = 0, .w = w, .h = h, .d = 1};

    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);

    if (!SDL_SubmitGPUCommandBuffer(cmd))
    {
        SDL_Log("[bh] SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, tbuf);
        SDL_ReleaseGPUTexture(device, tex);
        return NULL;
    }

    SDL_ReleaseGPUTransferBuffer(device, tbuf);
    return tex;
}

static BH_TextureHandle bh_texture_manager_alloc_slot(BH_TextureManager *tm)
{
    SDL_assert(tm);
    const uint32_t count = (uint32_t)(sizeof(tm->slots) / sizeof(tm->slots[0]));

    for (uint32_t i = 0; i < count; ++i)
    {
        if (!tm->slots[i].in_use)
        {
            tm->slots[i].in_use = true;
            tm->slots[i].refcount = 1;
            tm->slots[i].owned = true;
            return i + 1u;
        }
    }
    return 0;
}

static SDL_GPUTextureFormat bh_tex_format_for_semantic(BH_TextureSemantic semantic)
{
    if (semantic == BH_TEXTURE_SEMANTIC_ALBEDO)
    {
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    }
    /* HDR_LIGHTMAP should be UNORM (Linear), NOT sRGB */
    if (semantic == BH_TEXTURE_SEMANTIC_HDR_LIGHTMAP)
    {
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

static BH_TextureHandle bh_create_solid_rgba8(BH_TextureManager *tm, const char *debug_name, uint8_t rgba[4], bool srgb)
{
    SDL_assert(tm);

    const SDL_GPUTextureFormat fmt =
        srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUTexture *tex = bh_upload_rgba8_texture(tm->device, rgba, 1, 1, 4, fmt);

    if (!tex)
    {
        return 0;
    }

    BH_TextureHandle h = bh_texture_manager_alloc_slot(tm);
    if (h == 0)
    {
        SDL_ReleaseGPUTexture(tm->device, tex);
        return 0;
    }

    struct BH_TextureSlot *s = &tm->slots[h - 1u];
    s->path = debug_name;
    s->tex = tex;
    s->w = 1;
    s->h = 1;
    s->srgb = srgb;
    s->owned = true;
    return h;
}

static bool bh_slot_path_eq(const char *a, const char *b)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    return SDL_strcmp(a, b) == 0;
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_TextureManager_Init(BH_TextureManager *tm, SDL_GPUDevice *device, BH_Arena *permanent_arena)
{
    if (!tm || !device || !permanent_arena)
    {
        return false;
    }

    *tm = (BH_TextureManager){.device = device, .arena = permanent_arena};

    SDL_GPUSamplerCreateInfo sci = {.min_filter = SDL_GPU_FILTER_LINEAR,
                                    .mag_filter = SDL_GPU_FILTER_LINEAR,
                                    .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
                                    .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                    .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                    .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                                    .min_lod = 0.0f,
                                    .max_lod = 0.0f,
                                    .mip_lod_bias = 0.0f,
                                    .enable_anisotropy = false,
                                    .max_anisotropy = 1.0f,
                                    .compare_op = SDL_GPU_COMPAREOP_INVALID};

    tm->sampler_linear_repeat = SDL_CreateGPUSampler(device, &sci);
    if (!tm->sampler_linear_repeat)
    {
        SDL_Log("[bh] SDL_CreateGPUSampler failed: %s", SDL_GetError());
        BH_TextureManager_Shutdown(tm);
        return false;
    }

    /* Create built-in default textures */
    uint8_t white[4] = {255, 255, 255, 255};
    uint8_t black[4] = {0, 0, 0, 255};
    uint8_t normal[4] = {128, 128, 255, 255};

    tm->default_white = bh_create_solid_rgba8(tm, "__default_white", white, true);
    tm->default_black = bh_create_solid_rgba8(tm, "__default_black", black, false);
    tm->default_normal = bh_create_solid_rgba8(tm, "__default_normal", normal, false);

    if (tm->default_white == 0 || tm->default_black == 0 || tm->default_normal == 0)
    {
        SDL_Log("[bh] Failed to create default textures");
        BH_TextureManager_Shutdown(tm);
        return false;
    }

    return true;
}

void BH_TextureManager_Shutdown(BH_TextureManager *tm)
{
    if (!tm)
    {
        return;
    }

    if (tm->device)
    {
        if (tm->sampler_linear_repeat)
        {
            SDL_ReleaseGPUSampler(tm->device, tm->sampler_linear_repeat);
        }

        const uint32_t count = (uint32_t)(sizeof(tm->slots) / sizeof(tm->slots[0]));
        for (uint32_t i = 0; i < count; ++i)
        {
            struct BH_TextureSlot *s = &tm->slots[i];
            if (s->in_use && s->tex && s->owned)
            {
                SDL_ReleaseGPUTexture(tm->device, s->tex);
            }
        }
    }

    *tm = (BH_TextureManager){0};
}

SDL_GPUSampler *BH_TextureManager_GetSampler(const BH_TextureManager *tm)
{
    return tm ? tm->sampler_linear_repeat : NULL;
}

BH_TextureHandle BH_TextureManager_LoadTexture(BH_TextureManager *tm, const char *asset_root, const char *rel_path,
                                               BH_TextureSemantic semantic)
{
    if (!tm || !tm->device || !asset_root || !rel_path || !rel_path[0])
    {
        return 0;
    }

    const uint32_t slot_cap = (uint32_t)(sizeof(tm->slots) / sizeof(tm->slots[0]));

    /* Check cache */
    for (uint32_t i = 0; i < slot_cap; ++i)
    {
        struct BH_TextureSlot *s = &tm->slots[i];
        if (s->in_use && bh_slot_path_eq(s->path, rel_path))
        {
            s->refcount++;
            return i + 1u;
        }
    }

    char full_path[1024];
    SDL_snprintf(full_path, sizeof(full_path), "%s/%s", asset_root, rel_path);

    SDL_Surface *surf = SDL_LoadPNG(full_path);
    if (!surf)
    {
        SDL_Log("[bh] SDL_LoadPNG failed for %s: %s", full_path, SDL_GetError());
        return 0;
    }

    SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(surf);

    if (!rgba)
    {
        SDL_Log("[bh] SDL_ConvertSurface failed for %s: %s", full_path, SDL_GetError());
        return 0;
    }

    const bool srgb = (semantic == BH_TEXTURE_SEMANTIC_ALBEDO);
    SDL_GPUTexture *tex = bh_upload_rgba8_texture(tm->device, rgba->pixels, (uint32_t)rgba->w, (uint32_t)rgba->h,
                                                  (uint32_t)rgba->pitch, bh_tex_format_for_semantic(semantic));

    BH_TextureHandle handle = 0;
    if (tex)
    {
        handle = bh_texture_manager_alloc_slot(tm);
        if (handle)
        {
            struct BH_TextureSlot *s = &tm->slots[handle - 1u];
            s->path = bh_arena_strdup0(tm->arena, rel_path);
            s->tex = tex;
            s->w = (uint32_t)rgba->w;
            s->h = (uint32_t)rgba->h;
            s->srgb = srgb;
            s->owned = true;
        }
        else
        {
            SDL_Log("[bh] Texture slots full (cap=%u)", slot_cap);
            SDL_ReleaseGPUTexture(tm->device, tex);
        }
    }

    SDL_DestroySurface(rgba);
    return handle;
}

BH_TextureHandle BH_TextureManager_LoadTextureFromMemory(BH_TextureManager *tm, const char *virtual_path,
                                                         const void *bytes, size_t byte_len,
                                                         BH_TextureSemantic semantic)
{
    if (!tm || !tm->device || !tm->arena || !virtual_path || !virtual_path[0] || !bytes || byte_len == 0)
    {
        return 0;
    }

    const uint32_t slot_cap = (uint32_t)(sizeof(tm->slots) / sizeof(tm->slots[0]));

    /* Check cache */
    for (uint32_t i = 0; i < slot_cap; ++i)
    {
        struct BH_TextureSlot *s = &tm->slots[i];
        if (s->in_use && bh_slot_path_eq(s->path, virtual_path))
        {
            s->refcount++;
            return i + 1u;
        }
    }

    SDL_IOStream *io = SDL_IOFromConstMem(bytes, byte_len);
    if (!io)
    {
        SDL_Log("[bh] SDL_IOFromConstMem failed: %s", SDL_GetError());
        return 0;
    }

    SDL_Surface *surf = SDL_LoadPNG_IO(io, true);
    if (!surf)
    {
        SDL_Log("[bh] SDL_LoadPNG_IO failed: %s", SDL_GetError());
        return 0;
    }

    SDL_Surface *rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(surf);

    if (!rgba)
    {
        SDL_Log("[bh] SDL_ConvertSurface failed: %s", SDL_GetError());
        return 0;
    }

    const bool srgb = (semantic == BH_TEXTURE_SEMANTIC_ALBEDO);
    SDL_GPUTexture *tex = bh_upload_rgba8_texture(tm->device, rgba->pixels, (uint32_t)rgba->w, (uint32_t)rgba->h,
                                                  (uint32_t)rgba->pitch, bh_tex_format_for_semantic(semantic));

    BH_TextureHandle handle = 0;
    if (tex)
    {
        handle = bh_texture_manager_alloc_slot(tm);
        if (handle)
        {
            struct BH_TextureSlot *s = &tm->slots[handle - 1u];
            s->path = bh_arena_strdup0(tm->arena, virtual_path);
            s->tex = tex;
            s->w = (uint32_t)rgba->w;
            s->h = (uint32_t)rgba->h;
            s->srgb = srgb;
            s->owned = true;
        }
        else
        {
            SDL_Log("[bh] Texture slots full (cap=%u)", slot_cap);
            SDL_ReleaseGPUTexture(tm->device, tex);
        }
    }

    SDL_DestroySurface(rgba);
    return handle;
}

BH_TextureHandle BH_TextureManager_RegisterExternalTexture(BH_TextureManager *tm, const char *debug_name,
                                                           SDL_GPUTexture *tex, uint32_t w, uint32_t h, bool srgb,
                                                           bool take_ownership)
{
    if (!tm || !tm->device || !tm->arena || !debug_name || !debug_name[0] || !tex || w == 0 || h == 0)
    {
        return 0;
    }

    const uint32_t slot_cap = (uint32_t)(sizeof(tm->slots) / sizeof(tm->slots[0]));

    for (uint32_t i = 0; i < slot_cap; ++i)
    {
        struct BH_TextureSlot *s = &tm->slots[i];
        if (s->in_use && bh_slot_path_eq(s->path, debug_name))
        {
            s->refcount++;
            return i + 1u;
        }
    }

    BH_TextureHandle handle = bh_texture_manager_alloc_slot(tm);
    if (handle == 0)
    {
        SDL_Log("[bh] Texture slots full (cap=%u)", slot_cap);
        return 0;
    }

    struct BH_TextureSlot *s = &tm->slots[handle - 1u];
    s->path = bh_arena_strdup0(tm->arena, debug_name);
    s->tex = tex;
    s->w = w;
    s->h = h;
    s->srgb = srgb;
    s->owned = take_ownership;

    return handle;
}

void BH_TextureManager_ReleaseHandle(BH_TextureManager *tm, BH_TextureHandle handle)
{
    if (!tm || !tm->device || handle == 0)
    {
        return;
    }

    /* Never release built-in defaults */
    if (handle == tm->default_white || handle == tm->default_black || handle == tm->default_normal)
    {
        return;
    }

    const uint32_t idx = handle - 1u;
    if (idx >= (uint32_t)(sizeof(tm->slots) / sizeof(tm->slots[0])))
    {
        return;
    }

    struct BH_TextureSlot *s = &tm->slots[idx];
    if (!s->in_use)
    {
        return;
    }

    if (s->refcount > 0)
    {
        s->refcount--;
    }

    if (s->refcount > 0)
    {
        return;
    }

    if (s->tex && s->owned)
    {
        SDL_ReleaseGPUTexture(tm->device, s->tex);
    }

    /* Clear slot (path memory remains in arena) */
    *s = (struct BH_TextureSlot){0};
}

SDL_GPUTexture *BH_TextureManager_GetGPUTexture(const BH_TextureManager *tm, BH_TextureHandle handle)
{
    if (!tm || !tm->device || handle == 0)
    {
        return NULL;
    }
    const uint32_t idx = handle - 1u;
    if (idx >= (uint32_t)(sizeof(tm->slots) / sizeof(tm->slots[0])))
    {
        return NULL;
    }
    const struct BH_TextureSlot *s = &tm->slots[idx];
    return (s->in_use) ? s->tex : NULL;
}

BH_TextureHandle BH_TextureManager_GetDefaultWhite(const BH_TextureManager *tm)
{
    return tm ? tm->default_white : 0;
}

BH_TextureHandle BH_TextureManager_GetDefaultBlack(const BH_TextureManager *tm)
{
    return tm ? tm->default_black : 0;
}

BH_TextureHandle BH_TextureManager_GetDefaultNormal(const BH_TextureManager *tm)
{
    return tm ? tm->default_normal : 0;
}