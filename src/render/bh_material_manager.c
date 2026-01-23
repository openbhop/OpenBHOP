/* -----------------------------------------------------------------------------
   bh_material_manager.c
   ----------------------------------------------------------------------------- */

#include "bh_material_manager.h"

#include "../core/bh_file.h"
#include "../core/bh_parse.h"

#include <SDL3/SDL.h>
#include <string.h>

/* -----------------------------------------------------------------------------
    Internal Helpers
    ----------------------------------------------------------------------------- */

static const char *bh_arena_strdup0(BH_Arena *arena, const char *s)
{
    const size_t len = SDL_strlen(s);
    char *dst = (char *)BH_Arena_Alloc(arena, len + 1u, 1);
    if (dst)
    {
        SDL_memcpy(dst, s, len);
        dst[len] = '\0';
    }
    return dst;
}

static bool bh_streq(const char *a, const char *b)
{
    if (a == b)
        return true;
    return SDL_strcmp(a, b) == 0;
}

static bool bh_ends_with(const char *s, const char *suffix)
{
    const size_t sl = SDL_strlen(s);
    const size_t tl = SDL_strlen(suffix);
    if (tl > sl)
    {
        return false;
    }
    return SDL_strcmp(s + (sl - tl), suffix) == 0;
}

static void bh_normalize_path_inplace(char *p)
{
    for (char *c = p; *c; ++c)
    {
        if (*c == '\\')
        {
            *c = '/';
        }
    }

    char *s = p;
    while (*s && (unsigned char)*s <= ' ')
    {
        ++s;
    }

    while (s[0] == '.' && s[1] == '/')
    {
        s += 2;
    }

    while (s[0] == '/')
    {
        s++;
    }

    if (s != p)
    {
        SDL_memmove(p, s, SDL_strlen(s) + 1);
    }

    char *w = p;
    for (char *r = p; *r; ++r)
    {
        if (*r == '/' && w > p && w[-1] == '/')
        {
            continue;
        }
        *w++ = *r;
    }
    *w = '\0';
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

static bool bh_mat_parse_line(BH_MaterialManager *mm, BH_Material *mat, const char *line,
                              BH_TextureHandle *out_tex_albedo, BH_TextureHandle *out_tex_normal,
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

        /* Allow quotes and Windows-style backslashes in .mat files. */
        bh_strip_quotes_inplace(t2);
        bh_normalize_path_inplace(t2);

        const BH_TextureSemantic sem = bh_streq(t1, "albedo") ? BH_TEXTURE_SEMANTIC_ALBEDO : BH_TEXTURE_SEMANTIC_DATA;
        const BH_TextureHandle th = BH_TextureManager_LoadTexture(mm->textures, mm->asset_root, t2, sem);

        if (th == 0)
        {
            SDL_Log("[bh] material: failed to load texture %s", t2);
            return true;
        }

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
        {
            *io_normal_scale = v;
        }
        else if (bh_streq(t1, "metallic_value"))
        {
            *io_metallic_factor = v;
        }
        else if (bh_streq(t1, "roughness_value"))
        {
            *io_roughness_factor = v;
        }
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
        if (!rest)
            return true;

        if (bh_streq(t1, "tint") && io_tint)
        {
            *io_tint = BH_Parse_Vec3(rest, *io_tint);
        }
        return true;
    }

    if (bh_streq(t0, "string"))
    {
        char *t2 = SDL_strtok_r(NULL, " \t\r\n", &save);
        if (!t2)
            return true;

        bh_strip_quotes_inplace(t2);

        if (bh_streq(t1, "transparent"))
        {
            const bool on = bh_streq(t2, "1") || bh_streq(t2, "true") || bh_streq(t2, "True") || bh_streq(t2, "yes") ||
                            bh_streq(t2, "Yes") || bh_streq(t2, "alpha") || bh_streq(t2, "blend");

            if (on)
                mat->flags |= BH_MATERIAL_FLAG_TRANSPARENT;
            else
                mat->flags &= ~BH_MATERIAL_FLAG_TRANSPARENT;
        }
        return true;
    }

    return true;
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_MaterialManager_Init(BH_MaterialManager *mm, const char *asset_root, BH_TextureManager *textures,
                             const BH_ShaderStageReflection *fragment_refl, BH_Arena *permanent_arena)
{
    if (!mm || !asset_root || !textures || !fragment_refl || !permanent_arena)
    {
        return false;
    }

    *mm = (BH_MaterialManager){
        .asset_root = asset_root, .textures = textures, .fs_refl = fragment_refl, .arena = permanent_arena};
    return true;
}

void BH_MaterialManager_Shutdown(BH_MaterialManager *mm)
{
    if (!mm)
        return;
    *mm = (BH_MaterialManager){0};
}

const BH_Material *BH_MaterialManager_Load(BH_MaterialManager *mm, const char *mat_rel_path)
{
    if (!mm || !mm->asset_root || !mm->textures || !mm->fs_refl || !mm->arena || !mat_rel_path || !mat_rel_path[0])
    {
        return NULL;
    }

    const uint32_t cap = (uint32_t)(sizeof(mm->entries) / sizeof(mm->entries[0]));

    for (uint32_t i = 0; i < cap; ++i)
    {
        struct BH_MatEntry *e = &mm->entries[i];
        if (e->in_use && e->rel_path && bh_streq(e->rel_path, mat_rel_path))
        {
            e->refcount++;
            return e->material;
        }
    }

    char full_path[1024];
    SDL_snprintf(full_path, sizeof(full_path), "%s/%s", mm->asset_root, mat_rel_path);

    BH_FileData fd = {0};
    if (!BH_File_ReadAll(full_path, &fd))
    {
        SDL_Log("[bh] failed to read material: %s", full_path);
        return NULL;
    }

    BH_Material *mat = (BH_Material *)BH_Arena_Alloc(mm->arena, sizeof(BH_Material), 8);
    if (!mat)
    {
        BH_File_Free(&fd);
        return NULL;
    }

    if (!BH_Material_Init(mat, mat_rel_path, mm->fs_refl, mm->arena))
    {
        SDL_Log("[bh] failed to init material %s", mat_rel_path);
        BH_File_Free(&fd);
        return NULL;
    }

    /* Defaults */
    BH_TextureHandle tex_albedo = BH_TextureManager_GetDefaultWhite(mm->textures);
    BH_TextureHandle tex_normal = BH_TextureManager_GetDefaultNormal(mm->textures);
    BH_TextureHandle tex_roughness = BH_TextureManager_GetDefaultWhite(mm->textures);
    BH_TextureHandle tex_metallic = BH_TextureManager_GetDefaultBlack(mm->textures);
    BH_TextureHandle tex_ao = BH_TextureManager_GetDefaultWhite(mm->textures);

    vec3 tint = {1.0f, 1.0f, 1.0f};
    float normal_scale = 1.0f;
    float metallic_factor = 0.0f;
    float roughness_factor = 1.0f;

    {
        char *text = (char *)SDL_malloc(fd.size + 1u);
        if (!text)
        {
            BH_File_Free(&fd);
            return NULL;
        }
        SDL_memcpy(text, fd.data, fd.size);
        text[fd.size] = '\0';

        char *save = NULL;
        char *line = SDL_strtok_r(text, "\n", &save);
        while (line)
        {
            bh_mat_parse_line(mm, mat, line, &tex_albedo, &tex_normal, &tex_roughness, &tex_metallic, &tex_ao, &tint,
                              &normal_scale, &metallic_factor, &roughness_factor);
            line = SDL_strtok_r(NULL, "\n", &save);
        }

        SDL_free(text);
    }

    BH_File_Free(&fd);

    mat->textures[BH_MATERIAL_TEX_ALBEDO] = tex_albedo;
    mat->textures[BH_MATERIAL_TEX_NORMAL] = tex_normal;
    mat->textures[BH_MATERIAL_TEX_ROUGHNESS] = tex_roughness;
    mat->textures[BH_MATERIAL_TEX_METALLIC] = tex_metallic;
    mat->textures[BH_MATERIAL_TEX_AO] = tex_ao;

    BH_ParamBlock_SetVec3(&mat->fragment_params, "u_Tint", &tint);
    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_NormalScale", normal_scale);
    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_MetallicFactor", metallic_factor);
    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_RoughnessFactor", roughness_factor);

    for (uint32_t i = 0; i < cap; ++i)
    {
        struct BH_MatEntry *e = &mm->entries[i];
        if (!e->in_use)
        {
            e->in_use = true;
            e->rel_path = bh_arena_strdup0(mm->arena, mat_rel_path);
            e->material = mat;
            e->refcount = 1;
            return mat;
        }
    }

    SDL_Log("[bh] material manager out of entries (cap=%u)", cap);
    return mat;
}

const BH_Material *BH_MaterialManager_GetFallback(BH_MaterialManager *mm)
{
    if (!mm || !mm->fs_refl || !mm->arena)
    {
        return NULL;
    }

    if (mm->fallback_material)
    {
        return mm->fallback_material;
    }

    BH_Material *mat = (BH_Material *)BH_Arena_Alloc(mm->arena, sizeof(BH_Material), 8);
    if (!mat)
        return NULL;

    if (!BH_Material_Init(mat, "__fallback__", mm->fs_refl, mm->arena))
    {
        return NULL;
    }

    const vec3 tint = {1.0f, 1.0f, 1.0f};
    BH_ParamBlock_SetVec3(&mat->fragment_params, "u_Tint", &tint);
    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_NormalScale", 1.0f);
    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_MetallicFactor", 1.0f);
    BH_ParamBlock_SetFloat32(&mat->fragment_params, "u_RoughnessFactor", 1.0f);

    mm->fallback_material = mat;
    return mat;
}

const BH_Material *BH_MaterialManager_LoadById(BH_MaterialManager *mm, const char *material_id)
{
    if (!mm || !material_id || !material_id[0])
    {
        return BH_MaterialManager_GetFallback(mm);
    }

    char id[512];
    SDL_strlcpy(id, material_id, sizeof(id));
    bh_strip_quotes_inplace(id);
    bh_normalize_path_inplace(id);

    char noext[512];
    SDL_strlcpy(noext, id, sizeof(noext));
    if (bh_ends_with(noext, ".mat"))
    {
        noext[SDL_strlen(noext) - 4] = '\0';
    }

    const char *base = noext;
    const char *prefix = "materials/";
    if (SDL_strncmp(base, prefix, SDL_strlen(prefix)) == 0)
    {
        base += SDL_strlen(prefix);
    }

    /* Try canonical path first: materials/<base>.mat */
    char path[768];
    SDL_snprintf(path, sizeof(path), "materials/%s.mat", base);

    const BH_Material *mat = BH_MaterialManager_Load(mm, path);
    if (mat)
        return mat;

    /* Fallback logic for legacy/irregular IDs */
    if (bh_ends_with(id, ".mat"))
    {
        if (SDL_strncmp(id, prefix, SDL_strlen(prefix)) == 0)
        {
            mat = BH_MaterialManager_Load(mm, id);
        }
        else
        {
            SDL_snprintf(path, sizeof(path), "materials/%s", id);
            mat = BH_MaterialManager_Load(mm, path);
        }
    }
    else
    {
        if (SDL_strncmp(id, prefix, SDL_strlen(prefix)) == 0)
        {
            SDL_snprintf(path, sizeof(path), "%s.mat", id);
            mat = BH_MaterialManager_Load(mm, path);
        }
    }

    return mat ? mat : BH_MaterialManager_GetFallback(mm);
}