// -----------------------------------------------------------------------------
// bh_app.h
// -----------------------------------------------------------------------------

#pragma once

#include "bh_game.h"

typedef struct BH_AppConfig
{
    int num_windows;
    int window_width;
    int window_height;
    double fixed_dt;
    int target_fps;
    bool enable_vsync;
    bool debug_gpu;
} BH_AppConfig;

typedef struct BH_App
{
    BH_Game *games;
    int game_count;

    char *base_path;  /* SDL_GetBasePath() */
    char *asset_root; /* base_path + "assets" */

    int target_fps;
} BH_App;

bool BH_App_Init(BH_App *app, const BH_AppConfig *cfg);
void BH_App_Shutdown(BH_App *app);
int BH_App_Run(BH_App *app);
