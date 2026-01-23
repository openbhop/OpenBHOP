#include "bh_app.h"

#include "core/bh_parse.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------
// Graphics backend selection
// -----------------------------------------------------------------------------
// Toggle the default GPU backend in this one place.
//
// 1 = OpenGL backend (desktop)
// 0 = SDL_gpu backend
#ifndef BH_USE_OPENGL_BACKEND
#define BH_USE_OPENGL_BACKEND 1
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

static BH_App g_app;

static void bh_emscripten_mainloop(void *userdata)
{
    BH_App *app = (BH_App *)userdata;

    if (!BH_App_Tick(app))
    {
        BH_App_Shutdown(app);
        emscripten_cancel_main_loop();
    }
}
#endif

int main(int argc, char **argv)
{
#ifdef __EMSCRIPTEN__
    (void)argc;
    (void)argv;

    // Web builds use SDL_gpu.
    BH_GPU_SetBackend(&BH_GPU_BACKEND_GL);

    /* Ensure SDL uses the canvas element provided by our shell HTML. */
    (void)SDL_SetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR, "#canvas");

    BH_AppConfig cfg = {0};
    cfg.num_windows = 1;
    cfg.window_width = 1280;
    cfg.window_height = 720;
    cfg.fixed_dt = 1.0 / 100.0; // tickrate
    cfg.debug_gpu = false;
    cfg.enable_vsync = true;
    cfg.target_fps = 0; /* use requestAnimationFrame cadence */

    if (!BH_App_Init(&g_app, &cfg))
    {
        SDL_Log("[bh] App init failed");
        return 1;
    }

    /*
        Run the app through Emscripten's browser mainloop. Passing 0 fps
        generally maps to requestAnimationFrame (vsync).
    */
    emscripten_set_main_loop_arg(bh_emscripten_mainloop, &g_app, 0, 1);
    return 0;
#else
    (void)argc;

#if BH_USE_OPENGL_BACKEND
    BH_GPU_SetBackend(&BH_GPU_BACKEND_GL);
#else
    BH_GPU_SetBackend(&BH_GPU_BACKEND_SDL);
#endif

    BH_AppConfig cfg = {0};
    cfg.num_windows = (argc >= 2) ? (int)BH_Parse_Int32Clamp(argv[1], 1, 1, 8) : 1;
    cfg.window_width = 1280;
    cfg.window_height = 720;
    cfg.fixed_dt = 1.0 / 100.0; // tickrate
    cfg.debug_gpu = false;

    BH_App app;
    if (!BH_App_Init(&app, &cfg))
    {
        SDL_Log("[bh] App init failed");
        return 1;
    }

    const int code = BH_App_Run(&app);
    BH_App_Shutdown(&app);
    return code;
#endif
}
