// -----------------------------------------------------------------------------
// bh_game.h
// -----------------------------------------------------------------------------
#pragma once

#include "core/bh_arena.h"
#include "debug/bh_debug_draw.h"
#include "entity/bh_entity.h"
#include "input/bh_input.h"
#include "input/bh_input_sdl.h"
#include "interface/bh_interface.h"
#include "interface/bh_ui.h"
#include "physics/bh_physics.h"
#include "render/bh_renderer.h"
#include "scene/bh_scene.h"

#include <SDL3/SDL.h>

typedef struct BH_GameConfig
{
    const char *title;
    int width;
    int height;
    bool enable_vsync;
    double fixed_dt;
    bool debug_gpu;
} BH_GameConfig;

typedef struct BH_Game
{
    bool alive;
    bool want_close;

    SDL_Window *window;
    SDL_WindowID window_id;

    bool mouse_relative;
    bool mouse_grabbed;
    bool cursor_visible;

    /* Memory Management */
    BH_Arena permanent_arena;
    BH_Arena level_arena;   /* Reset on map change */
    BH_Arena frame_arena;   /* Reset every frame */
    BH_Arena physics_arena; /* Reset on map change */

    /* Systems */
    BH_InputState input;
    double fixed_dt;
    double accumulator;

    BH_EntityServices entity_sv;
    BH_Renderer renderer;
    BH_UI *ui_render;
    BH_Interface *ui;
    BH_DebugDraw debug_draw;
    BH_Scene scene;
    BH_PhysicsWorld physics_world;

    BH_SceneNode *player_node;
    const char *asset_root;
} BH_Game;

bool BH_Game_Init(BH_Game *game, const BH_GameConfig *cfg, const char *asset_root);
void BH_Game_Shutdown(BH_Game *game);
bool BH_Game_LoadMap(BH_Game *game, const char *map_rel_path);

void BH_Game_HandleEvent(BH_Game *game, const SDL_Event *e);
void BH_Game_Update(BH_Game *game, double frame_dt_s);