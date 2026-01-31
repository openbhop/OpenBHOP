// -----------------------------------------------------------------------------
// bh_app.h
// -----------------------------------------------------------------------------

#pragma once

#include "bh_game.h"

#include <stdint.h>

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

    /* Frame timing state (used by BH_App_Tick / BH_App_Run). */
    uint64_t timing_prev_ticks;
    uint64_t timing_freq;
    double timing_target_ns;
    bool timing_initialized;
} BH_App;

bool BH_App_Init(BH_App *app, const BH_AppConfig *cfg);
void BH_App_Shutdown(BH_App *app);
/*
    Run a single iteration of the app loop.

    Returns true if at least one game instance is still alive and the app should
    continue running. Returns false when the app is finished.

    This is used to drive the app from an external mainloop (e.g. Emscripten).
*/
bool BH_App_Tick(BH_App *app);
int BH_App_Run(BH_App *app);
