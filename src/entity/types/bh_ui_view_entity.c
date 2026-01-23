/* -----------------------------------------------------------------------------
   bh_ui_view_entity.c
   ----------------------------------------------------------------------------- */
#include "entity/types/bh_ui_view_entity.h"
#include "entity/types/bh_player_entity.h"

#include "interface/bh_ui.h"
#include "render/bh_material_manager.h"
#include "render/bh_texture_manager.h"
#include "scene/bh_scene.h"

#include <SDL3/SDL.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* -----------------------------------------------------------------------------
   Internal Helpers
   ----------------------------------------------------------------------------- */

static const char *bh_ui_view_default_content_rml(void)
{
    return "<div style=\"padding:10px; background: rgba(0,0,0,125); width: 100%;\">World Panel</div>";
}

static const char *bh_ui_view_kv_get_string_ci(const BH_Entity *e, const char *a, const char *b, const char *fallback)
{
    const char *v = BH_Entity_KVGetString(e, a, NULL);
    if (!v)
    {
        v = BH_Entity_KVGetString(e, b, NULL);
    }
    return (v && v[0]) ? v : fallback;
}

static int32_t bh_ui_view_kv_get_int_ci(const BH_Entity *e, const char *a, const char *b, int32_t fallback)
{
    const char *s = BH_Entity_KVGetString(e, a, NULL);
    if (!s)
    {
        s = BH_Entity_KVGetString(e, b, NULL);
    }
    return BH_Parse_Int32(s, fallback);
}

static bool bh_ui_view_markup_has_id(const char *markup, const char *id)
{
    if (!markup || !id || !id[0])
    {
        return false;
    }

    char pat0[128];
    char pat1[128];
    SDL_snprintf(pat0, sizeof(pat0), "id=\"%s\"", id);
    SDL_snprintf(pat1, sizeof(pat1), "id='%s'", id);

    return (SDL_strstr(markup, pat0) != NULL) || (SDL_strstr(markup, pat1) != NULL);
}

static char *bh_ui_view_build_rml_doc(const char *content_rml)
{
    if (!content_rml)
    {
        content_rml = "";
    }

    const char *pre = "<rml>\n"
                      "  <head>\n"
                      "    <title>World Panel</title>\n"
                      "  </head>\n"
                      "  <body "
                      "style=\"margin:0;width:100%;height:100%;font-family:Verdana;font-size:15px;color:white;"
                      "background:transparent;\">\n";

    const char *post = "\n"
                       "  </body>\n"
                       "</rml>\n";

    const size_t len = SDL_strlen(pre) + SDL_strlen(content_rml) + SDL_strlen(post) + 1u;
    char *out = (char *)SDL_malloc(len);

    if (out)
    {
        SDL_snprintf(out, len, "%s%s%s", pre, content_rml, post);
    }
    return out;
}

static BH_PlayerEntity *bh_find_active_player(const BH_Scene *scene)
{
    if (!scene)
    {
        return NULL;
    }

    for (BH_Entity *e = scene->entities; e; e = e->next)
    {
        if (e->vt && strcmp(e->vt->type_name, "bh_player") == 0)
        {
            return (BH_PlayerEntity *)e;
        }
    }
    return NULL;
}

/* -----------------------------------------------------------------------------
   Physics & Math
   ----------------------------------------------------------------------------- */

static quat quat_from_euler(vec3 angles_deg)
{
    const float deg2rad = BH_PI / 180.0f;

    /* Engine Coord System: Yaw Z (0,0,1), Pitch Y (0,1,0) */
    quat q_yaw = quat_fromaxisangle((vec3){0, 0, 1}, angles_deg.y * deg2rad);
    quat q_pitch = quat_fromaxisangle((vec3){0, 1, 0}, angles_deg.x * deg2rad);

    return quat_mul(q_yaw, q_pitch);
}

static void bh_ui_view_billboard(BH_UIViewEntity *v, vec3 target_pos)
{
    /* UI Quad faces +X. Align using standard angleto defaults. */
    const vec3 self_pos = v->base.node->local_curr.position;
    const vec3 angles = vec3_angleto(self_pos, target_pos);
    const quat q_look = quat_from_euler(angles);

    v->base.node->local_curr.rotation = q_look;

    /* Snap prev to current to prevent interpolation artifacts on sudden re-orientation */
    v->base.node->local_prev.rotation = q_look;
}

/* -----------------------------------------------------------------------------
   Initialization & Lifecycle
   ----------------------------------------------------------------------------- */

static bool bh_ui_view_try_init(BH_UIViewEntity *v, const BH_EntityServices *sv)
{
    if (!v || !sv || !sv->ui)
    {
        return false;
    }
    if (v->initialized)
    {
        return true;
    }

    /* Cleanup stale state */
    if (v->mesh_created)
    {
        BH_Mesh_Release(&v->mesh, sv->gpu_device);
        v->mesh_created = false;
    }
    if (v->ui_ctx)
    {
        BH_UIContext_Destroy(sv->ui, v->ui_ctx);
        v->ui_ctx = NULL;
    }

    /* Resolution setup */
    int32_t w_i = bh_ui_view_kv_get_int_ci(&v->base, "x", "X", 240);
    int32_t h_i = bh_ui_view_kv_get_int_ci(&v->base, "y", "Y", 120);

    w_i = (w_i < 16) ? 16 : (w_i > 4096) ? 4096 : w_i;
    h_i = (h_i < 16) ? 16 : (h_i > 4096) ? 4096 : h_i;

    const uint32_t w = (uint32_t)w_i;
    const uint32_t h = (uint32_t)h_i;

    /* RML Document generation */
    const char *content = bh_ui_view_kv_get_string_ci(&v->base, "markup", "Markup", NULL);
    if (!content)
    {
        content = bh_ui_view_default_content_rml();
    }

    v->has_player_info = bh_ui_view_markup_has_id(content, "player_info");
    v->has_fps_counter = bh_ui_view_markup_has_id(content, "fps_counter");

    char *doc = bh_ui_view_build_rml_doc(content);
    if (!doc)
    {
        SDL_Log("[bh][ui_view] Out of memory (rml doc)");
        goto fail;
    }

    /* Context creation */
    v->ui_ctx = BH_UIContext_Create(sv->ui, "world_space", w, h);
    if (!v->ui_ctx)
    {
        SDL_Log("[bh][ui_view] Failed to create offscreen UI context");
        goto fail;
    }

    if (!BH_UIContext_SetRenderTarget(v->ui_ctx, w, h))
    {
        SDL_Log("[bh][ui_view] Failed to create render target");
        goto fail;
    }

    if (!BH_UIContext_LoadDocumentFromMemory(v->ui_ctx, doc))
    {
        SDL_Log("[bh][ui_view] Failed to load world-space UI document");
        SDL_free(doc);
        goto fail;
    }

    SDL_free(doc);
    doc = NULL;

    /* Material Setup */
    BH_Material *mat = (BH_Material *)BH_Arena_Alloc(sv->permanent_arena, sizeof(BH_Material), 16);
    if (!mat)
    {
        SDL_Log("[bh][ui_view] Out of memory (material)");
        goto fail;
    }

    if (!BH_Material_Init(mat, "ui_view_mat", sv->materials->fs_refl, sv->permanent_arena))
    {
        SDL_Log("[bh][ui_view] Failed to init material");
        goto fail;
    }

    mat->flags |= BH_MATERIAL_FLAG_TRANSPARENT;
    mat->textures[BH_MATERIAL_TEX_ALBEDO] = BH_UIContext_GetRenderTargetHandle(v->ui_ctx);
    mat->textures[BH_MATERIAL_TEX_LIGHTMAP] = BH_TextureManager_GetDefaultWhite(sv->textures);
    mat->textures[BH_MATERIAL_TEX_LIGHTDIR] = BH_TextureManager_GetDefaultNormal(sv->textures);
    mat->textures[BH_MATERIAL_TEX_SHADOWMASK] = BH_TextureManager_GetDefaultWhite(sv->textures);

    const vec3 tint = {1.0f, 1.0f, 1.0f};
    const float normal_scale = 0.0f;
    const float metallic = 0.0f;
    const float roughness = 1.0f;
    const float ao_strength = 1.0f;

    /* Tune baked lerp: white RGBM lightmap (16/PI) cancels global ambient (0.15) */
    const float lm_white = 16.0f / BH_PI;
    const float ambient = 0.15f;
    const float lightmap_strength = (1.0f - ambient) / (lm_white - ambient);

    const float lightdir_strength = 0.0f;
    const float shadowmask_strength = 0.0f;

    (void)BH_Material_SetRaw(mat, "u_Tint", &tint, (uint32_t)sizeof(tint));
    (void)BH_Material_SetRaw(mat, "u_NormalScale", &normal_scale, (uint32_t)sizeof(normal_scale));
    (void)BH_Material_SetRaw(mat, "u_MetallicFactor", &metallic, (uint32_t)sizeof(metallic));
    (void)BH_Material_SetRaw(mat, "u_RoughnessFactor", &roughness, (uint32_t)sizeof(roughness));
    (void)BH_Material_SetRaw(mat, "u_AOStrength", &ao_strength, (uint32_t)sizeof(ao_strength));
    (void)BH_Material_SetRaw(mat, "u_LightmapStrength", &lightmap_strength, (uint32_t)sizeof(lightmap_strength));
    (void)BH_Material_SetRaw(mat, "u_LightmapDirStrength", &lightdir_strength, (uint32_t)sizeof(lightdir_strength));
    (void)BH_Material_SetRaw(mat, "u_ShadowmaskStrength", &shadowmask_strength, (uint32_t)sizeof(shadowmask_strength));

    /* Mesh Construction: Vertical quad in YZ plane, centered */
    const float half_w = (float)w * 0.5f;
    const float half_h = (float)h * 0.5f;
    const vec3 n = {1.0f, 0.0f, 0.0f};

    BH_Vertex verts[6] = {
        {.position = {0.0f, +half_w, +half_h}, .normal = n, .uv = {1.0f, 0.0f}},
        {.position = {0.0f, -half_w, +half_h}, .normal = n, .uv = {0.0f, 0.0f}},
        {.position = {0.0f, -half_w, -half_h}, .normal = n, .uv = {0.0f, 1.0f}},
        {.position = {0.0f, -half_w, -half_h}, .normal = n, .uv = {0.0f, 1.0f}},
        {.position = {0.0f, +half_w, -half_h}, .normal = n, .uv = {1.0f, 1.0f}},
        {.position = {0.0f, +half_w, +half_h}, .normal = n, .uv = {1.0f, 0.0f}},
    };

    if (!BH_Mesh_CreateTriangleList(&v->mesh, sv->gpu_device, sv->permanent_arena, verts, 6, mat))
    {
        SDL_Log("[bh][ui_view] Failed to create quad mesh");
        goto fail;
    }

    v->mesh_created = true;
    if (v->base.node)
    {
        v->base.node->mesh = &v->mesh;
    }

    v->initialized = true;
    return true;

fail:
    if (doc)
        SDL_free(doc);
    if (v->mesh_created)
    {
        BH_Mesh_Release(&v->mesh, sv->gpu_device);
        v->mesh_created = false;
    }
    if (v->ui_ctx)
    {
        BH_UIContext_Destroy(sv->ui, v->ui_ctx);
        v->ui_ctx = NULL;
    }
    v->initialized = false;
    return false;
}

static void ui_view_awake(BH_Entity *e, const BH_EntityServices *sv)
{
    (void)e;
    (void)sv;
    /* Lazy init in Update */
}

static void ui_view_update(BH_Entity *e, const BH_EntityServices *sv, float dt)
{
    BH_UIViewEntity *v = (BH_UIViewEntity *)e;
    if (!v)
        return;

    if (!v->initialized)
    {
        if (!bh_ui_view_try_init(v, sv))
        {
            return;
        }
    }

    BH_PlayerEntity *player = bh_find_active_player(sv->scene);

    if (player)
    {
        const vec3 player_eye = vec3_add(player->state.origin, player->state.view_offset);
        bh_ui_view_billboard(v, player_eye);

        if (v->ui_ctx && v->has_player_info)
        {
            char buffer[128];
            const vec3 p = player->state.origin;
            snprintf(buffer, sizeof(buffer),
                     "<div style='font-size:20px; color: yellow;'>"
                     "Pos: %.1f, %.1f, %.1f"
                     "</div>",
                     p.x, p.y, p.z);
            BH_UIContext_SetElementInnerRML(v->ui_ctx, "player_info", buffer);
        }
    }

    static float time_accumulator = 0.0f;
    static int frames_counted = 0;

    time_accumulator += dt;
    frames_counted++;

    if (time_accumulator >= 0.25f)
    {
        const float fps = (float)frames_counted / time_accumulator;

        time_accumulator = 0.0f;
        frames_counted = 0;

        if (v->ui_ctx && v->has_fps_counter)
        {
            char fps_buffer[32];
            snprintf(fps_buffer, sizeof(fps_buffer), "FPS: %.0f", fps);
            BH_UIContext_SetElementInnerRML(v->ui_ctx, "fps_counter", fps_buffer);
        }
    }

    if (v->ui_ctx)
    {
        (void)BH_UIContext_Render(v->ui_ctx);
    }
}

static void ui_view_destroy(BH_Entity *e, const BH_EntityServices *sv)
{
    BH_UIViewEntity *v = (BH_UIViewEntity *)e;
    if (!v)
        return;

    if (v->mesh_created)
    {
        BH_Mesh_Release(&v->mesh, sv->gpu_device);
        v->mesh_created = false;
    }

    if (v->ui_ctx && sv && sv->ui)
    {
        BH_UIContext_Destroy(sv->ui, v->ui_ctx);
        v->ui_ctx = NULL;
    }

    v->initialized = false;
}

/* -----------------------------------------------------------------------------
   Public API
   ----------------------------------------------------------------------------- */

static const BH_EntityVTable g_ui_view_vt = {
    .type_name = "bh_ui_view",
    .awake = ui_view_awake,
    .update = ui_view_update,
    .destroy = ui_view_destroy,
};

const BH_EntityVTable *bh_ui_view_entity_vtable(void)
{
    return &g_ui_view_vt;
}

BH_UIViewEntity *bh_ui_view_entity_create(BH_Arena *arena)
{
    BH_UIViewEntity *e = (BH_UIViewEntity *)BH_Entity_Alloc(arena, sizeof(BH_UIViewEntity), &g_ui_view_vt);
    if (!e)
    {
        return NULL;
    }

    /* Zero extended fields */
    memset((void *)(&e->initialized), 0, sizeof(*e) - offsetof(BH_UIViewEntity, initialized));

    return e;
}