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

static BH_GPUTexture *bh_upload_rgba8_texture(BH_GPUDevice *device, const void *pixels_rgba8, uint32_t w, uint32_t h,
                                               uint32_t pitch_bytes, BH_GPUTextureFormat fmt)
{
    SDL_assert(device && pixels_rgba8 && w > 0 && h > 0 && pitch_bytes > 0);

    BH_GPUTextureCreateInfo tci = {.type = BH_GPU_TEXTURETYPE_2D,
                                   .format = fmt,
                                   .usage = BH_GPU_TEXTUREUSAGE_SAMPLER,
                                   .width = w,
                                   .height = h,
                                   .layer_count_or_depth = 1,
                                   .num_levels = 1,
                                   .sample_count = BH_GPU_SAMPLECOUNT_1};

    BH_GPUTexture *tex = BH_GPU_CreateTexture(device, &tci);
    if (!tex)
    {
        SDL_Log("[bh] BH_GPU_CreateTexture failed: %s", BH_GPU_GetLastError());
        return NULL;
    }

    const uint32_t upload_bytes = pitch_bytes * h;

    BH_GPUTransferBufferCreateInfo tbci = {.usage = BH_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = upload_bytes};

    BH_GPUTransferBuffer *tbuf = BH_GPU_CreateTransferBuffer(device, &tbci);
    if (!tbuf)
    {
        SDL_Log("[bh] BH_GPU_CreateTransferBuffer failed: %s", BH_GPU_GetLastError());
        BH_GPU_ReleaseTexture(device, tex);
        return NULL;
    }

    void *mapped = BH_GPU_MapTransferBuffer(device, tbuf, false);
    if (!mapped)
    {
        SDL_Log("[bh] BH_GPU_MapTransferBuffer failed: %s", BH_GPU_GetLastError());
        BH_GPU_ReleaseTransferBuffer(device, tbuf);
        BH_GPU_ReleaseTexture(device, tex);
        return NULL;
    }

    SDL_memcpy(mapped, pixels_rgba8, upload_bytes);
    BH_GPU_UnmapTransferBuffer(device, tbuf);

    BH_GPUCommandBuffer *cmd = BH_GPU_AcquireCommandBuffer(device);
    if (!cmd)
    {
        SDL_Log("[bh] BH_GPU_AcquireCommandBuffer failed: %s", BH_GPU_GetLastError());
        BH_GPU_ReleaseTransferBuffer(device, tbuf);
        BH_GPU_ReleaseTexture(device, tex);
        return NULL;
    }

    BH_GPUCopyPass *copy = BH_GPU_BeginCopyPass(cmd);
    if (!copy)
    {
        SDL_Log("[bh] BH_GPU_BeginCopyPass failed: %s", BH_GPU_GetLastError());
        BH_GPU_CancelCommandBuffer(cmd);
        BH_GPU_ReleaseTransferBuffer(device, tbuf);
        BH_GPU_ReleaseTexture(device, tex);
        return NULL;
    }

    BH_GPUTextureTransferInfo src = {
        .transfer_buffer = tbuf, .offset = 0, .pixels_per_row = pitch_bytes / 4u, .rows_per_layer = h};

    BH_GPUTextureRegion dst = {
        .texture = tex, .mip_level = 0, .layer = 0, .x = 0, .y = 0, .z = 0, .w = w, .h = h, .d = 1};

    BH_GPU_UploadToTexture(copy, &src, &dst, false);
    BH_GPU_EndCopyPass(copy);

    if (!BH_GPU_SubmitCommandBuffer(cmd))
    {
        SDL_Log("[bh] bh_texture_manager.c BH_GPU_SubmitCommandBuffer failed: %s", BH_GPU_GetLastError());
        BH_GPU_ReleaseTransferBuffer(device, tbuf);
        BH_GPU_ReleaseTexture(device, tex);
        return NULL;
    }

    BH_GPU_ReleaseTransferBuffer(device, tbuf);
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

static BH_GPUTextureFormat bh_tex_format_for_semantic(BH_TextureSemantic semantic)
{
    if (semantic == BH_TEXTURE_SEMANTIC_ALBEDO)
    {
        return BH_GPU_GetTextureFormat_R8G8B8A8_UNORM_SRGB();
    }
    /* HDR_LIGHTMAP should be UNORM (Linear), NOT sRGB */
    if (semantic == BH_TEXTURE_SEMANTIC_HDR_LIGHTMAP)
    {
        return BH_GPU_GetTextureFormat_R8G8B8A8_UNORM();
    }
    return BH_GPU_GetTextureFormat_R8G8B8A8_UNORM();
}

static BH_TextureHandle bh_create_solid_rgba8(BH_TextureManager *tm, const char *debug_name, uint8_t rgba[4], bool srgb)
{
    SDL_assert(tm);

    const BH_GPUTextureFormat fmt =
        srgb ? BH_GPU_GetTextureFormat_R8G8B8A8_UNORM_SRGB() : BH_GPU_GetTextureFormat_R8G8B8A8_UNORM();
    BH_GPUTexture *tex = bh_upload_rgba8_texture(tm->device, rgba, 1, 1, 4, fmt);

    if (!tex)
    {
        return 0;
    }

    BH_TextureHandle h = bh_texture_manager_alloc_slot(tm);
    if (h == 0)
    {
        BH_GPU_ReleaseTexture(tm->device, tex);
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

bool BH_TextureManager_Init(BH_TextureManager *tm, BH_GPUDevice *device, BH_Arena *permanent_arena)
{
    if (!tm || !device || !permanent_arena)
    {
        return false;
    }

    *tm = (BH_TextureManager){.device = device, .arena = permanent_arena};

    BH_GPUSamplerCreateInfo sci = {.min_filter = BH_GPU_FILTER_LINEAR,
                                   .mag_filter = BH_GPU_FILTER_LINEAR,
                                   .mipmap_mode = BH_GPU_SAMPLERMIPMAPMODE_NEAREST,
                                   .address_mode_u = BH_GPU_SAMPLERADDRESSMODE_REPEAT,
                                   .address_mode_v = BH_GPU_SAMPLERADDRESSMODE_REPEAT,
                                   .address_mode_w = BH_GPU_SAMPLERADDRESSMODE_REPEAT,
                                   .min_lod = 0.0f,
                                   .max_lod = 0.0f,
                                   .mip_lod_bias = 0.0f,
                                   .enable_anisotropy = false,
                                   .max_anisotropy = 1.0f,
                                   .compare_op = BH_GPU_COMPAREOP_INVALID};

    tm->sampler_linear_repeat = BH_GPU_CreateSampler(device, &sci);
    if (!tm->sampler_linear_repeat)
    {
        SDL_Log("[bh] BH_GPU_CreateSampler failed: %s", BH_GPU_GetLastError());
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
            BH_GPU_ReleaseSampler(tm->device, tm->sampler_linear_repeat);
        }

        const uint32_t count = (uint32_t)(sizeof(tm->slots) / sizeof(tm->slots[0]));
        for (uint32_t i = 0; i < count; ++i)
        {
            struct BH_TextureSlot *s = &tm->slots[i];
            if (s->in_use && s->tex && s->owned)
            {
                BH_GPU_ReleaseTexture(tm->device, s->tex);
            }
        }
    }

    *tm = (BH_TextureManager){0};
}

BH_GPUSampler *BH_TextureManager_GetSampler(const BH_TextureManager *tm)
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
    BH_GPUTexture *tex = bh_upload_rgba8_texture(tm->device, rgba->pixels, (uint32_t)rgba->w, (uint32_t)rgba->h,
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
            BH_GPU_ReleaseTexture(tm->device, tex);
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
    BH_GPUTexture *tex = bh_upload_rgba8_texture(tm->device, rgba->pixels, (uint32_t)rgba->w, (uint32_t)rgba->h,
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
            BH_GPU_ReleaseTexture(tm->device, tex);
        }
    }

    SDL_DestroySurface(rgba);
    return handle;
}

BH_TextureHandle BH_TextureManager_RegisterExternalTexture(BH_TextureManager *tm, const char *debug_name,
                                                           BH_GPUTexture *tex, uint32_t w, uint32_t h, bool srgb,
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
        BH_GPU_ReleaseTexture(tm->device, s->tex);
    }

    /* Clear slot (path memory remains in arena) */
    *s = (struct BH_TextureSlot){0};
}

BH_GPUTexture *BH_TextureManager_GetGPUTexture(const BH_TextureManager *tm, BH_TextureHandle handle)
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