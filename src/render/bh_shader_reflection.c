/* -----------------------------------------------------------------------------
   bh_shader_reflection.c
   ----------------------------------------------------------------------------- */
#include "bh_shader_reflection.h"
#include "../core/bh_file.h"

#define JSMN_STATIC
#include "../vendor/jsmn.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static bool bh_json_token_streq(const char *json, const jsmntok_t *tok, const char *s)
{
    const int len = tok->end - tok->start;
    return (tok->type == JSMN_STRING) && ((int)strlen(s) == len) && (strncmp(json + tok->start, s, (size_t)len) == 0);
}

static bool bh_json_token_is_null(const char *json, const jsmntok_t *tok)
{
    const int len = tok->end - tok->start;
    return (tok->type == JSMN_PRIMITIVE) && (len == 4) && (strncmp(json + tok->start, "null", 4) == 0);
}

static bool bh_json_parse_u32(const char *json, const jsmntok_t *tok, uint32_t *out)
{
    *out = 0;

    if (tok->type != JSMN_PRIMITIVE)
    {
        return false;
    }

    char buf[64] = {0};
    const int len = tok->end - tok->start;
    const int copy_len = (len < (int)sizeof(buf) - 1) ? len : ((int)sizeof(buf) - 1);

    memcpy(buf, json + tok->start, (size_t)copy_len);

    char *end = NULL;
    const unsigned long v = strtoul(buf, &end, 10);

    if (end == buf)
    {
        return false;
    }

    *out = (uint32_t)v;
    return true;
}

static const char *bh_arena_strdup(BH_Arena *a, const char *src, int len)
{
    if (len < 0)
    {
        len = (int)strlen(src);
    }

    char *dst = (char *)BH_Arena_Alloc(a, (size_t)len + 1u, 1);
    if (dst)
    {
        memcpy(dst, src, (size_t)len);
        dst[len] = '\0';
    }
    return dst;
}

static int bh_json_token_skip(const jsmntok_t *toks, int idx)
{
    int to_skip = 1;
    int j = idx;

    while (to_skip > 0)
    {
        const jsmntok_t *cur = &toks[j];
        to_skip--;
        if (cur->type == JSMN_OBJECT)
        {
            to_skip += cur->size * 2;
        }
        else if (cur->type == JSMN_ARRAY)
        {
            to_skip += cur->size;
        }
        j++;
    }
    return j;
}

static int bh_json_find_key(const char *json, const jsmntok_t *toks, int obj_index, const char *key)
{
    const jsmntok_t *obj = &toks[obj_index];
    if (obj->type != JSMN_OBJECT)
    {
        return -1;
    }

    int idx = obj_index + 1;
    for (int i = 0; i < obj->size; ++i)
    {
        const jsmntok_t *k = &toks[idx];
        const int v_idx = idx + 1;

        if (bh_json_token_streq(json, k, key))
        {
            return v_idx;
        }
        idx = bh_json_token_skip(toks, v_idx);
    }

    return -1;
}

static BH_ParamType bh_paramtype_from_string(const char *s)
{
    if (!s)
        return BH_PARAMTYPE_UNKNOWN;
    if (strcmp(s, "float") == 0)
        return BH_PARAMTYPE_FLOAT;
    if (strcmp(s, "float2") == 0)
        return BH_PARAMTYPE_FLOAT2;
    if (strcmp(s, "float3") == 0)
        return BH_PARAMTYPE_FLOAT3;
    if (strcmp(s, "float4") == 0)
        return BH_PARAMTYPE_FLOAT4;
    if (strcmp(s, "mat4") == 0)
        return BH_PARAMTYPE_MAT4;
    return BH_PARAMTYPE_UNKNOWN;
}

static bool bh_json_get_string_dup(const char *json, const jsmntok_t *tok, BH_Arena *arena, const char **out)
{
    *out = NULL;

    if (tok->type == JSMN_STRING)
    {
        const int len = tok->end - tok->start;
        *out = bh_arena_strdup(arena, json + tok->start, len);
        return (*out != NULL);
    }

    if (bh_json_token_is_null(json, tok))
    {
        return true;
    }

    return false;
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

bool BH_ShaderReflection_Load(BH_ShaderStageReflection *out_refl, const char *json_path, BH_Arena *permanent_arena)
{
    if (!out_refl || !json_path || !permanent_arena)
    {
        return false;
    }

    *out_refl = (BH_ShaderStageReflection){0};

    BH_FileData fd = {0};
    if (!BH_File_ReadAll(json_path, &fd))
    {
        SDL_Log("[bh] Failed to read shader reflection JSON: %s", json_path);
        return false;
    }

    const char *json = (const char *)fd.data;
    int tok_cap = 256;
    jsmntok_t *toks = NULL;
    int tok_count = 0;

    for (;;)
    {
        free(toks);
        toks = (jsmntok_t *)malloc((size_t)tok_cap * sizeof(jsmntok_t));
        if (!toks)
        {
            BH_File_Free(&fd);
            return false;
        }

        jsmn_parser parser;
        jsmn_init(&parser);
        tok_count = jsmn_parse(&parser, json, (int)fd.size, toks, tok_cap);

        if (tok_count == JSMN_ERROR_NOMEM)
        {
            tok_cap *= 2;
            if (tok_cap > 8192)
            {
                SDL_Log("[bh] Too many JSON tokens in %s", json_path);
                free(toks);
                BH_File_Free(&fd);
                return false;
            }
            continue;
        }

        if (tok_count < 0)
        {
            SDL_Log("[bh] JSON parse error (%d) in %s", tok_count, json_path);
            free(toks);
            BH_File_Free(&fd);
            return false;
        }
        break;
    }

    if (tok_count < 1 || toks[0].type != JSMN_OBJECT)
    {
        SDL_Log("[bh] Reflection JSON root is not an object: %s", json_path);
        free(toks);
        BH_File_Free(&fd);
        return false;
    }

    const int root = 0;

    {
        const int stage_tok = bh_json_find_key(json, toks, root, "stage");
        if (stage_tok < 0 || toks[stage_tok].type != JSMN_STRING)
        {
            SDL_Log("[bh] Reflection JSON missing 'stage': %s", json_path);
            free(toks);
            BH_File_Free(&fd);
            return false;
        }

        const int len = toks[stage_tok].end - toks[stage_tok].start;
        const char *s = json + toks[stage_tok].start;

        if (len == 6 && strncmp(s, "vertex", 6) == 0)
        {
            out_refl->stage = BH_SHADERSTAGE_VERTEX;
        }
        else if (len == 8 && strncmp(s, "fragment", 8) == 0)
        {
            out_refl->stage = BH_SHADERSTAGE_FRAGMENT;
        }
        else
        {
            SDL_Log("[bh] Unknown shader stage in %s", json_path);
            free(toks);
            BH_File_Free(&fd);
            return false;
        }
    }

    {
        const int entry_tok = bh_json_find_key(json, toks, root, "entry");
        if (entry_tok >= 0)
        {
            bh_json_get_string_dup(json, &toks[entry_tok], permanent_arena, &out_refl->entry);
        }
        if (!out_refl->entry)
        {
            out_refl->entry = (out_refl->stage == BH_SHADERSTAGE_VERTEX) ? "vs_main" : "ps_main";
        }
    }

    {
        const int res_tok = bh_json_find_key(json, toks, root, "resources");
        if (res_tok >= 0 && toks[res_tok].type == JSMN_OBJECT)
        {
            const int ns = bh_json_find_key(json, toks, res_tok, "num_samplers");
            const int nst = bh_json_find_key(json, toks, res_tok, "num_storage_textures");
            const int nsb = bh_json_find_key(json, toks, res_tok, "num_storage_buffers");
            const int nub = bh_json_find_key(json, toks, res_tok, "num_uniform_buffers");

            if (ns >= 0)
                bh_json_parse_u32(json, &toks[ns], &out_refl->num_samplers);
            if (nst >= 0)
                bh_json_parse_u32(json, &toks[nst], &out_refl->num_storage_textures);
            if (nsb >= 0)
                bh_json_parse_u32(json, &toks[nsb], &out_refl->num_storage_buffers);
            if (nub >= 0)
                bh_json_parse_u32(json, &toks[nub], &out_refl->num_uniform_buffers);

            const int ubs_tok = bh_json_find_key(json, toks, res_tok, "uniform_buffers");
            if (ubs_tok >= 0 && toks[ubs_tok].type == JSMN_ARRAY)
            {
                const jsmntok_t *arr = &toks[ubs_tok];
                int idx = ubs_tok + 1;

                out_refl->uniform_buffer_count = 0;

                for (int i = 0; i < arr->size; ++i)
                {
                    if (out_refl->uniform_buffer_count >= BH_GPU_MAX_UNIFORM_SLOTS)
                    {
                        break;
                    }

                    const int obj = idx;
                    if (toks[obj].type != JSMN_OBJECT)
                    {
                        idx = bh_json_token_skip(toks, idx);
                        continue;
                    }

                    BH_UniformBufferReflection ub = {0};
                    const int name_tok = bh_json_find_key(json, toks, obj, "name");
                    const int slot_tok = bh_json_find_key(json, toks, obj, "slot");
                    const int size_tok = bh_json_find_key(json, toks, obj, "size");

                    if (name_tok >= 0)
                        bh_json_get_string_dup(json, &toks[name_tok], permanent_arena, &ub.name);
                    if (slot_tok >= 0)
                        bh_json_parse_u32(json, &toks[slot_tok], &ub.slot);
                    if (size_tok >= 0)
                        bh_json_parse_u32(json, &toks[size_tok], &ub.size_bytes);

                    out_refl->uniform_buffers[out_refl->uniform_buffer_count++] = ub;
                    idx = bh_json_token_skip(toks, idx);
                }

                if (out_refl->num_uniform_buffers == 0)
                {
                    uint32_t max_plus_one = 0;
                    for (uint32_t j = 0; j < out_refl->uniform_buffer_count; ++j)
                    {
                        const uint32_t s = out_refl->uniform_buffers[j].slot;
                        if (s + 1u > max_plus_one)
                        {
                            max_plus_one = s + 1u;
                        }
                    }
                    out_refl->num_uniform_buffers = max_plus_one;
                }
            }
        }
    }

    {
        const int params_tok = bh_json_find_key(json, toks, root, "params");
        if (params_tok >= 0 && toks[params_tok].type == JSMN_ARRAY)
        {
            const jsmntok_t *arr = &toks[params_tok];
            int idx = params_tok + 1;

            out_refl->param_count = 0;

            for (int i = 0; i < arr->size; ++i)
            {
                if (out_refl->param_count >= BH_GPU_MAX_PARAMS)
                {
                    break;
                }

                const int obj = idx;
                if (toks[obj].type != JSMN_OBJECT)
                {
                    idx = bh_json_token_skip(toks, idx);
                    continue;
                }

                BH_ParamReflection p = {0};
                const int name_tok = bh_json_find_key(json, toks, obj, "name");
                const int type_tok = bh_json_find_key(json, toks, obj, "type");
                const int slot_tok = bh_json_find_key(json, toks, obj, "slot");
                const int off_tok = bh_json_find_key(json, toks, obj, "offset");
                const int size_tok = bh_json_find_key(json, toks, obj, "size");

                if (name_tok >= 0)
                {
                    bh_json_get_string_dup(json, &toks[name_tok], permanent_arena, &p.name);
                    if (p.name)
                    {
                        p.name_hash = bh_hash_fnv1a_u32(p.name);
                    }
                }

                if (type_tok >= 0 && toks[type_tok].type == JSMN_STRING)
                {
                    const int len = toks[type_tok].end - toks[type_tok].start;
                    char tmp[32] = {0};
                    const int copy_len = (len < (int)sizeof(tmp) - 1) ? len : ((int)sizeof(tmp) - 1);
                    memcpy(tmp, json + toks[type_tok].start, (size_t)copy_len);
                    p.type = bh_paramtype_from_string(tmp);
                }

                if (slot_tok >= 0)
                    bh_json_parse_u32(json, &toks[slot_tok], &p.slot);
                if (off_tok >= 0)
                    bh_json_parse_u32(json, &toks[off_tok], &p.offset_bytes);
                if (size_tok >= 0)
                    bh_json_parse_u32(json, &toks[size_tok], &p.size_bytes);

                if (p.name)
                {
                    out_refl->params[out_refl->param_count++] = p;
                }

                idx = bh_json_token_skip(toks, idx);
            }
        }
    }

    free(toks);
    BH_File_Free(&fd);
    return true;
}

const BH_ParamReflection *BH_ShaderReflection_FindParam(const BH_ShaderStageReflection *refl, const char *name)
{
    if (!refl || !name)
    {
        return NULL;
    }

    const uint32_t h = bh_hash_fnv1a_u32(name);
    for (uint32_t i = 0; i < refl->param_count; ++i)
    {
        const BH_ParamReflection *p = &refl->params[i];
        if (p->name_hash == h && p->name && strcmp(p->name, name) == 0)
        {
            return p;
        }
    }
    return NULL;
}

const BH_UniformBufferReflection *BH_ShaderReflection_FindUniformBuffer(const BH_ShaderStageReflection *refl,
                                                                        uint32_t slot)
{
    if (!refl)
    {
        return NULL;
    }

    for (uint32_t i = 0; i < refl->uniform_buffer_count; ++i)
    {
        const BH_UniformBufferReflection *ub = &refl->uniform_buffers[i];
        if (ub->slot == slot)
        {
            return ub;
        }
    }
    return NULL;
}