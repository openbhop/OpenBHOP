/* -----------------------------------------------------------------------------
   bh_cmap_loader.c
   ----------------------------------------------------------------------------- */

#include "bh_cmap_loader.h"

#include "../core/bh_file.h"
#include "../vendor/lz4.h"

#include "../render/bh_material_manager.h"
#include "../render/bh_mesh.h"
#include "../render/bh_renderer.h"

#include "../physics/bh_physics.h"
#include "../scene/bh_node_brushes.h"
#include "../scene/bh_scene.h"

#include "../entity/types/bh_static_geometry_entity.h"
#include "../entity/bh_entity_factory.h"

#include <SDL3/SDL.h>
#include <float.h>
#include <string.h>

#define BH_CMAP_FOURCC(a, b, c, d)                                                                                     \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define BH_CMAP_MAGIC BH_CMAP_FOURCC('C', 'M', 'A', 'P')

#define BH_CMAP_LUMP_MATS BH_CMAP_FOURCC('M', 'A', 'T', 'S')
#define BH_CMAP_LUMP_RMSH BH_CMAP_FOURCC('R', 'M', 'S', 'H')
#define BH_CMAP_LUMP_BRUS BH_CMAP_FOURCC('B', 'R', 'U', 'S')
#define BH_CMAP_LUMP_ENTS BH_CMAP_FOURCC('E', 'N', 'T', 'S')
#define BH_CMAP_LUMP_ASST BH_CMAP_FOURCC('A', 'S', 'S', 'T')

#define BH_CMAP_LUMP_LMAP BH_CMAP_FOURCC('L', 'M', 'A', 'P')
#define BH_CMAP_LUMP_LDIR BH_CMAP_FOURCC('L', 'D', 'I', 'R')
#define BH_CMAP_LUMP_SMSK BH_CMAP_FOURCC('S', 'M', 'S', 'K')

#define BH_CMAP_LUMP_FLAG_COMPRESSED 1u

typedef struct BH_CmapHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t lump_count;
    uint32_t flags;
} BH_CmapHeader;

typedef struct BH_CmapLumpEntry
{
    uint32_t type;
    uint32_t offset;
    uint32_t size;
    uint32_t count;
    uint32_t flags;
    uint32_t unpacked_size;
} BH_CmapLumpEntry;

typedef struct BH_CmapRenderVertex
{
    float position[3];
    float normal[3];
    float uv[2];
    float uv2[2];
} BH_CmapRenderVertex;

typedef struct BH_CmapEntityNodeRef
{
    int32_t uid;
    BH_SceneNode *node;
} BH_CmapEntityNodeRef;

typedef struct BH_CmapBakedLighting
{
    BH_TextureHandle *lmap;
    uint32_t lmap_count;
    BH_TextureHandle *ldir;
    uint32_t ldir_count;
    BH_TextureHandle *smsk;
    uint32_t smsk_count;
} BH_CmapBakedLighting;

typedef struct BH_CmapAsset
{
    const char *id;
    const char *ext;
    uint32_t size;
    const uint8_t *bytes;
} BH_CmapAsset;

_Static_assert(sizeof(BH_CmapHeader) == 16, "BH_CmapHeader size");
_Static_assert(sizeof(BH_CmapLumpEntry) == 24, "BH_CmapLumpEntry size");
_Static_assert(sizeof(BH_CmapRenderVertex) == 40, "BH_CmapRenderVertex size");

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static const BH_CmapLumpEntry *bh_cmap_find_entry(const BH_CmapLumpEntry *entries, uint32_t lump_count, uint32_t type)
{
    if (!entries)
        return NULL;
    for (uint32_t i = 0; i < lump_count; ++i)
    {
        if (entries[i].type == type)
            return &entries[i];
    }
    return NULL;
}

static bool bh_cmap_get_lump_data(const BH_FileData *fd, const BH_CmapLumpEntry *e, const uint8_t **out_data,
                                  uint32_t *out_size, uint8_t **out_owned)
{
    *out_data = NULL;
    *out_size = 0;
    *out_owned = NULL;

    if (!fd->data)
        return false;

    const size_t off = (size_t)e->offset;
    const size_t sz = (size_t)e->size;
    if (off + sz > fd->size)
        return false;

    const uint8_t *src = (const uint8_t *)fd->data + off;

    if (!(e->flags & BH_CMAP_LUMP_FLAG_COMPRESSED))
    {
        *out_data = src;
        *out_size = e->size;
        return true;
    }

    if (e->unpacked_size == 0)
        return false;

    uint8_t *dst = (uint8_t *)SDL_malloc((size_t)e->unpacked_size);
    if (!dst)
        return false;

    const int written = LZ4_decompress_safe((const char *)src, (char *)dst, (int)e->size, (int)e->unpacked_size);
    if (written < 0 || (uint32_t)written != e->unpacked_size)
    {
        SDL_free(dst);
        return false;
    }

    *out_data = dst;
    *out_size = e->unpacked_size;
    *out_owned = dst;
    return true;
}

static inline bool bh_read_u32(const uint8_t **io_p, const uint8_t *end, uint32_t *out)
{
    if (*io_p + 4 > end)
        return false;
    SDL_memcpy(out, *io_p, 4);
    *io_p += 4;
    return true;
}

static inline bool bh_read_i32(const uint8_t **io_p, const uint8_t *end, int32_t *out)
{
    if (*io_p + 4 > end)
        return false;
    SDL_memcpy(out, *io_p, 4);
    *io_p += 4;
    return true;
}

static inline bool bh_read_f32(const uint8_t **io_p, const uint8_t *end, float *out)
{
    if (*io_p + 4 > end)
        return false;
    SDL_memcpy(out, *io_p, 4);
    *io_p += 4;
    return true;
}

static inline bool bh_read_vec3(const uint8_t **io_p, const uint8_t *end, vec3 *out)
{
    if (*io_p + 12 > end)
        return false;
    SDL_memcpy(out, *io_p, 12);
    *io_p += 12;
    return true;
}

static const char *bh_read_string_arena(const uint8_t **io_p, const uint8_t *end, BH_Arena *arena)
{
    uint32_t len = 0;
    if (!bh_read_u32(io_p, end, &len))
        return NULL;
    if (*io_p + len > end)
        return NULL;

    char *s = (char *)BH_Arena_Alloc(arena, (size_t)len + 1u, 1);
    if (!s)
        return NULL;

    if (len > 0)
        SDL_memcpy(s, *io_p, len);
    s[len] = '\0';
    *io_p += len;
    return s;
}

static void bh_skip_bytes(const uint8_t **io_p, const uint8_t *end, size_t n)
{
    if (*io_p + n > end)
    {
        *io_p = end;
    }
    else
    {
        *io_p += n;
    }
}

static bool bh_streq(const char *a, const char *b)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    return SDL_strcmp(a, b) == 0;
}

static bool bh_starts_with(const char *s, const char *prefix)
{
    return SDL_strncmp(s, prefix, SDL_strlen(prefix)) == 0;
}

static bool bh_ends_with(const char *s, const char *suffix)
{
    const size_t sl = SDL_strlen(s);
    const size_t tl = SDL_strlen(suffix);
    if (tl > sl)
        return false;
    return SDL_strcmp(s + (sl - tl), suffix) == 0;
}

static void bh_strip_quotes_inplace(char *s)
{
    const size_t len = SDL_strlen(s);
    if (len >= 2)
    {
        const char a = s[0];
        const char b = s[len - 1];
        if ((a == '"' && b == '"') || (a == '\'' && b == '\''))
        {
            SDL_memmove(s, s + 1, len - 2);
            s[len - 2] = '\0';
        }
    }
}

static void bh_normalize_path_inplace(char *p)
{
    for (char *c = p; *c; ++c)
    {
        if (*c == '\\')
            *c = '/';
    }

    char *s = p;
    while (*s && (unsigned char)*s <= ' ')
        ++s;
    while (s[0] == '.' && s[1] == '/')
        s += 2;
    while (s[0] == '/')
        s++;

    if (s != p)
    {
        SDL_memmove(p, s, SDL_strlen(s) + 1);
    }

    char *w = p;
    for (char *r = p; *r; ++r)
    {
        if (*r == '/' && w > p && w[-1] == '/')
            continue;
        *w++ = *r;
    }
    *w = '\0';
}

static const char *bh_find_path_ext(const char *path)
{
    const char *slash = SDL_strrchr(path, '/');
    const char *dot = SDL_strrchr(path, '.');
    if (!dot || (slash && dot < slash))
        return NULL;
    return dot;
}

/* -----------------------------------------------------------------------------
   Asset & Material Handling
   ----------------------------------------------------------------------------- */

static void bh_load_cmap_baked_pages(const char *cmap_abs_path, const uint8_t *data, uint32_t size, uint32_t page_count,
                                     BH_TextureManager *tm, BH_TextureSemantic semantic, const char *kind_tag,
                                     BH_Arena *arena, BH_TextureHandle **out_handles, uint32_t *out_count)
{
    *out_handles = NULL;
    *out_count = 0;

    BH_TextureHandle *handles =
        (BH_TextureHandle *)BH_Arena_Alloc(arena, (size_t)page_count * sizeof(BH_TextureHandle), 8);
    if (!handles)
        return;
    SDL_memset(handles, 0, (size_t)page_count * sizeof(BH_TextureHandle));

    const uint8_t *p = data;
    const uint8_t *end = data + size;

    for (uint32_t i = 0; i < page_count; ++i)
    {
        uint32_t page_index = 0, w = 0, h = 0, pad = 0, tpm = 0, png_len = 0;

        if (!bh_read_u32(&p, end, &page_index))
            break;
        if (!bh_read_u32(&p, end, &w))
            break;
        if (!bh_read_u32(&p, end, &h))
            break;
        if (!bh_read_u32(&p, end, &pad))
            break;
        if (!bh_read_u32(&p, end, &tpm))
            break;
        if (!bh_read_u32(&p, end, &png_len))
            break;

        if (p + png_len > end)
        {
            p = end;
            break;
        }

        const uint8_t *png_bytes = p;
        p += png_len;

        if (png_len == 0 || page_index >= page_count)
            continue;

        char virtual_path[1024];
        SDL_snprintf(virtual_path, sizeof(virtual_path), "%s::%s_%u.png",
                     (cmap_abs_path && cmap_abs_path[0]) ? cmap_abs_path : "__cmap__",
                     (kind_tag && kind_tag[0]) ? kind_tag : "page", page_index);

        handles[page_index] =
            BH_TextureManager_LoadTextureFromMemory(tm, virtual_path, png_bytes, (size_t)png_len, semantic);
    }

    *out_handles = handles;
    *out_count = page_count;
}

static void bh_load_cmap_material_ids(const uint8_t *data, uint32_t size, uint32_t count, BH_Arena *arena,
                                      const char ***out_ids, uint32_t *out_count)
{
    const char **ids = (const char **)BH_Arena_Alloc(arena, (size_t)count * sizeof(char *), 8);
    SDL_memset(ids, 0, (size_t)count * sizeof(char *));

    const uint8_t *p = data;
    const uint8_t *end = data + size;
    uint32_t written = 0;

    for (uint32_t i = 0; i < count; ++i)
    {
        const char *s = bh_read_string_arena(&p, end, arena);
        if (!s)
            break;
        ids[i] = s;
        written++;
    }

    *out_ids = ids;
    *out_count = written;
}

static void bh_load_cmap_assets(const uint8_t *data, uint32_t size, uint32_t count, BH_Arena *arena,
                                BH_CmapAsset **out_assets, uint32_t *out_count)
{
    BH_CmapAsset *assets = (BH_CmapAsset *)BH_Arena_Alloc(arena, (size_t)count * sizeof(BH_CmapAsset), 8);
    SDL_memset(assets, 0, (size_t)count * sizeof(BH_CmapAsset));

    const uint8_t *p = data;
    const uint8_t *end = data + size;
    uint32_t written = 0;

    for (uint32_t i = 0; i < count; ++i)
    {
        const char *id = bh_read_string_arena(&p, end, arena);
        const char *ext = bh_read_string_arena(&p, end, arena);
        uint32_t blob_size = 0;

        if (!id || !ext || !bh_read_u32(&p, end, &blob_size))
            break;
        if (p + blob_size > end)
            break;

        assets[i] = (BH_CmapAsset){.id = id, .ext = ext, .size = blob_size, .bytes = p};
        p += blob_size;
        written++;
    }

    *out_assets = assets;
    *out_count = written;
}

static const BH_CmapAsset *bh_cmap_find_asset_exact(const BH_CmapAsset *assets, uint32_t count, const char *id,
                                                    const char *ext)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (bh_streq(assets[i].id, id) && bh_streq(assets[i].ext, ext))
            return &assets[i];
    }
    return NULL;
}

static const BH_CmapAsset *bh_cmap_find_asset_by_id_anyext(const BH_CmapAsset *assets, uint32_t count, const char *id,
                                                           const char *preferred_ext)
{
    const BH_CmapAsset *first = NULL;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!bh_streq(assets[i].id, id))
            continue;
        if (!first)
            first = &assets[i];
        if (preferred_ext && bh_streq(assets[i].ext, preferred_ext))
            return &assets[i];
    }
    return first;
}

static const BH_CmapAsset *bh_cmap_find_material_asset(const BH_CmapAsset *assets, uint32_t count,
                                                       const char *material_id)
{
    char id[512];
    SDL_strlcpy(id, material_id, sizeof(id));
    bh_strip_quotes_inplace(id);
    bh_normalize_path_inplace(id);

    if (bh_ends_with(id, ".mat"))
        id[SDL_strlen(id) - 4] = '\0';

    const char *desired_ext = ".mat";
    const BH_CmapAsset *a;

    if ((a = bh_cmap_find_asset_exact(assets, count, id, desired_ext)) != NULL)
        return a;
    if ((a = bh_cmap_find_asset_by_id_anyext(assets, count, id, desired_ext)) != NULL && bh_streq(a->ext, desired_ext))
        return a;

    char with_prefix[768];
    if (!bh_starts_with(id, "materials/"))
    {
        SDL_snprintf(with_prefix, sizeof(with_prefix), "materials/%s", id);
        if ((a = bh_cmap_find_asset_exact(assets, count, with_prefix, desired_ext)) != NULL)
            return a;
        if ((a = bh_cmap_find_asset_by_id_anyext(assets, count, with_prefix, desired_ext)) != NULL &&
            bh_streq(a->ext, desired_ext))
            return a;
    }

    char with_ext[768];
    SDL_snprintf(with_ext, sizeof(with_ext), "%s.mat", id);
    if ((a = bh_cmap_find_asset_exact(assets, count, with_ext, desired_ext)) != NULL)
        return a;
    if ((a = bh_cmap_find_asset_by_id_anyext(assets, count, with_ext, desired_ext)) != NULL &&
        bh_streq(a->ext, desired_ext))
        return a;

    if (!bh_starts_with(id, "materials/"))
    {
        SDL_snprintf(with_ext, sizeof(with_ext), "materials/%s.mat", id);
        if ((a = bh_cmap_find_asset_exact(assets, count, with_ext, desired_ext)) != NULL)
            return a;
        if ((a = bh_cmap_find_asset_by_id_anyext(assets, count, with_ext, desired_ext)) != NULL &&
            bh_streq(a->ext, desired_ext))
            return a;
    }

    return NULL;
}

static const BH_CmapAsset *bh_cmap_find_texture_asset(const BH_CmapAsset *assets, uint32_t count, const char *tex_ref)
{
    char ref[768];
    SDL_strlcpy(ref, tex_ref, sizeof(ref));
    bh_strip_quotes_inplace(ref);
    bh_normalize_path_inplace(ref);

    const char *ext_in_ref = bh_find_path_ext(ref);
    if (ext_in_ref)
    {
        const char *desired_ext = ext_in_ref;
        const BH_CmapAsset *a;

        if ((a = bh_cmap_find_asset_exact(assets, count, ref, desired_ext)) != NULL)
            return a;
        if ((a = bh_cmap_find_asset_by_id_anyext(assets, count, ref, desired_ext)) != NULL &&
            bh_streq(a->ext, desired_ext))
            return a;

        char base[768];
        SDL_strlcpy(base, ref, sizeof(base));
        base[ext_in_ref - ref] = '\0';

        if ((a = bh_cmap_find_asset_exact(assets, count, base, desired_ext)) != NULL)
            return a;
        if ((a = bh_cmap_find_asset_by_id_anyext(assets, count, base, desired_ext)) != NULL &&
            bh_streq(a->ext, desired_ext))
            return a;

        return bh_cmap_find_asset_by_id_anyext(assets, count, ref, NULL);
    }

    const char *preferred = ".png";
    const BH_CmapAsset *a;
    if ((a = bh_cmap_find_asset_exact(assets, count, ref, preferred)) != NULL)
        return a;
    if ((a = bh_cmap_find_asset_by_id_anyext(assets, count, ref, preferred)) != NULL && bh_streq(a->ext, preferred))
        return a;

    return bh_cmap_find_asset_by_id_anyext(assets, count, ref, NULL);
}

static bool bh_cmap_mat_parse_line(const BH_CmapAsset *assets, uint32_t asset_count, BH_TextureManager *tm,
                                   const char *line, BH_TextureHandle *out_tex_albedo, BH_TextureHandle *out_tex_normal,
                                   BH_TextureHandle *out_tex_roughness, BH_TextureHandle *out_tex_metallic,
                                   BH_TextureHandle *out_tex_ao, vec3 *io_tint, float *io_normal_scale,
                                   float *io_metallic_factor, float *io_roughness_factor)
{
    char buf[1024];
    SDL_strlcpy(buf, line, sizeof(buf));

    char *hash = SDL_strchr(buf, '#');
    if (hash)
        *hash = '\0';
    char *slash = SDL_strstr(buf, "//");
    if (slash)
        *slash = '\0';

    char *save = NULL;
    char *t0 = SDL_strtok_r(buf, " \t\r\n", &save);
    if (!t0)
        return true;
    char *t1 = SDL_strtok_r(NULL, " \t\r\n", &save);
    if (!t1)
        return true;

    if (bh_streq(t0, "texture"))
    {
        char *t2 = SDL_strtok_r(NULL, " \t\r\n", &save);
        if (!t2)
            return true;

        bh_strip_quotes_inplace(t2);
        bh_normalize_path_inplace(t2);

        const BH_TextureSemantic sem = bh_streq(t1, "albedo") ? BH_TEXTURE_SEMANTIC_ALBEDO : BH_TEXTURE_SEMANTIC_DATA;
        const BH_CmapAsset *ta = bh_cmap_find_texture_asset(assets, asset_count, t2);

        if (!ta || !ta->bytes)
        {
            SDL_Log("[bh] cmap material: missing texture asset %s", t2);
            return true;
        }

        const BH_TextureHandle th = BH_TextureManager_LoadTextureFromMemory(tm, t2, ta->bytes, (size_t)ta->size, sem);
        if (th == 0)
            return true;

        if (bh_streq(t1, "albedo"))
            *out_tex_albedo = th;
        else if (bh_streq(t1, "normal"))
            *out_tex_normal = th;
        else if (bh_streq(t1, "roughness"))
            *out_tex_roughness = th;
        else if (bh_streq(t1, "metallic"))
            *out_tex_metallic = th;
        else if (bh_streq(t1, "ao"))
            *out_tex_ao = th;

        return true;
    }

    if (bh_streq(t0, "float"))
    {
        char *t2 = SDL_strtok_r(NULL, " \t\r\n", &save);
        if (!t2)
            return true;

        const float v = BH_Parse_Float32(t2, 0.0f);

        if (bh_streq(t1, "normal_value"))
            *io_normal_scale = v;
        else if (bh_streq(t1, "metallic_value"))
            *io_metallic_factor = v;
        else if (bh_streq(t1, "roughness_value"))
            *io_roughness_factor = v;
        else if (bh_streq(t1, "smoothness_value"))
        {
            float s = (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
            *io_roughness_factor = 1.0f - s;
        }
        return true;
    }

    if (bh_streq(t0, "float3"))
    {
        char *rest = SDL_strtok_r(NULL, "\r\n", &save);
        if (rest && bh_streq(t1, "tint"))
            *io_tint = BH_Parse_Vec3(rest, *io_tint);
        return true;
    }

    return true;
}

static const BH_Material *bh_cmap_build_material_from_assets(const BH_CmapAsset *assets, uint32_t asset_count,
                                                             BH_MaterialManager *mm, BH_Arena *arena,
                                                             const char *material_id)
{
    if (!material_id || !material_id[0])
        return BH_MaterialManager_GetFallback(mm);

    const BH_CmapAsset *ma = bh_cmap_find_material_asset(assets, asset_count, material_id);
    if (!ma || !ma->bytes)
    {
        SDL_Log("[bh] cmap: missing material asset %s", material_id);
        return BH_MaterialManager_GetFallback(mm);
    }

    BH_Material *mat = (BH_Material *)BH_Arena_Alloc(arena, sizeof(BH_Material), 8);
    if (!mat || !BH_Material_Init(mat, material_id, mm->fs_refl, arena))
    {
        return BH_MaterialManager_GetFallback(mm);
    }

    BH_TextureHandle tex_albedo = BH_TextureManager_GetDefaultWhite(mm->textures);
    BH_TextureHandle tex_normal = BH_TextureManager_GetDefaultNormal(mm->textures);
    BH_TextureHandle tex_roughness = BH_TextureManager_GetDefaultWhite(mm->textures);
    BH_TextureHandle tex_metallic = BH_TextureManager_GetDefaultBlack(mm->textures);
    BH_TextureHandle tex_ao = BH_TextureManager_GetDefaultWhite(mm->textures);

    vec3 tint = {1.0f, 1.0f, 1.0f};
    float normal_scale = 1.0f;
    float metallic_factor = 0.0f;
    float roughness_factor = 1.0f;

    char *text = (char *)SDL_malloc((size_t)ma->size + 1u);
    if (text)
    {
        SDL_memcpy(text, ma->bytes, (size_t)ma->size);
        text[ma->size] = '\0';

        char *save = NULL;
        char *line = SDL_strtok_r(text, "\n", &save);
        while (line)
        {
            bh_cmap_mat_parse_line(assets, asset_count, mm->textures, line, &tex_albedo, &tex_normal, &tex_roughness,
                                   &tex_metallic, &tex_ao, &tint, &normal_scale, &metallic_factor, &roughness_factor);
            line = SDL_strtok_r(NULL, "\n", &save);
        }
        SDL_free(text);
    }

    mat->textures[BH_MATERIAL_TEX_ALBEDO] = tex_albedo;
    mat->textures[BH_MATERIAL_TEX_NORMAL] = tex_normal;
    mat->textures[BH_MATERIAL_TEX_ROUGHNESS] = tex_roughness;
    mat->textures[BH_MATERIAL_TEX_METALLIC] = tex_metallic;
    mat->textures[BH_MATERIAL_TEX_AO] = tex_ao;

    BH_ParamBlock_SetVec3(&mat->fragment_params, "u_Tint", &tint);
    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_NormalScale", normal_scale);
    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_MetallicFactor", metallic_factor);
    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_RoughnessFactor", roughness_factor);

    return mat;
}

static const BH_Material *bh_cmap_material_with_baked_lighting(const BH_Material *base,
                                                               const BH_CmapBakedLighting *baked,
                                                               int32_t lightmap_index, const BH_EntityServices *sv,
                                                               BH_Arena *arena)
{
    const bool has_any = baked && (baked->lmap_count > 0 || baked->ldir_count > 0 || baked->smsk_count > 0);
    if (!has_any)
        return base;

    BH_Material *mat = (BH_Material *)BH_Arena_Alloc(arena, sizeof(BH_Material), 8);
    if (!mat || !BH_Material_Init(mat, base->name, sv->materials->fs_refl, arena))
        return base;

    const uint32_t sz = mat->fragment_params.slot_size[0];
    if (sz > 0 && base->fragment_params.slot_data[0] && mat->fragment_params.slot_data[0])
    {
        SDL_memcpy(mat->fragment_params.slot_data[0], base->fragment_params.slot_data[0], sz);
    }

    for (uint32_t i = 0; i < BH_MATERIAL_TEX_COUNT; ++i)
    {
        mat->textures[i] = base->textures[i];
    }

    float lm_str = 0.0f, dir_str = 0.0f, sm_str = 0.0f;
    const uint32_t li = (lightmap_index >= 0) ? (uint32_t)lightmap_index : 0u;

    if (baked->lmap && li < baked->lmap_count && baked->lmap[li])
    {
        mat->textures[BH_MATERIAL_TEX_LIGHTMAP] = baked->lmap[li];
        lm_str = 1.0f;
    }
    if (baked->ldir && li < baked->ldir_count && baked->ldir[li])
    {
        mat->textures[BH_MATERIAL_TEX_LIGHTDIR] = baked->ldir[li];
        dir_str = 1.0f;
    }
    if (baked->smsk && li < baked->smsk_count && baked->smsk[li])
    {
        mat->textures[BH_MATERIAL_TEX_SHADOWMASK] = baked->smsk[li];
        sm_str = 1.0f;
    }

    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_LightmapStrength", lm_str);
    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_LightmapDirStrength", dir_str);
    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_ShadowmaskStrength", sm_str);

    return mat;
}

static BH_SceneNode *bh_create_named_child(BH_Scene *scene, BH_SceneNode *parent, BH_Arena *arena, const char *name)
{
    BH_SceneNode *n = BH_Scene_CreateNode(arena, name);
    if (n)
        BH_Scene_AddChild(parent, n);
    return n;
}

static BH_SceneNode *bh_cmap_find_node_by_uid(const BH_CmapEntityNodeRef *refs, uint32_t count, int32_t uid)
{
    if (uid == 0)
        return NULL;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (refs[i].uid == uid)
            return refs[i].node;
    }
    return NULL;
}

/* -----------------------------------------------------------------------------
   Scene Loading
   ----------------------------------------------------------------------------- */

static void bh_load_cmap_render_meshes(const uint8_t *data, uint32_t size, uint32_t group_count,
                                       struct BH_Renderer *renderer, struct BH_Scene *scene,
                                       struct BH_EntityServices *sv, BH_Arena *arena,
                                       const BH_Material *const *materials, uint32_t material_count,
                                       const BH_CmapBakedLighting *baked)
{
    BH_SceneNode *world_root = bh_create_named_child(scene, scene->root, arena, "world");
    BH_StaticGeometryEntity *world_ent = bh_static_geometry_entity_create(arena);
    BH_Mesh *meshes = (BH_Mesh *)BH_Arena_Alloc(arena, (size_t)group_count * sizeof(BH_Mesh), 8);

    if (!world_root || !world_ent || !meshes)
        return;

    SDL_memset(meshes, 0, (size_t)group_count * sizeof(BH_Mesh));
    world_ent->meshes = meshes;
    world_ent->mesh_count = 0;
    BH_Entity_Attach(scene, (BH_Entity *)world_ent, world_root);

    const uint8_t *p = data;
    const uint8_t *end = data + size;

    for (uint32_t gi = 0; gi < group_count; ++gi)
    {
        int32_t material_index = -1, lightmap_index = -1;
        uint32_t vertex_count = 0;

        if (!bh_read_i32(&p, end, &material_index))
            break;
        if (!bh_read_i32(&p, end, &lightmap_index))
            break;
        if (!bh_read_u32(&p, end, &vertex_count))
            break;

        if (vertex_count == 0)
            continue;
        if (p + vertex_count * sizeof(BH_CmapRenderVertex) > end)
            break;

        const BH_CmapRenderVertex *src = (const BH_CmapRenderVertex *)p;
        p += vertex_count * sizeof(BH_CmapRenderVertex);

        BH_Vertex *verts = (BH_Vertex *)SDL_malloc((size_t)vertex_count * sizeof(BH_Vertex));
        if (!verts)
            break;

        for (uint32_t i = 0; i < vertex_count; ++i)
        {
            verts[i] = (BH_Vertex){.position = {src[i].position[0], src[i].position[1], src[i].position[2]},
                                   .normal = {src[i].normal[0], src[i].normal[1], src[i].normal[2]},
                                   .uv = {src[i].uv[0] / 1024.0f, src[i].uv[1] / 1024.0f},
                                   .uv2 = {src[i].uv2[0], src[i].uv2[1]}};
        }

        const BH_Material *base_mat = (material_index >= 0 && (uint32_t)material_index < material_count)
                                          ? materials[material_index]
                                          : BH_MaterialManager_GetFallback(sv->materials);

        const BH_Material *mat = bh_cmap_material_with_baked_lighting(base_mat, baked, lightmap_index, sv, arena);

        BH_Mesh m = {0};
        if (BH_Mesh_CreateTriangleList(&m, renderer->device, arena, verts, vertex_count, mat))
        {
            const uint32_t out_i = world_ent->mesh_count++;
            meshes[out_i] = m;

            char node_name[64];
            SDL_snprintf(node_name, sizeof(node_name), "mesh_%u", out_i);
            BH_SceneNode *n = bh_create_named_child(scene, world_root, arena, node_name);
            if (n)
                n->mesh = &meshes[out_i];
        }
        SDL_free(verts);
    }
}

static void bh_load_cmap_brushes(const uint8_t *data, uint32_t size, uint32_t brush_count,
                                 struct BH_PhysicsWorld *physics, const struct BH_CmapEntityNodeRef *ent_refs,
                                 uint32_t ent_ref_count, BH_Arena *arena)
{
    const uint8_t *p = data;
    const uint8_t *end = data + size;

    for (uint32_t bi = 0; bi < brush_count; ++bi)
    {
        int32_t brush_uid = 0, group_id = 0, entity_uid = 0;
        uint32_t vert_count = 0, face_count = 0;

        if (!bh_read_i32(&p, end, &brush_uid))
            break;
        if (!bh_read_i32(&p, end, &group_id))
            break;
        if (!bh_read_i32(&p, end, &entity_uid))
            break;
        if (!bh_read_u32(&p, end, &vert_count))
            break;

        if (entity_uid == 0)
        {
            /* World brush (physics only) */
            bh_skip_bytes(&p, end, (size_t)vert_count * sizeof(float) * 3u);
            if (!bh_read_u32(&p, end, &face_count))
                break;

            BH_TracePlane planes[BH_PHYS_MAX_SIDES];
            uint32_t plane_count = 0;

            for (uint32_t fi = 0; fi < face_count; ++fi)
            {
                vec3 n = {0};
                float d = 0.0f;
                uint32_t is_bevel = 0, fvert_count = 0;
                int32_t mat_index = 0;

                if (!bh_read_vec3(&p, end, &n))
                {
                    p = end;
                    break;
                }
                if (!bh_read_f32(&p, end, &d))
                {
                    p = end;
                    break;
                }
                if (!bh_read_u32(&p, end, &is_bevel))
                {
                    p = end;
                    break;
                }
                if (!bh_read_i32(&p, end, &mat_index))
                {
                    p = end;
                    break;
                }

                bh_skip_bytes(&p, end, sizeof(float) * 11u); /* axes, shifts, scales, rot */

                if (!bh_read_u32(&p, end, &fvert_count))
                {
                    p = end;
                    break;
                }
                bh_skip_bytes(&p, end, (size_t)fvert_count * 4u);

                if (!is_bevel && plane_count < BH_PHYS_MAX_SIDES)
                {
                    planes[plane_count++] = (BH_TracePlane){.normal = n, .dist = d, .type = 3};
                }
            }

            if (plane_count > 0)
            {
                BH_PhysicsAddBrushConvex(physics, planes, plane_count, BH_CONTENTS_SOLID, brush_uid, true);
            }
            continue;
        }

        /* Brush Entity (full geometry) */
        BH_BrushGeom *bg = (BH_BrushGeom *)BH_Arena_Alloc(arena, sizeof(BH_BrushGeom), 8);
        if (!bg)
            break;

        *bg = (BH_BrushGeom){
            .brush_uid = brush_uid, .group_id = group_id, .entity_uid = entity_uid, .physics_brush_index = -1};
        bg->vert_count = vert_count;
        bg->mins = (vec3){FLT_MAX, FLT_MAX, FLT_MAX};
        bg->maxs = (vec3){-FLT_MAX, -FLT_MAX, -FLT_MAX};

        if (vert_count > 0)
        {
            bg->verts = (vec3 *)BH_Arena_Alloc(arena, (size_t)vert_count * sizeof(vec3), 8);
            for (uint32_t vi = 0; vi < vert_count; ++vi)
            {
                vec3 v;
                if (!bh_read_vec3(&p, end, &v))
                {
                    p = end;
                    break;
                }
                bg->verts[vi] = v;
                bg->mins.x = (v.x < bg->mins.x) ? v.x : bg->mins.x;
                bg->mins.y = (v.y < bg->mins.y) ? v.y : bg->mins.y;
                bg->mins.z = (v.z < bg->mins.z) ? v.z : bg->mins.z;
                bg->maxs.x = (v.x > bg->maxs.x) ? v.x : bg->maxs.x;
                bg->maxs.y = (v.y > bg->maxs.y) ? v.y : bg->maxs.y;
                bg->maxs.z = (v.z > bg->maxs.z) ? v.z : bg->maxs.z;
            }
        }
        else
        {
            bg->mins = (vec3){0};
            bg->maxs = (vec3){0};
        }

        if (!bh_read_u32(&p, end, &face_count))
            break;
        bg->face_count = face_count;

        if (face_count > 0)
        {
            bg->faces = (BH_BrushFaceGeom *)BH_Arena_Alloc(arena, (size_t)face_count * sizeof(BH_BrushFaceGeom), 8);
            bg->planes = (BH_TracePlane *)BH_Arena_Alloc(arena, (size_t)face_count * sizeof(BH_TracePlane), 8);
            SDL_memset(bg->faces, 0, (size_t)face_count * sizeof(BH_BrushFaceGeom));
        }

        for (uint32_t fi = 0; fi < face_count; ++fi)
        {
            vec3 n, u_axis, v_axis;
            float d, u_shift, v_shift, u_scale, v_scale, rot;
            uint32_t is_bevel, fvert_count;
            int32_t mat_index;

            if (!bh_read_vec3(&p, end, &n))
            {
                p = end;
                break;
            }
            if (!bh_read_f32(&p, end, &d))
            {
                p = end;
                break;
            }
            if (!bh_read_u32(&p, end, &is_bevel))
            {
                p = end;
                break;
            }
            if (!bh_read_i32(&p, end, &mat_index))
            {
                p = end;
                break;
            }
            if (!bh_read_vec3(&p, end, &u_axis))
            {
                p = end;
                break;
            }
            if (!bh_read_vec3(&p, end, &v_axis))
            {
                p = end;
                break;
            }
            if (!bh_read_f32(&p, end, &u_shift))
            {
                p = end;
                break;
            }
            if (!bh_read_f32(&p, end, &v_shift))
            {
                p = end;
                break;
            }
            if (!bh_read_f32(&p, end, &u_scale))
            {
                p = end;
                break;
            }
            if (!bh_read_f32(&p, end, &v_scale))
            {
                p = end;
                break;
            }
            if (!bh_read_f32(&p, end, &rot))
            {
                p = end;
                break;
            }
            if (!bh_read_u32(&p, end, &fvert_count))
            {
                p = end;
                break;
            }

            uint32_t *indices =
                (fvert_count > 0) ? (uint32_t *)BH_Arena_Alloc(arena, (size_t)fvert_count * sizeof(uint32_t), 4) : NULL;
            for (uint32_t vi = 0; vi < fvert_count; ++vi)
            {
                if (!bh_read_u32(&p, end, &indices[vi]))
                {
                    p = end;
                    break;
                }
            }

            if (bg->faces)
            {
                bg->faces[fi] = (BH_BrushFaceGeom){.plane = {.normal = n, .dist = d, .type = 3},
                                                   .bevel = (is_bevel != 0u),
                                                   .material_index = mat_index,
                                                   .u_axis = u_axis,
                                                   .v_axis = v_axis,
                                                   .u_shift = u_shift,
                                                   .v_shift = v_shift,
                                                   .u_scale = u_scale,
                                                   .v_scale = v_scale,
                                                   .rotation_deg = rot,
                                                   .vert_count = fvert_count,
                                                   .verts = indices};
            }

            if (!is_bevel && bg->planes)
            {
                bg->planes[bg->plane_count++] = (BH_TracePlane){.normal = n, .dist = d, .type = 3};
            }
        }

        BH_SceneNode *owner_node = bh_cmap_find_node_by_uid(ent_refs, ent_ref_count, entity_uid);
        if (owner_node)
        {
            BH_NodeBrushes_AttachBrush(owner_node, bg, arena);
        }
    }
}

static void bh_load_cmap_entities(const uint8_t *data, uint32_t size, uint32_t ent_count, struct BH_Scene *scene,
                                  struct BH_EntityServices *sv, BH_Arena *arena, BH_CmapEntityNodeRef **out_ent_refs,
                                  uint32_t *out_ent_ref_count, BH_SceneNode **out_player_node)
{
    if (out_ent_refs)
        *out_ent_refs = NULL;
    if (out_ent_ref_count)
        *out_ent_ref_count = 0;

    BH_SceneNode *ents_root = bh_create_named_child(scene, scene->root, arena, "entities");
    if (!ents_root)
        ents_root = scene->root;

    BH_CmapEntityNodeRef *refs = NULL;
    uint32_t ref_count = 0;

    if (out_ent_refs && out_ent_ref_count)
    {
        refs = (BH_CmapEntityNodeRef *)BH_Arena_Alloc(arena, (size_t)ent_count * sizeof(BH_CmapEntityNodeRef), 8);
        SDL_memset(refs, 0, (size_t)ent_count * sizeof(BH_CmapEntityNodeRef));
    }

    const uint8_t *p = data;
    const uint8_t *end = data + size;

    for (uint32_t ei = 0; ei < ent_count; ++ei)
    {
        int32_t unique_id = 0, group_id = 0;
        vec3 pos, extents;
        uint32_t kv_count = 0;

        if (!bh_read_i32(&p, end, &unique_id))
            break;
        if (!bh_read_i32(&p, end, &group_id))
            break;

        const char *name = bh_read_string_arena(&p, end, arena);
        const char *class_name = bh_read_string_arena(&p, end, arena);
        if (!name)
            name = "entity";
        if (!class_name)
            class_name = "";

        if (!bh_read_vec3(&p, end, &pos))
            break;
        if (!bh_read_vec3(&p, end, &extents))
            break;
        if (!bh_read_u32(&p, end, &kv_count))
            break;

        BH_Entity *ent = BH_Entity_CreateByType(class_name, arena);
        BH_EntityKV *kvs = (ent && kv_count > 0)
                               ? (BH_EntityKV *)BH_Arena_Alloc(arena, (size_t)kv_count * sizeof(BH_EntityKV), 8)
                               : NULL;

        for (uint32_t ki = 0; ki < kv_count; ++ki)
        {
            const char *k = bh_read_string_arena(&p, end, arena);
            const char *v = bh_read_string_arena(&p, end, arena);
            if (!k || !v)
            {
                p = end;
                break;
            }
            if (kvs)
            {
                kvs[ki].key = k;
                kvs[ki].value = v;
            }
        }

        BH_SceneNode *node = BH_Scene_CreateNode(arena, name);
        if (!node)
            continue;

        node->map_uid = unique_id;
        node->has_world_bounds = true;
        node->world_mins = (vec3){pos.x - extents.x, pos.y - extents.y, pos.z - extents.z};
        node->world_maxs = (vec3){pos.x + extents.x, pos.y + extents.y, pos.z + extents.z};

        BH_Transform t = BH_Transform_GetIdentity();
        t.position = pos;
        BH_Scene_SetNodeLocalTransform(node, t, t);
        BH_Scene_AddChild(ents_root, node);

        if (ent)
        {
            ent->kvs = kvs;
            ent->kv_count = kv_count;
            BH_Entity_Attach(scene, ent, node);
            if (out_player_node && SDL_strcmp(class_name, "bh_player") == 0)
            {
                *out_player_node = node;
            }
        }

        if (refs)
            refs[ref_count++] = (BH_CmapEntityNodeRef){.uid = unique_id, .node = node};
    }

    if (refs && out_ent_refs)
    {
        *out_ent_refs = refs;
        *out_ent_ref_count = ref_count;
    }
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_Cmap_LoadIntoScene(const char *cmap_abs_path, struct BH_Renderer *renderer, struct BH_Scene *scene,
                           struct BH_PhysicsWorld *physics, struct BH_EntityServices *sv, struct BH_Arena *level_arena,
                           BH_CmapLoadResult *out_result)
{
    if (out_result)
        *out_result = (BH_CmapLoadResult){0};
    if (!cmap_abs_path || !renderer || !scene || !physics || !sv || !level_arena)
        return false;

    BH_FileData fd = {0};
    if (!BH_File_ReadAll(cmap_abs_path, &fd))
    {
        SDL_Log("[bh] failed to read cmap: %s", cmap_abs_path);
        return false;
    }

    if (fd.size < sizeof(BH_CmapHeader))
    {
        SDL_Log("[bh] cmap too small: %s", cmap_abs_path);
        BH_File_Free(&fd);
        return false;
    }

    const BH_CmapHeader *hdr = (const BH_CmapHeader *)fd.data;
    if (hdr->magic != BH_CMAP_MAGIC)
    {
        SDL_Log("[bh] invalid cmap magic: %s", cmap_abs_path);
        BH_File_Free(&fd);
        return false;
    }

    const BH_CmapLumpEntry *entries = (const BH_CmapLumpEntry *)(hdr + 1);
    bool ok = true;

    /* 1. Materials List */
    const char **material_ids = NULL;
    uint32_t material_count = 0;
    const BH_CmapLumpEntry *mats_e = bh_cmap_find_entry(entries, hdr->lump_count, BH_CMAP_LUMP_MATS);
    if (mats_e)
    {
        const uint8_t *d;
        uint32_t sz;
        uint8_t *owned;
        if (bh_cmap_get_lump_data(&fd, mats_e, &d, &sz, &owned))
        {
            bh_load_cmap_material_ids(d, sz, mats_e->count, level_arena, &material_ids, &material_count);
            SDL_free(owned);
        }
        else
            ok = false;
    }

    /* 2. Assets (Materials + Textures) */
    BH_CmapAsset *assets = NULL;
    uint32_t asset_count = 0;
    uint8_t *asst_owned = NULL;
    const BH_CmapLumpEntry *asst_e = bh_cmap_find_entry(entries, hdr->lump_count, BH_CMAP_LUMP_ASST);
    if (asst_e)
    {
        const uint8_t *d;
        uint32_t sz;
        if (bh_cmap_get_lump_data(&fd, asst_e, &d, &sz, &asst_owned))
        {
            bh_load_cmap_assets(d, sz, asst_e->count, level_arena, &assets, &asset_count);
        }
        else
            ok = false;
    }

    /* 3. Build Runtime Materials */
    const BH_Material **materials = NULL;
    if (sv->materials && material_count > 0)
    {
        materials =
            (const BH_Material **)BH_Arena_Alloc(level_arena, (size_t)material_count * sizeof(BH_Material *), 8);
        for (uint32_t i = 0; i < material_count; ++i)
        {
            materials[i] =
                bh_cmap_build_material_from_assets(assets, asset_count, sv->materials, level_arena, material_ids[i]);
        }
    }
    SDL_free(asst_owned);

    /* 4. Baked Lighting */
    BH_CmapBakedLighting baked = {0};
    if (sv->textures)
    {
        const BH_CmapLumpEntry *le;
        const uint8_t *d;
        uint32_t sz;
        uint8_t *owned;

        if ((le = bh_cmap_find_entry(entries, hdr->lump_count, BH_CMAP_LUMP_LMAP)) != NULL &&
            bh_cmap_get_lump_data(&fd, le, &d, &sz, &owned))
        {
            bh_load_cmap_baked_pages(cmap_abs_path, d, sz, le->count, sv->textures, BH_TEXTURE_SEMANTIC_HDR_LIGHTMAP,
                                     "lmap", level_arena, &baked.lmap, &baked.lmap_count);
            SDL_free(owned);
        }
        if ((le = bh_cmap_find_entry(entries, hdr->lump_count, BH_CMAP_LUMP_LDIR)) != NULL &&
            bh_cmap_get_lump_data(&fd, le, &d, &sz, &owned))
        {
            bh_load_cmap_baked_pages(cmap_abs_path, d, sz, le->count, sv->textures, BH_TEXTURE_SEMANTIC_DATA, "ldir",
                                     level_arena, &baked.ldir, &baked.ldir_count);
            SDL_free(owned);
        }
        if ((le = bh_cmap_find_entry(entries, hdr->lump_count, BH_CMAP_LUMP_SMSK)) != NULL &&
            bh_cmap_get_lump_data(&fd, le, &d, &sz, &owned))
        {
            bh_load_cmap_baked_pages(cmap_abs_path, d, sz, le->count, sv->textures, BH_TEXTURE_SEMANTIC_DATA, "smsk",
                                     level_arena, &baked.smsk, &baked.smsk_count);
            SDL_free(owned);
        }
    }

    /* 5. Render Meshes */
    const BH_CmapLumpEntry *rmsh_e = bh_cmap_find_entry(entries, hdr->lump_count, BH_CMAP_LUMP_RMSH);
    if (rmsh_e)
    {
        const uint8_t *d;
        uint32_t sz;
        uint8_t *owned;
        if (bh_cmap_get_lump_data(&fd, rmsh_e, &d, &sz, &owned))
        {
            bh_load_cmap_render_meshes(d, sz, rmsh_e->count, renderer, scene, sv, level_arena, materials,
                                       material_count, &baked);
            SDL_free(owned);
        }
        else
            ok = false;
    }

    /* 6. Entities */
    BH_CmapEntityNodeRef *ent_refs = NULL;
    uint32_t ent_ref_count = 0;
    BH_SceneNode *player_node = NULL;
    const BH_CmapLumpEntry *ents_e = bh_cmap_find_entry(entries, hdr->lump_count, BH_CMAP_LUMP_ENTS);
    if (ents_e)
    {
        const uint8_t *d;
        uint32_t sz;
        uint8_t *owned;
        if (bh_cmap_get_lump_data(&fd, ents_e, &d, &sz, &owned))
        {
            bh_load_cmap_entities(d, sz, ents_e->count, scene, sv, level_arena, &ent_refs, &ent_ref_count,
                                  &player_node);
            SDL_free(owned);
        }
        else
            ok = false;
    }

    if (out_result)
        out_result->player_node = player_node;

    /* 7. Brushes */
    const BH_CmapLumpEntry *brus_e = bh_cmap_find_entry(entries, hdr->lump_count, BH_CMAP_LUMP_BRUS);
    if (brus_e)
    {
        const uint8_t *d;
        uint32_t sz;
        uint8_t *owned;
        if (bh_cmap_get_lump_data(&fd, brus_e, &d, &sz, &owned))
        {
            bh_load_cmap_brushes(d, sz, brus_e->count, physics, ent_refs, ent_ref_count, level_arena);
            SDL_free(owned);
        }
        else
            ok = false;
    }

    BH_File_Free(&fd);
    return ok;
}