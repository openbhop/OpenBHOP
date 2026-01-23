// -----------------------------------------------------------------------------
// bh_game.c
// -----------------------------------------------------------------------------
#include "bh_game.h"
#include "core/bh_file.h"
#include "debug/bh_console.h"
#include "debug/bh_debug_draw.h"
#include "debug/bh_dev_console.h"
#include "entity/types/bh_player_entity.h"
#include "entity/types/bh_test_cube_entity.h"
#include "entity/bh_entity_factory.h"
#include "map/bh_cmap_loader.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

static bool bh_concmd_quit(void *ctx, int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    BH_Game *game = (BH_Game *)ctx;
    game->want_close = true;
    return true;
}

static void bh_game_on_ui_action(void *user, const char *action)
{
    BH_Game *game = (BH_Game *)user;

    if (SDL_strcmp(action, "quit") == 0)
    {
        game->want_close = true;
    }
    else if (SDL_strcmp(action, "play") == 0)
    {
        if (game->ui)
        {
            (void)BH_Interface_Show(game->ui, "player_hud");
        }
    }
}

static mat4 bh_build_viewproj(SDL_Window *window, vec3 cam_pos_hu, vec3 cam_angles_deg)
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);

    if (w <= 0)
        w = 1;
    if (h <= 0)
        h = 1;

    const float aspect = (float)w / (float)h;
    const float fov_y = 60.0f * (BH_PI / 180.0f);
    const float z_near = 1.0f;
    const float z_far = 16384.0f;

    const mat4 proj = mat4_perspective_lh_z0to1(fov_y, aspect, z_near, z_far);
    const mat4 view = mat4_view_from_angles_lh(cam_pos_hu, cam_angles_deg);

    return mat4_mul(proj, view);
}

static vec3 bh_node_lerp_position(const BH_SceneNode *n, float alpha)
{
    return (vec3){
        lerpf(n->local_prev.position.x, n->local_curr.position.x, alpha),
        lerpf(n->local_prev.position.y, n->local_curr.position.y, alpha),
        lerpf(n->local_prev.position.z, n->local_curr.position.z, alpha),
    };
}

static void bh_game_spawn_fallback_scene(BH_Game *game)
{
    if (!game->scene.root)
        return;

    {
        BH_SceneNode *node = BH_Scene_CreateNode(&game->level_arena, "bh_test_cube");
        if (node)
        {
            BH_Transform t = BH_Transform_GetIdentity();
            t.position = (vec3){160.0f, 160.0f, 48.0f};
            BH_Scene_SetNodeLocalTransform(node, t, t);
            BH_Scene_AddChild(game->scene.root, node);

            BH_TestCubeEntity *e = bh_test_cube_entity_create(&game->level_arena);
            if (e)
            {
                BH_EntityKV *kvs = (BH_EntityKV *)BH_Arena_Alloc(&game->level_arena, 2 * sizeof(BH_EntityKV), 8);
                if (kvs)
                {
                    kvs[0] = (BH_EntityKV){"cube_size", "32.0"};
                    kvs[1] = (BH_EntityKV){"cube_rotation_speed", "0.0"};
                    e->base.kvs = kvs;
                    e->base.kv_count = 2;
                }
                BH_Entity_Attach(&game->scene, (BH_Entity *)e, node);
            }
        }
    }

    {
        BH_SceneNode *node = BH_Scene_CreateNode(&game->level_arena, "bh_player");
        if (node)
        {
            BH_Transform t = BH_Transform_GetIdentity();
            t.position = (vec3){0.0f, 0.0f, 96.0f};
            BH_Scene_SetNodeLocalTransform(node, t, t);
            BH_Scene_AddChild(game->scene.root, node);

            BH_PlayerEntity *e = bh_player_entity_create(&game->level_arena);
            if (e)
            {
                BH_Entity_Attach(&game->scene, (BH_Entity *)e, node);
                game->player_node = node;
            }
        }
    }
}

static void bh_game_clear_level(BH_Game *game)
{
    if (game->renderer.device)
    {
        BH_GPU_WaitForIdle(game->renderer.device);
    }

    BH_Entity_DestroyAll(&game->scene, &game->entity_sv);

    BH_Arena_Reset(&game->physics_arena);
    (void)BH_Physics_Init(&game->physics_world, &game->physics_arena, 4096);

    BH_Arena_Reset(&game->level_arena);
    BH_Scene_Init(&game->scene, &game->level_arena);

    game->entity_sv.scene = &game->scene;
    game->entity_sv.physics = &game->physics_world;
    game->entity_sv.input = &game->input;

    game->player_node = NULL;
}

// -----------------------------------------------------------------------------
// Lifecycle (Init / Shutdown / Load)
// -----------------------------------------------------------------------------

bool BH_Game_LoadMap(BH_Game *game, const char *map_rel_path)
{
    if (!game || !map_rel_path || !game->asset_root)
    {
        return false;
    }

    bh_game_clear_level(game);

    char cmap_path[512];
    SDL_snprintf(cmap_path, (int)sizeof(cmap_path), "%s/%s", game->asset_root, map_rel_path);

    BH_CmapLoadResult res = {0};
    const bool loaded = BH_Cmap_LoadIntoScene(cmap_path, &game->renderer, &game->scene, &game->physics_world,
                                              &game->entity_sv, &game->level_arena, &res);

    game->player_node = res.player_node;

    if (!loaded)
    {
        SDL_Log("[bh] failed to load map: %s", cmap_path);
        bh_game_spawn_fallback_scene(game);
    }

    if (!game->player_node)
    {
        BH_SceneNode *node = BH_Scene_CreateNode(&game->level_arena, "bh_player");
        if (node)
        {
            BH_Transform t = BH_Transform_GetIdentity();
            t.position = (vec3){0.0f, 0.0f, 96.0f};
            BH_Scene_SetNodeLocalTransform(node, t, t);
            BH_Scene_AddChild(game->scene.root, node);

            BH_PlayerEntity *e = bh_player_entity_create(&game->level_arena);
            if (e)
            {
                BH_Entity_Attach(&game->scene, (BH_Entity *)e, node);
                game->player_node = node;
            }
        }
    }

    BH_Entity_AwakeAll(&game->scene, &game->entity_sv);
    return loaded;
}

bool BH_Game_Init(BH_Game *game, const BH_GameConfig *cfg, const char *asset_root)
{
    if (!game || !cfg || !asset_root)
    {
        return false;
    }

    *game = (BH_Game){0};
    game->alive = true;
    game->want_close = false;
    game->fixed_dt = (cfg->fixed_dt > 0.0) ? cfg->fixed_dt : (1.0 / 100.0);
    game->accumulator = 0.0;
    game->asset_root = asset_root;

    if (!BH_Arena_Init(&game->permanent_arena, 64 * 1024 * 1024))
    {
        return false;
    }
    if (!BH_Arena_Init(&game->level_arena, 32 * 1024 * 1024))
    {
        BH_Arena_Shutdown(&game->permanent_arena);
        return false;
    }
    if (!BH_Arena_Init(&game->frame_arena, 2 * 1024 * 1024))
    {
        BH_Arena_Shutdown(&game->level_arena);
        BH_Arena_Shutdown(&game->permanent_arena);
        return false;
    }
    if (!BH_Arena_Init(&game->physics_arena, 32 * 1024 * 1024))
    {
        BH_Arena_Shutdown(&game->frame_arena);
        BH_Arena_Shutdown(&game->level_arena);
        BH_Arena_Shutdown(&game->permanent_arena);
        return false;
    }

    const bool use_gl = (SDL_strcasecmp(BH_GPU_GetBackend()->name, "OpenGL") == 0);

    SDL_WindowFlags wflags = SDL_WINDOW_RESIZABLE;
    if (use_gl)
    {
        // When using the OpenGL backend, SDL needs to create an OpenGL-capable
        // window and we must request the GL context attributes up front.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        wflags |= SDL_WINDOW_OPENGL;
    }

    game->window = SDL_CreateWindow(cfg->title, cfg->width, cfg->height, wflags);
    if (!game->window)
    {
        BH_Arena_Shutdown(&game->physics_arena);
        BH_Arena_Shutdown(&game->frame_arena);
        BH_Arena_Shutdown(&game->level_arena);
        BH_Arena_Shutdown(&game->permanent_arena);
        return false;
    }
    game->window_id = SDL_GetWindowID(game->window);

    if (!SDL_SetWindowRelativeMouseMode(game->window, true))
    {
        SDL_Log("[bh] SDL_SetWindowRelativeMouseMode failed: %s", SDL_GetError());
    }

    BH_Input_Init(&game->input);

    BH_Console_Init();
    (void)BH_Console_RegisterCommand("quit", "Close the application", &bh_concmd_quit);

    BH_RendererConfig rcfg = {0};
    rcfg.debug_gpu = cfg->debug_gpu;

    if (!BH_Renderer_Init(&game->renderer, game->window, asset_root, &rcfg, &game->permanent_arena))
    {
        SDL_DestroyWindow(game->window);
        BH_Arena_Shutdown(&game->physics_arena);
        BH_Arena_Shutdown(&game->frame_arena);
        BH_Arena_Shutdown(&game->level_arena);
        BH_Arena_Shutdown(&game->permanent_arena);
        return false;
    }

    if (!BH_DebugDraw_Init(&game->debug_draw, &game->renderer, asset_root, &game->permanent_arena))
    {
        BH_Renderer_Shutdown(&game->renderer);
        SDL_DestroyWindow(game->window);
        BH_Arena_Shutdown(&game->physics_arena);
        BH_Arena_Shutdown(&game->frame_arena);
        BH_Arena_Shutdown(&game->level_arena);
        BH_Arena_Shutdown(&game->permanent_arena);
        return false;
    }

    BH_DebugDraw_SetActive(&game->debug_draw);
    BH_DebugDraw_SetDepthMode(BH_DBG_DEPTH_ALWAYS);
    BH_DebugDraw_DrawLine((vec3){0, 0, 0}, (vec3){64, 0, 0}, bh_color_red(), -1.0f);
    BH_DebugDraw_DrawLine((vec3){0, 0, 0}, (vec3){0, 64, 0}, bh_color_green(), -1.0f);
    BH_DebugDraw_DrawLine((vec3){0, 0, 0}, (vec3){0, 0, 64}, bh_color_blue(), -1.0f);
    BH_DebugDraw_SetDepthMode(BH_DBG_DEPTH_TEST);

    game->entity_sv = (BH_EntityServices){0};
    game->entity_sv.permanent_arena = &game->level_arena;
    game->entity_sv.gpu_device = game->renderer.device;
    game->entity_sv.textures = &game->renderer.textures;
    game->entity_sv.materials = &game->renderer.materials;
    game->entity_sv.scene = &game->scene;
    game->entity_sv.user = game;
    game->entity_sv.physics = &game->physics_world;

    if (!BH_Game_LoadMap(game, "maps/test_movement.cmap"))
    {
        SDL_Log("[bh] Map load failed (fallback scene should be active)");
    }

    if (game->player_node)
    {
        game->input.view_angles_deg = vec3_angleto(game->player_node->local_curr.position, (vec3){0, 0, 0});
    }

    game->ui_render = BH_UI_Create(&game->renderer, game->window, asset_root);
    game->entity_sv.ui = game->ui_render;
    if (game->ui_render)
    {
        const BH_RenderPassHooks ui_hooks = BH_UI_GetRenderHooks(game->ui_render);
        if (!BH_Renderer_AddHooks(&game->renderer, ui_hooks))
        {
            SDL_Log("[bh] failed to register UI render hooks");
        }
    }

    if (game->ui_render)
    {
        const BH_InterfaceConfig iface_cfg = {
            .user = game,
            .on_action = bh_game_on_ui_action,
        };
        game->ui = BH_Interface_Create(game->ui_render, &iface_cfg);
        if (game->ui)
        {
            (void)BH_Interface_Show(game->ui, "dev_overlay");
            BH_Interface_Show(game->ui, "main_menu");
        }
    }

    return true;
}

void BH_Game_Shutdown(BH_Game *game)
{
    if (!game)
        return;

    if (game->renderer.device)
    {
        BH_GPU_WaitForIdle(game->renderer.device);
        BH_Entity_DestroyAll(&game->scene, &game->entity_sv);
    }

    BH_Interface_Destroy(game->ui);
    game->ui = NULL;

    BH_UI_Destroy(game->ui_render);
    game->ui_render = NULL;

    BH_DebugDraw_Shutdown(&game->debug_draw);
    BH_Renderer_Shutdown(&game->renderer);

    if (game->window)
    {
        (void)SDL_SetWindowRelativeMouseMode(game->window, false);
        (void)SDL_ShowCursor();
        SDL_DestroyWindow(game->window);
        game->window = NULL;
    }

    BH_Arena_Shutdown(&game->physics_arena);
    BH_Arena_Shutdown(&game->frame_arena);
    BH_Arena_Shutdown(&game->level_arena);
    BH_Arena_Shutdown(&game->permanent_arena);

    BH_Console_Shutdown();
    game->alive = false;
}

// -----------------------------------------------------------------------------
// Update & Input
// -----------------------------------------------------------------------------

void BH_Game_HandleEvent(BH_Game *game, const SDL_Event *e)
{
    if (!game || !e)
        return;

    if (e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        game->want_close = true;
        return;
    }

    const bool ui_consumed = (game->ui && BH_Interface_HandleEvent(game->ui, e));
    const bool always_forward = (e->type == SDL_EVENT_KEY_UP);

    if (!ui_consumed || always_forward)
    {
        BH_Input_ProcessSDLEvent(&game->input, e);
    }
}

void BH_Game_Update(BH_Game *game, double frame_dt_s)
{
    if (!game || !game->alive)
        return;

    if (frame_dt_s < 0.0)
        frame_dt_s = 0.0;
    if (frame_dt_s > 0.25)
        frame_dt_s = 0.25; /* Clamp to avoid death spiral */

    BH_DebugDraw_SetActive(&game->debug_draw);
    BH_DebugDraw_Tick(&game->debug_draw, (float)frame_dt_s);

    BH_Arena_Reset(&game->frame_arena);

    if (game->ui)
    {
        BH_Interface_Update(game->ui, frame_dt_s);
    }

    const bool ui_blocking = (game->ui && BH_Interface_IsBlocking(game->ui));
    const bool ui_wants_mouse = (game->ui && BH_Interface_WantsMouse(game->ui));
    const bool ui_wants_text_input = (game->ui && BH_Interface_WantsTextInput(game->ui));

    if (game->window)
    {
        if (ui_wants_mouse)
        {
            (void)SDL_SetWindowRelativeMouseMode(game->window, false);
            (void)SDL_ShowCursor();
        }
        else
        {
            (void)SDL_SetWindowRelativeMouseMode(game->window, true);
            (void)SDL_HideCursor();
        }

        const bool text_active = SDL_TextInputActive(game->window);
        if (ui_wants_text_input && !text_active)
        {
            (void)SDL_StartTextInput(game->window);
        }
        else if (!ui_wants_text_input && text_active)
        {
            (void)SDL_StopTextInput(game->window);
        }
    }

    if (ui_blocking)
    {
        BH_Input_ClearFixedAccumulators(&game->input);
    }

    if (!ui_wants_mouse)
    {
        BH_Input_UpdateViewAngles(&game->input);
    }

    if (game->input.pressed[BH_KEY_ESCAPE])
    {
        game->want_close = true;
    }

    if (game->want_close)
        return;

    if (!ui_blocking)
    {
        game->accumulator += frame_dt_s;

        while (game->accumulator >= game->fixed_dt)
        {
            const BH_Intent tick_intent = BH_Input_BuildIntentFixed(&game->input);
            if (tick_intent.quit)
            {
                game->want_close = true;
                break;
            }

            BH_Entity_FixedUpdateAll(&game->scene, &game->entity_sv, &tick_intent, (float)game->fixed_dt);
            game->accumulator -= game->fixed_dt;
        }
    }
    else
    {
        game->accumulator = 0.0;
    }

    if (game->want_close)
        return;

    BH_Entity_UpdateAll(&game->scene, &game->entity_sv, (float)frame_dt_s);

    const float alpha = (float)(game->accumulator / game->fixed_dt);
    vec3 view_offset = {0, 0, 0};

    if (game->player_node && game->player_node->entity)
    {
        view_offset = BH_Entity_GetViewOffset(game->player_node->entity, &game->entity_sv);
    }

    const vec3 node_pos = bh_node_lerp_position(game->player_node, alpha);
    const vec3 cam_pos = vec3_add(node_pos, view_offset);
    const mat4 view_proj = bh_build_viewproj(game->window, cam_pos, game->input.view_angles_deg);

    (void)BH_Renderer_RenderScene(&game->renderer, &game->scene, &view_proj, cam_pos, alpha);
}