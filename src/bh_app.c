// -----------------------------------------------------------------------------
// bh_app.c
// -----------------------------------------------------------------------------

#include "bh_app.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

static bool bh_app_any_alive(const BH_App *app)
{
    for (int i = 0; i < app->game_count; ++i)
    {
        if (app->games[i].alive)
        {
            return true;
        }
    }
    return false;
}

static BH_Game *bh_app_find_game(BH_App *app, SDL_WindowID window_id)
{
    for (int i = 0; i < app->game_count; ++i)
    {
        BH_Game *g = &app->games[i];
        if (g->alive && g->window_id == window_id)
        {
            return g;
        }
    }
    return NULL;
}

static void bh_app_close_all(BH_App *app)
{
    for (int i = 0; i < app->game_count; ++i)
    {
        if (app->games[i].alive)
        {
            app->games[i].want_close = true;
        }
    }
}

static char *bh_strdup_heap(const char *s)
{
    const size_t len = SDL_strlen(s);
    char *dst = (char *)SDL_malloc(len + 1);
    if (dst)
    {
        SDL_memcpy(dst, s, len);
        dst[len] = '\0';
    }
    return dst;
}

// -----------------------------------------------------------------------------
// App Lifecycle
// -----------------------------------------------------------------------------

bool BH_App_Init(BH_App *app, const BH_AppConfig *cfg)
{
    if (!app || !cfg)
    {
        return false;
    }

    *app = (BH_App){0};
    app->target_fps = cfg->target_fps;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("[bh] SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    // Force raw mouse input to avoid OS acceleration/warping artifacts
    (void)SDL_SetHint("SDL_MOUSE_RELATIVE_MODE_WARP", "0");
    (void)SDL_SetHint("SDL_MOUSE_RELATIVE_SCALING", "0");

#ifdef __EMSCRIPTEN__
    /*
        In Emscripten builds we preload game assets into the virtual filesystem
        at "/assets" (see CMakeLists.txt). SDL_GetBasePath() has different
        semantics on the web, so we standardize on an absolute, Unix-like path.
    */
    app->base_path = SDL_strdup("/");
    if (!app->base_path)
    {
        SDL_Quit();
        return false;
    }

    app->asset_root = SDL_strdup("/assets");
    if (!app->asset_root)
    {
        SDL_free(app->base_path);
        SDL_Quit();
        return false;
    }
#else
    char *sdl_owned_path = SDL_GetBasePath();
    if (!sdl_owned_path)
    {
        SDL_Log("[bh] SDL_GetBasePath failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    app->base_path = SDL_strdup(sdl_owned_path);
    SDL_free(sdl_owned_path);
    if (!app->base_path)
    {
        SDL_Quit();
        return false;
    }

    {
        const char *suffix = "assets";
        const size_t len = SDL_strlen(app->base_path) + SDL_strlen(suffix) + 1;
        app->asset_root = (char *)SDL_malloc(len);
        if (!app->asset_root)
        {
            SDL_free(app->base_path);
            SDL_Quit();
            return false;
        }
        SDL_snprintf(app->asset_root, len, "%s%s", app->base_path, suffix);
    }
#endif

    int n = (cfg->num_windows > 0) ? cfg->num_windows : 1;
#ifdef __EMSCRIPTEN__
    /* Browser builds should run a single instance (single canvas). */
    n = 1;
#endif
    app->game_count = n;
    app->games = (BH_Game *)calloc((size_t)n, sizeof(BH_Game));

    if (!app->games)
    {
        SDL_free(app->asset_root);
        SDL_free(app->base_path);
        SDL_Quit();
        return false;
    }

    for (int i = 0; i < n; ++i)
    {
        char title_buf[64];
        SDL_snprintf(title_buf, (int)sizeof(title_buf), "WR Window %d", i + 1);

        BH_GameConfig gcfg = {0};
        gcfg.title = bh_strdup_heap(title_buf);
        gcfg.width = cfg->window_width;
        gcfg.height = cfg->window_height;
        gcfg.fixed_dt = cfg->fixed_dt;
        gcfg.debug_gpu = cfg->debug_gpu;
        gcfg.enable_vsync = cfg->enable_vsync;

        const bool ok = BH_Game_Init(&app->games[i], &gcfg, app->asset_root);
        SDL_free((void *)gcfg.title);

        if (!ok)
        {
            SDL_Log("[bh] Game init failed for window %d", i + 1);
            for (int j = 0; j < i; ++j)
            {
                if (app->games[j].alive)
                {
                    BH_Game_Shutdown(&app->games[j]);
                }
            }
            free(app->games);
            SDL_free(app->asset_root);
            SDL_free(app->base_path);
            SDL_Quit();
            return false;
        }
    }

    return true;
}

void BH_App_Shutdown(BH_App *app)
{
    if (!app)
    {
        return;
    }

    if (app->games)
    {
        for (int i = 0; i < app->game_count; ++i)
        {
            if (app->games[i].alive)
            {
                BH_Game_Shutdown(&app->games[i]);
            }
        }
        free(app->games);
        app->games = NULL;
    }

    if (app->asset_root)
    {
        SDL_free(app->asset_root);
        app->asset_root = NULL;
    }
    if (app->base_path)
    {
        SDL_free(app->base_path);
        app->base_path = NULL;
    }

    SDL_Quit();
}

// -----------------------------------------------------------------------------
// Update & Render Loop
// -----------------------------------------------------------------------------

bool BH_App_Tick(BH_App *app)
{
    if (!app)
    {
        return false;
    }

    if (!bh_app_any_alive(app))
    {
        return false;
    }

    const uint64_t frame_start_ticks = SDL_GetPerformanceCounter();
    const uint64_t frame_start_ns = SDL_GetTicksNS();
    if (!app->timing_initialized)
    {
        app->timing_freq = SDL_GetPerformanceFrequency();

        app->timing_target_ns = 0.0;
        if (app->target_fps > 0)
        {
            app->timing_target_ns = (1.0 / (double)app->target_fps) * 1e9;
        }

        app->timing_prev_ticks = frame_start_ticks;
        app->timing_initialized = true;
    }

    const uint64_t delta_ticks = frame_start_ticks - app->timing_prev_ticks;
    app->timing_prev_ticks = frame_start_ticks;

    const double dt = (app->timing_freq > 0) ? ((double)delta_ticks / (double)app->timing_freq) : 0.0;

    for (int i = 0; i < app->game_count; ++i)
    {
        if (app->games[i].alive)
        {
            BH_Input_BeginFrame(&app->games[i].input, frame_start_ns);
        }
    }

    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_EVENT_QUIT)
        {
            bh_app_close_all(app);
            continue;
        }

        const SDL_WindowID wid = BH_Input_WindowIDFromSDLEvent(&e);
        BH_Game *g = bh_app_find_game(app, wid);
        if (g)
        {
            BH_Game_HandleEvent(g, &e);
        }
    }

    for (int i = 0; i < app->game_count; ++i)
    {
        BH_Game *g = &app->games[i];
        if (!g->alive)
        {
            continue;
        }

        BH_Game_Update(g, dt);

        if (g->want_close)
        {
            BH_Game_Shutdown(g);
        }
    }

    /*
        In browser builds the outer main thread must not block; we let
        Emscripten/SDL drive the cadence (requestAnimationFrame).
    */
#ifndef __EMSCRIPTEN__
    if (app->timing_target_ns > 0.0)
    {
        const uint64_t frame_end_ticks = SDL_GetPerformanceCounter();
        const uint64_t elapsed_ticks = frame_end_ticks - frame_start_ticks;
        const double elapsed_ns = ((double)elapsed_ticks / (double)app->timing_freq) * 1e9;

        if (elapsed_ns < app->timing_target_ns)
        {
            // SDL_DelayNS((uint64_t)(app->timing_target_ns - elapsed_ns));
        }
    }
#endif

    return bh_app_any_alive(app);
}

int BH_App_Run(BH_App *app)
{
    if (!app)
    {
        return 1;
    }

    while (BH_App_Tick(app))
    {
        /* no-op */
    }

    return 0;
}